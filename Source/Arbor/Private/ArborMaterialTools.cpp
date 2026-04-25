#include "ArborMaterialTools.h"
#include "ArborActorTools.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
#include "Engine/Texture2D.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "EditorAssetLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// ============================================================================
// Helpers
// ============================================================================

static FString SerializeJson(TSharedPtr<FJsonObject> Root)
{
	FString Output;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}

static TSharedPtr<FJsonObject> ParseJson(const FString& Json)
{
	TSharedPtr<FJsonObject> Obj;
	auto Reader = TJsonReaderFactory<>::Create(Json);
	FJsonSerializer::Deserialize(Reader, Obj);
	return Obj;
}

static double GetOpt(const TSharedPtr<FJsonObject>& Obj, const FString& Key, double Default)
{
	double Val;
	if (Obj->TryGetNumberField(Key, Val)) return Val;
	return Default;
}

static FString GetOptStr(const TSharedPtr<FJsonObject>& Obj, const FString& Key, const FString& Default = TEXT(""))
{
	FString Val;
	if (Obj->TryGetStringField(Key, Val)) return Val;
	return Default;
}

static FString MakeSuccessResult(const FString& AssetPath)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	return SerializeJson(Root);
}

static FString MakeErrorResult(const FString& Error)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), false);
	Root->SetStringField(TEXT("error"), Error);
	return SerializeJson(Root);
}

static void ConnectTexture(UMaterial* Mat, const FString& TexturePath, EMaterialProperty Property)
{
	UTexture* Tex = Cast<UTexture>(UEditorAssetLibrary::LoadAsset(TexturePath));
	if (!Tex)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ArborMaterialTools] Texture not found: %s"), *TexturePath);
		return;
	}

	UMaterialExpressionTextureSample* Node =
		Cast<UMaterialExpressionTextureSample>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Mat, UMaterialExpressionTextureSample::StaticClass(), 0, 0));
	if (Node)
	{
		Node->Texture = Tex;
		UMaterialEditingLibrary::ConnectMaterialProperty(Node, TEXT("RGB"), Property);
	}
}

static void ConnectWorldAlignedTexture(UMaterial* Mat, const FString& TexturePath,
	EMaterialProperty Property, float TilingScale, bool bIsNormal)
{
	UTexture* Tex = Cast<UTexture>(UEditorAssetLibrary::LoadAsset(TexturePath));
	if (!Tex)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ArborMaterialTools] Texture not found: %s"), *TexturePath);
		return;
	}

	// Texture object node
	UMaterialExpressionTextureObject* TexObj =
		Cast<UMaterialExpressionTextureObject>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Mat, UMaterialExpressionTextureObject::StaticClass(), 0, 0));
	if (!TexObj) return;
	TexObj->Texture = Tex;

	// Material function call
	FString FuncPath = bIsNormal
		? TEXT("/Engine/Functions/Engine_MaterialFunctions02/WorldAlignedNormal")
		: TEXT("/Engine/Functions/Engine_MaterialFunctions02/WorldAlignedTexture");
	UMaterialFunction* Func = Cast<UMaterialFunction>(UEditorAssetLibrary::LoadAsset(FuncPath));
	if (!Func)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ArborMaterialTools] Material function not found: %s"), *FuncPath);
		return;
	}

	UMaterialExpressionMaterialFunctionCall* FuncCall =
		Cast<UMaterialExpressionMaterialFunctionCall>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Mat, UMaterialExpressionMaterialFunctionCall::StaticClass(), 0, 0));
	if (!FuncCall) return;
	FuncCall->SetMaterialFunction(Func);

	// Texture size constant
	UMaterialExpressionConstant3Vector* SizeNode =
		Cast<UMaterialExpressionConstant3Vector>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Mat, UMaterialExpressionConstant3Vector::StaticClass(), 0, 0));
	if (!SizeNode) return;
	SizeNode->Constant = FLinearColor(TilingScale, TilingScale, TilingScale, 0.0f);

	// Wire connections
	UMaterialEditingLibrary::ConnectMaterialExpressions(TexObj, TEXT(""), FuncCall, TEXT("TextureObject"));
	UMaterialEditingLibrary::ConnectMaterialExpressions(SizeNode, TEXT(""), FuncCall, TEXT("TextureSize"));
	UMaterialEditingLibrary::ConnectMaterialProperty(FuncCall, TEXT("XYZ"), Property);
}

// ============================================================================
// Public API
// ============================================================================

FString UArborMaterialTools::CreateMaterial(const FString& ParamsJson)
{
	auto Params = ParseJson(ParamsJson);
	if (!Params.IsValid()) return MakeErrorResult(TEXT("Invalid JSON"));

	const FString Name = Params->GetStringField(TEXT("name"));
	const FString ContentPath = GetOptStr(Params, TEXT("content_path"), TEXT("/Game/Materials"));
	const double Metallic = GetOpt(Params, TEXT("metallic"), 0.0);
	const double Roughness = GetOpt(Params, TEXT("roughness"), 0.5);

	// Parse color
	double R = 1.0, G = 1.0, B = 1.0;
	const TArray<TSharedPtr<FJsonValue>>* ColorArr;
	if (Params->TryGetArrayField(TEXT("color"), ColorArr) && ColorArr->Num() >= 3)
	{
		R = (*ColorArr)[0]->AsNumber();
		G = (*ColorArr)[1]->AsNumber();
		B = (*ColorArr)[2]->AsNumber();
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
	UMaterial* Mat = Cast<UMaterial>(AssetTools.CreateAsset(Name, ContentPath, UMaterial::StaticClass(), Factory));
	if (!Mat) return MakeErrorResult(FString::Printf(TEXT("Failed to create material '%s'"), *Name));

	// Base color constant
	UMaterialExpressionConstant3Vector* ColorNode =
		Cast<UMaterialExpressionConstant3Vector>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Mat, UMaterialExpressionConstant3Vector::StaticClass(), 0, 0));
	if (ColorNode)
	{
		ColorNode->Constant = FLinearColor(R, G, B, 1.0f);
		UMaterialEditingLibrary::ConnectMaterialProperty(ColorNode, TEXT(""), MP_BaseColor);
	}

	// Metallic constant
	UMaterialExpressionConstant* MetNode =
		Cast<UMaterialExpressionConstant>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Mat, UMaterialExpressionConstant::StaticClass(), 0, 0));
	if (MetNode)
	{
		MetNode->R = Metallic;
		UMaterialEditingLibrary::ConnectMaterialProperty(MetNode, TEXT(""), MP_Metallic);
	}

	// Roughness constant
	UMaterialExpressionConstant* RoughNode =
		Cast<UMaterialExpressionConstant>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Mat, UMaterialExpressionConstant::StaticClass(), 0, 0));
	if (RoughNode)
	{
		RoughNode->R = Roughness;
		UMaterialEditingLibrary::ConnectMaterialProperty(RoughNode, TEXT(""), MP_Roughness);
	}

	UMaterialEditingLibrary::RecompileMaterial(Mat);
	UEditorAssetLibrary::SaveLoadedAsset(Mat);

	return MakeSuccessResult(FString::Printf(TEXT("%s/%s"), *ContentPath, *Name));
}

FString UArborMaterialTools::CreateMaterialFromTextures(const FString& ParamsJson)
{
	auto Params = ParseJson(ParamsJson);
	if (!Params.IsValid()) return MakeErrorResult(TEXT("Invalid JSON"));

	const FString Name = Params->GetStringField(TEXT("name"));
	const FString ContentPath = GetOptStr(Params, TEXT("content_path"), TEXT("/Game/Materials"));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
	UMaterial* Mat = Cast<UMaterial>(AssetTools.CreateAsset(Name, ContentPath, UMaterial::StaticClass(), Factory));
	if (!Mat) return MakeErrorResult(FString::Printf(TEXT("Failed to create material '%s'"), *Name));

	FString TexPath;
	if (Params->TryGetStringField(TEXT("base_color_path"), TexPath) && !TexPath.IsEmpty())
		ConnectTexture(Mat, TexPath, MP_BaseColor);
	if (Params->TryGetStringField(TEXT("normal_path"), TexPath) && !TexPath.IsEmpty())
		ConnectTexture(Mat, TexPath, MP_Normal);
	if (Params->TryGetStringField(TEXT("roughness_path"), TexPath) && !TexPath.IsEmpty())
		ConnectTexture(Mat, TexPath, MP_Roughness);
	if (Params->TryGetStringField(TEXT("metallic_path"), TexPath) && !TexPath.IsEmpty())
		ConnectTexture(Mat, TexPath, MP_Metallic);

	UMaterialEditingLibrary::RecompileMaterial(Mat);
	UEditorAssetLibrary::SaveLoadedAsset(Mat);

	return MakeSuccessResult(FString::Printf(TEXT("%s/%s"), *ContentPath, *Name));
}

FString UArborMaterialTools::CreateParameterizedPBRMaterial(const FString& ParamsJson)
{
	auto Params = ParseJson(ParamsJson);
	if (!Params.IsValid()) return MakeErrorResult(TEXT("Invalid JSON"));

	const FString Name = Params->GetStringField(TEXT("name"));
	const FString ContentPath = GetOptStr(Params, TEXT("content_path"), TEXT("/Game/Materials"));
	const float DefaultTiling = GetOpt(Params, TEXT("default_tiling"), 1.0);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
	UMaterial* Mat = Cast<UMaterial>(AssetTools.CreateAsset(Name, ContentPath, UMaterial::StaticClass(), Factory));
	if (!Mat) return MakeErrorResult(FString::Printf(TEXT("Failed to create material '%s'"), *Name));

	// Tiling scalar parameter
	UMaterialExpressionScalarParameter* TilingParam =
		Cast<UMaterialExpressionScalarParameter>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Mat, UMaterialExpressionScalarParameter::StaticClass(), 0, 0));
	if (TilingParam)
	{
		TilingParam->ParameterName = TEXT("Tiling");
		TilingParam->DefaultValue = DefaultTiling;
	}

	// Texture coordinate
	UMaterialExpressionTextureCoordinate* TexCoord =
		Cast<UMaterialExpressionTextureCoordinate>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Mat, UMaterialExpressionTextureCoordinate::StaticClass(), 0, 0));

	// Multiply(TexCoord, Tiling)
	UMaterialExpressionMultiply* Multiply =
		Cast<UMaterialExpressionMultiply>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Mat, UMaterialExpressionMultiply::StaticClass(), 0, 0));

	if (TexCoord && TilingParam && Multiply)
	{
		UMaterialEditingLibrary::ConnectMaterialExpressions(TexCoord, TEXT(""), Multiply, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(TilingParam, TEXT(""), Multiply, TEXT("B"));
	}

	// PBR texture parameter nodes
	struct FChannelDef
	{
		const TCHAR* ParamName;
		const TCHAR* OutputName;
		EMaterialProperty Property;
	};
	const FChannelDef Channels[] = {
		{ TEXT("Albedo"),    TEXT("RGB"), MP_BaseColor },
		{ TEXT("Normal"),    TEXT("RGB"), MP_Normal },
		{ TEXT("Roughness"), TEXT("R"),   MP_Roughness },
		{ TEXT("Metallic"),  TEXT("R"),   MP_Metallic },
		{ TEXT("AO"),        TEXT("R"),   MP_AmbientOcclusion },
	};

	for (const auto& Ch : Channels)
	{
		UMaterialExpressionTextureSampleParameter2D* TexNode =
			Cast<UMaterialExpressionTextureSampleParameter2D>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Mat, UMaterialExpressionTextureSampleParameter2D::StaticClass(), 0, 0));
		if (!TexNode) continue;

		TexNode->ParameterName = Ch.ParamName;

		// Connect tiled UVs
		if (Multiply)
		{
			UMaterialEditingLibrary::ConnectMaterialExpressions(Multiply, TEXT(""), TexNode, TEXT("UVs"));
		}

		// Connect to material output
		UMaterialEditingLibrary::ConnectMaterialProperty(TexNode, Ch.OutputName, Ch.Property);
	}

	UMaterialEditingLibrary::RecompileMaterial(Mat);
	UEditorAssetLibrary::SaveLoadedAsset(Mat);

	return MakeSuccessResult(FString::Printf(TEXT("%s/%s"), *ContentPath, *Name));
}

FString UArborMaterialTools::CreateMaterialInstance(const FString& ParamsJson)
{
	auto Params = ParseJson(ParamsJson);
	if (!Params.IsValid()) return MakeErrorResult(TEXT("Invalid JSON"));

	const FString ParentPath = Params->GetStringField(TEXT("parent_path"));
	const FString Name = Params->GetStringField(TEXT("name"));
	const FString ContentPath = GetOptStr(Params, TEXT("content_path"), TEXT("/Game/Materials"));

	UMaterialInterface* Parent = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(ParentPath));
	if (!Parent) return MakeErrorResult(FString::Printf(TEXT("Parent material not found: %s"), *ParentPath));

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
	UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(
		AssetTools.CreateAsset(Name, ContentPath, UMaterialInstanceConstant::StaticClass(), Factory));
	if (!MIC) return MakeErrorResult(FString::Printf(TEXT("Failed to create material instance '%s'"), *Name));

	MIC->SetParentEditorOnly(Parent);

	// Apply parameters
	const TSharedPtr<FJsonObject>* ParamsObj;
	if (Params->TryGetObjectField(TEXT("params"), ParamsObj) && ParamsObj->IsValid())
	{
		for (const auto& Pair : (*ParamsObj)->Values)
		{
			const FString& ParamName = Pair.Key;
			const TSharedPtr<FJsonValue>& Value = Pair.Value;

			if (Value->Type == EJson::Number)
			{
				// Scalar parameter
				UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(
					MIC, FName(*ParamName), static_cast<float>(Value->AsNumber()));
			}
			else if (Value->Type == EJson::Array)
			{
				// Vector parameter
				const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
				FLinearColor Color(
					Arr.Num() > 0 ? Arr[0]->AsNumber() : 0.0f,
					Arr.Num() > 1 ? Arr[1]->AsNumber() : 0.0f,
					Arr.Num() > 2 ? Arr[2]->AsNumber() : 0.0f,
					Arr.Num() > 3 ? Arr[3]->AsNumber() : 1.0f
				);
				UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(
					MIC, FName(*ParamName), Color);
			}
			else if (Value->Type == EJson::String)
			{
				// Texture parameter
				UTexture* Tex = Cast<UTexture>(UEditorAssetLibrary::LoadAsset(Value->AsString()));
				if (Tex)
				{
					UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(
						MIC, FName(*ParamName), Tex);
				}
			}
		}
	}

	UEditorAssetLibrary::SaveLoadedAsset(MIC);

	return MakeSuccessResult(FString::Printf(TEXT("%s/%s"), *ContentPath, *Name));
}

FString UArborMaterialTools::CreateWorldAlignedMaterial(const FString& ParamsJson)
{
	auto Params = ParseJson(ParamsJson);
	if (!Params.IsValid()) return MakeErrorResult(TEXT("Invalid JSON"));

	const FString Name = Params->GetStringField(TEXT("name"));
	const FString ContentPath = GetOptStr(Params, TEXT("content_path"), TEXT("/Game/Materials"));
	const float TilingScale = GetOpt(Params, TEXT("tiling_scale"), 200.0);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
	UMaterial* Mat = Cast<UMaterial>(AssetTools.CreateAsset(Name, ContentPath, UMaterial::StaticClass(), Factory));
	if (!Mat) return MakeErrorResult(FString::Printf(TEXT("Failed to create material '%s'"), *Name));

	FString TexPath;
	if (Params->TryGetStringField(TEXT("base_color_path"), TexPath) && !TexPath.IsEmpty())
		ConnectWorldAlignedTexture(Mat, TexPath, MP_BaseColor, TilingScale, false);
	if (Params->TryGetStringField(TEXT("normal_path"), TexPath) && !TexPath.IsEmpty())
		ConnectWorldAlignedTexture(Mat, TexPath, MP_Normal, TilingScale, true);
	if (Params->TryGetStringField(TEXT("roughness_path"), TexPath) && !TexPath.IsEmpty())
		ConnectWorldAlignedTexture(Mat, TexPath, MP_Roughness, TilingScale, false);
	if (Params->TryGetStringField(TEXT("metallic_path"), TexPath) && !TexPath.IsEmpty())
		ConnectWorldAlignedTexture(Mat, TexPath, MP_Metallic, TilingScale, false);
	if (Params->TryGetStringField(TEXT("ao_path"), TexPath) && !TexPath.IsEmpty())
		ConnectWorldAlignedTexture(Mat, TexPath, MP_AmbientOcclusion, TilingScale, false);

	UMaterialEditingLibrary::RecompileMaterial(Mat);
	UEditorAssetLibrary::SaveLoadedAsset(Mat);

	return MakeSuccessResult(FString::Printf(TEXT("%s/%s"), *ContentPath, *Name));
}

FString UArborMaterialTools::AssignMaterial(const FString& ParamsJson)
{
	auto Params = ParseJson(ParamsJson);
	if (!Params.IsValid()) return MakeErrorResult(TEXT("Invalid JSON"));

	const FString MaterialPath = Params->GetStringField(TEXT("material_path"));
	const int32 Slot = (int32)GetOpt(Params, TEXT("slot"), 0);

	UMaterialInterface* Mat = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
	if (!Mat) return MakeErrorResult(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));

	// Collect actor names (single string or array)
	TArray<FString> ActorNames;
	FString SingleName;
	if (Params->TryGetStringField(TEXT("actor_names"), SingleName))
	{
		ActorNames.Add(SingleName);
	}
	else
	{
		const TArray<TSharedPtr<FJsonValue>>* NamesArr;
		if (Params->TryGetArrayField(TEXT("actor_names"), NamesArr))
		{
			for (const auto& V : *NamesArr)
			{
				ActorNames.Add(V->AsString());
			}
		}
	}

	int32 Assigned = 0;
	int32 Failed = 0;
	TArray<TSharedPtr<FJsonValue>> Details;

	for (const FString& ActorName : ActorNames)
	{
		AActor* Actor = UArborActorTools::FindActorByAnyIdentifier(ActorName);
		TSharedPtr<FJsonObject> Detail = MakeShared<FJsonObject>();

		if (!Actor)
		{
			Detail->SetBoolField(TEXT("success"), false);
			Detail->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
			Failed++;
		}
		else
		{
			UStaticMeshComponent* SMC = Actor->FindComponentByClass<UStaticMeshComponent>();
			if (!SMC)
			{
				Detail->SetBoolField(TEXT("success"), false);
				Detail->SetStringField(TEXT("error"),
					FString::Printf(TEXT("No StaticMeshComponent on '%s'"), *Actor->GetActorLabel()));
				Failed++;
			}
			else
			{
				SMC->SetMaterial(Slot, Mat);
				Detail->SetBoolField(TEXT("success"), true);
				Detail->SetStringField(TEXT("actor"), Actor->GetActorLabel());
				Detail->SetNumberField(TEXT("slot"), Slot);
				Assigned++;
			}
		}

		Details.Add(MakeShared<FJsonValueObject>(Detail));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), Failed == 0);
	Root->SetNumberField(TEXT("assigned"), Assigned);
	Root->SetNumberField(TEXT("failed"), Failed);
	Root->SetArrayField(TEXT("details"), Details);
	return SerializeJson(Root);
}

FString UArborMaterialTools::EnsurePBRBaseMaterial(const FString& ContentPath)
{
	const FString BasePath = ContentPath / TEXT("M_PBR_Parameterized");
	if (UEditorAssetLibrary::DoesAssetExist(BasePath))
	{
		return MakeSuccessResult(BasePath);
	}

	FString ParamsJson = FString::Printf(
		TEXT("{\"name\":\"M_PBR_Parameterized\",\"content_path\":\"%s\",\"default_tiling\":1.0}"),
		*ContentPath);
	return CreateParameterizedPBRMaterial(ParamsJson);
}
