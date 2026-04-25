#include "ArborAIPromptDialog.h"
#include "ArborClaude.h"
#include "ArborCodexStyle.h"
#include "ArborCodexWidgetHelpers.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "ArborAIPromptDialog"

void ArborAIPromptDialog::Show(const FString& GeneratedPrompt, const FString& WindowTitle, bool bUsePlanMode)
{
	TSharedPtr<SMultiLineEditableTextBox> PromptBox;
	TSharedPtr<SMultiLineEditableTextBox> InstructionsBox;

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::FromString(WindowTitle))
		.ClientSize(FVector2D(700, 550))
		.SupportsMaximize(true)
		.SupportsMinimize(true)
		.SizingRule(ESizingRule::UserSized);

	TWeakPtr<SWindow> WeakWindow = Window;
	bool bPlanMode = bUsePlanMode;

	Window->SetContent(
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(ArborCodexStyle::Bg::Panel)
		.Padding(ArborCodexStyle::Spacing::SectionPadding)
		[
			SNew(SVerticalBox)

			// "AI Prompt" header
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PromptHeader", "AI Prompt"))
				.Font(ArborCodexStyle::Font::SectionHeader())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PromptSubtext", "Review and edit the prompt before sending"))
				.Font(ArborCodexStyle::Font::Body())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Muted))
			]

			// Prompt text box
			+ SVerticalBox::Slot()
			.FillHeight(0.6f)
			.Padding(0.0f, 0.0f, 0.0f, ArborCodexStyle::Spacing::SectionGap)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(ArborCodexStyle::Bg::Input)
				.Padding(4.0f)
				[
					SAssignNew(PromptBox, SMultiLineEditableTextBox)
					.Text(FText::FromString(GeneratedPrompt))
					.Font(ArborCodexStyle::Font::Input())
					.AutoWrapText(true)
				]
			]

			// "Additional Instructions" header
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("InstructionsHeader", "Additional Instructions"))
				.Font(ArborCodexStyle::Font::SectionHeader())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Secondary))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("InstructionsSubtext", "Optional — add any specific guidance for the AI"))
				.Font(ArborCodexStyle::Font::Body())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Muted))
			]

			// Instructions text box
			+ SVerticalBox::Slot()
			.FillHeight(0.3f)
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(ArborCodexStyle::Bg::Input)
				.Padding(4.0f)
				[
					SAssignNew(InstructionsBox, SMultiLineEditableTextBox)
					.HintText(LOCTEXT("InstructionsHint", "e.g. Make the tone darker, focus on combat mechanics..."))
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
						bPlanMode ? LOCTEXT("SendToAIPlan", "Send to Claude (Plan)") : LOCTEXT("SendToAI", "Send to AI"),
						FOnClicked::CreateLambda([WeakWindow, PromptBox, InstructionsBox, bPlanMode]()
						{
							FString FinalPrompt = PromptBox->GetText().ToString();
							FString Instructions = InstructionsBox->GetText().ToString().TrimStartAndEnd();

							if (!Instructions.IsEmpty())
							{
								FinalPrompt += FString::Printf(
									TEXT("\n\nAdditional user instructions:\n%s"), *Instructions);
							}

							if (bPlanMode)
							{
								FArborClaude::SendMessageInPlanMode(FinalPrompt);
							}
							else
							{
								FArborClaude::SendMessage(FinalPrompt);
							}

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
