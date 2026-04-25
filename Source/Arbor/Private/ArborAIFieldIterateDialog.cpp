#include "ArborAIFieldIterateDialog.h"
#include "ArborClaude.h"
#include "ArborCodexStyle.h"
#include "ArborCodexWidgetHelpers.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "ArborAIFieldIterateDialog"

void ArborAIFieldIterateDialog::Show(
	const FString& FieldName,
	const FString& CurrentValue,
	const FString& ContextSummary,
	const FString& AssetPath,
	const FString& FieldKey)
{
	TSharedPtr<SMultiLineEditableTextBox> InstructionsBox;

	FString WindowTitle = FString::Printf(TEXT("Iterate on %s"), *FieldName);

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::FromString(WindowTitle))
		.ClientSize(FVector2D(500, 380))
		.SupportsMaximize(true)
		.SupportsMinimize(true)
		.SizingRule(ESizingRule::UserSized);

	TWeakPtr<SWindow> WeakWindow = Window;

	// Capture copies for the lambda
	FString CapturedContext = ContextSummary;
	FString CapturedAssetPath = AssetPath;
	FString CapturedFieldKey = FieldKey;
	FString CapturedFieldName = FieldName;
	FString CapturedCurrentValue = CurrentValue;

	Window->SetContent(
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(ArborCodexStyle::Bg::Panel)
		.Padding(ArborCodexStyle::Spacing::SectionPadding)
		[
			SNew(SVerticalBox)

			// Header
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("Iterate on %s"), *FieldName)))
				.Font(ArborCodexStyle::Font::SectionHeader())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Accent::AI))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CurrentValueLabel", "Current value"))
				.Font(ArborCodexStyle::Font::Body())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Muted))
			]

			// Current value (read-only)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, ArborCodexStyle::Spacing::SectionGap)
			[
				SNew(SBox)
				.MaxDesiredHeight(120.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(ArborCodexStyle::Bg::Input)
					.Padding(4.0f)
					[
						SNew(SMultiLineEditableTextBox)
						.Text(FText::FromString(CurrentValue))
						.Font(ArborCodexStyle::Font::Input())
						.AutoWrapText(true)
						.IsReadOnly(true)
					]
				]
			]

			// Instructions header
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("InstructionsHeader", "What would you like to change?"))
				.Font(ArborCodexStyle::Font::SectionHeader())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
			]

			// Instructions input
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(ArborCodexStyle::Bg::Input)
				.Padding(4.0f)
				[
					SAssignNew(InstructionsBox, SMultiLineEditableTextBox)
					.HintText(LOCTEXT("InstructionsHint", "e.g. Make it darker, add more detail, simplify..."))
					.Font(ArborCodexStyle::Font::Input())
					.AutoWrapText(true)
				]
			]

			// Button bar
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					ArborCodexHelpers::MakeSecondaryButton(
						LOCTEXT("Cancel", "Cancel"),
						FOnClicked::CreateLambda([WeakWindow]()
						{
							if (TSharedPtr<SWindow> Win = WeakWindow.Pin())
							{
								Win->RequestDestroyWindow();
							}
							return FReply::Handled();
						}))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					ArborCodexHelpers::MakeAIButton(
						LOCTEXT("SendToAI", "Send to AI"),
						FOnClicked::CreateLambda([WeakWindow, InstructionsBox,
							CapturedContext, CapturedAssetPath, CapturedFieldKey,
							CapturedFieldName, CapturedCurrentValue]()
						{
							FString Instructions = InstructionsBox->GetText().ToString().TrimStartAndEnd();
							if (Instructions.IsEmpty())
							{
								return FReply::Handled();
							}

							FString Prompt = FString::Printf(
								TEXT("I need to iterate on a single field of a game design document.\n"
									 "Game context: %s\n\n"
									 "Field: %s\n"
									 "Current value:\n%s\n\n"
									 "User's request: %s\n\n"
									 "Update ONLY the \"%s\" property on the asset at %s via ue5_run_python. "
									 "Keep all other fields unchanged."),
								*CapturedContext, *CapturedFieldName, *CapturedCurrentValue,
								*Instructions, *CapturedFieldKey, *CapturedAssetPath);

							FArborClaude::SendMessage(Prompt);

							if (TSharedPtr<SWindow> Win = WeakWindow.Pin())
							{
								Win->RequestDestroyWindow();
							}
							return FReply::Handled();
						}))
				]
			]
		]
	);

	FSlateApplication::Get().AddWindow(Window);
}

#undef LOCTEXT_NAMESPACE
