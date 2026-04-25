#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborEnvironmentSpawner.generated.h"

UCLASS()
class ARBOR_API UArborEnvironmentSpawner : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Spawn static mesh actors at resolved transforms.
	 * Each actor is labelled "Env_{environment_id}_{node_id}" for tracking.
	 *
	 * @param ParamsJson  JSON: {environment_id, nodes:{node_id:{asset_path, location:{x,y,z},
	 *                    rotation:{pitch,yaw,roll}, scale:{x,y,z}, label?},...}}
	 * @return JSON: {success, spawned:[{node_id, actor_name, actor_path},...], failed:[...]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Environment")
	static FString SpawnEnvironment(const FString& ParamsJson);

	/**
	 * Destroy all actors belonging to an environment (by label prefix "Env_{id}_").
	 *
	 * @param EnvironmentId  The environment ID used during spawning.
	 * @return JSON: {success, destroyed_count}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Environment")
	static FString DespawnEnvironment(const FString& EnvironmentId);
};
