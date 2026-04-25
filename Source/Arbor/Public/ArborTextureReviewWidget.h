#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SScrollBox;
class SMultiLineEditableTextBox;
class STextBlock;
class SButton;
class SImage;
class SBorder;
class SBox;

/**
 * Editor widget that displays AI-generated texture/image variants for user review.
 * The user can select an image, request regeneration with comments, or cancel.
 * Results are written to Saved/Arbor/texture_review_result.json for the MCP bridge to read.
 */
class SArborTextureReviewWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArborTextureReviewWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Load images from a JSON manifest and display them in the review grid.
	 * Called from the Arbor.TextureReview console command.
	 *
	 * JSON format:
	 * {
	 *   "images": [{ "path": "...", "label": "Variant 1", "pbr": { "albedo": "...", "normal": "...", ... } }],
	 *   "prompt": "...",
	 *   "source": "scenario" | "fal",
	 *   "codex_asset_path": "/Game/GameCodex/MyGame/GC_MyGame" (optional — enables auto-import on selection)
	 * }
	 */
	void LoadImages(const FString& JsonManifest);

private:
	// Image data
	struct FImageEntry
	{
		FString Path;
		FString Label;
		UTexture2D* Texture = nullptr;
		TSharedPtr<FSlateBrush> Brush;

		// PBR map paths (may be empty for non-texture sources)
		TMap<FString, FString> PBRPaths;
	};

	TArray<FImageEntry> ImageEntries;
	int32 SelectedIndex = -1;
	FString OriginalPrompt;
	FString Source;
	FString CodexAssetPath;

	// UI elements
	TSharedPtr<STextBlock> PromptLabel;
	TSharedPtr<STextBlock> SourceLabel;
	TSharedPtr<STextBlock> StatusLabel;
	TSharedPtr<SBox> ImageGridContainer;
	TSharedPtr<SButton> UseSelectedButton;
	TSharedPtr<SButton> RegenerateButton;

	// Per-image border widgets for selection highlighting
	TArray<TSharedPtr<SBorder>> ImageBorders;

	// PBR preview section
	TSharedPtr<SBox> PBRPreviewContainer;

	// Actions
	void OnImageClicked(int32 Index);
	void OnUseSelected();
	void OnRegenerate();
	void OnCancel();

	// Helpers
	void RebuildImageGrid();
	void UpdateSelectionHighlight();
	void UpdatePBRPreview();
	void WriteResult(const FString& Action);
	void ClearImages();

	void OpenFullSizeViewer(UTexture2D* Texture);

	FSlateBrush FullSizeViewerBrush;
	TArray<TObjectPtr<UTexture2D>> PinnedTextures;

	static UTexture2D* LoadImageFromDisk(const FString& FilePath);
	static TSharedPtr<FSlateBrush> CreateBrushFromTexture(UTexture2D* Texture, int32 Size);
};
