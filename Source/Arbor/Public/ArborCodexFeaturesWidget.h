#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"

struct FArborCodexContext;
class SArborTagInput;
class SEditableTextBox;
class SMultiLineEditableTextBox;
class SScrollBox;
class SVerticalBox;

class SArborCodexFeaturesWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArborCodexFeaturesWidget) {}
		SLATE_ARGUMENT(TSharedPtr<FArborCodexContext>, CodexContext)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedPtr<FArborCodexContext> CodexContext;
	FDelegateHandle ContextChangedHandle;

	TArray<FString> FeatureAssetPaths;
	TArray<FString> FeatureDisplayNames;
	TArray<FString> FeatureCategories;
	int32 SelectedFeatureIndex = -1;

	FString FilterCategory;
	TArray<TSharedPtr<FString>> CategoryOptions;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> CategoryComboBox;

	TSharedPtr<SMultiLineEditableTextBox> FeatureNameInput;
	TSharedPtr<SMultiLineEditableTextBox> FeatureCategoryInput;
	TSharedPtr<SMultiLineEditableTextBox> FeatureDescInput;
	TSharedPtr<SArborTagInput> FeatureTagsInput;

	TSharedPtr<SVerticalBox> FeatureListBox;
	TSharedPtr<SScrollBox> FeatureDetailPanel;

	/** Cached thumbnail brushes for list items, keyed by list index. */
	TMap<int32, FSlateBrush> ThumbnailBrushes;

	TSet<FString> LockedFields;
	bool IsFieldLocked(const FString& FieldKey) const;
	void ToggleFieldLock(const FString& FieldKey);
	void SaveLockedFieldsToAsset();

	void OnContextChanged();
	void ScanFeatures();
	void RebuildCategoryOptions();
	void RebuildFeatureList();
	void ShowFeatureDetail(int32 Index);

	FReply OnSaveFeatureClicked();
	FReply OnNewFeatureClicked();
	FReply OnImproveFeatureClicked();
	FReply OnHelpImplementClicked();
	FReply OnDeleteFeatureClicked();
	FReply OnAIIterateField(const FString& FieldKey, const FString& DisplayName);

	FString BuildContextSummary() const;
};
