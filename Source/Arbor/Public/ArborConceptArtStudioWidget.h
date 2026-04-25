#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SWidgetSwitcher;
class SScrollBox;
class SMultiLineEditableTextBox;
class STextBlock;
class SButton;
class SImage;
class SBorder;
class SBox;
template<typename T> class SSpinBox;

/**
 * Unified concept art generation pipeline in a single persistent editor tab.
 *
 * Steps:
 *   Context → Prompt Review → Generating → Results → Done
 *
 * Communication with the MCP bridge happens via:
 *   Saved/Arbor/concept_art_studio_state.json
 */
class SArborConceptArtStudioWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArborConceptArtStudioWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Load/update from a JSON state payload.
	 * Called from the Arbor.ConceptArtStudio console command.
	 */
	void UpdateFromState(const FString& JsonManifest);

	/** Open for a specific codex asset (shows context, user configures before generating). */
	void OpenForAsset(const FString& InAssetPath);

private:
	// Step enum
	enum class EStep : uint8
	{
		Main,       // Context + Prompt Review combined
		Generating,
		Results,
		Done,
	};

	EStep CurrentStep = EStep::Main;

	// --- Data ---
	FString CodexAssetPath;
	FString CodexName;
	FString CodexDescription;
	TArray<FString> StyleImagePaths;
	int32 NumImages = 4;
	FString Prompt;
	FString UserFeedback;
	FString PendingAction;
	int32 SelectedIndex = -1;

	// Image data for the results grid
	struct FImageEntry
	{
		FString Path;
		FString Label;
		UTexture2D* Texture = nullptr;
		TSharedPtr<FSlateBrush> Brush;
	};
	TArray<FImageEntry> ImageEntries;
	TArray<TObjectPtr<UTexture2D>> PinnedTextures;

	// Style image brushes
	struct FStyleImageEntry
	{
		FString Path;
		UTexture2D* Texture = nullptr;
		TSharedPtr<FSlateBrush> Brush;
		bool bSelected = true;
	};
	TArray<FStyleImageEntry> StyleImageEntries;

	// --- UI ---
	TSharedPtr<SWidgetSwitcher> StepSwitcher;

	// Context step
	TSharedPtr<STextBlock> ContextNameLabel;
	TSharedPtr<STextBlock> ContextDescLabel;
	TSharedPtr<STextBlock> ContextStatusLabel;
	TSharedPtr<SBox> StyleImageGrid;
	TSharedPtr<SSpinBox<int32> > NumImagesSpinBox;

	// Prompt review step
	TSharedPtr<SMultiLineEditableTextBox> PromptEditor;

	// Generating step
	TSharedPtr<STextBlock> GeneratingStatusLabel;

	// Results step
	TSharedPtr<SBox> ResultsGridContainer;
	TSharedPtr<STextBlock> ResultsStatusLabel;
	TSharedPtr<SMultiLineEditableTextBox> FeedbackEditor;
	TArray<TSharedPtr<SBorder>> ImageBorders;

	// Done step
	TSharedPtr<STextBlock> DoneStatusLabel;
	TSharedPtr<SBox> DoneImageContainer;

	// --- Step management ---
	void SetStep(EStep Step);

	// --- User actions ---
	FReply OnGeneratePrompt();
	FReply OnApprovePrompt();
	FReply OnRegenerate();
	void OnImageClicked(int32 Index);
	FReply OnUseSelected();
	FReply OnCancel();
	FReply OnAddStyleImageFromDisk();
	FReply OnGenerateMore();

	// --- UI helpers ---
	void RebuildStyleImageGrid();
	void RebuildResultsGrid();
	void UpdateSelectionHighlight();
	void SetPromptSectionVisible(bool bVisible);
	void WriteState();
	FString GetStatePath() const;

	// Build each step's content
	TSharedRef<SWidget> BuildMainPanel();
	TSharedRef<SWidget> BuildGeneratingPanel();
	TSharedRef<SWidget> BuildResultsPanel();
	TSharedRef<SWidget> BuildDonePanel();

	// Generate Prompt button section (shown initially, hidden after prompt arrives)
	TSharedPtr<SVerticalBox> GeneratePromptSection;
	// Prompt section (shown/hidden within Main panel)
	TSharedPtr<SVerticalBox> PromptSection;

	void OpenFullSizeViewer(UTexture2D* Texture);
	FSlateBrush FullSizeViewerBrush;

	// --- Image loading (same pattern as SArborTextureReviewWidget) ---
	static UTexture2D* LoadImageFromDisk(const FString& FilePath);
	static TSharedPtr<FSlateBrush> CreateBrushFromTexture(UTexture2D* Texture, int32 Size);
};
