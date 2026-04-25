#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UTexture2D;
class SImage;
class SHorizontalBox;

DECLARE_DELEGATE(FOnImageChanged);

/**
 * Reusable Slate widget for displaying concept art on any codex entry.
 * Shows primary image, gallery thumbnails, prompt text, Generate and Import buttons.
 * Images are clickable (full-size viewer) and removable (X overlay).
 */
class SArborCodexImagePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArborCodexImagePanel)
		: _ConceptArt(nullptr)
		, _Prompt()
		, _AssetPath()
		, _ImageCategory()
		, _GenerateButtonLabel()
	{}
		SLATE_ARGUMENT(UTexture2D*, ConceptArt)
		SLATE_ARGUMENT(TArray<UTexture2D*>, Gallery)
		SLATE_ARGUMENT(FString, Prompt)
		SLATE_ARGUMENT(FString, AssetPath)
		SLATE_ARGUMENT(FString, ImageCategory)
		SLATE_ARGUMENT(FString, GenerateButtonLabel)
		SLATE_EVENT(FOnImageChanged, OnImageChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Update the displayed image and prompt without reconstructing the widget. */
	void Refresh(UTexture2D* InConceptArt, const TArray<UTexture2D*>& InGallery, const FString& InPrompt);

private:
	FReply OnGenerateClicked();
	FReply OnImportClicked();
	FReply OnRemovePrimary();
	FReply OnRemoveGalleryImage(int32 Index);
	void OpenFullSizeViewer(UTexture2D* Texture);
	void RefreshFromAsset();

	TSharedRef<SWidget> MakeGalleryThumbnail(int32 Index);

	void SetupBrush(UTexture2D* Texture);
	void SetupGalleryBrush(int32 Index, UTexture2D* Texture);

	/** The codex entry's asset path — passed to Claude for context. */
	FString AssetPath;

	/** Image category (empty = concept_art, "style_images" for style references). */
	FString ImageCategory;

	/** Custom label for the generate button. */
	FString GenerateButtonLabel;

	/** Primary image brush. */
	FSlateBrush PrimaryBrush;
	TSharedPtr<SImage> PrimaryImageWidget;

	/** Gallery. */
	TArray<FSlateBrush> GalleryBrushes;
	TSharedPtr<SHorizontalBox> GalleryBox;
	TArray<FString> GalleryTexturePaths;

	/** Full-size viewer brush (one at a time). */
	FSlateBrush FullSizeViewerBrush;

	/** Pinned textures (prevent GC). */
	TArray<TObjectPtr<UTexture2D>> PinnedTextures;

	FOnImageChanged OnImageChangedDelegate;
};
