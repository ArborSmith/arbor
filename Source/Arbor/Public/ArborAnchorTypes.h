#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/StaticMesh.h"
#include "ArborAnchorTypes.generated.h"

/** A single anchor point on a mesh. */
USTRUCT(BlueprintType)
struct ARBOR_API FArborAnchor
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor")
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor")
	FString Type;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor")
	FVector Position = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor")
	FVector Direction = FVector::ForwardVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor")
	float Width = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor")
	float Height = 0.f;
};

/** All anchor data for a single mesh asset. */
USTRUCT(BlueprintType)
struct ARBOR_API FArborMeshAnchors
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchors")
	TSoftObjectPtr<UStaticMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchors")
	FString AssetType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchors")
	TArray<FArborAnchor> Anchors;
};

/**
 * Data asset storing anchor metadata for all meshes in an asset pack.
 * Place one at the root of each pack folder (e.g. /Game/MedievalMarket/DA_AnchorRegistry).
 * AnalyzeMesh auto-creates and populates these.
 */
UCLASS(BlueprintType)
class ARBOR_API UArborAnchorRegistry : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Map from content path (e.g. "/Game/Pack/Meshes/SM_Foo") to anchor data. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anchors")
	TMap<FString, FArborMeshAnchors> Entries;

	/** Find anchor data for a mesh path. Returns nullptr if not found. */
	const FArborMeshAnchors* FindAnchors(const FString& AssetPath) const;

	/** Add or update anchor data for a mesh. Marks the asset dirty. */
	void SetAnchors(const FString& AssetPath, const FArborMeshAnchors& Data);

	// --- Static utilities ---

	/** Derive the pack root folder from an asset path.
	 *  "/Game/MedievalMarket/Meshes/SM_Floor" → "/Game/MedievalMarket" */
	static FString GetPackRoot(const FString& AssetPath);

	/** Get the expected registry asset path for a given asset's pack.
	 *  "/Game/MedievalMarket/Meshes/SM_Floor" → "/Game/MedievalMarket/DA_AnchorRegistry" */
	static FString GetRegistryPath(const FString& AssetPath);

	/** Find an existing registry for the given asset's pack. Returns nullptr if none. */
	static UArborAnchorRegistry* FindRegistry(const FString& AssetPath);

	/** Find or create a registry for the given asset's pack. */
	static UArborAnchorRegistry* FindOrCreateRegistry(const FString& AssetPath);

private:
	/** Strip object suffix from an asset path (".SM_Foo" from "/Game/.../SM_Foo.SM_Foo"). */
	static FString CleanAssetPath(const FString& AssetPath);
};
