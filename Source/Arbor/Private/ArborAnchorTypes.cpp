#include "ArborAnchorTypes.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

// ---------------------------------------------------------------------------
// Path utilities
// ---------------------------------------------------------------------------

FString UArborAnchorRegistry::CleanAssetPath(const FString& AssetPath)
{
	FString Clean = AssetPath;
	int32 DotIdx;
	if (Clean.FindChar('.', DotIdx))
	{
		Clean.LeftInline(DotIdx);
	}
	return Clean;
}

FString UArborAnchorRegistry::GetPackRoot(const FString& AssetPath)
{
	FString Path = CleanAssetPath(AssetPath);

	// "/Game/MedievalMarket/Meshes/SM_Floor" → "MedievalMarket/Meshes/SM_Floor"
	if (!Path.RemoveFromStart(TEXT("/Game/")))
	{
		return FString();
	}

	// Take first path component → "MedievalMarket"
	int32 SlashIdx;
	if (Path.FindChar('/', SlashIdx))
	{
		Path.LeftInline(SlashIdx);
	}

	return TEXT("/Game/") + Path;
}

FString UArborAnchorRegistry::GetRegistryPath(const FString& AssetPath)
{
	FString Root = GetPackRoot(AssetPath);
	if (Root.IsEmpty())
	{
		return FString();
	}
	return Root / TEXT("DA_AnchorRegistry");
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

const FArborMeshAnchors* UArborAnchorRegistry::FindAnchors(const FString& AssetPath) const
{
	return Entries.Find(CleanAssetPath(AssetPath));
}

void UArborAnchorRegistry::SetAnchors(const FString& AssetPath, const FArborMeshAnchors& Data)
{
	Entries.Add(CleanAssetPath(AssetPath), Data);
	MarkPackageDirty();
}

// ---------------------------------------------------------------------------
// Find / create registry
// ---------------------------------------------------------------------------

UArborAnchorRegistry* UArborAnchorRegistry::FindRegistry(const FString& AssetPath)
{
	FString RegistryPath = GetRegistryPath(AssetPath);
	if (RegistryPath.IsEmpty())
	{
		return nullptr;
	}
	return Cast<UArborAnchorRegistry>(UEditorAssetLibrary::LoadAsset(RegistryPath));
}

UArborAnchorRegistry* UArborAnchorRegistry::FindOrCreateRegistry(const FString& AssetPath)
{
	UArborAnchorRegistry* Registry = FindRegistry(AssetPath);
	if (Registry)
	{
		return Registry;
	}

	FString RegistryPath = GetRegistryPath(AssetPath);
	if (RegistryPath.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[ArborAnchorRegistry] Cannot determine pack root for %s"), *AssetPath);
		return nullptr;
	}

	// Derive package path and asset name
	FString PackagePath = RegistryPath;  // e.g. "/Game/MedievalMarket/DA_AnchorRegistry"
	FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);  // "DA_AnchorRegistry"

	UPackage* Package = CreatePackage(*PackagePath);
	Package->FullyLoad();

	Registry = NewObject<UArborAnchorRegistry>(
		Package, UArborAnchorRegistry::StaticClass(),
		*AssetName, RF_Public | RF_Standalone);

	FAssetRegistryModule::AssetCreated(Registry);
	Registry->MarkPackageDirty();

	// Save to disk
	FString Filename = FPackageName::LongPackageNameToFilename(
		PackagePath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, Registry, *Filename, SaveArgs);

	UE_LOG(LogTemp, Log, TEXT("[ArborAnchorRegistry] Created registry at %s"), *RegistryPath);
	return Registry;
}
