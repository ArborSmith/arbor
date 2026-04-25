#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborMeshTools.generated.h"

UCLASS()
class ARBOR_API UArborMeshTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Adjust the pivot point of a static mesh by shifting all vertices.
	 *
	 * @param ParamsJson  JSON: {asset_path, pivot:"bottom"|"center"|"top"}
	 * @return JSON: {success, offset:{x,y,z}, bounds_before:{min,max}, bounds_after:{min,max}}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Mesh")
	static FString FixMeshPivot(const FString& ParamsJson);

	/**
	 * Scale a static mesh's vertices by a uniform factor.
	 * Primarily for Meshy imports (metres → centimetres, scale=100).
	 *
	 * @param ParamsJson  JSON: {asset_path, scale:float}
	 * @return JSON: {success, scale, bounds_before:{min,max}, bounds_after:{min,max}}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Mesh")
	static FString FixMeshScale(const FString& ParamsJson);

	/**
	 * Set up collision geometry on a static mesh.
	 *
	 * @param ParamsJson  JSON: {asset_path, mode:"box"|"sphere"|"capsule"|"convex"|"complex_simple"|"complex_only",
	 *                           hull_count?, max_hull_verts?, hull_precision?}
	 * @return JSON: {success, mode}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Mesh")
	static FString SetupCollision(const FString& ParamsJson);

	/**
	 * Get the bounding box of a static mesh asset.
	 *
	 * @param AssetPath  Content path to the static mesh.
	 * @return JSON: {success, min:{x,y,z}, max:{x,y,z}, center:{x,y,z}, extent:{x,y,z}}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Mesh")
	static FString GetMeshBounds(const FString& AssetPath);

private:
	/** Shift all vertex positions in a mesh by an offset. */
	static bool ApplyVertexOffset(UStaticMesh* Mesh, const FVector& Offset);

	/** Scale all vertex positions in a mesh by a uniform factor. */
	static bool ApplyVertexScale(UStaticMesh* Mesh, float Scale);
};
