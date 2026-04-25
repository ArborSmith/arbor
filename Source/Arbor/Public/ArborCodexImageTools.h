#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborCodexImageTools.generated.h"

/**
 * Concept art management for Game Codex entries.
 * Called by the ue5_codex MCP tool via Remote Control API.
 */
UCLASS()
class ARBOR_API UArborCodexImageTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Set primary concept art texture and prompt on a codex entry.
	 *
	 * @param AssetPath   UE5 asset path of the codex entry.
	 * @param TexturePath UE5 asset path of the UTexture2D to set as primary concept art.
	 * @param Prompt      The image generation prompt used (optional).
	 * @return JSON: {success, asset_path} or {success:false, error}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Codex")
	static FString SetConceptArt(const FString& AssetPath, const FString& TexturePath, const FString& Prompt);

	/**
	 * Append a texture to a codex entry's concept art gallery.
	 *
	 * @param AssetPath   UE5 asset path of the codex entry.
	 * @param TexturePath UE5 asset path of the UTexture2D to add.
	 * @return JSON: {success, asset_path, gallery_count} or {success:false, error}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Codex")
	static FString AddGalleryImage(const FString& AssetPath, const FString& TexturePath);

	/**
	 * Remove a texture from a codex entry's concept art gallery.
	 *
	 * @param AssetPath   UE5 asset path of the codex entry.
	 * @param TexturePath UE5 asset path of the UTexture2D to remove.
	 * @return JSON: {success, asset_path, gallery_count} or {success:false, error}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Codex")
	static FString RemoveGalleryImage(const FString& AssetPath, const FString& TexturePath);

	/**
	 * Get concept art references for a codex entry.
	 *
	 * @param AssetPath UE5 asset path of the codex entry.
	 * @return JSON: {concept_art, gallery: [...], prompt} or {success:false, error}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Codex")
	static FString GetCodexImages(const FString& AssetPath);

};
