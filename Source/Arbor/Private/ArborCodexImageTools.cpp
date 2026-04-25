#include "ArborCodexImageTools.h"
#include "Engine/Texture2D.h"
#include "EditorAssetLibrary.h"
#include "UObject/SavePackage.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"

// ============================================================================
// Helpers
// ============================================================================

namespace
{

FString ToJsonString(const TSharedPtr<FJsonObject>& Obj)
{
	FString Output;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
	return Output;
}

FString MakeJsonError(const FString& Message)
{
	TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
	Err->SetBoolField(TEXT("success"), false);
	Err->SetStringField(TEXT("error"), Message);
	return ToJsonString(Err);
}

UObject* LoadCodexAsset(const FString& AssetPath)
{
	FSoftObjectPath SoftPath(AssetPath);
	UObject* Asset = SoftPath.TryLoad();

	if (!Asset && !AssetPath.Contains(TEXT(".")))
	{
		const FString ObjName = FPaths::GetBaseFilename(AssetPath);
		FSoftObjectPath FullPath(FString::Printf(TEXT("%s.%s"), *AssetPath, *ObjName));
		Asset = FullPath.TryLoad();
	}

	return Asset;
}

void SaveAsset(UObject* Asset)
{
	UPackage* Package = Asset->GetOutermost();
	Package->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;

	FString PackageFilename = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());

	UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs);

	// Notify asset registry so the codex widget refreshes
	FAssetRegistryModule::GetRegistry().AssetCreated(Asset);
}

// Get the ConceptArt soft object property via reflection
FSoftObjectProperty* FindConceptArtProp(UObject* Asset)
{
	FProperty* Prop = Asset->GetClass()->FindPropertyByName(TEXT("ConceptArt"));
	return Prop ? CastField<FSoftObjectProperty>(Prop) : nullptr;
}

// Get the ConceptArtGallery array property via reflection
FArrayProperty* FindGalleryProp(UObject* Asset)
{
	FProperty* Prop = Asset->GetClass()->FindPropertyByName(TEXT("ConceptArtGallery"));
	return Prop ? CastField<FArrayProperty>(Prop) : nullptr;
}

// Get the ConceptArtPrompt string property via reflection
FStrProperty* FindPromptProp(UObject* Asset)
{
	FProperty* Prop = Asset->GetClass()->FindPropertyByName(TEXT("ConceptArtPrompt"));
	return Prop ? CastField<FStrProperty>(Prop) : nullptr;
}

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

FString UArborCodexImageTools::SetConceptArt(const FString& AssetPath, const FString& TexturePath, const FString& Prompt)
{
	UObject* Asset = LoadCodexAsset(AssetPath);
	if (!Asset) return MakeJsonError(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));

	FSoftObjectProperty* ArtProp = FindConceptArtProp(Asset);
	if (!ArtProp) return MakeJsonError(FString::Printf(TEXT("Asset has no ConceptArt property: %s"), *AssetPath));

	// Set the concept art soft reference
	FSoftObjectPtr* SoftPtr = ArtProp->ContainerPtrToValuePtr<FSoftObjectPtr>(Asset);
	if (!TexturePath.IsEmpty())
	{
		FString FullTexPath = TexturePath;
		if (!FullTexPath.Contains(TEXT(".")))
		{
			const FString ObjName = FPaths::GetBaseFilename(FullTexPath);
			FullTexPath = FString::Printf(TEXT("%s.%s"), *FullTexPath, *ObjName);
		}
		*SoftPtr = FSoftObjectPtr(FSoftObjectPath(FullTexPath));
	}
	else
	{
		*SoftPtr = FSoftObjectPtr();
	}

	// Set the prompt if provided
	FStrProperty* PromptProp = FindPromptProp(Asset);
	if (PromptProp && !Prompt.IsEmpty())
	{
		FString* PromptVal = PromptProp->ContainerPtrToValuePtr<FString>(Asset);
		*PromptVal = Prompt;
	}

	SaveAsset(Asset);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("texture_path"), TexturePath);
	return ToJsonString(Result);
}

FString UArborCodexImageTools::AddGalleryImage(const FString& AssetPath, const FString& TexturePath)
{
	UObject* Asset = LoadCodexAsset(AssetPath);
	if (!Asset) return MakeJsonError(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));

	FArrayProperty* GalleryProp = FindGalleryProp(Asset);
	if (!GalleryProp) return MakeJsonError(FString::Printf(TEXT("Asset has no ConceptArtGallery property: %s"), *AssetPath));

	// Build the full texture path
	FString FullTexPath = TexturePath;
	if (!FullTexPath.Contains(TEXT(".")))
	{
		const FString ObjName = FPaths::GetBaseFilename(FullTexPath);
		FullTexPath = FString::Printf(TEXT("%s.%s"), *FullTexPath, *ObjName);
	}

	// Add via reflection
	FScriptArrayHelper Helper(GalleryProp, GalleryProp->ContainerPtrToValuePtr<void>(Asset));
	const int32 NewIdx = Helper.AddValue();

	// The inner type is FSoftObjectProperty — set it
	FSoftObjectPtr* ElemPtr = reinterpret_cast<FSoftObjectPtr*>(Helper.GetRawPtr(NewIdx));
	*ElemPtr = FSoftObjectPtr(FSoftObjectPath(FullTexPath));

	SaveAsset(Asset);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("gallery_count"), Helper.Num());
	return ToJsonString(Result);
}

FString UArborCodexImageTools::RemoveGalleryImage(const FString& AssetPath, const FString& TexturePath)
{
	UObject* Asset = LoadCodexAsset(AssetPath);
	if (!Asset) return MakeJsonError(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));

	FArrayProperty* GalleryProp = FindGalleryProp(Asset);
	if (!GalleryProp) return MakeJsonError(FString::Printf(TEXT("Asset has no ConceptArtGallery property: %s"), *AssetPath));

	FString FullTexPath = TexturePath;
	if (!FullTexPath.Contains(TEXT(".")))
	{
		const FString ObjName = FPaths::GetBaseFilename(FullTexPath);
		FullTexPath = FString::Printf(TEXT("%s.%s"), *FullTexPath, *ObjName);
	}

	FScriptArrayHelper Helper(GalleryProp, GalleryProp->ContainerPtrToValuePtr<void>(Asset));
	bool bFound = false;

	for (int32 i = Helper.Num() - 1; i >= 0; i--)
	{
		const FSoftObjectPtr* ElemPtr = reinterpret_cast<const FSoftObjectPtr*>(Helper.GetRawPtr(i));
		if (ElemPtr->ToSoftObjectPath().ToString() == FullTexPath)
		{
			Helper.RemoveValues(i, 1);
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		return MakeJsonError(FString::Printf(TEXT("Texture not found in gallery: %s"), *TexturePath));
	}

	SaveAsset(Asset);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("gallery_count"), Helper.Num());
	return ToJsonString(Result);
}

FString UArborCodexImageTools::GetCodexImages(const FString& AssetPath)
{
	UObject* Asset = LoadCodexAsset(AssetPath);
	if (!Asset) return MakeJsonError(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	// Primary concept art
	FSoftObjectProperty* ArtProp = FindConceptArtProp(Asset);
	if (ArtProp)
	{
		const FSoftObjectPtr* SoftPtr = ArtProp->ContainerPtrToValuePtr<FSoftObjectPtr>(Asset);
		Result->SetStringField(TEXT("concept_art"), SoftPtr->ToSoftObjectPath().ToString());
	}
	else
	{
		Result->SetStringField(TEXT("concept_art"), TEXT(""));
	}

	// Gallery
	FArrayProperty* GalleryProp = FindGalleryProp(Asset);
	TArray<TSharedPtr<FJsonValue>> GalleryArr;
	if (GalleryProp)
	{
		FScriptArrayHelper Helper(GalleryProp, GalleryProp->ContainerPtrToValuePtr<void>(Asset));
		for (int32 i = 0; i < Helper.Num(); i++)
		{
			const FSoftObjectPtr* ElemPtr = reinterpret_cast<const FSoftObjectPtr*>(Helper.GetRawPtr(i));
			GalleryArr.Add(MakeShared<FJsonValueString>(ElemPtr->ToSoftObjectPath().ToString()));
		}
	}
	Result->SetArrayField(TEXT("gallery"), GalleryArr);

	// Prompt
	FStrProperty* PromptProp = FindPromptProp(Asset);
	if (PromptProp)
	{
		Result->SetStringField(TEXT("prompt"), *PromptProp->ContainerPtrToValuePtr<FString>(Asset));
	}
	else
	{
		Result->SetStringField(TEXT("prompt"), TEXT(""));
	}

	Result->SetStringField(TEXT("asset_path"), AssetPath);
	return ToJsonString(Result);
}

