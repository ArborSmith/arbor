#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborFoliageTools.generated.h"

UCLASS()
class ARBOR_API UArborFoliageTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create a FoliageType_InstancedStaticMesh asset for a given mesh.
	 *
	 * @param ParamsJson  JSON: {mesh_path, name?, content_path?, density?,
	 *                           scale_min?, scale_max?, align_to_normal?,
	 *                           random_yaw?, ground_slope_angle?, cull_distance_max?}
	 * @return JSON: {success, asset_path}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Foliage")
	static FString CreateFoliageType(const FString& ParamsJson);

	/**
	 * Scatter foliage instances in a circular or rectangular area with ground snapping.
	 * Handles foliage actor creation, transform generation, ground tracing, and
	 * instance placement all in C++ for maximum performance.
	 *
	 * @param ParamsJson  JSON: {foliage_type_path, count,
	 *                           center?:[x,y,z], radius?,
	 *                           bounds_min?:[x,y,z], bounds_max?:[x,y,z],
	 *                           snap_to_ground?, landscape?,
	 *                           random_yaw?, scale_min?, scale_max?, seed?}
	 * @return JSON: {success, placed, method}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Foliage")
	static FString PaintFoliageInstances(const FString& ParamsJson);

	/**
	 * Remove foliage instances, optionally within a radius.
	 *
	 * @param ParamsJson  JSON: {foliage_type_path, center?:[x,y,z], radius?}
	 * @return JSON: {success, removed}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Foliage")
	static FString RemoveFoliageInstances(const FString& ParamsJson);

	/**
	 * Get total foliage instance count.
	 *
	 * @return JSON: {count}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Foliage")
	static FString GetFoliageCount();
};
