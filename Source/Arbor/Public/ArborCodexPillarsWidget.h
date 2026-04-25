#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

struct FArborCodexContext;
class SArborTagInput;
class SEditableTextBox;
class SMultiLineEditableTextBox;
class SScrollBox;
class SVerticalBox;
class STextComboBox;

class SArborCodexPillarsWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArborCodexPillarsWidget) {}
		SLATE_ARGUMENT(TSharedPtr<FArborCodexContext>, CodexContext)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedPtr<FArborCodexContext> CodexContext;
	FDelegateHandle ContextChangedHandle;

	TArray<FString> PillarAssetPaths;
	TArray<FString> PillarDisplayNames;
	int32 SelectedPillarIndex = -1;

	TSharedPtr<SMultiLineEditableTextBox> PillarNameInput;
	TSharedPtr<SMultiLineEditableTextBox> PillarDescInput;
	TSharedPtr<SArborTagInput> PillarTagsInput;

	/** PillarType dropdown */
	TSharedPtr<STextComboBox> PillarTypeCombo;
	TArray<TSharedPtr<FString>> PillarTypeOptions;

	TSharedPtr<SVerticalBox> PillarListBox;
	TSharedPtr<SScrollBox> PillarDetailPanel;

	/** Cached thumbnail brushes for list items, keyed by list index. */
	TMap<int32, FSlateBrush> ThumbnailBrushes;

	TSet<FString> LockedFields;
	bool IsFieldLocked(const FString& FieldKey) const;
	void ToggleFieldLock(const FString& FieldKey);
	void SaveLockedFieldsToAsset();

	void OnContextChanged();
	void ScanPillars();
	void RebuildPillarList();
	void ShowPillarDetail(int32 Index);

	FReply OnSavePillarClicked();
	FReply OnNewPillarClicked();
	FReply OnImprovePillarClicked();
	FReply OnHelpImplementClicked();
	FReply OnDeletePillarClicked();
	FReply OnAIIterateField(const FString& FieldKey, const FString& DisplayName);

	FString BuildContextSummary() const;
};
