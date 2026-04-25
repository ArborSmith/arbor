#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborMaterialTools.generated.h"

UCLASS()
class ARBOR_API UArborMaterialTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create a simple material with constant base color, metallic, and roughness.
	 *
	 * @param ParamsJson  JSON: {name, content_path, color:[r,g,b], metallic, roughness}
	 * @return JSON: {success, asset_path}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Materials")
	static FString CreateMaterial(const FString& ParamsJson);

	/**
	 * Create a material with texture samplers wired to material outputs.
	 *
	 * @param ParamsJson  JSON: {name, content_path, base_color_path?, normal_path?, roughness_path?, metallic_path?}
	 * @return JSON: {success, asset_path}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Materials")
	static FString CreateMaterialFromTextures(const FString& ParamsJson);

	/**
	 * Create a PBR base material with TextureSampleParameter2D nodes and tiling.
	 *
	 * @param ParamsJson  JSON: {name, content_path, default_tiling}
	 * @return JSON: {success, asset_path}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Materials")
	static FString CreateParameterizedPBRMaterial(const FString& ParamsJson);

	/**
	 * Create a MaterialInstanceConstant from a parent material.
	 *
	 * @param ParamsJson  JSON: {parent_path, name, content_path, params:{scalar:float, vector:[r,g,b,a], texture:"path"}}
	 * @return JSON: {success, asset_path}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Materials")
	static FString CreateMaterialInstance(const FString& ParamsJson);

	/**
	 * Create a world-aligned (tri-planar) PBR material.
	 *
	 * @param ParamsJson  JSON: {name, content_path, base_color_path?, normal_path?, roughness_path?, metallic_path?, ao_path?, tiling_scale}
	 * @return JSON: {success, asset_path}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Materials")
	static FString CreateWorldAlignedMaterial(const FString& ParamsJson);

	/**
	 * Assign a material to actor(s).
	 *
	 * @param ParamsJson  JSON: {actor_names: string|[string], material_path, slot}
	 * @return JSON: {success, assigned, failed, details:[...]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Materials")
	static FString AssignMaterial(const FString& ParamsJson);

	/**
	 * Ensure the shared PBR base material exists.
	 *
	 * @param ContentPath  Content folder (e.g. "/Game/Materials").
	 * @return JSON: {success, asset_path}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Materials")
	static FString EnsurePBRBaseMaterial(const FString& ContentPath);
};
