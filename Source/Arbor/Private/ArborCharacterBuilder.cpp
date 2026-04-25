#include "ArborCharacterBuilder.h"
#include "ArborCharacterTypes.h"
#include "ArborGameContextTypes.h"
#include "ArborPathUtils.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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

/** Write .arbor.json sidecar next to the .uasset */
static void WriteSidecar(const FString& PackagePath, TSharedPtr<FJsonObject> Data)
{
	FString Filename = FPackageName::LongPackageNameToFilename(
		PackagePath, FPackageName::GetAssetPackageExtension());
	FString SidecarPath = FPaths::ChangeExtension(Filename, TEXT(".arbor.json"));

	Data->SetStringField(TEXT("_sidecar_version"), TEXT("1.0"));
	Data->SetStringField(TEXT("_asset_type"), TEXT("character"));

	FString JsonStr;
	auto Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonStr);
	FJsonSerializer::Serialize(Data.ToSharedRef(), Writer);
	FFileHelper::SaveStringToFile(JsonStr, *SidecarPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

/** Serialize a character data asset to JSON */
static TSharedPtr<FJsonObject> CharacterToJson(const UCharacterDataAsset* Asset)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("name"), Asset->CharacterName);
	Root->SetStringField(TEXT("description"), Asset->Description);

	// Tags
	TArray<TSharedPtr<FJsonValue>> TagsArr;
	for (const FString& Tag : Asset->Tags)
	{
		TagsArr.Add(MakeShared<FJsonValueString>(Tag));
	}
	Root->SetArrayField(TEXT("tags"), TagsArr);

	if (!Asset->GameContext.IsNull())
	{
		Root->SetStringField(TEXT("game_context"), Asset->GameContext.ToSoftObjectPath().ToString());
	}

	return Root;
}

/** Populate a character data asset from JSON */
static void PopulateCharacter(UCharacterDataAsset* Asset, const TSharedPtr<FJsonObject>& Params)
{
	Asset->CharacterName = GetOptStr(Params, TEXT("name"), Asset->CharacterName);
	Asset->Description = GetOptStr(Params, TEXT("description"), Asset->Description);

	// Tags
	const TArray<TSharedPtr<FJsonValue>>* TagsArr;
	if (Params->TryGetArrayField(TEXT("tags"), TagsArr))
	{
		Asset->Tags.Empty();
		for (const auto& V : *TagsArr)
		{
			Asset->Tags.Add(V->AsString());
		}
	}

	// Game context
	FString GameContextPath = GetOptStr(Params, TEXT("game_context"));
	if (!GameContextPath.IsEmpty())
	{
		Asset->GameContext = TSoftObjectPtr<UArborGameContextAsset>(FSoftObjectPath(GameContextPath));
	}
}

// ============================================================================
// Public API
// ============================================================================

FString UArborCharacterBuilder::CreateCharacterAsset(const FString& ParamsJson)
{
	auto Params = ParseJson(ParamsJson);
	if (!Params.IsValid()) return MakeErrorResult(TEXT("Invalid JSON"));

	const FString Name = GetOptStr(Params, TEXT("name"));
	if (Name.IsEmpty()) return MakeErrorResult(TEXT("'name' is required"));

	const FString ExplicitPath = GetOptStr(Params, TEXT("content_path"));
	const FString GameContextPath = GetOptStr(Params, TEXT("game_context"));

	FString ContentPath;
	if (!ExplicitPath.IsEmpty())
	{
		ContentPath = ExplicitPath;
	}
	else if (!GameContextPath.IsEmpty())
	{
		FString Derived = DerivePathFromGameContext(GameContextPath, TEXT("Characters"));
		ContentPath = Derived.IsEmpty() ? TEXT("/Game/Characters") : Derived;
	}
	else
	{
		ContentPath = TEXT("/Game/Characters");
	}

	FString AssetName = TEXT("DA_") + Name.Replace(TEXT(" "), TEXT("_"));
	FString PackagePath = ContentPath / AssetName;

	// Check for existing asset (idempotent update)
	UCharacterDataAsset* Asset = Cast<UCharacterDataAsset>(UEditorAssetLibrary::LoadAsset(PackagePath));

	if (Asset)
	{
		// Update existing
		PopulateCharacter(Asset, Params);
		Asset->MarkPackageDirty();
		UEditorAssetLibrary::SaveLoadedAsset(Asset);
	}
	else
	{
		// Create new
		UPackage* Package = CreatePackage(*PackagePath);
		Package->FullyLoad();

		Asset = NewObject<UCharacterDataAsset>(
			Package, UCharacterDataAsset::StaticClass(),
			*AssetName, RF_Public | RF_Standalone);

		PopulateCharacter(Asset, Params);

		FAssetRegistryModule::AssetCreated(Asset);
		Asset->MarkPackageDirty();

		FString Filename = FPackageName::LongPackageNameToFilename(
			PackagePath, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Package, Asset, *Filename, SaveArgs);
	}

	// Write sidecar
	TSharedPtr<FJsonObject> SidecarData = CharacterToJson(Asset);
	WriteSidecar(PackagePath, SidecarData);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), PackagePath);
	return SerializeJson(Result);
}

FString UArborCharacterBuilder::QueryCharacterAsset(const FString& AssetPath)
{
	UCharacterDataAsset* Asset = Cast<UCharacterDataAsset>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Asset)
	{
		return MakeErrorResult(FString::Printf(TEXT("Character asset not found: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> Root = CharacterToJson(Asset);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	return SerializeJson(Root);
}

FString UArborCharacterBuilder::UpdateCharacterSection(const FString& ParamsJson)
{
	auto Params = ParseJson(ParamsJson);
	if (!Params.IsValid()) return MakeErrorResult(TEXT("Invalid JSON"));

	const FString AssetPath = GetOptStr(Params, TEXT("asset_path"));
	if (AssetPath.IsEmpty()) return MakeErrorResult(TEXT("'asset_path' is required"));

	const FString Section = GetOptStr(Params, TEXT("section"));
	if (Section.IsEmpty()) return MakeErrorResult(TEXT("'section' is required"));

	UCharacterDataAsset* Asset = Cast<UCharacterDataAsset>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Asset)
	{
		return MakeErrorResult(FString::Printf(TEXT("Character asset not found: %s"), *AssetPath));
	}

	if (Section == TEXT("description"))
	{
		FString Data;
		if (Params->TryGetStringField(TEXT("data"), Data))
		{
			Asset->Description = Data;
		}
	}
	else if (Section == TEXT("tags"))
	{
		const TArray<TSharedPtr<FJsonValue>>* DataArr;
		if (Params->TryGetArrayField(TEXT("data"), DataArr))
		{
			Asset->Tags.Empty();
			for (const auto& V : *DataArr)
			{
				Asset->Tags.Add(V->AsString());
			}
		}
	}
	else
	{
		return MakeErrorResult(FString::Printf(TEXT("Unknown section: %s. Expected: description, tags"), *Section));
	}

	Asset->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(Asset);

	// Re-write sidecar
	WriteSidecar(AssetPath, CharacterToJson(Asset));

	return MakeSuccessResult(AssetPath);
}

FString UArborCharacterBuilder::ListCharacterAssets(const FString& FolderPath)
{
	FString Folder = FolderPath.IsEmpty() ? TEXT("/Game/Characters") : FolderPath;

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> AssetList;
	AR.GetAssetsByPath(FName(*Folder), AssetList, true);

	TArray<TSharedPtr<FJsonValue>> Characters;
	for (const FAssetData& AD : AssetList)
	{
		if (AD.AssetClassPath == UCharacterDataAsset::StaticClass()->GetClassPathName())
		{
			UCharacterDataAsset* Asset = Cast<UCharacterDataAsset>(AD.GetAsset());
			if (!Asset) continue;

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("asset_path"), AD.GetObjectPathString());
			Entry->SetStringField(TEXT("name"), Asset->CharacterName);
			Characters.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetArrayField(TEXT("characters"), Characters);
	return SerializeJson(Root);
}

FString UArborCharacterBuilder::ImportFromSidecar(const FString& SidecarPath)
{
	FString JsonStr;
	if (!FFileHelper::LoadFileToString(JsonStr, *SidecarPath))
	{
		return MakeErrorResult(FString::Printf(TEXT("Failed to read sidecar file: %s"), *SidecarPath));
	}

	auto Params = ParseJson(JsonStr);
	if (!Params.IsValid()) return MakeErrorResult(TEXT("Invalid JSON in sidecar file"));

	// Derive content_path from sidecar location if not present
	if (!Params->HasField(TEXT("content_path")))
	{
		// Convert disk path back to content path
		FString UassetPath = FPaths::ChangeExtension(SidecarPath, FPackageName::GetAssetPackageExtension());
		FString PackageName;
		if (FPackageName::TryConvertFilenameToLongPackageName(UassetPath, PackageName))
		{
			FString Dir = FPaths::GetPath(PackageName);
			Params->SetStringField(TEXT("content_path"), Dir);
		}
	}

	return CreateCharacterAsset(SerializeJson(Params));
}
