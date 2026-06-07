#include "ArborMaterialGraphTools.h"
#include "ArborPropertyJson.h"

#include "ObjectTools.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "ImageUtils.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Factories/MaterialFunctionFactoryNew.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialParameters.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "Materials/MaterialInterface.h"
#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "RHI.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/MaterialFactoryNew.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// ============================================================================
// JSON helpers (mirrors ArborMaterialTools.cpp style)
// ============================================================================

namespace
{
	FString SerializeJson(const TSharedRef<FJsonObject>& Root)
	{
		FString Output;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Root, Writer);
		return Output;
	}

	TSharedPtr<FJsonObject> ParseJson(const FString& Json)
	{
		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		FJsonSerializer::Deserialize(Reader, Obj);
		return Obj;
	}

	FString JsonError(const FString& Message)
	{
		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("success"), false);
		Root->SetStringField(TEXT("error"), Message);
		return SerializeJson(Root);
	}

	FString JsonOk(const TSharedRef<FJsonObject>& Extra)
	{
		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("success"), true);
		for (const auto& Pair : Extra->Values)
		{
			Root->SetField(Pair.Key, Pair.Value);
		}
		return SerializeJson(Root);
	}

	FString JsonOk()
	{
		return JsonOk(MakeShared<FJsonObject>());
	}
}

// ============================================================================
// Sentinel ID scheme (Arbor IDs stored in UMaterialExpression::Desc)
// ============================================================================

namespace
{
	static const FString ArborIdPrefix = TEXT("__arbor_id:");

	FString GetArborId(const UMaterialExpression* Expr)
	{
		if (!Expr) return FString();
		const FString& Desc = Expr->Desc;
		if (!Desc.StartsWith(ArborIdPrefix)) return FString();
		FString Rest = Desc.Mid(ArborIdPrefix.Len());
		// Allow optional user-readable suffix after a newline.
		int32 NewlineIdx;
		if (Rest.FindChar('\n', NewlineIdx))
		{
			Rest = Rest.Left(NewlineIdx);
		}
		return Rest.TrimStartAndEnd();
	}

	void SetArborId(UMaterialExpression* Expr, const FString& Id)
	{
		if (!Expr) return;
		Expr->Desc = ArborIdPrefix + Id;
	}

	const TArray<TObjectPtr<UMaterialExpression>>& GetExpressions(UMaterial* Material)
	{
		// Access via the expression collection; stable across UE 5.4-5.7.
		return Material->GetExpressionCollection().Expressions;
	}

	const TArray<TObjectPtr<UMaterialExpression>>& GetFunctionExpressions(UMaterialFunction* Function)
	{
		// UMaterialFunction exposes the same expression-collection accessor as
		// UMaterial, so the sentinel-ID scheme works unchanged for functions.
		return Function->GetExpressionCollection().Expressions;
	}

	UMaterialExpression* FindExpressionById(UMaterial* Material, const FString& Id)
	{
		if (!Material || Id.IsEmpty()) return nullptr;
		for (UMaterialExpression* Expr : GetExpressions(Material))
		{
			if (GetArborId(Expr) == Id) return Expr;
		}
		return nullptr;
	}

	FString GenerateUniqueId(UMaterial* Material, const FString& ClassName)
	{
		// Strip "UMaterialExpression" / "MaterialExpression" prefix for friendliness.
		FString Base = ClassName;
		Base.RemoveFromStart(TEXT("U"));
		Base.RemoveFromStart(TEXT("MaterialExpression"));
		Base = Base.ToLower();
		if (Base.IsEmpty()) Base = TEXT("expr");

		for (int32 i = 0; i < 10000; ++i)
		{
			const FString Candidate = FString::Printf(TEXT("%s_%d"), *Base, i);
			if (!FindExpressionById(Material, Candidate)) return Candidate;
		}
		return FString::Printf(TEXT("%s_%lld"), *Base, FDateTime::Now().GetTicks());
	}
}

// ============================================================================
// Expression class resolution + reflection
// ============================================================================

namespace
{
	UClass* ResolveExpressionClass(const FString& Name)
	{
		if (Name.IsEmpty()) return nullptr;

		// Try as-is first.
		UClass* Cls = FindObject<UClass>(nullptr, *Name);
		if (Cls && Cls->IsChildOf(UMaterialExpression::StaticClass())) return Cls;

		// Try with U-prefix.
		const FString WithU = Name.StartsWith(TEXT("U")) ? Name : TEXT("U") + Name;
		Cls = FindObject<UClass>(nullptr, *WithU);
		if (Cls && Cls->IsChildOf(UMaterialExpression::StaticClass())) return Cls;

		// Walk all UClass to find a match by short name.
		const FString WithoutU = Name.StartsWith(TEXT("U")) ? Name.RightChop(1) : Name;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Candidate = *It;
			if (!Candidate->IsChildOf(UMaterialExpression::StaticClass())) continue;
			const FString CName = Candidate->GetName();
			if (CName == Name || CName == WithoutU || CName == WithU) return Candidate;
		}
		return nullptr;
	}

	/**
	 * Apply a JSON value to a property, with material-specific extensions:
	 *   - FLinearColor / FColor structs (RGB[A] object or array)
	 *   - Enum properties via FByteProperty / FEnumProperty (string -> enum value)
	 * Falls back to Arbor::Json::ApplyJsonToProperty for everything else.
	 */
	bool ApplyMaterialProperty(UObject* Object, const FString& PropertyName,
		const TSharedPtr<FJsonValue>& JsonValue, FString& OutError)
	{
		if (!Object || !JsonValue.IsValid())
		{
			OutError = TEXT("null object or invalid JSON value");
			return false;
		}

		FProperty* Prop = Object->GetClass()->FindPropertyByName(FName(*PropertyName));
		if (!Prop)
		{
			OutError = FString::Printf(TEXT("Property '%s' not found on %s"),
				*PropertyName, *Object->GetClass()->GetName());
			return false;
		}
		void* Addr = Prop->ContainerPtrToValuePtr<void>(Object);

		// FLinearColor / FColor — common on material expressions.
		if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			if (StructProp->Struct == TBaseStructure<FLinearColor>::Get())
			{
				FLinearColor Color(0, 0, 0, 1);
				const TArray<TSharedPtr<FJsonValue>>* Arr;
				const TSharedPtr<FJsonObject>* Obj;
				if (JsonValue->TryGetArray(Arr) && Arr)
				{
					Color.R = (Arr->Num() > 0) ? (*Arr)[0]->AsNumber() : 0.0f;
					Color.G = (Arr->Num() > 1) ? (*Arr)[1]->AsNumber() : 0.0f;
					Color.B = (Arr->Num() > 2) ? (*Arr)[2]->AsNumber() : 0.0f;
					Color.A = (Arr->Num() > 3) ? (*Arr)[3]->AsNumber() : 1.0f;
				}
				else if (JsonValue->TryGetObject(Obj) && Obj)
				{
					(*Obj)->TryGetNumberField(TEXT("R"), Color.R);
					(*Obj)->TryGetNumberField(TEXT("G"), Color.G);
					(*Obj)->TryGetNumberField(TEXT("B"), Color.B);
					(*Obj)->TryGetNumberField(TEXT("A"), Color.A);
				}
				else
				{
					OutError = FString::Printf(TEXT("Property '%s' is FLinearColor; expected JSON array or object"), *PropertyName);
					return false;
				}
				*static_cast<FLinearColor*>(Addr) = Color;
				return true;
			}
			if (StructProp->Struct == TBaseStructure<FColor>::Get())
			{
				FColor C(0, 0, 0, 255);
				const TArray<TSharedPtr<FJsonValue>>* Arr;
				if (JsonValue->TryGetArray(Arr) && Arr)
				{
					C.R = (uint8)FMath::Clamp((Arr->Num() > 0) ? (*Arr)[0]->AsNumber() * 255.0 : 0.0, 0.0, 255.0);
					C.G = (uint8)FMath::Clamp((Arr->Num() > 1) ? (*Arr)[1]->AsNumber() * 255.0 : 0.0, 0.0, 255.0);
					C.B = (uint8)FMath::Clamp((Arr->Num() > 2) ? (*Arr)[2]->AsNumber() * 255.0 : 0.0, 0.0, 255.0);
					C.A = (uint8)FMath::Clamp((Arr->Num() > 3) ? (*Arr)[3]->AsNumber() * 255.0 : 255.0, 0.0, 255.0);
					*static_cast<FColor*>(Addr) = C;
					return true;
				}
			}
			if (StructProp->Struct == TBaseStructure<FVector2D>::Get())
			{
				const TArray<TSharedPtr<FJsonValue>>* Arr;
				if (JsonValue->TryGetArray(Arr) && Arr && Arr->Num() >= 2)
				{
					FVector2D V((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber());
					*static_cast<FVector2D*>(Addr) = V;
					return true;
				}
			}
			if (StructProp->Struct == TBaseStructure<FVector>::Get())
			{
				const TArray<TSharedPtr<FJsonValue>>* Arr;
				if (JsonValue->TryGetArray(Arr) && Arr && Arr->Num() >= 3)
				{
					FVector V((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
					*static_cast<FVector*>(Addr) = V;
					return true;
				}
			}
			if (StructProp->Struct == FParameterChannelNames::StaticStruct())
			{
				const TSharedPtr<FJsonObject>* Obj;
				if (JsonValue->TryGetObject(Obj) && Obj)
				{
					FParameterChannelNames C;
					FString S;
					if ((*Obj)->TryGetStringField(TEXT("R"), S)) C.R = FText::FromString(S);
					if ((*Obj)->TryGetStringField(TEXT("G"), S)) C.G = FText::FromString(S);
					if ((*Obj)->TryGetStringField(TEXT("B"), S)) C.B = FText::FromString(S);
					if ((*Obj)->TryGetStringField(TEXT("A"), S)) C.A = FText::FromString(S);
					*static_cast<FParameterChannelNames*>(Addr) = C;
					return true;
				}
			}
			// fall through to Arbor::Json (handles tag structs etc.)
		}

		// Enum properties (FEnumProperty wraps FByteProperty, but ParameterName
		// uses FByteProperty directly with an enum type set).
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			const FString S = JsonValue->AsString();
			UEnum* Enum = EnumProp->GetEnum();
			int64 Value = Enum ? Enum->GetValueByNameString(S) : INDEX_NONE;
			if (Value == INDEX_NONE && Enum)
			{
				// Try with qualified name (Enum->GetNameStringByValue style)
				Value = Enum->GetValueByNameString(Enum->GenerateFullEnumName(*S));
			}
			if (Value == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("Unknown enum value '%s' for property '%s'"), *S, *PropertyName);
				return false;
			}
			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(Addr, Value);
			return true;
		}
		if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			if (UEnum* Enum = ByteProp->Enum)
			{
				const FString S = JsonValue->AsString();
				int64 Value = Enum->GetValueByNameString(S);
				if (Value == INDEX_NONE) Value = Enum->GetValueByNameString(Enum->GenerateFullEnumName(*S));
				if (Value == INDEX_NONE)
				{
					OutError = FString::Printf(TEXT("Unknown enum value '%s' for property '%s'"), *S, *PropertyName);
					return false;
				}
				ByteProp->SetPropertyValue(Addr, (uint8)Value);
				return true;
			}
			// raw byte
			ByteProp->SetPropertyValue(Addr, (uint8)JsonValue->AsNumber());
			return true;
		}

		// Defer to shared helper for scalars, names, objects, tags.
		return Arbor::Json::ApplyJsonToProperty(Object, PropertyName, JsonValue, OutError);
	}

	/** Some expression types need a side-effecting setup call after their
	 *  properties are set. Reflection alone writes the field, but the
	 *  associated runtime state (e.g. MaterialFunctionCall's FunctionInputs)
	 *  only gets populated by the type's dedicated setter. */
	void RunPostPropertyHooks(UMaterialExpression* Expr)
	{
		if (!Expr) return;
		if (auto* MFCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
		{
			// MaterialFunction was set via reflection above; trigger the
			// function-input/output rebuild so connections by pin name resolve.
			if (auto* MF = Cast<UMaterialFunctionInterface>(MFCall->MaterialFunction))
			{
				MFCall->SetMaterialFunction(MF);
			}
		}
		else if (auto* VecParam = Cast<UMaterialExpressionVectorParameter>(Expr))
		{
			// ChannelNames was set via reflection above; rewrite the Outputs[]
			// array's OutputName fields so connections by named channel
			// (e.g. "U"/"V"/"UOffset"/"VOffset" on a VectorParameter that
			// packs UV transform data) resolve correctly. Inlined from
			// UMaterialExpressionVectorParameter::ApplyChannelNames since it
			// isn't ENGINE_API-exported.
			if (VecParam->Outputs.Num() >= 5)
			{
				const FParameterChannelNames& CN = VecParam->ChannelNames;
				VecParam->Outputs[1].OutputName = !CN.R.IsEmpty() ? FName(*CN.R.ToString()) : TEXT("R");
				VecParam->Outputs[2].OutputName = !CN.G.IsEmpty() ? FName(*CN.G.ToString()) : TEXT("G");
				VecParam->Outputs[3].OutputName = !CN.B.IsEmpty() ? FName(*CN.B.ToString()) : TEXT("B");
				VecParam->Outputs[4].OutputName = !CN.A.IsEmpty() ? FName(*CN.A.ToString()) : TEXT("A");
			}
		}
	}

	void ApplyExpressionProperties(UMaterialExpression* Expr, const TSharedPtr<FJsonObject>& Properties)
	{
		if (!Expr || !Properties.IsValid()) return;

		// ComponentMask's CDO defaults to R+G. If the spec names any channel, make
		// it authoritative by clearing all four first, so {"R":true} yields R-only
		// instead of silently leaving G on (which widens the output and breaks the
		// graph with no compile error).
		if (UMaterialExpressionComponentMask* Mask = Cast<UMaterialExpressionComponentMask>(Expr))
		{
			if (Properties->HasField(TEXT("R")) || Properties->HasField(TEXT("G"))
				|| Properties->HasField(TEXT("B")) || Properties->HasField(TEXT("A")))
			{
				Mask->R = Mask->G = Mask->B = Mask->A = 0;
			}
		}

		for (const auto& Pair : Properties->Values)
		{
			FString Err;
			if (!ApplyMaterialProperty(Expr, Pair.Key, Pair.Value, Err))
			{
				UE_LOG(LogTemp, Warning, TEXT("[ArborMaterialGraph] %s.%s: %s"),
					*Expr->GetClass()->GetName(), *Pair.Key, *Err);
			}
		}
		RunPostPropertyHooks(Expr);
	}

	// MaterialExpressionCustom's named HLSL input pins live in a TArray<FCustomInput>
	// that the generic reflection setter can't populate. Read a top-level "custom_inputs"
	// array ([{ "name": "UV" }, ...]) off the expression spec and rebuild the Inputs list
	// so each name becomes a connectable pin. Code/OutputType/Description go through normal
	// properties. No-op for non-Custom expressions or when custom_inputs is absent.
	void ApplyCustomNodeInputs(UMaterialExpression* Expr, const TSharedPtr<FJsonObject>& Spec)
	{
		UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expr);
		if (!Custom || !Spec.IsValid()) return;
		const TArray<TSharedPtr<FJsonValue>>* InputsArr = nullptr;
		if (!Spec->TryGetArrayField(TEXT("custom_inputs"), InputsArr) || !InputsArr) return;

		Custom->Inputs.Reset();
		for (const TSharedPtr<FJsonValue>& V : *InputsArr)
		{
			const TSharedPtr<FJsonObject> Obj = V->AsObject();
			if (!Obj.IsValid()) continue;
			FString Name;
			Obj->TryGetStringField(TEXT("name"), Name);
			FCustomInput NewInput;
			NewInput.InputName = FName(*Name);
			Custom->Inputs.Add(NewInput);
		}
	}
}

// ============================================================================
// Read-back property reflection (richer than Arbor::Json::PropertyToJson)
// ============================================================================

namespace
{
	/** Best-effort property read-back. Handles FLinearColor / FVector(2D) /
	 *  byte+enum properties; falls back to Arbor::Json::PropertyToJson for
	 *  scalars / strings / object refs / tags. */
	TSharedPtr<FJsonValue> ReadMaterialProperty(UObject* Object, FProperty* Prop)
	{
		if (!Object || !Prop) return MakeShared<FJsonValueNull>();
		void* Addr = Prop->ContainerPtrToValuePtr<void>(Object);

		if (FStructProperty* SP = CastField<FStructProperty>(Prop))
		{
			if (SP->Struct == TBaseStructure<FLinearColor>::Get())
			{
				const FLinearColor& C = *static_cast<const FLinearColor*>(Addr);
				TArray<TSharedPtr<FJsonValue>> A;
				A.Add(MakeShared<FJsonValueNumber>(C.R));
				A.Add(MakeShared<FJsonValueNumber>(C.G));
				A.Add(MakeShared<FJsonValueNumber>(C.B));
				A.Add(MakeShared<FJsonValueNumber>(C.A));
				return MakeShared<FJsonValueArray>(A);
			}
			if (SP->Struct == TBaseStructure<FVector>::Get())
			{
				const FVector& V = *static_cast<const FVector*>(Addr);
				TArray<TSharedPtr<FJsonValue>> A;
				A.Add(MakeShared<FJsonValueNumber>(V.X));
				A.Add(MakeShared<FJsonValueNumber>(V.Y));
				A.Add(MakeShared<FJsonValueNumber>(V.Z));
				return MakeShared<FJsonValueArray>(A);
			}
			if (SP->Struct == TBaseStructure<FVector2D>::Get())
			{
				const FVector2D& V = *static_cast<const FVector2D*>(Addr);
				TArray<TSharedPtr<FJsonValue>> A;
				A.Add(MakeShared<FJsonValueNumber>(V.X));
				A.Add(MakeShared<FJsonValueNumber>(V.Y));
				return MakeShared<FJsonValueArray>(A);
			}
			if (SP->Struct == FParameterChannelNames::StaticStruct())
			{
				const FParameterChannelNames& C = *static_cast<const FParameterChannelNames*>(Addr);
				const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
				Obj->SetStringField(TEXT("R"), C.R.ToString());
				Obj->SetStringField(TEXT("G"), C.G.ToString());
				Obj->SetStringField(TEXT("B"), C.B.ToString());
				Obj->SetStringField(TEXT("A"), C.A.ToString());
				return MakeShared<FJsonValueObject>(Obj);
			}
			// Fall through to shared helper for tag structs etc.
		}
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			const int64 Val = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(Addr);
			if (UEnum* E = EnumProp->GetEnum())
			{
				return MakeShared<FJsonValueString>(E->GetNameStringByValue(Val));
			}
			return MakeShared<FJsonValueNumber>(Val);
		}
		if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			const uint8 Val = ByteProp->GetPropertyValue(Addr);
			if (UEnum* E = ByteProp->Enum)
			{
				return MakeShared<FJsonValueString>(E->GetNameStringByValue(Val));
			}
			return MakeShared<FJsonValueNumber>(Val);
		}
		return Arbor::Json::PropertyToJson(Object, Prop);
	}
}

// ============================================================================
// Output property mapping
// ============================================================================

namespace
{
	/** Map a property name string to a UE5 EMaterialProperty enum value. */
	bool ResolveOutputProperty(const FString& Name, EMaterialProperty& OutProp)
	{
		const FString N = Name;
		if (N == TEXT("BaseColor")) { OutProp = MP_BaseColor; return true; }
		if (N == TEXT("Normal")) { OutProp = MP_Normal; return true; }
		if (N == TEXT("Metallic")) { OutProp = MP_Metallic; return true; }
		if (N == TEXT("Roughness")) { OutProp = MP_Roughness; return true; }
		if (N == TEXT("Specular")) { OutProp = MP_Specular; return true; }
		if (N == TEXT("EmissiveColor")) { OutProp = MP_EmissiveColor; return true; }
		if (N == TEXT("Opacity")) { OutProp = MP_Opacity; return true; }
		if (N == TEXT("OpacityMask")) { OutProp = MP_OpacityMask; return true; }
		if (N == TEXT("AmbientOcclusion")) { OutProp = MP_AmbientOcclusion; return true; }
		if (N == TEXT("WorldPositionOffset")) { OutProp = MP_WorldPositionOffset; return true; }
		if (N == TEXT("Refraction")) { OutProp = MP_Refraction; return true; }
		if (N == TEXT("PixelDepthOffset")) { OutProp = MP_PixelDepthOffset; return true; }
		if (N == TEXT("SubsurfaceColor")) { OutProp = MP_SubsurfaceColor; return true; }
		if (N == TEXT("Tangent")) { OutProp = MP_Tangent; return true; }
		if (N == TEXT("Anisotropy")) { OutProp = MP_Anisotropy; return true; }
		return false;
	}

	/** Inverse of ResolveOutputProperty for read-back. */
	const TCHAR* OutputPropertyName(EMaterialProperty Prop)
	{
		switch (Prop)
		{
		case MP_BaseColor: return TEXT("BaseColor");
		case MP_Normal: return TEXT("Normal");
		case MP_Metallic: return TEXT("Metallic");
		case MP_Roughness: return TEXT("Roughness");
		case MP_Specular: return TEXT("Specular");
		case MP_EmissiveColor: return TEXT("EmissiveColor");
		case MP_Opacity: return TEXT("Opacity");
		case MP_OpacityMask: return TEXT("OpacityMask");
		case MP_AmbientOcclusion: return TEXT("AmbientOcclusion");
		case MP_WorldPositionOffset: return TEXT("WorldPositionOffset");
		case MP_Refraction: return TEXT("Refraction");
		case MP_PixelDepthOffset: return TEXT("PixelDepthOffset");
		case MP_SubsurfaceColor: return TEXT("SubsurfaceColor");
		case MP_Tangent: return TEXT("Tangent");
		case MP_Anisotropy: return TEXT("Anisotropy");
		default: return nullptr;
		}
	}
}

// ============================================================================
// Material asset load (no-create)
// ============================================================================

namespace
{
	UMaterial* LoadMaterial(const FString& Path)
	{
		UObject* Asset = UEditorAssetLibrary::LoadAsset(Path);
		return Cast<UMaterial>(Asset);
	}

	UMaterial* LoadOrCreateMaterial(const FString& Path)
	{
		if (UMaterial* Existing = LoadMaterial(Path)) return Existing;

		// Split into ContentPath + AssetName.
		FString ContentPath, AssetName;
		if (!Path.Split(TEXT("/"), &ContentPath, &AssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
		{
			return nullptr;
		}
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		return Cast<UMaterial>(AssetTools.CreateAsset(AssetName, ContentPath, UMaterial::StaticClass(), Factory));
	}
}

// ============================================================================
// QueryMaterial
// ============================================================================

FString UArborMaterialGraphTools::QueryMaterial(const FString& MaterialPath)
{
	UMaterial* Mat = LoadMaterial(MaterialPath);
	if (!Mat) return JsonError(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));

	const TArray<TObjectPtr<UMaterialExpression>>& Expressions = GetExpressions(Mat);

	// First pass: assign a stable index to every expression so non-Arbor-authored
	// materials (no sentinel IDs) can still be referenced consistently.
	TMap<UMaterialExpression*, int32> ExprToIdx;
	int32 NextIdx = 0;
	for (UMaterialExpression* Expr : Expressions)
	{
		if (Expr) ExprToIdx.Add(Expr, NextIdx++);
	}

	TArray<TSharedPtr<FJsonValue>> ExprArr;
	TArray<TSharedPtr<FJsonValue>> ConnArr;

	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr) continue;
		const int32 ExprIdx = ExprToIdx[Expr];
		const TSharedRef<FJsonObject> ExprObj = MakeShared<FJsonObject>();
		const FString Id = GetArborId(Expr);
		ExprObj->SetNumberField(TEXT("idx"), ExprIdx);
		ExprObj->SetStringField(TEXT("id"), Id);
		ExprObj->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
		ExprObj->SetNumberField(TEXT("x"), Expr->MaterialExpressionEditorX);
		ExprObj->SetNumberField(TEXT("y"), Expr->MaterialExpressionEditorY);

		// Property summary - richer than the shared helper (handles FLinearColor,
		// FVector, byte+enum).
		const TSharedRef<FJsonObject> PropsObj = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> PropIt(Expr->GetClass()); PropIt; ++PropIt)
		{
			FProperty* P = *PropIt;
			if (!P->HasAnyPropertyFlags(CPF_Edit)) continue;
			TSharedPtr<FJsonValue> Val = ReadMaterialProperty(Expr, P);
			if (Val.IsValid()) PropsObj->SetField(P->GetName(), Val);
		}
		ExprObj->SetObjectField(TEXT("properties"), PropsObj);
		ExprArr.Add(MakeShared<FJsonValueObject>(ExprObj));

		// Collect connections (each expression's incoming inputs). Reference
		// both by ID (may be empty) and by index (always reliable).
		// Input/output names are normalized to what ConnectMaterialExpressions
		// uses for lookup, so the round-trip works:
		//   - MFCall: GetInputNameWithType(i, false) - no type annotation
		//   - other:  GetShortenPinName(GetInputName(i)) - e.g. Coordinates -> UVs
		const int32 NumInputs = Expr->CountInputs();
		UMaterialExpressionMaterialFunctionCall* ExprAsMFCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expr);
		for (int32 i = 0; i < NumInputs; ++i)
		{
			FExpressionInput* Input = Expr->GetInput(i);
			if (!Input) continue;
			UMaterialExpression* FromExpr = Input->Expression;
			if (!FromExpr) continue;
			const int32* FromIdxPtr = ExprToIdx.Find(FromExpr);
			const TSharedRef<FJsonObject> ConnObj = MakeShared<FJsonObject>();
			ConnObj->SetStringField(TEXT("from_id"), GetArborId(FromExpr));
			ConnObj->SetNumberField(TEXT("from_idx"), FromIdxPtr ? *FromIdxPtr : -1);
			FString FromOutputName;
			if (FromExpr->Outputs.IsValidIndex(Input->OutputIndex))
			{
				FromOutputName = FromExpr->Outputs[Input->OutputIndex].OutputName.ToString();
			}
			ConnObj->SetStringField(TEXT("from_output"), FromOutputName);
			ConnObj->SetStringField(TEXT("to_id"), Id);
			ConnObj->SetNumberField(TEXT("to_idx"), ExprIdx);

			FName ToInputLookupName;
			if (ExprAsMFCall)
			{
				ToInputLookupName = ExprAsMFCall->GetInputNameWithType(i, /*bWithType=*/false);
			}
			else
			{
				ToInputLookupName = UMaterialGraphNode::GetShortenPinName(Expr->GetInputName(i));
			}
			ConnObj->SetStringField(TEXT("to_input"), ToInputLookupName.IsNone() ? FString() : ToInputLookupName.ToString());
			ConnArr.Add(MakeShared<FJsonValueObject>(ConnObj));
		}
	}

	// Material output bindings: which expression is wired to BaseColor/Normal/etc.
	TArray<TSharedPtr<FJsonValue>> OutputsArr;
	static const EMaterialProperty TargetProps[] = {
		MP_BaseColor, MP_Normal, MP_Metallic, MP_Roughness, MP_Specular,
		MP_EmissiveColor, MP_Opacity, MP_OpacityMask, MP_AmbientOcclusion,
		MP_WorldPositionOffset, MP_Refraction, MP_PixelDepthOffset,
		MP_SubsurfaceColor, MP_Tangent, MP_Anisotropy,
	};
	for (EMaterialProperty Prop : TargetProps)
	{
		FExpressionInput* Input = Mat->GetExpressionInputForProperty(Prop);
		if (!Input || !Input->Expression) continue;
		UMaterialExpression* Src = Input->Expression;
		const TCHAR* PropName = OutputPropertyName(Prop);
		if (!PropName) continue;
		const TSharedRef<FJsonObject> OutObj = MakeShared<FJsonObject>();
		OutObj->SetStringField(TEXT("property"), PropName);
		OutObj->SetStringField(TEXT("from_id"), GetArborId(Src));
		const int32* IdxPtr = ExprToIdx.Find(Src);
		OutObj->SetNumberField(TEXT("from_idx"), IdxPtr ? *IdxPtr : -1);
		if (Src->Outputs.IsValidIndex(Input->OutputIndex))
		{
			OutObj->SetStringField(TEXT("from_output"), Src->Outputs[Input->OutputIndex].OutputName.ToString());
		}
		OutputsArr.Add(MakeShared<FJsonValueObject>(OutObj));
	}

	// Flags summary.
	const TSharedRef<FJsonObject> Flags = MakeShared<FJsonObject>();
	Flags->SetStringField(TEXT("blend_mode"),
		StaticEnum<EBlendMode>()->GetNameStringByValue((int64)Mat->BlendMode));
	Flags->SetStringField(TEXT("shading_model"),
		StaticEnum<EMaterialShadingModel>()->GetNameStringByValue((int64)Mat->GetShadingModels().GetFirstShadingModel()));
	Flags->SetBoolField(TEXT("two_sided"), Mat->TwoSided);
	Flags->SetBoolField(TEXT("use_material_attributes"), Mat->bUseMaterialAttributes);

	// Compile errors from the current feature level's material resource. Lets
	// callers see WHY a material fell back to the default (lit-grey) material
	// instead of guessing - the #1 material-debugging pain point. Empty array =
	// compiled clean (or shaders not yet built; recompile first if unsure).
	TArray<TSharedPtr<FJsonValue>> ErrArr;
	if (FMaterialResource* Res = Mat->GetMaterialResource(GMaxRHIShaderPlatform))
	{
		for (const FString& Err : Res->GetCompileErrors())
		{
			ErrArr.Add(MakeShared<FJsonValueString>(Err));
		}
	}

	const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetArrayField(TEXT("expressions"), ExprArr);
	Out->SetArrayField(TEXT("connections"), ConnArr);
	Out->SetArrayField(TEXT("outputs"), OutputsArr);
	Out->SetObjectField(TEXT("flags"), Flags);
	Out->SetArrayField(TEXT("compile_errors"), ErrArr);
	Out->SetStringField(TEXT("material_path"), MaterialPath);
	return JsonOk(Out);
}

// ============================================================================
// ListMaterialExpressionTypes
// ============================================================================

FString UArborMaterialGraphTools::ListMaterialExpressionTypes(const FString& Filter)
{
	TArray<TSharedPtr<FJsonValue>> Classes;
	const FString F = Filter.ToLower();
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Cls = *It;
		if (!Cls->IsChildOf(UMaterialExpression::StaticClass())) continue;
		if (Cls->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) continue;
		const FString CName = Cls->GetName();
		if (!F.IsEmpty() && !CName.ToLower().Contains(F)) continue;
		Classes.Add(MakeShared<FJsonValueString>(CName));
	}
	Classes.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
	{
		return A->AsString() < B->AsString();
	});
	const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetArrayField(TEXT("classes"), Classes);
	return JsonOk(Out);
}

// ============================================================================
// GetMaterialExpressionClassParams
// ============================================================================

FString UArborMaterialGraphTools::GetMaterialExpressionClassParams(const FString& ClassName)
{
	UClass* Cls = ResolveExpressionClass(ClassName);
	if (!Cls) return JsonError(FString::Printf(TEXT("Expression class not found: %s"), *ClassName));

	UObject* CDO = Cls->GetDefaultObject();
	TArray<TSharedPtr<FJsonValue>> Props;
	for (TFieldIterator<FProperty> It(Cls); It; ++It)
	{
		FProperty* P = *It;
		if (!P->HasAnyPropertyFlags(CPF_Edit)) continue;
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), P->GetName());
		Entry->SetStringField(TEXT("type"), P->GetCPPType());
		const TSharedPtr<FJsonValue> Default = Arbor::Json::PropertyToJson(CDO, P);
		if (Default.IsValid()) Entry->SetField(TEXT("default"), Default);

		// Surface enum values when applicable.
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(P))
		{
			TArray<TSharedPtr<FJsonValue>> Vals;
			if (UEnum* E = EnumProp->GetEnum())
			{
				for (int32 i = 0; i < E->NumEnums() - 1; ++i)
				{
					Vals.Add(MakeShared<FJsonValueString>(E->GetNameStringByIndex(i)));
				}
			}
			Entry->SetArrayField(TEXT("enum_values"), Vals);
		}
		else if (FByteProperty* ByteProp = CastField<FByteProperty>(P))
		{
			if (UEnum* E = ByteProp->Enum)
			{
				TArray<TSharedPtr<FJsonValue>> Vals;
				for (int32 i = 0; i < E->NumEnums() - 1; ++i)
				{
					Vals.Add(MakeShared<FJsonValueString>(E->GetNameStringByIndex(i)));
				}
				Entry->SetArrayField(TEXT("enum_values"), Vals);
			}
		}

		Props.Add(MakeShared<FJsonValueObject>(Entry));
	}
	const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("class"), Cls->GetName());
	Out->SetArrayField(TEXT("properties"), Props);
	return JsonOk(Out);
}

// ============================================================================
// AddMaterialExpression
// ============================================================================

FString UArborMaterialGraphTools::AddMaterialExpression(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> Params = ParseJson(ParamsJson);
	if (!Params.IsValid()) return JsonError(TEXT("Invalid JSON"));

	const FString MaterialPath = Params->GetStringField(TEXT("material_path"));
	const FString ExprClassName = Params->GetStringField(TEXT("expression_class"));
	FString ExprId; Params->TryGetStringField(TEXT("expression_id"), ExprId);
	int32 NodeX = 0; Params->TryGetNumberField(TEXT("node_x"), NodeX);
	int32 NodeY = 0; Params->TryGetNumberField(TEXT("node_y"), NodeY);

	UMaterial* Mat = LoadMaterial(MaterialPath);
	if (!Mat) return JsonError(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));

	UClass* ExprClass = ResolveExpressionClass(ExprClassName);
	if (!ExprClass) return JsonError(FString::Printf(TEXT("Expression class not found: %s"), *ExprClassName));

	if (ExprId.IsEmpty()) ExprId = GenerateUniqueId(Mat, ExprClassName);
	else if (FindExpressionById(Mat, ExprId))
		return JsonError(FString::Printf(TEXT("Expression with id '%s' already exists"), *ExprId));

	UMaterialExpression* Expr = UMaterialEditingLibrary::CreateMaterialExpression(Mat, ExprClass, NodeX, NodeY);
	if (!Expr) return JsonError(TEXT("CreateMaterialExpression returned null"));

	SetArborId(Expr, ExprId);

	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj && PropsObj->IsValid())
	{
		ApplyExpressionProperties(Expr, *PropsObj);
	}
	ApplyCustomNodeInputs(Expr, Params);

	Mat->MarkPackageDirty();

	const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("expression_id"), ExprId);
	return JsonOk(Out);
}

// ============================================================================
// RemoveMaterialExpressionById
// ============================================================================

FString UArborMaterialGraphTools::RemoveMaterialExpressionById(const FString& MaterialPath, const FString& ExpressionId)
{
	UMaterial* Mat = LoadMaterial(MaterialPath);
	if (!Mat) return JsonError(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));

	UMaterialExpression* Target = FindExpressionById(Mat, ExpressionId);
	if (!Target) return JsonError(FString::Printf(TEXT("Expression not found: %s"), *ExpressionId));

	UMaterialEditingLibrary::DeleteMaterialExpression(Mat, Target);
	Mat->MarkPackageDirty();
	return JsonOk();
}

// ============================================================================
// SetMaterialExpressionProperty
// ============================================================================

FString UArborMaterialGraphTools::SetMaterialExpressionProperty(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> Params = ParseJson(ParamsJson);
	if (!Params.IsValid()) return JsonError(TEXT("Invalid JSON"));

	const FString MaterialPath = Params->GetStringField(TEXT("material_path"));
	const FString ExprId = Params->GetStringField(TEXT("expression_id"));
	const FString PropName = Params->GetStringField(TEXT("property_name"));
	const TSharedPtr<FJsonValue> Value = Params->TryGetField(TEXT("value"));

	UMaterial* Mat = LoadMaterial(MaterialPath);
	if (!Mat) return JsonError(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));

	UMaterialExpression* Expr = FindExpressionById(Mat, ExprId);
	if (!Expr) return JsonError(FString::Printf(TEXT("Expression not found: %s"), *ExprId));

	FString Err;
	if (!ApplyMaterialProperty(Expr, PropName, Value, Err)) return JsonError(Err);
	Mat->MarkPackageDirty();
	return JsonOk();
}

// ============================================================================
// ConnectMaterialNodes
// ============================================================================

FString UArborMaterialGraphTools::ConnectMaterialNodes(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> Params = ParseJson(ParamsJson);
	if (!Params.IsValid()) return JsonError(TEXT("Invalid JSON"));

	const FString MaterialPath = Params->GetStringField(TEXT("material_path"));
	const FString FromId = Params->GetStringField(TEXT("from_id"));
	const FString ToId = Params->GetStringField(TEXT("to_id"));
	FString FromOutput; Params->TryGetStringField(TEXT("from_output"), FromOutput);
	FString ToInput; Params->TryGetStringField(TEXT("to_input"), ToInput);

	UMaterial* Mat = LoadMaterial(MaterialPath);
	if (!Mat) return JsonError(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));

	UMaterialExpression* From = FindExpressionById(Mat, FromId);
	UMaterialExpression* To = FindExpressionById(Mat, ToId);
	if (!From) return JsonError(FString::Printf(TEXT("from_id not found: %s"), *FromId));
	if (!To) return JsonError(FString::Printf(TEXT("to_id not found: %s"), *ToId));

	FString Warning;
	if (ToInput.IsEmpty())
	{
		const int32 InputCount = To->CountInputs();
		if (InputCount > 1)
		{
			Warning = FString::Printf(TEXT("to_input not specified; target %s has %d inputs - connecting to first"),
				*To->GetClass()->GetName(), InputCount);
		}
	}

	const bool bOk = UMaterialEditingLibrary::ConnectMaterialExpressions(From, FromOutput, To, ToInput);
	if (!bOk) return JsonError(TEXT("ConnectMaterialExpressions returned false"));
	Mat->MarkPackageDirty();

	const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	if (!Warning.IsEmpty()) Out->SetStringField(TEXT("warning"), Warning);
	return JsonOk(Out);
}

// ============================================================================
// ConnectMaterialOutput
// ============================================================================

FString UArborMaterialGraphTools::ConnectMaterialOutput(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> Params = ParseJson(ParamsJson);
	if (!Params.IsValid()) return JsonError(TEXT("Invalid JSON"));

	const FString MaterialPath = Params->GetStringField(TEXT("material_path"));
	const FString ExprId = Params->GetStringField(TEXT("expression_id"));
	const FString PropertyName = Params->GetStringField(TEXT("property"));
	FString FromOutput; Params->TryGetStringField(TEXT("from_output"), FromOutput);

	UMaterial* Mat = LoadMaterial(MaterialPath);
	if (!Mat) return JsonError(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));

	UMaterialExpression* Expr = FindExpressionById(Mat, ExprId);
	if (!Expr) return JsonError(FString::Printf(TEXT("Expression not found: %s"), *ExprId));

	EMaterialProperty Prop;
	if (!ResolveOutputProperty(PropertyName, Prop))
		return JsonError(FString::Printf(TEXT("Unknown output property: %s"), *PropertyName));

	const bool bOk = UMaterialEditingLibrary::ConnectMaterialProperty(Expr, FromOutput, Prop);
	if (!bOk) return JsonError(TEXT("ConnectMaterialProperty returned false"));
	Mat->MarkPackageDirty();
	return JsonOk();
}

// ============================================================================
// RecompileMaterialAsset
// ============================================================================

FString UArborMaterialGraphTools::RecompileMaterialAsset(const FString& MaterialPath)
{
	UMaterial* Mat = LoadMaterial(MaterialPath);
	if (!Mat) return JsonError(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));

	UMaterialEditingLibrary::RecompileMaterial(Mat);
	UEditorAssetLibrary::SaveLoadedAsset(Mat);
	return JsonOk();
}

// ============================================================================
// BuildMaterial (Phase 2)
// ============================================================================

namespace
{
	struct FBuildContext
	{
		UMaterial* Material = nullptr;
		TMap<FString, UMaterialExpression*> IdToExpression;
	};

	void IndexExistingExpressions(FBuildContext& Ctx)
	{
		for (UMaterialExpression* Expr : GetExpressions(Ctx.Material))
		{
			if (!Expr) continue;
			const FString Id = GetArborId(Expr);
			if (!Id.IsEmpty()) Ctx.IdToExpression.Add(Id, Expr);
		}
	}

	bool ApplyExpressionSpec(FBuildContext& Ctx, const TSharedPtr<FJsonObject>& ExprSpec, FString& OutError)
	{
		const FString Id = ExprSpec->GetStringField(TEXT("id"));
		if (Id.IsEmpty()) { OutError = TEXT("expression missing 'id'"); return false; }

		const FString ClassName = ExprSpec->GetStringField(TEXT("class"));
		UClass* ExprClass = ResolveExpressionClass(ClassName);
		if (!ExprClass) { OutError = FString::Printf(TEXT("class not found: %s"), *ClassName); return false; }

		int32 X = 0, Y = 0;
		ExprSpec->TryGetNumberField(TEXT("x"), X);
		ExprSpec->TryGetNumberField(TEXT("y"), Y);

		UMaterialExpression* Expr = nullptr;
		if (UMaterialExpression** Existing = Ctx.IdToExpression.Find(Id))
		{
			if ((*Existing)->GetClass() == ExprClass)
			{
				Expr = *Existing;
				Expr->MaterialExpressionEditorX = X;
				Expr->MaterialExpressionEditorY = Y;
			}
			else
			{
				// Class changed - replace.
				UMaterialEditingLibrary::DeleteMaterialExpression(Ctx.Material, *Existing);
				Ctx.IdToExpression.Remove(Id);
			}
		}
		if (!Expr)
		{
			Expr = UMaterialEditingLibrary::CreateMaterialExpression(Ctx.Material, ExprClass, X, Y);
			if (!Expr) { OutError = TEXT("CreateMaterialExpression returned null"); return false; }
			SetArborId(Expr, Id);
			Ctx.IdToExpression.Add(Id, Expr);
		}

		const TSharedPtr<FJsonObject>* PropsObj = nullptr;
		if (ExprSpec->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj && PropsObj->IsValid())
		{
			ApplyExpressionProperties(Expr, *PropsObj);
		}
		ApplyCustomNodeInputs(Expr, ExprSpec);
		return true;
	}
}

// Connect From -> To, recording any connection that cannot be resolved instead
// of dropping it silently. When a named to_input fails on a single-input target
// (Abs/Length/ComponentMask/Sine/RgbToHsv/etc. name their sole pin ""), retry
// against that lone pin so C++-style pin names ("Input"/"VectorInput") also work.
// Returns true if the wire was made; otherwise appends {from,to,to_input} to Unresolved.
static bool ConnectExpressionsTracked(
	UMaterialExpression* From, const FString& FromOut,
	UMaterialExpression* To, const FString& ToIn,
	const FString& FromId, const FString& ToId,
	TArray<TSharedPtr<FJsonValue>>& Unresolved)
{
	if (UMaterialEditingLibrary::ConnectMaterialExpressions(From, FromOut, To, ToIn))
	{
		return true;
	}
	// Single-input fallback: the sole pin is reachable via an empty name.
	if (!ToIn.IsEmpty() && To->CountInputs() == 1
		&& UMaterialEditingLibrary::ConnectMaterialExpressions(From, FromOut, To, FString()))
	{
		return true;
	}
	const TSharedRef<FJsonObject> U = MakeShared<FJsonObject>();
	U->SetStringField(TEXT("from"), FromId);
	U->SetStringField(TEXT("to"), ToId);
	U->SetStringField(TEXT("to_input"), ToIn);
	Unresolved.Add(MakeShared<FJsonValueObject>(U));
	return false;
}

FString UArborMaterialGraphTools::BuildMaterial(const FString& JsonSpec)
{
	TSharedPtr<FJsonObject> Spec = ParseJson(JsonSpec);
	if (!Spec.IsValid()) return JsonError(TEXT("Invalid JSON spec"));

	const FString MaterialPath = Spec->GetStringField(TEXT("path"));
	if (MaterialPath.IsEmpty()) return JsonError(TEXT("spec.path is required"));

	UMaterial* Mat = LoadOrCreateMaterial(MaterialPath);
	if (!Mat) return JsonError(FString::Printf(TEXT("Could not load or create material: %s"), *MaterialPath));

	FBuildContext Ctx;
	Ctx.Material = Mat;

	int32 ExpressionCount = 0;
	int32 ConnectionCount = 0;
	TArray<TSharedPtr<FJsonValue>> Unresolved;

	{
		// Scope the FMaterialUpdateContext so its destructor (which triggers
		// the recompile) runs only after all edits are complete.
		FMaterialUpdateContext UpdateCtx;
		UpdateCtx.AddMaterial(Mat);

		IndexExistingExpressions(Ctx);

		// 1. Expressions (create / update in place).
		TSet<FString> SpecIds;
		const TArray<TSharedPtr<FJsonValue>>* ExprArr = nullptr;
		if (Spec->TryGetArrayField(TEXT("expressions"), ExprArr) && ExprArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *ExprArr)
			{
				const TSharedPtr<FJsonObject>& ExprSpec = V->AsObject();
				if (!ExprSpec.IsValid()) continue;
				FString Err;
				if (!ApplyExpressionSpec(Ctx, ExprSpec, Err))
				{
					return JsonError(FString::Printf(TEXT("expression spec: %s"), *Err));
				}
				SpecIds.Add(ExprSpec->GetStringField(TEXT("id")));
				++ExpressionCount;
			}
		}

		// 2. Orphan cleanup: remove sentinel-IDed expressions not in spec.
		TArray<FString> ToRemove;
		for (const auto& Pair : Ctx.IdToExpression)
		{
			if (!SpecIds.Contains(Pair.Key)) ToRemove.Add(Pair.Key);
		}
		for (const FString& Id : ToRemove)
		{
			if (UMaterialExpression* Expr = Ctx.IdToExpression.FindRef(Id))
			{
				UMaterialEditingLibrary::DeleteMaterialExpression(Mat, Expr);
				Ctx.IdToExpression.Remove(Id);
			}
		}

		// 3. Connections.
		const TArray<TSharedPtr<FJsonValue>>* ConnArr = nullptr;
		if (Spec->TryGetArrayField(TEXT("connections"), ConnArr) && ConnArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *ConnArr)
			{
				const TSharedPtr<FJsonObject>& C = V->AsObject();
				if (!C.IsValid()) continue;
				const FString FromId = C->GetStringField(TEXT("from"));
				const FString ToId = C->GetStringField(TEXT("to"));
				FString FromOut; C->TryGetStringField(TEXT("from_output"), FromOut);
				FString ToIn; C->TryGetStringField(TEXT("to_input"), ToIn);
				UMaterialExpression* From = Ctx.IdToExpression.FindRef(FromId);
				UMaterialExpression* To = Ctx.IdToExpression.FindRef(ToId);
				if (!From || !To) continue;
				if (ConnectExpressionsTracked(From, FromOut, To, ToIn, FromId, ToId, Unresolved))
					++ConnectionCount;
			}
		}

		// 4. Outputs.
		const TArray<TSharedPtr<FJsonValue>>* OutArr = nullptr;
		if (Spec->TryGetArrayField(TEXT("outputs"), OutArr) && OutArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *OutArr)
			{
				const TSharedPtr<FJsonObject>& O = V->AsObject();
				if (!O.IsValid()) continue;
				const FString FromId = O->GetStringField(TEXT("from"));
				const FString PropName = O->GetStringField(TEXT("property"));
				FString FromOut; O->TryGetStringField(TEXT("from_output"), FromOut);
				UMaterialExpression* From = Ctx.IdToExpression.FindRef(FromId);
				if (!From) continue;
				EMaterialProperty Prop;
				if (!ResolveOutputProperty(PropName, Prop)) continue;
				UMaterialEditingLibrary::ConnectMaterialProperty(From, FromOut, Prop);
			}
		}

		// 5. Flags.
		const TSharedPtr<FJsonObject>* FlagsObj = nullptr;
		if (Spec->TryGetObjectField(TEXT("flags"), FlagsObj) && FlagsObj && FlagsObj->IsValid())
		{
			bool TwoSided = false;
			if ((*FlagsObj)->TryGetBoolField(TEXT("two_sided"), TwoSided)) Mat->TwoSided = TwoSided;

			FString BlendStr;
			if ((*FlagsObj)->TryGetStringField(TEXT("blend_mode"), BlendStr))
			{
				const int64 Val = StaticEnum<EBlendMode>()->GetValueByNameString(BlendStr);
				if (Val != INDEX_NONE) Mat->BlendMode = (EBlendMode)Val;
			}
			FString ShadingStr;
			if ((*FlagsObj)->TryGetStringField(TEXT("shading_model"), ShadingStr))
			{
				const int64 Val = StaticEnum<EMaterialShadingModel>()->GetValueByNameString(ShadingStr);
				if (Val != INDEX_NONE) Mat->SetShadingModel((EMaterialShadingModel)Val);
			}
		}

		Mat->MarkPackageDirty();
		// UpdateCtx destructor here triggers the recompile.
	}

	// Auto-arrange nodes (default on) so Arbor-built graphs aren't a pile of
	// overlapping nodes. Layout only moves nodes - no recompile. Overrides any
	// manual x/y; pass "auto_layout": false to keep hand-placed positions.
	bool bAutoLayout = true;
	Spec->TryGetBoolField(TEXT("auto_layout"), bAutoLayout);
	if (bAutoLayout)
	{
		UMaterialEditingLibrary::LayoutMaterialExpressions(Mat);
	}

	UEditorAssetLibrary::SaveLoadedAsset(Mat);

	const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("material_path"), MaterialPath);
	Out->SetNumberField(TEXT("expression_count"), ExpressionCount);
	Out->SetNumberField(TEXT("connection_count"), ConnectionCount);
	if (Unresolved.Num() > 0) Out->SetArrayField(TEXT("unresolved_connections"), Unresolved);
	return JsonOk(Out);
}

// ============================================================================
// RenderMaterialThumbnail
// ============================================================================

FString UArborMaterialGraphTools::RenderMaterialThumbnail(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> Params = ParseJson(ParamsJson);
	if (!Params.IsValid()) return JsonError(TEXT("Invalid JSON"));

	const FString MaterialPath = Params->GetStringField(TEXT("material_path"));
	const FString OutputPath = Params->GetStringField(TEXT("output_path"));
	int32 Width = 256, Height = 256;
	Params->TryGetNumberField(TEXT("width"), Width);
	Params->TryGetNumberField(TEXT("height"), Height);

	// Accept either a UMaterial master or a UMaterialInstance (so the catalog
	// can preview a representative MI with real parameter overrides applied).
	UObject* Asset = UEditorAssetLibrary::LoadAsset(MaterialPath);
	UMaterialInterface* MatInterface = Cast<UMaterialInterface>(Asset);
	if (!MatInterface) return JsonError(FString::Printf(
		TEXT("Material or MaterialInstance not found: %s"), *MaterialPath));

	// NOTE: a material whose shaders are still compiling renders here as the
	// default (lit grey) material - a silent wrong result. Callers must ensure
	// the material's shaders are ready first: run `r.ShaderCompiler.AsyncCompiling 0`
	// then `MaterialEditingLibrary.recompile_material(m)` in the SAME call before
	// render_thumbnail. (An in-renderer auto-flush was attempted but the thumbnail
	// pipeline requests shaders lazily per feature-level/vertex-factory, so a
	// flush here does not reliably catch them.)

	// UThumbnailManager dispatches to the right ThumbnailRenderer for the
	// object's class — FMaterialThumbnailRenderer for masters,
	// FMaterialInstanceThumbnailRenderer for instances.
	FObjectThumbnail Thumb;
	ThumbnailTools::RenderThumbnail(
		MatInterface,
		Width, Height,
		ThumbnailTools::EThumbnailTextureFlushMode::AlwaysFlush,
		nullptr,
		&Thumb);

	const TArray<uint8>& RawBytes = Thumb.GetUncompressedImageData();
	if (RawBytes.Num() == 0)
	{
		return JsonError(TEXT("Thumbnail render produced no bytes - is the material valid?"));
	}

	const int32 ActualWidth = Thumb.GetImageWidth();
	const int32 ActualHeight = Thumb.GetImageHeight();

	// PNG-encode via ImageWrapper. UE thumbnails come back as BGRA8.
	IImageWrapperModule& ImageWrapperModule =
		FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> PngWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!PngWrapper.IsValid())
	{
		return JsonError(TEXT("Could not create PNG ImageWrapper"));
	}
	if (!PngWrapper->SetRaw(RawBytes.GetData(), RawBytes.Num(),
		ActualWidth, ActualHeight, ERGBFormat::BGRA, 8))
	{
		return JsonError(TEXT("ImageWrapper SetRaw failed"));
	}
	const TArray64<uint8> PngBytes = PngWrapper->GetCompressed(95);

	// Ensure parent directory exists, then write the PNG.
	const FString ParentDir = FPaths::GetPath(OutputPath);
	if (!ParentDir.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*ParentDir, /*Tree=*/true);
	}
	if (!FFileHelper::SaveArrayToFile(PngBytes, *OutputPath))
	{
		return JsonError(FString::Printf(TEXT("Failed to write PNG to %s"), *OutputPath));
	}

	const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("output_path"), OutputPath);
	Out->SetNumberField(TEXT("width"), ActualWidth);
	Out->SetNumberField(TEXT("height"), ActualHeight);
	return JsonOk(Out);
}

// ============================================================================
// Phase 7: Material Function authoring
//
// Mirrors QueryMaterial / BuildMaterial but operates on a UMaterialFunction.
// Reuses every file-scope helper above (GetArborId/SetArborId,
// ResolveExpressionClass, ApplyExpressionProperties, ReadMaterialProperty) so
// the only function-specific code is asset load/create, the create/delete API
// (CreateMaterialExpressionInFunction / DeleteMaterialExpressionInFunction), and
// input/output enumeration. Functions have no material-output pins or flags.
// ============================================================================

namespace
{
	UMaterialFunction* LoadMaterialFunction(const FString& Path)
	{
		UObject* Asset = UEditorAssetLibrary::LoadAsset(Path);
		return Cast<UMaterialFunction>(Asset);
	}

	// UE's UMaterialEditingLibrary::LayoutMaterialFunctionExpressions only positions
	// FunctionInput nodes when the function has inputs (it walks backward FROM the
	// inputs, which dead-ends, leaving every other node piled at the origin). Do a
	// proper layered layout: column = longest backward distance from the FunctionOutput
	// nodes (output column 0 at the right, inputs furthest left), stacked within columns.
	void LayoutFunctionGraph(UMaterialFunction* Func)
	{
		if (!Func) return;
		const TArray<TObjectPtr<UMaterialExpression>>& Exprs = GetFunctionExpressions(Func);

		// Roots = FunctionOutput nodes (fallback: everything, if the function has none).
		TArray<UMaterialExpression*> Roots;
		for (const TObjectPtr<UMaterialExpression>& E : Exprs)
		{
			if (E && E->IsA<UMaterialExpressionFunctionOutput>()) Roots.Add(E);
		}
		if (Roots.Num() == 0)
		{
			for (const TObjectPtr<UMaterialExpression>& E : Exprs) { if (E) Roots.Add(E); }
		}

		// Longest-path column, walking backward through each node's inputs.
		TMap<UMaterialExpression*, int32> Column;
		TArray<TPair<UMaterialExpression*, int32>> Stack;
		for (UMaterialExpression* R : Roots) Stack.Add(TPair<UMaterialExpression*, int32>(R, 0));
		while (Stack.Num() > 0)
		{
			const TPair<UMaterialExpression*, int32> Cur = Stack.Pop();
			UMaterialExpression* E = Cur.Key;
			if (!E) continue;
			const int32 Col = Cur.Value;
			if (int32* Existing = Column.Find(E)) { if (*Existing >= Col) continue; }
			Column.Add(E, Col);
			const int32 NumInputs = E->CountInputs();
			for (int32 i = 0; i < NumInputs; ++i)
			{
				FExpressionInput* In = E->GetInput(i);
				if (In && In->Expression) Stack.Add(TPair<UMaterialExpression*, int32>(In->Expression, Col + 1));
			}
		}
		for (const TObjectPtr<UMaterialExpression>& E : Exprs)  // disconnected -> column 0
		{
			if (E && !Column.Contains(E)) Column.Add(E, 0);
		}

		// Place: X by column (output right, inputs left); stack vertically per column.
		static const int32 ColWidth = 300;
		static const int32 RowPadding = 40;
		TMap<int32, int32> ColumnY;
		for (const TObjectPtr<UMaterialExpression>& E : Exprs)
		{
			if (!E) continue;
			const int32 Col = Column[E];
			int32& Y = ColumnY.FindOrAdd(Col);
			E->MaterialExpressionEditorX = -ColWidth * Col;
			E->MaterialExpressionEditorY = Y;
			Y += E->GetHeight() + RowPadding;
		}
	}

	UMaterialFunction* LoadOrCreateMaterialFunction(const FString& Path, bool& bOutCreated)
	{
		bOutCreated = false;
		if (UMaterialFunction* Existing = LoadMaterialFunction(Path)) return Existing;

		FString ContentPath, AssetName;
		if (!Path.Split(TEXT("/"), &ContentPath, &AssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
		{
			return nullptr;
		}
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UMaterialFunctionFactoryNew* Factory = NewObject<UMaterialFunctionFactoryNew>();
		UMaterialFunction* Created = Cast<UMaterialFunction>(
			AssetTools.CreateAsset(AssetName, ContentPath, UMaterialFunction::StaticClass(), Factory));
		bOutCreated = (Created != nullptr);
		return Created;
	}

	struct FFunctionBuildContext
	{
		UMaterialFunction* Function = nullptr;
		TMap<FString, UMaterialExpression*> IdToExpression;
	};

	void IndexExistingFunctionExpressions(FFunctionBuildContext& Ctx)
	{
		for (UMaterialExpression* Expr : GetFunctionExpressions(Ctx.Function))
		{
			if (!Expr) continue;
			const FString Id = GetArborId(Expr);
			if (!Id.IsEmpty()) Ctx.IdToExpression.Add(Id, Expr);
		}
	}

	bool ApplyFunctionExpressionSpec(FFunctionBuildContext& Ctx, const TSharedPtr<FJsonObject>& ExprSpec, FString& OutError)
	{
		const FString Id = ExprSpec->GetStringField(TEXT("id"));
		if (Id.IsEmpty()) { OutError = TEXT("expression missing 'id'"); return false; }

		const FString ClassName = ExprSpec->GetStringField(TEXT("class"));
		UClass* ExprClass = ResolveExpressionClass(ClassName);
		if (!ExprClass) { OutError = FString::Printf(TEXT("class not found: %s"), *ClassName); return false; }

		int32 X = 0, Y = 0;
		ExprSpec->TryGetNumberField(TEXT("x"), X);
		ExprSpec->TryGetNumberField(TEXT("y"), Y);

		UMaterialExpression* Expr = nullptr;
		if (UMaterialExpression** Existing = Ctx.IdToExpression.Find(Id))
		{
			if ((*Existing)->GetClass() == ExprClass)
			{
				Expr = *Existing;
				Expr->MaterialExpressionEditorX = X;
				Expr->MaterialExpressionEditorY = Y;
			}
			else
			{
				UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(Ctx.Function, *Existing);
				Ctx.IdToExpression.Remove(Id);
			}
		}
		if (!Expr)
		{
			Expr = UMaterialEditingLibrary::CreateMaterialExpressionInFunction(Ctx.Function, ExprClass, X, Y);
			if (!Expr) { OutError = TEXT("CreateMaterialExpressionInFunction returned null"); return false; }
			SetArborId(Expr, Id);
			Ctx.IdToExpression.Add(Id, Expr);
		}

		const TSharedPtr<FJsonObject>* PropsObj = nullptr;
		if (ExprSpec->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj && PropsObj->IsValid())
		{
			ApplyExpressionProperties(Expr, *PropsObj);
		}
		ApplyCustomNodeInputs(Expr, ExprSpec);
		return true;
	}

	/** Collect FunctionInput/FunctionOutput summaries, ordered by SortPriority. */
	void CollectFunctionIO(UMaterialFunction* Function,
		TArray<TSharedPtr<FJsonValue>>& OutInputs, TArray<TSharedPtr<FJsonValue>>& OutOutputs)
	{
		struct FIn { FString Name; FString Type; int32 Sort; };
		struct FOut { FString Name; int32 Sort; };
		TArray<FIn> Ins;
		TArray<FOut> Outs;
		for (UMaterialExpression* Expr : GetFunctionExpressions(Function))
		{
			if (auto* In = Cast<UMaterialExpressionFunctionInput>(Expr))
			{
				FIn E;
				E.Name = In->InputName.ToString();
				E.Type = StaticEnum<EFunctionInputType>()->GetNameStringByValue((int64)In->InputType.GetValue());
				E.Sort = In->SortPriority;
				Ins.Add(E);
			}
			else if (auto* O = Cast<UMaterialExpressionFunctionOutput>(Expr))
			{
				FOut E;
				E.Name = O->OutputName.ToString();
				E.Sort = O->SortPriority;
				Outs.Add(E);
			}
		}
		Ins.Sort([](const FIn& A, const FIn& B) { return A.Sort < B.Sort; });
		Outs.Sort([](const FOut& A, const FOut& B) { return A.Sort < B.Sort; });
		for (const FIn& E : Ins)
		{
			const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), E.Name);
			O->SetStringField(TEXT("type"), E.Type);
			O->SetNumberField(TEXT("sort"), E.Sort);
			OutInputs.Add(MakeShared<FJsonValueObject>(O));
		}
		for (const FOut& E : Outs)
		{
			const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), E.Name);
			O->SetNumberField(TEXT("sort"), E.Sort);
			OutOutputs.Add(MakeShared<FJsonValueObject>(O));
		}
	}
}

FString UArborMaterialGraphTools::QueryMaterialFunction(const FString& FunctionPath)
{
	UMaterialFunction* Func = LoadMaterialFunction(FunctionPath);
	if (!Func) return JsonError(FString::Printf(TEXT("Material function not found: %s"), *FunctionPath));

	const TArray<TObjectPtr<UMaterialExpression>>& Expressions = GetFunctionExpressions(Func);

	// Stable index per expression (same scheme as QueryMaterial) so non-Arbor
	// authored functions can still be referenced.
	TMap<UMaterialExpression*, int32> ExprToIdx;
	int32 NextIdx = 0;
	for (UMaterialExpression* Expr : Expressions)
	{
		if (Expr) ExprToIdx.Add(Expr, NextIdx++);
	}

	TArray<TSharedPtr<FJsonValue>> ExprArr;
	TArray<TSharedPtr<FJsonValue>> ConnArr;

	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr) continue;
		const int32 ExprIdx = ExprToIdx[Expr];
		const TSharedRef<FJsonObject> ExprObj = MakeShared<FJsonObject>();
		const FString Id = GetArborId(Expr);
		ExprObj->SetNumberField(TEXT("idx"), ExprIdx);
		ExprObj->SetStringField(TEXT("id"), Id);
		ExprObj->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
		ExprObj->SetNumberField(TEXT("x"), Expr->MaterialExpressionEditorX);
		ExprObj->SetNumberField(TEXT("y"), Expr->MaterialExpressionEditorY);

		const TSharedRef<FJsonObject> PropsObj = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> PropIt(Expr->GetClass()); PropIt; ++PropIt)
		{
			FProperty* P = *PropIt;
			if (!P->HasAnyPropertyFlags(CPF_Edit)) continue;
			TSharedPtr<FJsonValue> Val = ReadMaterialProperty(Expr, P);
			if (Val.IsValid()) PropsObj->SetField(P->GetName(), Val);
		}
		ExprObj->SetObjectField(TEXT("properties"), PropsObj);
		ExprArr.Add(MakeShared<FJsonValueObject>(ExprObj));

		const int32 NumInputs = Expr->CountInputs();
		UMaterialExpressionMaterialFunctionCall* ExprAsMFCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expr);
		for (int32 i = 0; i < NumInputs; ++i)
		{
			FExpressionInput* Input = Expr->GetInput(i);
			if (!Input) continue;
			UMaterialExpression* FromExpr = Input->Expression;
			if (!FromExpr) continue;
			const int32* FromIdxPtr = ExprToIdx.Find(FromExpr);
			const TSharedRef<FJsonObject> ConnObj = MakeShared<FJsonObject>();
			ConnObj->SetStringField(TEXT("from_id"), GetArborId(FromExpr));
			ConnObj->SetNumberField(TEXT("from_idx"), FromIdxPtr ? *FromIdxPtr : -1);
			FString FromOutputName;
			if (FromExpr->Outputs.IsValidIndex(Input->OutputIndex))
			{
				FromOutputName = FromExpr->Outputs[Input->OutputIndex].OutputName.ToString();
			}
			ConnObj->SetStringField(TEXT("from_output"), FromOutputName);
			ConnObj->SetStringField(TEXT("to_id"), Id);
			ConnObj->SetNumberField(TEXT("to_idx"), ExprIdx);

			FName ToInputLookupName;
			if (ExprAsMFCall)
			{
				ToInputLookupName = ExprAsMFCall->GetInputNameWithType(i, /*bWithType=*/false);
			}
			else
			{
				ToInputLookupName = UMaterialGraphNode::GetShortenPinName(Expr->GetInputName(i));
			}
			ConnObj->SetStringField(TEXT("to_input"), ToInputLookupName.IsNone() ? FString() : ToInputLookupName.ToString());
			ConnArr.Add(MakeShared<FJsonValueObject>(ConnObj));
		}
	}

	TArray<TSharedPtr<FJsonValue>> InputsArr, OutputsArr;
	CollectFunctionIO(Func, InputsArr, OutputsArr);

	TArray<TSharedPtr<FJsonValue>> Categories;
	for (const FText& Cat : Func->LibraryCategoriesText)
	{
		Categories.Add(MakeShared<FJsonValueString>(Cat.ToString()));
	}

	const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("function_path"), FunctionPath);
	Out->SetStringField(TEXT("description"), Func->Description);
	Out->SetBoolField(TEXT("expose_to_library"), Func->bExposeToLibrary != 0);
	Out->SetArrayField(TEXT("library_categories"), Categories);
	Out->SetArrayField(TEXT("expressions"), ExprArr);
	Out->SetArrayField(TEXT("connections"), ConnArr);
	Out->SetArrayField(TEXT("inputs"), InputsArr);
	Out->SetArrayField(TEXT("outputs"), OutputsArr);
	return JsonOk(Out);
}

FString UArborMaterialGraphTools::BuildMaterialFunction(const FString& JsonSpec)
{
	TSharedPtr<FJsonObject> Spec = ParseJson(JsonSpec);
	if (!Spec.IsValid()) return JsonError(TEXT("Invalid JSON spec"));

	const FString FunctionPath = Spec->GetStringField(TEXT("path"));
	if (FunctionPath.IsEmpty()) return JsonError(TEXT("spec.path is required"));

	bool bCreated = false;
	UMaterialFunction* Func = LoadOrCreateMaterialFunction(FunctionPath, bCreated);
	if (!Func) return JsonError(FString::Printf(TEXT("Could not load or create material function: %s"), *FunctionPath));

	// On a fresh asset, clear any factory/editor-seeded default nodes so the
	// graph is exactly what the spec describes (the spec carries its own
	// FunctionOutput). Existing functions are managed by sentinel ID instead.
	if (bCreated)
	{
		UMaterialEditingLibrary::DeleteAllMaterialExpressionsInFunction(Func);
	}

	FFunctionBuildContext Ctx;
	Ctx.Function = Func;

	int32 ExpressionCount = 0;
	int32 ConnectionCount = 0;
	TArray<TSharedPtr<FJsonValue>> Unresolved;

	IndexExistingFunctionExpressions(Ctx);

	// 1. Expressions (create / update in place).
	TSet<FString> SpecIds;
	const TArray<TSharedPtr<FJsonValue>>* ExprArr = nullptr;
	if (Spec->TryGetArrayField(TEXT("expressions"), ExprArr) && ExprArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *ExprArr)
		{
			const TSharedPtr<FJsonObject>& ExprSpec = V->AsObject();
			if (!ExprSpec.IsValid()) continue;
			FString Err;
			if (!ApplyFunctionExpressionSpec(Ctx, ExprSpec, Err))
			{
				return JsonError(FString::Printf(TEXT("expression spec: %s"), *Err));
			}
			SpecIds.Add(ExprSpec->GetStringField(TEXT("id")));
			++ExpressionCount;
		}
	}

	// 2. Orphan cleanup.
	TArray<FString> ToRemove;
	for (const auto& Pair : Ctx.IdToExpression)
	{
		if (!SpecIds.Contains(Pair.Key)) ToRemove.Add(Pair.Key);
	}
	for (const FString& Id : ToRemove)
	{
		if (UMaterialExpression* Expr = Ctx.IdToExpression.FindRef(Id))
		{
			UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(Func, Expr);
			Ctx.IdToExpression.Remove(Id);
		}
	}

	// 3. Connections (expression-to-expression; FunctionOutput's input pin is
	//    unnamed, so wiring into an output uses an empty to_input).
	const TArray<TSharedPtr<FJsonValue>>* ConnArr = nullptr;
	if (Spec->TryGetArrayField(TEXT("connections"), ConnArr) && ConnArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *ConnArr)
		{
			const TSharedPtr<FJsonObject>& C = V->AsObject();
			if (!C.IsValid()) continue;
			const FString FromId = C->GetStringField(TEXT("from"));
			const FString ToId = C->GetStringField(TEXT("to"));
			FString FromOut; C->TryGetStringField(TEXT("from_output"), FromOut);
			FString ToIn; C->TryGetStringField(TEXT("to_input"), ToIn);
			UMaterialExpression* From = Ctx.IdToExpression.FindRef(FromId);
			UMaterialExpression* To = Ctx.IdToExpression.FindRef(ToId);
			if (!From || !To) continue;
			if (ConnectExpressionsTracked(From, FromOut, To, ToIn, FromId, ToId, Unresolved))
				++ConnectionCount;
		}
	}

	// 4. Function metadata (no flags / shading model on a function).
	FString Description;
	if (Spec->TryGetStringField(TEXT("description"), Description)) Func->Description = Description;

	bool bExpose = false;
	if (Spec->TryGetBoolField(TEXT("expose_to_library"), bExpose)) Func->bExposeToLibrary = bExpose ? 1 : 0;

	const TArray<TSharedPtr<FJsonValue>>* CatArr = nullptr;
	if (Spec->TryGetArrayField(TEXT("library_categories"), CatArr) && CatArr)
	{
		Func->LibraryCategoriesText.Reset();
		for (const TSharedPtr<FJsonValue>& V : *CatArr)
		{
			Func->LibraryCategoriesText.Add(FText::FromString(V->AsString()));
		}
	}

	Func->MarkPackageDirty();

	// 5. Finalise: rebuild input/output metadata and propagate to any materials
	//    referencing this function. Functions do not self-recompile.
	UMaterialEditingLibrary::UpdateMaterialFunction(Func, nullptr);

	// Auto-arrange nodes (default on). Overrides manual x/y; pass
	// "auto_layout": false to keep hand-placed positions.
	bool bAutoLayout = true;
	Spec->TryGetBoolField(TEXT("auto_layout"), bAutoLayout);
	if (bAutoLayout)
	{
		LayoutFunctionGraph(Func);
	}

	UEditorAssetLibrary::SaveLoadedAsset(Func);

	TArray<TSharedPtr<FJsonValue>> InputsArr, OutputsArr;
	CollectFunctionIO(Func, InputsArr, OutputsArr);

	const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("function_path"), FunctionPath);
	Out->SetNumberField(TEXT("expression_count"), ExpressionCount);
	Out->SetNumberField(TEXT("connection_count"), ConnectionCount);
	Out->SetArrayField(TEXT("inputs"), InputsArr);
	Out->SetArrayField(TEXT("outputs"), OutputsArr);
	if (Unresolved.Num() > 0) Out->SetArrayField(TEXT("unresolved_connections"), Unresolved);
	return JsonOk(Out);
}

// ============================================================================
// LayoutMaterial - auto-arrange a material / material function's nodes
// ============================================================================

FString UArborMaterialGraphTools::LayoutMaterial(const FString& JsonParams)
{
	TSharedPtr<FJsonObject> Params = ParseJson(JsonParams);
	if (!Params.IsValid()) return JsonError(TEXT("Invalid JSON"));

	FString Path;
	if (!Params->TryGetStringField(TEXT("path"), Path))
		Params->TryGetStringField(TEXT("material_path"), Path);
	if (Path.IsEmpty()) return JsonError(TEXT("path is required"));

	UObject* Asset = UEditorAssetLibrary::LoadAsset(Path);
	if (!Asset) return JsonError(FString::Printf(TEXT("Asset not found: %s"), *Path));

	const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("path"), Path);

	if (UMaterial* Mat = Cast<UMaterial>(Asset))
	{
		UMaterialEditingLibrary::LayoutMaterialExpressions(Mat);
		Mat->MarkPackageDirty();
		Out->SetStringField(TEXT("type"), TEXT("material"));
		Out->SetNumberField(TEXT("expression_count"), GetExpressions(Mat).Num());
	}
	else if (UMaterialFunction* Func = Cast<UMaterialFunction>(Asset))
	{
		LayoutFunctionGraph(Func);
		Func->MarkPackageDirty();
		Out->SetStringField(TEXT("type"), TEXT("material_function"));
		Out->SetNumberField(TEXT("expression_count"), GetFunctionExpressions(Func).Num());
	}
	else
	{
		return JsonError(FString::Printf(TEXT("Asset is not a Material or MaterialFunction: %s"), *Path));
	}

	UEditorAssetLibrary::SaveLoadedAsset(Asset);
	return JsonOk(Out);
}
