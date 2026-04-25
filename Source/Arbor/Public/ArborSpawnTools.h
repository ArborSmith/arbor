#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborSpawnTools.generated.h"

UCLASS()
class ARBOR_API UArborSpawnTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Spawn a light actor (point, spot, directional, rect).
	 *
	 * @param ParamsJson  JSON: {light_type, x, y, z, intensity, color:{r,g,b}, attenuation_radius, label}
	 * @return JSON: {actor_path, light_type, position:{x,y,z}}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Spawn")
	static FString SpawnLight(const FString& ParamsJson);

	/**
	 * Spawn a basic primitive shape.
	 *
	 * @param ParamsJson  JSON: {shape, x, y, z, scale_x, scale_y, scale_z, pitch, yaw, roll, label}
	 * @return JSON: {actor_path, actor_name, shape, position:{x,y,z}, scale:{x,y,z}}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Spawn")
	static FString SpawnPrimitive(const FString& ParamsJson);

	/**
	 * Spawn a NavMeshBoundsVolume.
	 *
	 * @param ParamsJson  JSON: {x, y, z, extent_x, extent_y, extent_z}
	 * @return JSON: {actor_path, position:{x,y,z}, extent:{x,y,z}}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Spawn")
	static FString SpawnNavMesh(const FString& ParamsJson);

	/**
	 * Place an actor from an asset path.
	 *
	 * @param ParamsJson  JSON: {asset_path, x, y, z, pitch, yaw, roll, scale_x, scale_y, scale_z}
	 * @return JSON: {success, actor_name, actor_path, location:{x,y,z}}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Spawn")
	static FString PlaceActor(const FString& ParamsJson);

	/**
	 * Scatter static mesh actors randomly within a bounding box.
	 * All spawning, random transform, and ground snapping in one C++ call.
	 *
	 * @param ParamsJson  JSON: {mesh_path, count, bounds_min:[x,y,z], bounds_max:[x,y,z],
	 *                           snap_to_ground?, random_yaw?, scale_min?, scale_max?,
	 *                           seed?, label_prefix?}
	 * @return JSON: {success, placed, actors:[{name, location:{x,y,z}}]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Spawn")
	static FString ScatterMeshes(const FString& ParamsJson);
};
