#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

struct FArborCodexContext;
class SEditableTextBox;
class SMultiLineEditableTextBox;
class SScrollBox;
class SVerticalBox;
class SArborTagInput;

class SArborCodexLocationsWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArborCodexLocationsWidget) {}
		SLATE_ARGUMENT(TSharedPtr<FArborCodexContext>, CodexContext)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedPtr<FArborCodexContext> CodexContext;
	FDelegateHandle ContextChangedHandle;

	// Location state
	TArray<FString> LocationAssetPaths;
	TArray<FString> LocationDisplayNames;
	int32 SelectedLocationIndex = -1;

	// Edit form widgets
	TSharedPtr<SMultiLineEditableTextBox> LocNameInput;
	TSharedPtr<SMultiLineEditableTextBox> LocRegionInput;
	TSharedPtr<SMultiLineEditableTextBox> LocAtmosphereInput;
	TSharedPtr<SMultiLineEditableTextBox> LocDescInput;
	TSharedPtr<SArborTagInput> LocTagsInput;

	// Lists
	TSharedPtr<SVerticalBox> LocationListBox;
	TSharedPtr<SScrollBox> LocationDetailPanel;

	// Field lock state (loaded from asset)
	TSet<FString> LockedFields;
	bool IsFieldLocked(const FString& FieldKey) const;
	void ToggleFieldLock(const FString& FieldKey);
	void SaveLockedFieldsToAsset();

	void OnContextChanged();
	void ScanLocations();
	void RebuildLocationList();
	void ShowLocationDetail(int32 Index);

	FReply OnSaveLocationClicked();
	FReply OnNewLocationClicked();
	FReply OnImproveLocationClicked();
	FReply OnDeleteLocationClicked();
	FReply OnAIIterateField(const FString& FieldKey, const FString& DisplayName);

	FString BuildContextSummary() const;
};
