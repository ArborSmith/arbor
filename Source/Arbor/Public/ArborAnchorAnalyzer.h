#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborAnchorAnalyzer.generated.h"

UCLASS()
class ARBOR_API UArborAnchorAnalyzer : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Analyze a static mesh and generate anchor metadata.
	 * Computes footprint from bounds, generates cardinal-face anchors (N/S/E/W)
	 * and a bottom-center snap_base. Accepts an asset_type hint for type-specific
	 * anchors (e.g. "building" adds "front_door").
	 *
	 * @param ParamsJson  JSON: {asset_path, asset_type?:"building"|"road_segment"|"prop"|"wall"|"floor"}
	 * @return JSON: {success, asset_path, asset_type, footprint:{...}, bounds_3d:{...}, anchors:[...]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Anchors")
	static FString AnalyzeMesh(const FString& ParamsJson);

	/**
	 * Read anchor metadata from a .anchor.json sidecar file.
	 *
	 * @param AssetPath  Content path to the static mesh (e.g. "/Game/Fab/SM_House")
	 * @return JSON: the full anchor metadata, or {success:false, error:...}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Anchors")
	static FString GetAnchorMetadata(const FString& AssetPath);

	/**
	 * Write anchor metadata to a .anchor.json sidecar file.
	 *
	 * @param ParamsJson  JSON: {asset_path, metadata:{...}}
	 * @return JSON: {success, sidecar_path}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Anchors")
	static FString SetAnchorMetadata(const FString& ParamsJson);

	/**
	 * Draw debug visualization of anchor points in the editor viewport.
	 * Uses DrawDebugHelpers which render in the editor world.
	 *
	 * @param ParamsJson  JSON: {asset_path, location?:{x,y,z}, radius?:6, arrow_length?:30, duration?:-1, thickness?:2}
	 * @return JSON: {success, anchor_count}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Anchors")
	static FString DrawAnchors(const FString& ParamsJson);

	/**
	 * Find compatible anchor pairs between two assets using the compatibility table.
	 * Loads both sidecars and the anchor_compatibility.json config, returns ranked pairs.
	 *
	 * @param ParamsJson  JSON: {from_asset, to_asset, filter_type?}
	 *        filter_type: optional anchor type filter (e.g. "wall_snap", "wall_connector").
	 *        Only returns pairs where from_type or to_type matches.
	 * @return JSON: {success, pairs:[{from_anchor, to_anchor, from_type, to_type, hint, relationship},...]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Anchors")
	static FString FindCompatibleAnchors(const FString& ParamsJson);

	/**
	 * Flush all persistent debug draws from the editor world.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Anchors")
	static void FlushAnchors();

	/**
	 * Add ArborAnchorComponent to all StaticMeshActors matching a label prefix.
	 * Skips actors that already have the component.
	 *
	 * @param LabelPrefix  Actor label prefix filter (e.g. "Cat_"). Empty = all StaticMeshActors.
	 * @return JSON: {success, added, skipped}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Anchors")
	static FString AddAnchorDebugToActors(const FString& LabelPrefix);

	/**
	 * Analyze all static meshes in a content folder (batch AnalyzeMesh).
	 *
	 * @param ParamsJson  JSON: {folder_path, asset_type?}
	 * @return JSON: {success, folder_path, analyzed, failed, results:[{asset_path,asset_type,anchor_count,socket_count},...], errors:[...]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Anchors")
	static FString AnalyzePack(const FString& ParamsJson);

	/**
	 * List all assets that have anchor metadata in anchor registries.
	 *
	 * @param FolderPath  Optional folder filter (e.g. "/Game/Fab"). Empty = all.
	 * @return JSON: {success, count, assets:[{asset_path, asset_type, anchor_count},...]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Anchors")
	static FString ListAnalyzedAssets(const FString& FolderPath);

	/** Convert a /Game/ asset path to its .anchor.json sidecar disk path. */
	static FString GetSidecarPath(const FString& AssetPath);

private:
	static TSharedPtr<FJsonObject> MakeAnchor(const FString& Id, const FString& Type,
		const FVector& Position, const FVector& Direction, double Width = 0.0, double Height = 0.0);

	/** Detect door/window openings in a wall mesh via grid raycasting. */
	static void DetectOpenings(UStaticMesh* Mesh, const FBox& Bounds,
		TArray<TSharedPtr<FJsonValue>>& OutAnchors);
};
