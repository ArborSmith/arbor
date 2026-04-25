#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

struct FArborCodexContext;
class SEditableTextBox;
class SMultiLineEditableTextBox;
class SScrollBox;
class SArborTagInput;
class SArborCodexImagePanel;

class SArborCodexOverviewWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArborCodexOverviewWidget) {}
		SLATE_ARGUMENT(TSharedPtr<FArborCodexContext>, CodexContext)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedPtr<FArborCodexContext> CodexContext;
	FDelegateHandle ContextChangedHandle;

	// Edit form widgets
	TSharedPtr<SMultiLineEditableTextBox> CtxTitleInput;
	TSharedPtr<SMultiLineEditableTextBox> CtxGenreInput;
	TSharedPtr<SMultiLineEditableTextBox> CtxSettingInput;
	TSharedPtr<SMultiLineEditableTextBox> CtxToneInput;
	TSharedPtr<SMultiLineEditableTextBox> CtxWorldInput;
	TSharedPtr<SArborTagInput> CtxTagsInput;

	TSharedPtr<SScrollBox> DetailPanel;
	TSharedPtr<SArborCodexImagePanel> StyleImagePanel;

	// Field lock state (loaded from asset)
	TSet<FString> LockedFields;
	bool IsFieldLocked(const FString& FieldKey) const;
	void ToggleFieldLock(const FString& FieldKey);
	void SaveLockedFieldsToAsset();

	void OnContextChanged();
	void ShowContextDetail();

	FReply OnSaveContextClicked();
	FReply OnImproveContextClicked();
	FReply OnAIIterateField(const FString& FieldKey, const FString& DisplayName);

	FString BuildContextSummary() const;
};
