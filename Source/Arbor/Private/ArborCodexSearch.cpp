#include "ArborCodexSearch.h"
#include "ArborGameContextTypes.h"
#include "ArborCharacterTypes.h"
#include "ArborPathUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "UObject/SavePackage.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// ============================================================================
// Category registry
// ============================================================================

namespace
{

struct FCodexCategoryInfo
{
	FTopLevelAssetPath ClassPath;
	FString NameField;      // property that holds the entry's display name
	FString DefaultPath;    // default content directory for new assets
	FString AssetPrefix;    // prefix for generated asset names
	FString SubfolderName;  // subfolder under GameContext root (empty = context itself)
};

const TMap<FString, FCodexCategoryInfo>& GetCategories()
{
	static const TMap<FString, FCodexCategoryInfo> Map = {
		{TEXT("context"),   {FTopLevelAssetPath(TEXT("/Script/Arbor"), TEXT("ArborGameContextAsset")), TEXT("GameTitle"),    TEXT("/Game/GameCodex"), TEXT("GC"),      TEXT("")}},
		{TEXT("location"),  {FTopLevelAssetPath(TEXT("/Script/Arbor"), TEXT("ArborLocationAsset")),  TEXT("LocationName"),  TEXT("/Game/GameCodex"), TEXT("Loc"),     TEXT("Locations")}},
		{TEXT("feature"),   {FTopLevelAssetPath(TEXT("/Script/Arbor"), TEXT("ArborFeatureAsset")),   TEXT("FeatureName"),   TEXT("/Game/GameCodex"), TEXT("Feature"), TEXT("Features")}},
		{TEXT("system"),    {FTopLevelAssetPath(TEXT("/Script/Arbor"), TEXT("ArborFeatureAsset")),   TEXT("FeatureName"),   TEXT("/Game/GameCodex"), TEXT("Feature"), TEXT("Features")}},  // legacy alias
		{TEXT("pillar"),    {FTopLevelAssetPath(TEXT("/Script/Arbor"), TEXT("ArborPillarAsset")),    TEXT("PillarName"),   TEXT("/Game/GameCodex"), TEXT("Pillar"),   TEXT("Pillars")}},
		{TEXT("theme"),     {FTopLevelAssetPath(TEXT("/Script/Arbor"), TEXT("ArborPillarAsset")),    TEXT("PillarName"),   TEXT("/Game/GameCodex"), TEXT("Pillar"),   TEXT("Pillars")}},  // alias
		{TEXT("character"), {FTopLevelAssetPath(TEXT("/Script/Arbor"), TEXT("CharacterDataAsset")),  TEXT("CharacterName"), TEXT("/Game/GameCodex"), TEXT("DA"),      TEXT("Characters")}},
	};
	return Map;
}

FString FindCategoryForClass(UClass* Class)
{
	if (!Class) return TEXT("");
	const FTopLevelAssetPath ClassPath = Class->GetClassPathName();
	for (const auto& Pair : GetCategories())
	{
		if (Pair.Value.ClassPath == ClassPath)
		{
			return Pair.Key;
		}
	}
	return TEXT("");
}

const FString& GetNameFieldForCategory(const FString& Category)
{
	static const FString Empty;
	const FCodexCategoryInfo* Info = GetCategories().Find(Category);
	return Info ? Info->NameField : Empty;
}

TArray<FAssetData> FindAssetsOfClass(const FTopLevelAssetPath& ClassPath)
{
	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	FARFilter Filter;
	Filter.ClassPaths.Add(ClassPath);
	Filter.PackagePaths.Add(FName(TEXT("/Game")));
	Filter.bRecursivePaths = true;
	TArray<FAssetData> Assets;
	AR.GetAssets(Filter, Assets);
	return Assets;
}

// ============================================================================
// Property serialization (reflection-based, recursive)
// ============================================================================

TSharedPtr<FJsonObject> SerializeProperties(const void* Container, const UStruct* Struct, bool bFilterOwner = false, const UClass* OwnerFilter = nullptr)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();

	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		FProperty* Prop = *It;

		// For top-level UObjects, skip inherited UObject/UDataAsset properties
		if (bFilterOwner)
		{
			const UClass* PropOwner = Prop->GetOwnerClass();
			if (PropOwner && PropOwner != OwnerFilter)
			{
				continue;
			}
		}

		const FString PropName = Prop->GetName();

		// FString
		if (CastField<FStrProperty>(Prop))
		{
			const FString& Value = *Prop->ContainerPtrToValuePtr<FString>(Container);
			Obj->SetStringField(PropName, Value);
			continue;
		}

		// Soft object reference (TSoftObjectPtr<T>) — export as path string
		if (CastField<FSoftObjectProperty>(Prop))
		{
			FString PathStr;
			Prop->ExportText_InContainer(0, PathStr, Container, nullptr, nullptr, PPF_None);
			Obj->SetStringField(PropName, PathStr);
			continue;
		}

		// TArray<T>
		if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
		{
			FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Container));
			TArray<TSharedPtr<FJsonValue>> Arr;

			if (CastField<FStrProperty>(ArrayProp->Inner))
			{
				for (int32 i = 0; i < Helper.Num(); i++)
				{
					const FString& Elem = *reinterpret_cast<const FString*>(Helper.GetRawPtr(i));
					Arr.Add(MakeShared<FJsonValueString>(Elem));
				}
			}
			else if (const FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner))
			{
				for (int32 i = 0; i < Helper.Num(); i++)
				{
					Arr.Add(MakeShared<FJsonValueObject>(
						SerializeProperties(Helper.GetRawPtr(i), InnerStruct->Struct)));
				}
			}

			Obj->SetArrayField(PropName, Arr);
			continue;
		}

		// TSet<FString> — serialize as JSON array
		if (const FSetProperty* SetProp = CastField<FSetProperty>(Prop))
		{
			if (CastField<FStrProperty>(SetProp->ElementProp))
			{
				FScriptSetHelper Helper(SetProp, SetProp->ContainerPtrToValuePtr<void>(Container));
				TArray<TSharedPtr<FJsonValue>> Arr;
				for (int32 i = 0; i < Helper.Num(); i++)
				{
					if (Helper.IsValidIndex(i))
					{
						Arr.Add(MakeShared<FJsonValueString>(
							*reinterpret_cast<const FString*>(Helper.GetElementPtr(i))));
					}
				}
				Obj->SetArrayField(PropName, Arr);
			}
			continue;
		}

		// Nested struct
		if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			const void* StructPtr = StructProp->ContainerPtrToValuePtr<void>(Container);
			Obj->SetObjectField(PropName, SerializeProperties(StructPtr, StructProp->Struct));
			continue;
		}

		// Enum
		if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			const UEnum* Enum = EnumProp->GetEnum();
			const void* ValuePtr = EnumProp->ContainerPtrToValuePtr<void>(Container);
			const int64 Value = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
			Obj->SetStringField(PropName, Enum->GetDisplayNameTextByValue(Value).ToString());
			continue;
		}

		// Bool
		if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			Obj->SetBoolField(PropName, BoolProp->GetPropertyValue_InContainer(Container));
			continue;
		}

		// Numeric
		if (const FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
		{
			const void* ValuePtr = NumProp->ContainerPtrToValuePtr<void>(Container);
			if (NumProp->IsFloatingPoint())
			{
				Obj->SetNumberField(PropName, NumProp->GetFloatingPointPropertyValue(ValuePtr));
			}
			else if (NumProp->IsInteger())
			{
				Obj->SetNumberField(PropName, static_cast<double>(NumProp->GetSignedIntPropertyValue(ValuePtr)));
			}
			continue;
		}
	}

	return Obj;
}

// ============================================================================
// Property deserialization (reflection-based, recursive — inverse of SerializeProperties)
// ============================================================================

bool DeserializeProperties(void* Container, const UStruct* Struct, const TSharedPtr<FJsonObject>& Json, bool bFilterOwner = false, const UClass* OwnerFilter = nullptr)
{
	if (!Json.IsValid()) return false;

	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		FProperty* Prop = *It;

		if (bFilterOwner)
		{
			const UClass* PropOwner = Prop->GetOwnerClass();
			if (PropOwner && PropOwner != OwnerFilter)
			{
				continue;
			}
		}

		const FString PropName = Prop->GetName();

		// Only set properties present in the JSON (partial update)
		if (!Json->HasField(PropName))
			continue;

		// FString
		if (CastField<FStrProperty>(Prop))
		{
			FString* Value = Prop->ContainerPtrToValuePtr<FString>(Container);
			*Value = Json->GetStringField(PropName);
			continue;
		}

		// Soft object reference (TSoftObjectPtr<T>)
		if (CastField<FSoftObjectProperty>(Prop))
		{
			const FString PathStr = Json->GetStringField(PropName);
			FSoftObjectPtr* SoftPtr = Prop->ContainerPtrToValuePtr<FSoftObjectPtr>(Container);
			*SoftPtr = PathStr.IsEmpty() ? FSoftObjectPtr() : FSoftObjectPtr(FSoftObjectPath(PathStr));
			continue;
		}

		// TArray<T>
		if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
		{
			const TArray<TSharedPtr<FJsonValue>>& JsonArr = Json->GetArrayField(PropName);
			FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Container));
			Helper.EmptyValues();

			if (CastField<FStrProperty>(ArrayProp->Inner))
			{
				for (const auto& Val : JsonArr)
				{
					const int32 Idx = Helper.AddValue();
					*reinterpret_cast<FString*>(Helper.GetRawPtr(Idx)) = Val->AsString();
				}
			}
			else if (const FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner))
			{
				for (const auto& Val : JsonArr)
				{
					const int32 Idx = Helper.AddValue();
					const TSharedPtr<FJsonObject>* ElemObj = nullptr;
					if (Val->TryGetObject(ElemObj) && ElemObj)
					{
						DeserializeProperties(Helper.GetRawPtr(Idx), InnerStruct->Struct, *ElemObj);
					}
				}
			}
			continue;
		}

		// Nested struct
		if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			const TSharedPtr<FJsonObject>* NestedObj = nullptr;
			if (Json->TryGetObjectField(PropName, NestedObj) && NestedObj)
			{
				void* StructPtr = StructProp->ContainerPtrToValuePtr<void>(Container);
				DeserializeProperties(StructPtr, StructProp->Struct, *NestedObj);
			}
			continue;
		}

		// Enum
		if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			const UEnum* Enum = EnumProp->GetEnum();
			const FString EnumStr = Json->GetStringField(PropName);

			// Try C++ name first, then display name
			int64 Value = Enum->GetValueByNameString(EnumStr);
			if (Value == INDEX_NONE)
			{
				for (int32 i = 0; i < Enum->NumEnums() - 1; i++)
				{
					if (Enum->GetDisplayNameTextByIndex(i).ToString() == EnumStr)
					{
						Value = Enum->GetValueByIndex(i);
						break;
					}
				}
			}
			if (Value != INDEX_NONE)
			{
				void* ValuePtr = EnumProp->ContainerPtrToValuePtr<void>(Container);
				EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, Value);
			}
			continue;
		}

		// Bool
		if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			BoolProp->SetPropertyValue_InContainer(Container, Json->GetBoolField(PropName));
			continue;
		}

		// Numeric
		if (const FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
		{
			void* ValuePtr = NumProp->ContainerPtrToValuePtr<void>(Container);
			const double NumVal = Json->GetNumberField(PropName);
			if (NumProp->IsFloatingPoint())
			{
				NumProp->SetFloatingPointPropertyValue(ValuePtr, NumVal);
			}
			else if (NumProp->IsInteger())
			{
				NumProp->SetIntPropertyValue(ValuePtr, static_cast<int64>(NumVal));
			}
			continue;
		}
	}

	return true;
}

TSharedPtr<FJsonObject> SerializeCodexAsset(UObject* Asset, const FString& Category, const FString& NameField)
{
	TSharedPtr<FJsonObject> Obj = SerializeProperties(Asset, Asset->GetClass(), true, Asset->GetClass());

	// Add metadata fields
	Obj->SetStringField(TEXT("_category"), Category);
	Obj->SetStringField(TEXT("_path"), Asset->GetPathName());

	// Extract display name
	FString DisplayName = Asset->GetName();
	if (!NameField.IsEmpty())
	{
		if (FProperty* NameProp = Asset->GetClass()->FindPropertyByName(FName(*NameField)))
		{
			if (CastField<FStrProperty>(NameProp))
			{
				const FString& Value = *NameProp->ContainerPtrToValuePtr<FString>(Asset);
				if (!Value.IsEmpty())
				{
					DisplayName = Value;
				}
			}
		}
	}
	Obj->SetStringField(TEXT("_name"), DisplayName);

	return Obj;
}

// ============================================================================
// Search scoring
// ============================================================================

void CollectSearchableStrings(const void* Container, const UStruct* Struct, TArray<FString>& OutStrings, bool bFilterOwner = false, const UClass* OwnerFilter = nullptr)
{
	for (TFieldIterator<FProperty> It(Struct); It; ++It)
	{
		FProperty* Prop = *It;

		if (bFilterOwner)
		{
			const UClass* PropOwner = Prop->GetOwnerClass();
			if (PropOwner && PropOwner != OwnerFilter)
			{
				continue;
			}
		}

		// Skip soft references
		if (CastField<FSoftObjectProperty>(Prop)) continue;

		if (CastField<FStrProperty>(Prop))
		{
			const FString& Value = *Prop->ContainerPtrToValuePtr<FString>(Container);
			if (!Value.IsEmpty())
			{
				OutStrings.Add(Value);
			}
		}
		else if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
		{
			FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Container));

			if (CastField<FStrProperty>(ArrayProp->Inner))
			{
				for (int32 i = 0; i < Helper.Num(); i++)
				{
					const FString& Elem = *reinterpret_cast<const FString*>(Helper.GetRawPtr(i));
					if (!Elem.IsEmpty())
					{
						OutStrings.Add(Elem);
					}
				}
			}
			else if (const FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner))
			{
				for (int32 i = 0; i < Helper.Num(); i++)
				{
					CollectSearchableStrings(Helper.GetRawPtr(i), InnerStruct->Struct, OutStrings);
				}
			}
		}
		else if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			const void* StructPtr = StructProp->ContainerPtrToValuePtr<void>(Container);
			CollectSearchableStrings(StructPtr, StructProp->Struct, OutStrings);
		}
		else if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			const UEnum* Enum = EnumProp->GetEnum();
			const void* ValuePtr = EnumProp->ContainerPtrToValuePtr<void>(Container);
			const int64 Value = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
			FString DisplayName = Enum->GetDisplayNameTextByValue(Value).ToString();
			if (!DisplayName.IsEmpty())
			{
				OutStrings.Add(DisplayName);
			}
		}
	}
}

FString GetDisplayName(UObject* Asset, const FString& NameField)
{
	if (!NameField.IsEmpty())
	{
		if (FProperty* NameProp = Asset->GetClass()->FindPropertyByName(FName(*NameField)))
		{
			if (CastField<FStrProperty>(NameProp))
			{
				const FString& Value = *NameProp->ContainerPtrToValuePtr<FString>(Asset);
				if (!Value.IsEmpty())
				{
					return Value;
				}
			}
		}
	}
	return Asset->GetName();
}

FString GetStatusDisplayName(UObject* Asset)
{
	FProperty* StatusProp = Asset->GetClass()->FindPropertyByName(FName(TEXT("Status")));
	if (!StatusProp) return TEXT("");
	const FEnumProperty* EnumProp = CastField<FEnumProperty>(StatusProp);
	if (!EnumProp) return TEXT("");
	const UEnum* Enum = EnumProp->GetEnum();
	const void* ValuePtr = EnumProp->ContainerPtrToValuePtr<void>(Asset);
	const int64 Value = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
	return Enum->GetDisplayNameTextByValue(Value).ToString();
}

bool MatchesStatusFilter(UObject* Asset, const FString& StatusFilter)
{
	if (StatusFilter.IsEmpty()) return true;
	const FString StatusName = GetStatusDisplayName(Asset);
	return StatusName.Equals(StatusFilter, ESearchCase::IgnoreCase);
}

int32 ScoreEntry(UObject* Asset, const FString& NameField, const TArray<FString>& Tokens)
{
	const FString NameLower = GetDisplayName(Asset, NameField).ToLower();

	TArray<FString> AllStrings;
	CollectSearchableStrings(Asset, Asset->GetClass(), AllStrings, true, Asset->GetClass());

	int32 Score = 0;

	for (const FString& Token : Tokens)
	{
		// Name scoring
		if (Token == NameLower)
		{
			Score += 10;
		}
		else if (NameLower.Contains(Token))
		{
			Score += 5;
		}

		// Field scoring (1 point if token found in any field)
		for (const FString& Str : AllStrings)
		{
			if (Str.ToLower().Contains(Token))
			{
				Score += 1;
				break;
			}
		}
	}

	return Score;
}

// ============================================================================
// JSON output helpers
// ============================================================================

FString ToJsonString(const TArray<TSharedPtr<FJsonValue>>& Array)
{
	FString Output;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Array, Writer);
	return Output;
}

FString ToJsonString(const TSharedPtr<FJsonObject>& Obj)
{
	FString Output;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
	return Output;
}

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

FString UArborCodexSearch::SearchCodex(const FString& Query, const FString& Category, int32 Limit, const FString& StatusFilter)
{
	if (Limit <= 0) Limit = 20;

	// Parse tokens
	TArray<FString> Tokens;
	Query.ToLower().ParseIntoArray(Tokens, TEXT(" "), true);

	if (Tokens.Num() == 0)
	{
		return TEXT("[]");
	}

	// Determine which categories to search
	TArray<TPair<FString, FCodexCategoryInfo>> CategoriesToSearch;
	const FString CategoryLower = Category.ToLower().TrimStartAndEnd();

	if (CategoryLower.IsEmpty())
	{
		for (const auto& Pair : GetCategories())
		{
			CategoriesToSearch.Add({Pair.Key, Pair.Value});
		}
	}
	else
	{
		const FCodexCategoryInfo* Info = GetCategories().Find(CategoryLower);
		if (!Info)
		{
			return TEXT("[]");
		}
		CategoriesToSearch.Add({CategoryLower, *Info});
	}

	// Score all entries
	struct FScoredResult
	{
		int32 Score;
		UObject* Asset;
		FString Category;
		FString NameField;
	};
	TArray<FScoredResult> Results;

	for (const auto& Cat : CategoriesToSearch)
	{
		TArray<FAssetData> AssetDataList = FindAssetsOfClass(Cat.Value.ClassPath);

		for (const FAssetData& AssetData : AssetDataList)
		{
			UObject* Asset = AssetData.GetAsset();
			if (!Asset) continue;

			if (!MatchesStatusFilter(Asset, StatusFilter)) continue;

			const int32 Score = ScoreEntry(Asset, Cat.Value.NameField, Tokens);
			if (Score > 0)
			{
				Results.Add({Score, Asset, Cat.Key, Cat.Value.NameField});
			}
		}
	}

	// Sort by score descending
	Results.Sort([](const FScoredResult& A, const FScoredResult& B) { return A.Score > B.Score; });

	// Build JSON output
	const int32 Count = FMath::Min(Results.Num(), Limit);
	TArray<TSharedPtr<FJsonValue>> JsonArray;
	JsonArray.Reserve(Count);

	for (int32 i = 0; i < Count; i++)
	{
		const FScoredResult& R = Results[i];
		TSharedPtr<FJsonObject> Obj = SerializeCodexAsset(R.Asset, R.Category, R.NameField);
		Obj->SetNumberField(TEXT("_score"), R.Score);
		JsonArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	return ToJsonString(JsonArray);
}

FString UArborCodexSearch::ListCodexEntries(const FString& Category, const FString& StatusFilter)
{
	const FString CategoryLower = Category.ToLower().TrimStartAndEnd();
	const FCodexCategoryInfo* Info = GetCategories().Find(CategoryLower);
	if (!Info)
	{
		// Return all categories if empty
		if (CategoryLower.IsEmpty())
		{
			TArray<TSharedPtr<FJsonValue>> Categories;
			for (const auto& Pair : GetCategories())
			{
				Categories.Add(MakeShared<FJsonValueString>(Pair.Key));
			}
			return ToJsonString(Categories);
		}
		return TEXT("[]");
	}

	TArray<FAssetData> AssetDataList = FindAssetsOfClass(Info->ClassPath);
	TArray<TSharedPtr<FJsonValue>> JsonArray;
	JsonArray.Reserve(AssetDataList.Num());

	for (const FAssetData& AssetData : AssetDataList)
	{
		UObject* Asset = AssetData.GetAsset();
		if (!Asset) continue;

		if (!MatchesStatusFilter(Asset, StatusFilter)) continue;

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("_path"), Asset->GetPathName());
		Obj->SetStringField(TEXT("_name"), GetDisplayName(Asset, Info->NameField));
		JsonArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	return ToJsonString(JsonArray);
}

FString UArborCodexSearch::GetCodexEntry(const FString& AssetPath)
{
	// Try loading the asset
	FSoftObjectPath SoftPath(AssetPath);
	UObject* Asset = SoftPath.TryLoad();

	// If that fails, try appending the object name
	if (!Asset && !AssetPath.Contains(TEXT(".")))
	{
		const FString AssetName = FPaths::GetBaseFilename(AssetPath);
		FSoftObjectPath FullPath(FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName));
		Asset = FullPath.TryLoad();
	}

	if (!Asset)
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("success"), false);
		Err->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
		return ToJsonString(Err);
	}

	// Determine category
	const FString Category = FindCategoryForClass(Asset->GetClass());
	if (Category.IsEmpty())
	{
		// Check if it's a game context
		if (Asset->IsA<UArborGameContextAsset>())
		{
			TSharedPtr<FJsonObject> Obj = SerializeProperties(Asset, Asset->GetClass(), true, Asset->GetClass());
			Obj->SetStringField(TEXT("_category"), TEXT("context"));
			Obj->SetStringField(TEXT("_path"), Asset->GetPathName());
			FProperty* TitlePropCtx = Asset->GetClass()->FindPropertyByName(FName(TEXT("GameTitle")));
			Obj->SetStringField(TEXT("_name"), (TitlePropCtx && CastField<FStrProperty>(TitlePropCtx))
				? *CastField<FStrProperty>(TitlePropCtx)->ContainerPtrToValuePtr<FString>(Asset)
				: Asset->GetName());
			return ToJsonString(Obj);
		}

		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("success"), false);
		Err->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset is not a codex entry: %s"), *AssetPath));
		return ToJsonString(Err);
	}

	const FString& NameField = GetNameFieldForCategory(Category);
	TSharedPtr<FJsonObject> Obj = SerializeCodexAsset(Asset, Category, NameField);
	return ToJsonString(Obj);
}

FString UArborCodexSearch::GetGameContext()
{
	const FTopLevelAssetPath ContextClassPath(TEXT("/Script/Arbor"), TEXT("ArborGameContextAsset"));
	TArray<FAssetData> AssetDataList = FindAssetsOfClass(ContextClassPath);

	TArray<TSharedPtr<FJsonValue>> JsonArray;
	JsonArray.Reserve(AssetDataList.Num());

	for (const FAssetData& AssetData : AssetDataList)
	{
		UObject* Asset = AssetData.GetAsset();
		if (!Asset) continue;

		TSharedPtr<FJsonObject> Obj = SerializeProperties(Asset, Asset->GetClass(), true, Asset->GetClass());
		Obj->SetStringField(TEXT("_path"), Asset->GetPathName());

		// Use GameTitle as display name
		FProperty* TitleProp = Asset->GetClass()->FindPropertyByName(FName(TEXT("GameTitle")));
		if (TitleProp && CastField<FStrProperty>(TitleProp))
		{
			const FString& Title = *TitleProp->ContainerPtrToValuePtr<FString>(Asset);
			Obj->SetStringField(TEXT("_name"), Title.IsEmpty() ? Asset->GetName() : Title);
		}
		else
		{
			Obj->SetStringField(TEXT("_name"), Asset->GetName());
		}

		JsonArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	return ToJsonString(JsonArray);
}

FString UArborCodexSearch::CreateCodexEntry(const FString& Category, const FString& AssetName, const FString& ContentPath, const FString& GameContextPath, const FString& PropertiesJson)
{
	// Validate category
	const FString CategoryLower = Category.ToLower().TrimStartAndEnd();
	const FCodexCategoryInfo* Info = GetCategories().Find(CategoryLower);
	if (!Info)
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("success"), false);
		Err->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown category: %s"), *Category));
		return ToJsonString(Err);
	}

	// Resolve UClass
	UClass* AssetClass = FindObject<UClass>(nullptr, *Info->ClassPath.ToString());
	if (!AssetClass)
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("success"), false);
		Err->SetStringField(TEXT("error"), FString::Printf(TEXT("Cannot resolve class for category: %s"), *Category));
		return ToJsonString(Err);
	}

	// Build package path
	FString BasePath;
	const FString SanitizedName = AssetName.Replace(TEXT(" "), TEXT("_"));

	if (!ContentPath.IsEmpty())
	{
		// Explicit content_path always wins
		BasePath = ContentPath;
	}
	else if (CategoryLower == TEXT("context"))
	{
		// GameContext creates its own subfolder: /Game/GameCodex/<Name>/
		BasePath = Info->DefaultPath / SanitizedName;
	}
	else if (!GameContextPath.IsEmpty())
	{
		// Derive path from GameContext parent + category subfolder
		FString Derived = DerivePathFromGameContext(GameContextPath, Info->SubfolderName);
		BasePath = Derived.IsEmpty() ? Info->DefaultPath : Derived;
	}
	else
	{
		BasePath = Info->DefaultPath;
	}

	const FString FullAssetName = FString::Printf(TEXT("%s_%s"), *Info->AssetPrefix, *SanitizedName);
	const FString PackagePath = BasePath / FullAssetName;

	// Parse properties JSON
	TSharedPtr<FJsonObject> PropsJson;
	if (!PropertiesJson.IsEmpty())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PropertiesJson);
		if (!FJsonSerializer::Deserialize(Reader, PropsJson) || !PropsJson.IsValid())
		{
			TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
			Err->SetBoolField(TEXT("success"), false);
			Err->SetStringField(TEXT("error"), TEXT("Failed to parse PropertiesJson"));
			return ToJsonString(Err);
		}
	}
	else
	{
		PropsJson = MakeShared<FJsonObject>();
	}

	// Inject display name into the category's NameField if not already set
	if (!Info->NameField.IsEmpty() && !PropsJson->HasField(Info->NameField))
	{
		PropsJson->SetStringField(Info->NameField, AssetName);
	}

	// Inject GameContext soft reference if provided
	if (!GameContextPath.IsEmpty())
	{
		FString FullGCPath = GameContextPath;
		if (!FullGCPath.Contains(TEXT(".")))
		{
			const FString ObjName = FPaths::GetBaseFilename(FullGCPath);
			FullGCPath = FString::Printf(TEXT("%s.%s"), *FullGCPath, *ObjName);
		}
		PropsJson->SetStringField(TEXT("GameContext"), FullGCPath);
	}

	// Check for existing asset (idempotent update)
	UObject* Asset = UEditorAssetLibrary::LoadAsset(PackagePath);

	if (Asset)
	{
		// Update existing — enforce locked fields
		if (FProperty* LockProp = Asset->GetClass()->FindPropertyByName(TEXT("LockedFields")))
		{
			if (const FSetProperty* SetProp = CastField<FSetProperty>(LockProp))
			{
				TSet<FString> LockedSet;
				FScriptSetHelper Helper(SetProp, SetProp->ContainerPtrToValuePtr<void>(Asset));
				for (int32 i = 0; i < Helper.Num(); i++)
				{
					if (Helper.IsValidIndex(i))
						LockedSet.Add(*reinterpret_cast<const FString*>(Helper.GetElementPtr(i)));
				}

				TArray<FString> KeysToRemove;
				PropsJson->Values.GetKeys(KeysToRemove);
				for (const FString& Key : KeysToRemove)
				{
					if (Key == TEXT("LockedFields") || LockedSet.Contains(Key))
					{
						PropsJson->RemoveField(Key);
					}
				}
			}
		}

		DeserializeProperties(Asset, AssetClass, PropsJson, true, AssetClass);
		Asset->MarkPackageDirty();
		UEditorAssetLibrary::SaveLoadedAsset(Asset);
	}
	else
	{
		// Create new
		UPackage* Package = CreatePackage(*PackagePath);
		Package->FullyLoad();

		Asset = NewObject<UObject>(Package, AssetClass, *FullAssetName, RF_Public | RF_Standalone);
		DeserializeProperties(Asset, AssetClass, PropsJson, true, AssetClass);

		FAssetRegistryModule::AssetCreated(Asset);
		Asset->MarkPackageDirty();

		FString Filename = FPackageName::LongPackageNameToFilename(
			PackagePath, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Package, Asset, *Filename, SaveArgs);
	}

	// Build result
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), PackagePath);
	Result->SetStringField(TEXT("_name"), GetDisplayName(Asset, Info->NameField));
	Result->SetStringField(TEXT("_category"), CategoryLower);
	return ToJsonString(Result);
}

FString UArborCodexSearch::UpdateCodexEntry(const FString& AssetPath, const FString& PropertiesJson)
{
	// Load the asset
	FSoftObjectPath SoftPath(AssetPath);
	UObject* Asset = SoftPath.TryLoad();

	if (!Asset && !AssetPath.Contains(TEXT(".")))
	{
		const FString ObjName = FPaths::GetBaseFilename(AssetPath);
		FSoftObjectPath FullPath(FString::Printf(TEXT("%s.%s"), *AssetPath, *ObjName));
		Asset = FullPath.TryLoad();
	}

	if (!Asset)
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("success"), false);
		Err->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
		return ToJsonString(Err);
	}

	// Determine category
	const FString Category = FindCategoryForClass(Asset->GetClass());
	const bool bIsGameContext = Category.IsEmpty() && Asset->IsA<UArborGameContextAsset>();
	if (Category.IsEmpty() && !bIsGameContext)
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("success"), false);
		Err->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset is not a codex entry: %s"), *AssetPath));
		return ToJsonString(Err);
	}

	// Parse properties JSON
	TSharedPtr<FJsonObject> PropsJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PropertiesJson);
	if (!FJsonSerializer::Deserialize(Reader, PropsJson) || !PropsJson.IsValid())
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("success"), false);
		Err->SetStringField(TEXT("error"), TEXT("Failed to parse PropertiesJson"));
		return ToJsonString(Err);
	}

	// --- Read LockedFields from asset ---
	TSet<FString> AssetLockedFields;
	if (FProperty* LockProp = Asset->GetClass()->FindPropertyByName(TEXT("LockedFields")))
	{
		if (const FSetProperty* SetProp = CastField<FSetProperty>(LockProp))
		{
			FScriptSetHelper Helper(SetProp, SetProp->ContainerPtrToValuePtr<void>(Asset));
			for (int32 i = 0; i < Helper.Num(); i++)
			{
				if (Helper.IsValidIndex(i))
					AssetLockedFields.Add(*reinterpret_cast<const FString*>(Helper.GetElementPtr(i)));
			}
		}
	}

	// --- Filter locked fields from update ---
	TArray<FString> SkippedFields;
	TArray<FString> UpdatedFields;
	TArray<FString> AllKeys;
	PropsJson->Values.GetKeys(AllKeys);

	for (const FString& Key : AllKeys)
	{
		if (Key == TEXT("LockedFields") || AssetLockedFields.Contains(Key))
		{
			SkippedFields.Add(Key);
			PropsJson->RemoveField(Key);
		}
		else
		{
			UpdatedFields.Add(Key);
		}
	}

	// Apply partial update (only non-locked fields remain in PropsJson)
	DeserializeProperties(Asset, Asset->GetClass(), PropsJson, true, Asset->GetClass());
	Asset->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(Asset);

	// Notify the asset registry so the codex widget refreshes
	FAssetRegistryModule::GetRegistry().AssetCreated(Asset);

	// Return updated entry with feedback arrays
	TSharedPtr<FJsonObject> Obj;
	if (bIsGameContext)
	{
		Obj = SerializeProperties(Asset, Asset->GetClass(), true, Asset->GetClass());
		Obj->SetStringField(TEXT("_category"), TEXT("context"));
		Obj->SetStringField(TEXT("_path"), Asset->GetPathName());
		FProperty* TitlePropCtx = Asset->GetClass()->FindPropertyByName(FName(TEXT("GameTitle")));
		Obj->SetStringField(TEXT("_name"), (TitlePropCtx && CastField<FStrProperty>(TitlePropCtx))
			? *CastField<FStrProperty>(TitlePropCtx)->ContainerPtrToValuePtr<FString>(Asset)
			: Asset->GetName());
	}
	else
	{
		const FString& NameField = GetNameFieldForCategory(Category);
		Obj = SerializeCodexAsset(Asset, Category, NameField);
	}
	Obj->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> UpdatedArr, SkippedArr;
	for (const FString& F : UpdatedFields) UpdatedArr.Add(MakeShared<FJsonValueString>(F));
	for (const FString& F : SkippedFields) SkippedArr.Add(MakeShared<FJsonValueString>(F));
	Obj->SetArrayField(TEXT("_updated_fields"), UpdatedArr);
	Obj->SetArrayField(TEXT("_skipped_locked_fields"), SkippedArr);

	return ToJsonString(Obj);
}

FString UArborCodexSearch::DeleteCodexEntry(const FString& AssetPath)
{
	// Load the asset
	FSoftObjectPath SoftPath(AssetPath);
	UObject* Asset = SoftPath.TryLoad();

	if (!Asset && !AssetPath.Contains(TEXT(".")))
	{
		const FString ObjName = FPaths::GetBaseFilename(AssetPath);
		FSoftObjectPath FullPath(FString::Printf(TEXT("%s.%s"), *AssetPath, *ObjName));
		Asset = FullPath.TryLoad();
	}

	if (!Asset)
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("success"), false);
		Err->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
		return ToJsonString(Err);
	}

	const bool bDeleted = UEditorAssetLibrary::DeleteLoadedAsset(Asset);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bDeleted);
	Result->SetStringField(TEXT("deleted_path"), AssetPath);
	if (!bDeleted)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to delete asset — it may be referenced by other assets"));
	}
	return ToJsonString(Result);
}

FString UArborCodexSearch::GetEntriesForContext(const FString& GameContextPath)
{
	// Normalize the GameContext path (ensure it has .ObjectName suffix for comparison)
	FString NormalizedGC = GameContextPath;
	if (!NormalizedGC.Contains(TEXT(".")))
	{
		const FString ObjName = FPaths::GetBaseFilename(NormalizedGC);
		NormalizedGC = FString::Printf(TEXT("%s.%s"), *NormalizedGC, *ObjName);
	}

	// Verify the GameContext asset exists
	FSoftObjectPath GCSoftPath(NormalizedGC);
	UObject* GCAsset = GCSoftPath.TryLoad();
	if (!GCAsset)
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("success"), false);
		Err->SetStringField(TEXT("error"), FString::Printf(TEXT("GameContext not found: %s"), *GameContextPath));
		return ToJsonString(Err);
	}

	// Build result grouped by category
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("game_context"), NormalizedGC);

	for (const auto& CatPair : GetCategories())
	{
		// Skip the context category itself
		if (CatPair.Key == TEXT("context")) continue;

		TArray<FAssetData> Assets = FindAssetsOfClass(CatPair.Value.ClassPath);
		TArray<TSharedPtr<FJsonValue>> CatEntries;

		for (const FAssetData& AD : Assets)
		{
			UObject* Asset = AD.GetAsset();
			if (!Asset) continue;

			// Check if the GameContext soft reference matches
			FProperty* GCProp = Asset->GetClass()->FindPropertyByName(FName(TEXT("GameContext")));
			if (!GCProp) continue;

			if (CastField<FSoftObjectProperty>(GCProp))
			{
				FString RefPath;
				GCProp->ExportText_InContainer(0, RefPath, Asset, nullptr, nullptr, PPF_None);
				if (RefPath == NormalizedGC)
				{
					TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("_path"), Asset->GetPathName());
					Entry->SetStringField(TEXT("_name"), GetDisplayName(Asset, CatPair.Value.NameField));
					CatEntries.Add(MakeShared<FJsonValueObject>(Entry));
				}
			}
		}

		if (CatEntries.Num() > 0)
		{
			Root->SetArrayField(CatPair.Key, CatEntries);
		}
	}

	return ToJsonString(Root);
}
