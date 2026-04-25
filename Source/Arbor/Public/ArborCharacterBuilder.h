#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborCharacterBuilder.generated.h"

UCLASS()
class ARBOR_API UArborCharacterBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create or update a character data asset from JSON.
	 *
	 * @param ParamsJson  JSON: {name, content_path?, character_id?, archetype?, personality_traits?, backstory?, personality_profile?, dialogue_lines?}
	 * @return JSON: {success, asset_path, character_id}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Characters")
	static FString CreateCharacterAsset(const FString& ParamsJson);

	/**
	 * Query a character asset and return all fields as JSON.
	 *
	 * @param AssetPath  Content path to the character data asset
	 * @return JSON: {success, character_id, name, archetype, ...}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Characters")
	static FString QueryCharacterAsset(const FString& AssetPath);

	/**
	 * Update a single section of a character asset.
	 *
	 * @param ParamsJson  JSON: {asset_path, section, data}
	 * @return JSON: {success, asset_path}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Characters")
	static FString UpdateCharacterSection(const FString& ParamsJson);

	/**
	 * List character data assets under a folder.
	 *
	 * @param FolderPath  Content folder to scan (default: /Game/Characters)
	 * @return JSON: {success, characters: [{asset_path, character_id, name, archetype}]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Characters")
	static FString ListCharacterAssets(const FString& FolderPath);

	/**
	 * Re-import a character from an .arbor.json sidecar file.
	 *
	 * @param SidecarPath  Disk path to the .arbor.json file
	 * @return JSON: {success, asset_path}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Characters")
	static FString ImportFromSidecar(const FString& SidecarPath);
};
