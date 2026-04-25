#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "ArborCodexWidgetHelpers.h"

class SEditableTextBox;
class SWrapBox;
class SHorizontalBox;

DECLARE_DELEGATE_OneParam(FOnTagsChanged, const TArray<FString>&);

/**
 * Reusable tag/pill input widget.
 * Displays current values as removable pills in a wrap box,
 * with a text input + Add button below for adding new tags.
 */
class SArborTagInput : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArborTagInput)
		: _HintText(NSLOCTEXT("ArborTagInput", "DefaultHint", "Add tag..."))
		, _IsLocked(false)
	{}
		SLATE_ARGUMENT(TArray<FString>, InitialTags)
		SLATE_ARGUMENT(FText, HintText)
		SLATE_ATTRIBUTE(bool, IsLocked)
		SLATE_EVENT(FOnTagsChanged, OnTagsChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Returns the current list of tags. */
	TArray<FString> GetTags() const { return Tags; }

	/** Replaces all tags programmatically. */
	void SetTags(const TArray<FString>& InTags);

private:
	TArray<FString> Tags;
	TAttribute<bool> IsLocked;
	FOnTagsChanged OnTagsChanged;

	TSharedPtr<SWrapBox> PillContainer;
	TSharedPtr<SEditableTextBox> InputBox;
	TSharedPtr<SHorizontalBox> InputRow;

	void AddTag(const FString& InTag);
	void RemoveTag(const FString& InTag);
	void RebuildPills();
	TSharedRef<SWidget> CreatePill(const FString& InTag);
};

// ═══════════════════════════════════════════════════
// TAG FIELD ROW HELPER
// ═══════════════════════════════════════════════════

namespace ArborCodexHelpers
{
	/**
	 * Creates a styled label + lock toggle + tag/pill input.
	 * Same pattern as MakeFieldRow but with SArborTagInput instead of SEditableTextBox.
	 */
	inline TSharedRef<SWidget> MakeTagFieldRow(
		const FText& Label,
		TSharedPtr<SArborTagInput>& OutTagInput,
		const TArray<FString>& Values,
		const FText& Hint,
		TFunction<bool()> IsLockedFn,
		TFunction<FReply()> OnToggleLock,
		TFunction<FReply()> OnAIIterate = nullptr)
	{
		return SNew(SVerticalBox)

			// Label + AI button + lock toggle
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 2.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(ArborCodexStyle::Font::FieldLabel())
					.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Secondary))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 2.0f, 0.0f)
				[
					OnAIIterate
						? MakeAIIterateButton(OnAIIterate)
						: SNullWidget::NullWidget
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					MakeLockToggle(IsLockedFn, OnToggleLock)
				]
			]

			// Tag input
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(OutTagInput, SArborTagInput)
				.InitialTags(Values)
				.HintText(Hint)
				.IsLocked_Lambda([IsLockedFn]() { return IsLockedFn(); })
			];
	}
}
