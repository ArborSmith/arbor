#pragma once

#include "CoreMinimal.h"

class UTexture2D;

/**
 * Dialog for configuring 3D mesh generation from character concept art.
 * Lets the user choose a reference image, toggle animation, pick an API,
 * and provide extra instructions before sending the task.
 */
namespace ArborGenerate3DMeshDialog
{
	struct FMeshGenOptions
	{
		/** Character name for context. */
		FString CharacterName;

		/** Codex asset path for the character. */
		FString AssetPath;

		/** Primary concept art texture (may be null). */
		UTexture2D* PrimaryConceptArt = nullptr;

		/** Gallery textures available as reference. */
		TArray<UTexture2D*> GalleryTextures;

		/** Gallery asset paths (parallel to GalleryTextures). */
		TArray<FString> GalleryAssetPaths;

		/** Primary concept art disk path (for API calls). */
		FString PrimaryConceptArtPath;
	};

	/**
	 * Opens the 3D mesh generation options window.
	 */
	void Show(const FMeshGenOptions& Options);
}
