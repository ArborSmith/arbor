#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SScrollBox;
class SMultiLineEditableTextBox;
class STextBlock;
class SButton;
class SBorder;
class SBox;

/**
 * Editor widget that displays AI-generated text variations for user review.
 * Shows 3 variations side-by-side as cards. The user selects one, requests
 * regeneration with comments, or cancels.
 * Results are written to Saved/Arbor/text_variation_result.json for the MCP bridge to read.
 */
class SArborTextVariationWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArborTextVariationWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Load variations from a JSON manifest and display them as cards.
	 * Called from the Arbor.TextVariation console command.
	 *
	 * JSON format:
	 * {
	 *   "variations": [
	 *     { "label": "Variation A", "fields": { "FieldName": "value", ... } },
	 *     ...
	 *   ],
	 *   "category": "context",
	 *   "asset_path": "/Game/GameCodex/...",
	 *   "prompt": "...",
	 *   "locked_fields": ["FieldName"],
	 *   "field_order": ["Field1", "Field2", ...]
	 * }
	 */
	void LoadVariations(const FString& JsonManifest);

private:
	// Variation data
	struct FTextVariationEntry
	{
		FString Label;
		TMap<FString, FString> Fields;
	};

	TArray<FTextVariationEntry> VariationEntries;
	int32 SelectedIndex = -1;
	FString OriginalPrompt;
	FString Category;
	FString AssetPath;
	TArray<FString> LockedFieldNames;
	TArray<FString> FieldOrder;

	// UI elements
	TSharedPtr<STextBlock> HeaderLabel;
	TSharedPtr<STextBlock> CategoryLabel;
	TSharedPtr<STextBlock> StatusLabel;
	TSharedPtr<SBox> CardsContainer;
	TSharedPtr<SMultiLineEditableTextBox> CommentsBox;
	TSharedPtr<SButton> UseSelectedButton;
	TSharedPtr<SButton> RegenerateButton;

	// Per-card border widgets for selection highlighting
	TArray<TSharedPtr<SBorder>> CardBorders;

	// Actions
	void OnCardClicked(int32 Index);
	void OnUseSelected();
	void OnRegenerate();
	void OnCancel();

	// Helpers
	void RebuildCards();
	void UpdateSelectionHighlight();
	void WriteResult(const FString& Action);
	void ClearVariations();
};
