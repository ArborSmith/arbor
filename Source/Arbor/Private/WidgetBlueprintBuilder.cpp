#include "WidgetBlueprintBuilder.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Animation/WidgetAnimation.h"

#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"

#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"
#include "Engine/Texture2D.h"

#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Field.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogArborWidget, Log, All);

// ============================================================================
// Small JSON helpers
// ============================================================================

namespace
{
	FString SerializeJson(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}

	TSharedPtr<FJsonObject> ParseJson(const FString& JsonString)
	{
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		FJsonSerializer::Deserialize(Reader, Root);
		return Root;
	}

	/** Find a loaded UClass derived from BaseClass by short or U-prefixed name. */
	UClass* FindClassByShortName(const FString& Name, UClass* BaseClass)
	{
		const FString UName = Name.StartsWith(TEXT("U")) ? Name : (TEXT("U") + Name);
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* C = *It;
			if (!C->IsChildOf(BaseClass)) continue;
			if (C->GetName() == Name || C->GetName() == UName)
			{
				return C;
			}
		}
		return nullptr;
	}

	/** Resolve a class by full path (/Script/Module.Class or /Game/BP.BP_C) or short name. */
	UClass* ResolveClassLenient(const FString& Spec, UClass* BaseClass)
	{
		if (Spec.IsEmpty()) return nullptr;

		if (Spec.StartsWith(TEXT("/")))
		{
			if (UClass* Loaded = LoadObject<UClass>(nullptr, *Spec))
			{
				if (Loaded->IsChildOf(BaseClass)) return Loaded;
			}
			if (!Spec.EndsWith(TEXT("_C")))
			{
				const FString WithC = Spec + TEXT("_C");
				if (UClass* Loaded = LoadObject<UClass>(nullptr, *WithC))
				{
					if (Loaded->IsChildOf(BaseClass)) return Loaded;
				}
			}
			return nullptr;
		}
		return FindClassByShortName(Spec, BaseClass);
	}
}

FString UWidgetBlueprintBuilder::MakeError(const FString& Message)
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), false);
	Obj->SetStringField(TEXT("error"), Message);
	UE_LOG(LogArborWidget, Warning, TEXT("WidgetBuilder error: %s"), *Message);
	return SerializeJson(Obj);
}

// ============================================================================
// Class resolution
// ============================================================================

UClass* UWidgetBlueprintBuilder::ResolveParentWidgetClass(const FString& ClassName)
{
	UClass* Resolved = ResolveClassLenient(ClassName, UUserWidget::StaticClass());
	return Resolved ? Resolved : UUserWidget::StaticClass();
}

UClass* UWidgetBlueprintBuilder::ResolveWidgetClass(const FString& TypeName)
{
	return ResolveClassLenient(TypeName, UWidget::StaticClass());
}

// ============================================================================
// Generic JSON -> FProperty applier
// ============================================================================

void UWidgetBlueprintBuilder::ApplyJsonToProperty(FProperty* Prop, void* ValuePtr, const TSharedPtr<FJsonValue>& Json)
{
	if (!Prop || !ValuePtr || !Json.IsValid()) return;

	// Bool
	if (FBoolProperty* BoolP = CastField<FBoolProperty>(Prop))
	{
		BoolP->SetPropertyValue(ValuePtr, Json->AsBool());
		return;
	}
	// String / Name / Text
	if (FStrProperty* StrP = CastField<FStrProperty>(Prop))
	{
		StrP->SetPropertyValue(ValuePtr, Json->AsString());
		return;
	}
	if (FNameProperty* NameP = CastField<FNameProperty>(Prop))
	{
		NameP->SetPropertyValue(ValuePtr, FName(*Json->AsString()));
		return;
	}
	if (FTextProperty* TextP = CastField<FTextProperty>(Prop))
	{
		TextP->SetPropertyValue(ValuePtr, FText::FromString(Json->AsString()));
		return;
	}
	// Enum (FEnumProperty)
	if (FEnumProperty* EnumP = CastField<FEnumProperty>(Prop))
	{
		FNumericProperty* Under = EnumP->GetUnderlyingProperty();
		FString S;
		if (Json->TryGetString(S))
		{
			UEnum* E = EnumP->GetEnum();
			int64 V = E ? E->GetValueByNameString(S) : INDEX_NONE;
			if (V == INDEX_NONE && E) V = E->GetValueByNameString(E->GenerateFullEnumName(*S));
			if (V != INDEX_NONE) Under->SetIntPropertyValue(ValuePtr, V);
		}
		else
		{
			Under->SetIntPropertyValue(ValuePtr, (int64)Json->AsNumber());
		}
		return;
	}
	// Byte (possibly enum-backed)
	if (FByteProperty* ByteP = CastField<FByteProperty>(Prop))
	{
		FString S;
		if (ByteP->Enum && Json->TryGetString(S))
		{
			int64 V = ByteP->Enum->GetValueByNameString(S);
			if (V == INDEX_NONE) V = ByteP->Enum->GetValueByNameString(ByteP->Enum->GenerateFullEnumName(*S));
			if (V != INDEX_NONE) ByteP->SetIntPropertyValue(ValuePtr, V);
		}
		else
		{
			ByteP->SetIntPropertyValue(ValuePtr, (int64)Json->AsNumber());
		}
		return;
	}
	// Object / Class
	if (FObjectPropertyBase* ObjP = CastField<FObjectPropertyBase>(Prop))
	{
		FString Path;
		if (Json->TryGetString(Path) && !Path.IsEmpty())
		{
			if (CastField<FClassProperty>(Prop) || CastField<FSoftClassProperty>(Prop))
			{
				if (UClass* C = ResolveClassLenient(Path, UObject::StaticClass()))
				{
					ObjP->SetObjectPropertyValue(ValuePtr, C);
				}
			}
			else if (UObject* Loaded = LoadObject<UObject>(nullptr, *Path))
			{
				ObjP->SetObjectPropertyValue(ValuePtr, Loaded);
			}
		}
		return;
	}
	// Struct (recurse; FSlateBrush special-cased)
	if (FStructProperty* SP = CastField<FStructProperty>(Prop))
	{
		UScriptStruct* SS = SP->Struct;
		if (SS && SS->GetName() == TEXT("SlateBrush"))
		{
			FSlateBrush* Brush = static_cast<FSlateBrush*>(ValuePtr);
			const TSharedPtr<FJsonObject>* BObj;
			if (Json->TryGetObject(BObj))
			{
				FString Img;
				if ((*BObj)->TryGetStringField(TEXT("image"), Img) && !Img.IsEmpty())
				{
					if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Img))
					{
						Brush->SetResourceObject(Tex);
					}
				}
				FString DrawAs;
				if ((*BObj)->TryGetStringField(TEXT("draw_as"), DrawAs))
				{
					if (DrawAs.Equals(TEXT("Box"), ESearchCase::IgnoreCase)) Brush->DrawAs = ESlateBrushDrawType::Box;
					else if (DrawAs.Equals(TEXT("Border"), ESearchCase::IgnoreCase)) Brush->DrawAs = ESlateBrushDrawType::Border;
					else if (DrawAs.Equals(TEXT("RoundedBox"), ESearchCase::IgnoreCase)) Brush->DrawAs = ESlateBrushDrawType::RoundedBox;
					else if (DrawAs.Equals(TEXT("NoDrawType"), ESearchCase::IgnoreCase) || DrawAs.Equals(TEXT("None"), ESearchCase::IgnoreCase)) Brush->DrawAs = ESlateBrushDrawType::NoDrawType;
					else Brush->DrawAs = ESlateBrushDrawType::Image;
				}
				const TSharedPtr<FJsonObject>* SizeObj;
				if ((*BObj)->TryGetObjectField(TEXT("image_size"), SizeObj))
				{
					double X = 0, Y = 0;
					(*SizeObj)->TryGetNumberField(TEXT("x"), X);
					(*SizeObj)->TryGetNumberField(TEXT("y"), Y);
					Brush->SetImageSize(FVector2D(X, Y));
				}
				const TSharedPtr<FJsonObject>* TintObj;
				if ((*BObj)->TryGetObjectField(TEXT("tint"), TintObj))
				{
					double R = 1, G = 1, B = 1, A = 1;
					(*TintObj)->TryGetNumberField(TEXT("r"), R);
					(*TintObj)->TryGetNumberField(TEXT("g"), G);
					(*TintObj)->TryGetNumberField(TEXT("b"), B);
					(*TintObj)->TryGetNumberField(TEXT("a"), A);
					Brush->TintColor = FSlateColor(FLinearColor(R, G, B, A));
				}
				const TSharedPtr<FJsonObject>* MarginObj;
				if ((*BObj)->TryGetObjectField(TEXT("margin"), MarginObj))
				{
					double L = 0, T = 0, Rr = 0, Bm = 0;
					(*MarginObj)->TryGetNumberField(TEXT("left"), L);
					(*MarginObj)->TryGetNumberField(TEXT("top"), T);
					(*MarginObj)->TryGetNumberField(TEXT("right"), Rr);
					(*MarginObj)->TryGetNumberField(TEXT("bottom"), Bm);
					Brush->Margin = FMargin(L, T, Rr, Bm);
				}
			}
			return;
		}

		const TSharedPtr<FJsonObject>* Obj;
		if (Json->TryGetObject(Obj))
		{
			for (TFieldIterator<FProperty> It(SS); It; ++It)
			{
				FProperty* Member = *It;
				for (const auto& Pair : (*Obj)->Values)
				{
					if (Pair.Key.Equals(Member->GetName(), ESearchCase::IgnoreCase))
					{
						ApplyJsonToProperty(Member, Member->ContainerPtrToValuePtr<void>(ValuePtr), Pair.Value);
						break;
					}
				}
			}
			return;
		}
		FString S;
		if (Json->TryGetString(S))
		{
			Prop->ImportText_Direct(*S, ValuePtr, nullptr, PPF_None);
		}
		return;
	}
	// Array
	if (FArrayProperty* ArrP = CastField<FArrayProperty>(Prop))
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr;
		if (Json->TryGetArray(Arr))
		{
			FScriptArrayHelper Helper(ArrP, ValuePtr);
			Helper.Resize(Arr->Num());
			for (int32 i = 0; i < Arr->Num(); ++i)
			{
				ApplyJsonToProperty(ArrP->Inner, Helper.GetRawPtr(i), (*Arr)[i]);
			}
		}
		return;
	}
	// Numeric fallback (int/float/double)
	if (FNumericProperty* NumP = CastField<FNumericProperty>(Prop))
	{
		const double N = Json->AsNumber();
		if (NumP->IsFloatingPoint()) NumP->SetFloatingPointPropertyValue(ValuePtr, N);
		else NumP->SetIntPropertyValue(ValuePtr, (int64)N);
		return;
	}
	// Last resort: import from string
	FString S;
	if (Json->TryGetString(S))
	{
		Prop->ImportText_Direct(*S, ValuePtr, nullptr, PPF_None);
	}
}

void UWidgetBlueprintBuilder::ApplyProperties(UObject* Target, const TSharedPtr<FJsonObject>& Props)
{
	if (!Target || !Props.IsValid()) return;

	for (const auto& Pair : Props->Values)
	{
		FProperty* Prop = Target->GetClass()->FindPropertyByName(FName(*Pair.Key));
		if (!Prop)
		{
			// Case-insensitive search fallback
			for (TFieldIterator<FProperty> It(Target->GetClass()); It; ++It)
			{
				if (It->GetName().Equals(Pair.Key, ESearchCase::IgnoreCase)) { Prop = *It; break; }
			}
		}
		if (Prop)
		{
			ApplyJsonToProperty(Prop, Prop->ContainerPtrToValuePtr<void>(Target), Pair.Value);
		}
		else
		{
			UE_LOG(LogArborWidget, Warning, TEXT("Property '%s' not found on %s"), *Pair.Key, *Target->GetClass()->GetName());
		}
	}
}

// ============================================================================
// Widget creation
// ============================================================================

UWidget* UWidgetBlueprintBuilder::CreateWidgetFromSpec(UWidgetBlueprint* WBP, const TSharedPtr<FJsonObject>& Spec, FString& OutError)
{
	FString Name, Type;
	if (!Spec->TryGetStringField(TEXT("name"), Name) || !Spec->TryGetStringField(TEXT("type"), Type))
	{
		OutError = TEXT("widget spec requires 'name' and 'type'");
		return nullptr;
	}

	if (WBP->WidgetTree->FindWidget(FName(*Name)))
	{
		OutError = FString::Printf(TEXT("widget '%s' already exists"), *Name);
		return nullptr;
	}

	UClass* WidgetClass = ResolveWidgetClass(Type);
	if (!WidgetClass)
	{
		OutError = FString::Printf(TEXT("unknown widget type '%s' (use list_widget_types)"), *Type);
		return nullptr;
	}

	UWidget* W = WBP->WidgetTree->ConstructWidget<UWidget>(WidgetClass, FName(*Name));
	if (!W)
	{
		OutError = FString::Printf(TEXT("failed to construct widget '%s'"), *Name);
		return nullptr;
	}

	bool bIsVariable = true;
	Spec->TryGetBoolField(TEXT("is_variable"), bIsVariable);
	W->bIsVariable = bIsVariable;

	bool bRoot = false;
	Spec->TryGetBoolField(TEXT("root"), bRoot);
	FString ParentName;
	const bool bHasParent = Spec->TryGetStringField(TEXT("parent"), ParentName) && !ParentName.IsEmpty();

	if (bRoot || !bHasParent)
	{
		WBP->WidgetTree->RootWidget = W;
	}
	else
	{
		UWidget* ParentW = WBP->WidgetTree->FindWidget(FName(*ParentName));
		UPanelWidget* Panel = Cast<UPanelWidget>(ParentW);
		if (!Panel)
		{
			OutError = FString::Printf(TEXT("parent '%s' not found or not a panel widget"), *ParentName);
			return nullptr;
		}
		UPanelSlot* Slot = Panel->AddChild(W);
		const TSharedPtr<FJsonObject>* SlotProps;
		if (Slot && Spec->TryGetObjectField(TEXT("slot_properties"), SlotProps))
		{
			ApplyProperties(Slot, *SlotProps);
		}
	}

	const TSharedPtr<FJsonObject>* Props;
	if (Spec->TryGetObjectField(TEXT("properties"), Props))
	{
		ApplyProperties(W, *Props);
	}

	if (bIsVariable)
	{
		WBP->OnVariableAdded(W->GetFName());
	}

	return W;
}

// ============================================================================
// Asset creation / loading / saving
// ============================================================================

UWidgetBlueprint* UWidgetBlueprintBuilder::CreateWidgetBlueprintAsset(const FString& Name, const FString& AssetPath, UClass* ParentClass)
{
	const FString PackageName = AssetPath / Name;
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package) return nullptr;

	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(
		FKismetEditorUtilities::CreateBlueprint(
			ParentClass, Package, FName(*Name), BPTYPE_Normal,
			UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));

	if (!WBP) return nullptr;

	if (!WBP->WidgetTree)
	{
		WBP->WidgetTree = NewObject<UWidgetTree>(WBP, TEXT("WidgetTree"), RF_Transactional);
	}

	FAssetRegistryModule::AssetCreated(WBP);
	Package->MarkPackageDirty();
	return WBP;
}

UWidgetBlueprint* UWidgetBlueprintBuilder::LoadWidgetBlueprint(const FString& AssetPath)
{
	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	return Cast<UWidgetBlueprint>(Asset);
}

bool UWidgetBlueprintBuilder::SaveAsset(UObject* Asset)
{
	if (!Asset) return false;
	return UEditorAssetLibrary::SaveLoadedAsset(Asset, false);
}

// ============================================================================
// BindWidget reporting
// ============================================================================

TArray<TSharedPtr<FJsonValue>> UWidgetBlueprintBuilder::CollectBindWidgetProperties(UClass* ParentClass)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	if (!ParentClass) return Out;

	for (TFieldIterator<FObjectPropertyBase> It(ParentClass); It; ++It)
	{
		FObjectPropertyBase* P = *It;
		const bool bBind = P->HasMetaData(TEXT("BindWidget"));
		const bool bOpt = P->HasMetaData(TEXT("BindWidgetOptional"));
		if (!bBind && !bOpt) continue;

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), P->GetName());
		Entry->SetStringField(TEXT("type"), P->PropertyClass ? P->PropertyClass->GetName() : TEXT("Widget"));
		Entry->SetBoolField(TEXT("optional"), bOpt);
		Out.Add(MakeShared<FJsonValueObject>(Entry));
	}
	return Out;
}

// ============================================================================
// Public UFUNCTIONs
// ============================================================================

FString UWidgetBlueprintBuilder::BuildWidgetFromJSONString(const FString& JsonString, const FString& AssetPath)
{
	TSharedPtr<FJsonObject> Root = ParseJson(JsonString);
	if (!Root.IsValid()) return MakeError(TEXT("invalid JSON"));

	FString Name, ParentClassName;
	if (!Root->TryGetStringField(TEXT("name"), Name))
		return MakeError(TEXT("'name' required"));
	Root->TryGetStringField(TEXT("parent_class"), ParentClassName);

	FString ContentPath = AssetPath;
	Root->TryGetStringField(TEXT("content_path"), ContentPath);
	if (ContentPath.IsEmpty()) ContentPath = TEXT("/Game/UI/Widgets");

	UClass* ParentClass = ResolveParentWidgetClass(ParentClassName);

	const FString FullPath = ContentPath / Name;
	UWidgetBlueprint* WBP = LoadWidgetBlueprint(FullPath);
	if (!WBP)
	{
		WBP = CreateWidgetBlueprintAsset(Name, ContentPath, ParentClass);
		if (!WBP) return MakeError(FString::Printf(TEXT("failed to create WidgetBlueprint at %s"), *FullPath));
	}

	// Build the tree (parents must appear before children).
	const TArray<TSharedPtr<FJsonValue>>* Tree;
	if (Root->TryGetArrayField(TEXT("tree"), Tree))
	{
		for (const TSharedPtr<FJsonValue>& V : *Tree)
		{
			const TSharedPtr<FJsonObject>& Spec = V->AsObject();
			if (!Spec.IsValid()) continue;
			FString Err;
			if (!CreateWidgetFromSpec(WBP, Spec, Err))
			{
				return MakeError(FString::Printf(TEXT("tree build failed: %s"), *Err));
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	FKismetEditorUtilities::CompileBlueprint(WBP);
	SaveAsset(WBP);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), FullPath);
	Result->SetArrayField(TEXT("bind_widgets"), CollectBindWidgetProperties(ParentClass));
	return SerializeJson(Result);
}

FString UWidgetBlueprintBuilder::QueryWidget(const FString& AssetPath)
{
	UWidgetBlueprint* WBP = LoadWidgetBlueprint(AssetPath);
	if (!WBP) return MakeError(FString::Printf(TEXT("WidgetBlueprint not found: %s"), *AssetPath));

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);

	// Widget tree
	TArray<TSharedPtr<FJsonValue>> Widgets;
	TArray<UWidget*> All;
	WBP->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) continue;
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), W->GetName());
		Entry->SetStringField(TEXT("class"), W->GetClass()->GetName());
		Entry->SetBoolField(TEXT("is_variable"), W->bIsVariable);
		Entry->SetBoolField(TEXT("is_root"), W == WBP->WidgetTree->RootWidget);
		int32 ChildIdx = 0;
		if (UPanelWidget* Parent = UWidgetTree::FindWidgetParent(W, ChildIdx))
		{
			Entry->SetStringField(TEXT("parent"), Parent->GetName());
		}
		if (W->Slot)
		{
			Entry->SetStringField(TEXT("slot"), W->Slot->GetClass()->GetName());
		}
		Widgets.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("widgets"), Widgets);

	// Animations
	TArray<TSharedPtr<FJsonValue>> Anims;
	for (UWidgetAnimation* A : WBP->Animations)
	{
		if (A) Anims.Add(MakeShared<FJsonValueString>(A->GetName()));
	}
	Result->SetArrayField(TEXT("animations"), Anims);

	// BindWidget properties from the parent class
	UClass* ParentClass = WBP->ParentClass ? WBP->ParentClass.Get() : nullptr;
	Result->SetArrayField(TEXT("bind_widgets"), CollectBindWidgetProperties(ParentClass));

	return SerializeJson(Result);
}

FString UWidgetBlueprintBuilder::AddWidget(const FString& AssetPath, const FString& WidgetJsonString)
{
	UWidgetBlueprint* WBP = LoadWidgetBlueprint(AssetPath);
	if (!WBP) return MakeError(FString::Printf(TEXT("WidgetBlueprint not found: %s"), *AssetPath));

	TSharedPtr<FJsonObject> Spec = ParseJson(WidgetJsonString);
	if (!Spec.IsValid()) return MakeError(TEXT("invalid widget JSON"));

	FString Err;
	UWidget* W = CreateWidgetFromSpec(WBP, Spec, Err);
	if (!W) return MakeError(Err);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	SaveAsset(WBP);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widget"), W->GetName());
	return SerializeJson(Result);
}

FString UWidgetBlueprintBuilder::RemoveWidget(const FString& AssetPath, const FString& WidgetName)
{
	UWidgetBlueprint* WBP = LoadWidgetBlueprint(AssetPath);
	if (!WBP) return MakeError(FString::Printf(TEXT("WidgetBlueprint not found: %s"), *AssetPath));

	UWidget* W = WBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!W) return MakeError(FString::Printf(TEXT("widget '%s' not found"), *WidgetName));

	WBP->WidgetTree->RemoveWidget(W);
	WBP->OnVariableRemoved(FName(*WidgetName));

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	SaveAsset(WBP);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	return SerializeJson(Result);
}

FString UWidgetBlueprintBuilder::SetWidgetProperty(const FString& AssetPath, const FString& WidgetName, const FString& PropertyJsonString)
{
	UWidgetBlueprint* WBP = LoadWidgetBlueprint(AssetPath);
	if (!WBP) return MakeError(FString::Printf(TEXT("WidgetBlueprint not found: %s"), *AssetPath));

	UWidget* W = WBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!W) return MakeError(FString::Printf(TEXT("widget '%s' not found"), *WidgetName));

	TSharedPtr<FJsonObject> Props = ParseJson(PropertyJsonString);
	if (!Props.IsValid()) return MakeError(TEXT("invalid property JSON"));

	// "__slot": {..} targets the widget's slot instead of the widget.
	const TSharedPtr<FJsonObject>* SlotProps;
	if (Props->TryGetObjectField(TEXT("__slot"), SlotProps) && W->Slot)
	{
		ApplyProperties(W->Slot, *SlotProps);
	}
	ApplyProperties(W, Props);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	SaveAsset(WBP);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	return SerializeJson(Result);
}

FString UWidgetBlueprintBuilder::SetRootWidget(const FString& AssetPath, const FString& WidgetName)
{
	UWidgetBlueprint* WBP = LoadWidgetBlueprint(AssetPath);
	if (!WBP) return MakeError(FString::Printf(TEXT("WidgetBlueprint not found: %s"), *AssetPath));

	UWidget* W = WBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!W) return MakeError(FString::Printf(TEXT("widget '%s' not found"), *WidgetName));

	WBP->WidgetTree->RootWidget = W;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	SaveAsset(WBP);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	return SerializeJson(Result);
}

FString UWidgetBlueprintBuilder::ListWidgetTypes(const FString& Filter)
{
	TArray<TSharedPtr<FJsonValue>> Types;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* C = *It;
		if (!C->IsChildOf(UWidget::StaticClass())) continue;
		if (C->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Hidden)) continue;
		const FString CName = C->GetName();
		if (!Filter.IsEmpty() && !CName.Contains(Filter)) continue;

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), CName);
		Entry->SetStringField(TEXT("path"), C->GetPathName());
		Types.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetArrayField(TEXT("types"), Types);
	return SerializeJson(Result);
}

FString UWidgetBlueprintBuilder::CompileAndSaveWidget(const FString& AssetPath)
{
	UWidgetBlueprint* WBP = LoadWidgetBlueprint(AssetPath);
	if (!WBP) return MakeError(FString::Printf(TEXT("WidgetBlueprint not found: %s"), *AssetPath));

	FCompilerResultsLog Results;
	Results.bSilentMode = true;
	FKismetEditorUtilities::CompileBlueprint(WBP, EBlueprintCompileOptions::None, &Results);

	TArray<TSharedPtr<FJsonValue>> Errors;
	TArray<TSharedPtr<FJsonValue>> Warnings;
	for (const TSharedRef<FTokenizedMessage>& Msg : Results.Messages)
	{
		const FString Text = Msg->ToText().ToString();
		if (Msg->GetSeverity() == EMessageSeverity::Error)
			Errors.Add(MakeShared<FJsonValueString>(Text));
		else if (Msg->GetSeverity() == EMessageSeverity::Warning)
			Warnings.Add(MakeShared<FJsonValueString>(Text));
	}

	const bool bOk = (Results.NumErrors == 0);
	if (bOk) SaveAsset(WBP);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bOk);
	Result->SetArrayField(TEXT("compile_errors"), Errors);
	Result->SetArrayField(TEXT("compile_warnings"), Warnings);
	return SerializeJson(Result);
}
