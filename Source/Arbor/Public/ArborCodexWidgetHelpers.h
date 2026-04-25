#pragma once

#include "CoreMinimal.h"
#include "ArborCodexStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SNullWidget.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

class SArborTagInput;

/**
 * Shared UI builders for GameCodex widgets.
 * Eliminates duplicated AddField lambdas, button styling, and CSV helpers.
 */
namespace ArborCodexHelpers
{
	// ═══════════════════════════════════════════════════
	// CSV UTILITIES
	// ═══════════════════════════════════════════════════

	inline TArray<FString> SplitCSV(const FString& Input)
	{
		TArray<FString> Result;
		Input.ParseIntoArray(Result, TEXT(","), true);
		for (FString& S : Result) { S.TrimStartAndEndInline(); }
		Result.RemoveAll([](const FString& S) { return S.IsEmpty(); });
		return Result;
	}

	inline FString JoinCSV(const TArray<FString>& Items)
	{
		return FString::Join(Items, TEXT(", "));
	}

	// ═══════════════════════════════════════════════════
	// LOCK TOGGLE (shared by all field row helpers)
	// ═══════════════════════════════════════════════════

	/** Creates a lock/unlock icon button. */
	inline TSharedRef<SWidget> MakeLockToggle(
		TFunction<bool()> IsLockedFn,
		TFunction<FReply()> OnToggleLock)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.OnClicked_Lambda([OnToggleLock]() { return OnToggleLock(); })
			[
				SNew(SImage)
				.Image_Lambda([IsLockedFn]() -> const FSlateBrush*
				{
					return FAppStyle::GetBrush(
						IsLockedFn() ? "PropertyWindow.Locked" : "PropertyWindow.Unlocked");
				})
				.ColorAndOpacity_Lambda([IsLockedFn]() -> FSlateColor
				{
					return IsLockedFn()
						? FSlateColor(ArborCodexStyle::State::Locked)
						: FSlateColor(ArborCodexStyle::State::Unlocked);
				})
				.DesiredSizeOverride(FVector2D(14.0f, 14.0f))
			];
	}

	// ═══════════════════════════════════════════════════
	// AI ITERATE BUTTON
	// ═══════════════════════════════════════════════════

	/** Creates a small "AI" button for per-field iteration. */
	inline TSharedRef<SWidget> MakeAIIterateButton(TFunction<FReply()> OnClicked)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.ToolTipText(NSLOCTEXT("ArborCodex", "AIIterateTooltip", "Iterate on this field with AI"))
			.OnClicked_Lambda([OnClicked]() { return OnClicked(); })
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("ArborCodex", "AIIterateIcon", "AI"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Accent::AI))
			];
	}

	// ═══════════════════════════════════════════════════
	// FIELD ROWS
	// ═══════════════════════════════════════════════════

	/**
	 * Creates a styled label + lock toggle + multiline text input field.
	 * Input is read-only when locked.
	 * Optional OnAIIterate callback adds an AI iterate button next to the lock.
	 */
	inline TSharedRef<SWidget> MakeFieldRow(
		const FText& Label,
		TSharedPtr<SMultiLineEditableTextBox>& OutInput,
		const FString& Value,
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

			// Input
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(OutInput, SMultiLineEditableTextBox)
				.Text(FText::FromString(Value))
				.HintText(Hint)
				.Font(ArborCodexStyle::Font::Input())
				.AutoWrapText(true)
				.IsReadOnly_Lambda([IsLockedFn]() { return IsLockedFn(); })
			];
	}

	/**
	 * Creates a styled label + lock toggle + multiline text input.
	 * Input is read-only when locked.
	 * Optional OnAIIterate callback adds an AI iterate button next to the lock.
	 */
	inline TSharedRef<SWidget> MakeMultilineFieldRow(
		const FText& Label,
		TSharedPtr<SMultiLineEditableTextBox>& OutInput,
		const FString& Value,
		const FText& Hint,
		TFunction<bool()> IsLockedFn,
		TFunction<FReply()> OnToggleLock,
		TFunction<FReply()> OnAIIterate = nullptr,
		float MinHeight = 100.0f,
		float MaxHeight = 240.0f)
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

			// Multiline input
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.MinDesiredHeight(MinHeight)
				.MaxDesiredHeight(MaxHeight)
				[
					SAssignNew(OutInput, SMultiLineEditableTextBox)
					.Text(FText::FromString(Value))
					.HintText(Hint)
					.Font(ArborCodexStyle::Font::Input())
					.AutoWrapText(true)
					.IsReadOnly_Lambda([IsLockedFn]() { return IsLockedFn(); })
				]
			];
	}

	// ═══════════════════════════════════════════════════
	// BUTTONS
	// ═══════════════════════════════════════════════════

	/** Primary action button (accent blue background, white text). */
	inline TSharedRef<SWidget> MakePrimaryButton(const FText& Label, FOnClicked OnClicked)
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(ArborCodexStyle::Accent::Primary)
			.Padding(ArborCodexStyle::Spacing::ButtonPadding)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.OnClicked(OnClicked)
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(ArborCodexStyle::Font::ButtonText())
					.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::OnAccent))
				]
			];
	}

	/** Secondary action button (elevated background, normal text). */
	inline TSharedRef<SWidget> MakeSecondaryButton(const FText& Label, FOnClicked OnClicked)
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(ArborCodexStyle::Bg::Elevated)
			.Padding(ArborCodexStyle::Spacing::ButtonPadding)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.OnClicked(OnClicked)
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(ArborCodexStyle::Font::ButtonText())
					.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
				]
			];
	}

	/** AI-powered action button (purple tint). */
	inline TSharedRef<SWidget> MakeAIButton(const FText& Label, FOnClicked OnClicked)
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(ArborCodexStyle::Accent::AIDim)
			.Padding(ArborCodexStyle::Spacing::ButtonPadding)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.OnClicked(OnClicked)
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(ArborCodexStyle::Font::ButtonText())
					.ColorAndOpacity(FSlateColor(ArborCodexStyle::Accent::AI))
				]
			];
	}

	/** Danger/destructive action button (red background, white text). */
	inline TSharedRef<SWidget> MakeDangerButton(const FText& Label, FOnClicked OnClicked)
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(ArborCodexStyle::State::Danger)
			.Padding(ArborCodexStyle::Spacing::ButtonPadding)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.OnClicked(OnClicked)
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(ArborCodexStyle::Font::ButtonText())
					.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::OnAccent))
				]
			];
	}
}
