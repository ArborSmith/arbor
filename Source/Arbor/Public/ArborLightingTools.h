#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborLightingTools.generated.h"

UCLASS()
class ARBOR_API UArborLightingTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Set up a complete outdoor lighting rig.
	 * Removes existing lighting actors first.
	 *
	 * @param ParamsJson  JSON: {sun_rotation:[pitch,yaw,roll], fog_density}
	 * @return JSON: {success, sun, sky_atmosphere, sky_light, fog, clouds, post_process}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Lighting")
	static FString SetupOutdoorScene(const FString& ParamsJson);

	/**
	 * Set up a basic indoor lighting rig.
	 * Removes existing SkyLight and PostProcessVolume first.
	 *
	 * @param ParamsJson  JSON: {ambient_intensity}
	 * @return JSON: {success, sky_light, post_process, rect_light}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Lighting")
	static FString SetupIndoorScene(const FString& ParamsJson);

	/**
	 * Spawn a PostProcessVolume.
	 *
	 * @param ParamsJson  JSON: {location, extent, infinite_extent, bloom_intensity, auto_exposure_min, auto_exposure_max, label}
	 * @return JSON: {success, actor_path}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Lighting")
	static FString AddPostProcessVolume(const FString& ParamsJson);

private:
	static void RemoveExistingByClass(UClass* ActorClass);
	static AActor* SpawnActor(UClass* ActorClass, const FVector& Location,
		const FRotator& Rotation, const FString& Label);
};
