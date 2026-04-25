#include "ArborChatWidget.h"
#include "ArborSettings.h"
#include "ArborImageViewerTab.h"
#include "ArborImageViewerWidget.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Framework/Text/SlateHyperlinkRun.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/AppStyle.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "Internationalization/Regex.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"

#define LOCTEXT_NAMESPACE "ArborChatWidget"

void SArborChatWidget::Construct(const FArguments& InArgs)
{
	const UArborSettings* Settings = GetDefault<UArborSettings>();
	const int32 FontSize = Settings ? Settings->ChatFontSize : 11;
	MonoFont = FCoreStyle::GetDefaultFontStyle("Mono", FontSize);
	MonoFont.FontFallback = EFontFallback::FF_LastResortFallback;
	BodyFont = FCoreStyle::GetDefaultFontStyle("Regular", FontSize);
	LabelFont = FCoreStyle::GetDefaultFontStyle("Bold", FMath::Max(FontSize - 1, 8));
	SmallFont = FCoreStyle::GetDefaultFontStyle("Regular", FMath::Max(FontSize - 3, 6));

	// Rich text styles for SRichTextBlock (one per message type, must outlive widgets)
	RichTextStyleUser = FTextBlockStyle::GetDefault();
	RichTextStyleUser.SetFont(BodyFont);
	RichTextStyleUser.SetColorAndOpacity(FSlateColor(FLinearColor(0.88f, 0.88f, 0.88f)));

	RichTextStyleAssistant = FTextBlockStyle::GetDefault();
	RichTextStyleAssistant.SetFont(BodyFont);
	RichTextStyleAssistant.SetColorAndOpacity(FSlateColor(FLinearColor(0.88f, 0.88f, 0.88f)));

	RichTextStyleSystem = FTextBlockStyle::GetDefault();
	RichTextStyleSystem.SetFont(MonoFont);
	RichTextStyleSystem.SetColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)));

	// Model options
	ModelOptions.Add(MakeShared<FString>(TEXT("default")));
	ModelOptions.Add(MakeShared<FString>(TEXT("opus")));
	ModelOptions.Add(MakeShared<FString>(TEXT("sonnet")));
	ModelOptions.Add(MakeShared<FString>(TEXT("haiku")));
	SelectedModel = ModelOptions[0];

	ChildSlot
	[
		SNew(SVerticalBox)

		// ── Toolbar ──
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
				.Padding(FMargin(6.0f, 4.0f))
				[
					SNew(SHorizontalBox)

					// Model dropdown
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 4, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ModelLabel", "Model:"))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 8, 0)
					[
						SAssignNew(ModelComboBox, STextComboBox)
						.OptionsSource(&ModelOptions)
						.InitiallySelectedItem(SelectedModel)
						.OnSelectionChanged_Lambda([this](TSharedPtr<FString> NewValue, ESelectInfo::Type)
						{
							if (!NewValue.IsValid() || NewValue == SelectedModel)
							{
								return;
							}

							if (!bProcessRunning)
							{
								SelectedModel = NewValue;
								return;
							}

							// Process is running — model change requires a new session
							EAppReturnType::Type Result = FMessageDialog::Open(
								EAppMsgType::OkCancel,
								FText::Format(
									LOCTEXT("ModelChangeConfirm",
										"Switching to {0} requires a new session.\nCurrent conversation will be lost.\n\nStart new session?"),
									FText::FromString(*NewValue)));

							if (Result == EAppReturnType::Ok)
							{
								SelectedModel = NewValue;
								OnNewSessionClicked();
							}
							else
							{
								ModelComboBox->SetSelectedItem(SelectedModel);
							}
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					]

					// Debug checkbox
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4, 0, 0, 0)
					[
						SAssignNew(DebugCheckBox, SCheckBox)
						.IsChecked_Lambda([this]() { return bDebugMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
						{
							bDebugMode = (NewState == ECheckBoxState::Checked);
						})
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(2, 0, 0, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("DebugLabel", "Debug"))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
					]

					// Plan mode checkbox
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8, 0, 0, 0)
					[
						SAssignNew(PlanCheckBox, SCheckBox)
						.IsChecked_Lambda([this]() { return bPlanMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
						{
							bPlanMode = (NewState == ECheckBoxState::Checked);
							FString ResumeId = SessionId;
							CleanUpProcess();
							AddMessage(EArborMessageType::System,
								bPlanMode ? TEXT("Switching to plan mode...") : TEXT("Switching to edit mode..."));
							FinalizeCurrentMessage();
							StartClaudeProcess(ResumeId);
						})
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(2, 0, 0, 0)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("PlanLabel", "Plan"))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
					]

					// Mode indicator badge
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(8, 0, 0, 0)
					[
						SAssignNew(ModeIndicatorBorder, SBorder)
						.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
						.BorderBackgroundColor_Lambda([this]()
						{
							return bPlanMode
								? FSlateColor(FLinearColor(0.7f, 0.5f, 0.1f, 0.8f))
								: FSlateColor(FLinearColor(0.15f, 0.55f, 0.15f, 0.8f));
						})
						.Padding(FMargin(6.0f, 2.0f))
						[
							SAssignNew(ModeIndicatorText, STextBlock)
							.Text_Lambda([this]()
							{
								return bPlanMode
									? FText::FromString(TEXT("PLAN"))
									: FText::FromString(TEXT("EDIT"));
							})
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
							.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.95f)))
						]
					]

					// Spacer
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SSpacer)
					]

					// Restart button (quit + resume same session — reloads MCP)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 4, 0)
					[
						SNew(SButton)
						.ToolTipText(LOCTEXT("RestartTip", "Restart Claude process and resume session (reloads MCP servers)"))
						.OnClicked_Lambda([this]()
						{
							OnRestartClicked();
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Text(LOCTEXT("Restart", "Restart"))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						]
					]

					// New Session button
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 6, 0)
					[
						SNew(SButton)
						.ToolTipText(LOCTEXT("NewSessionTip", "Start a fresh session (clears chat history)"))
						.OnClicked_Lambda([this]()
						{
							OnNewSessionClicked();
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Text(LOCTEXT("NewSession", "New"))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						]
					]

					// Connection status badge
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SAssignNew(ConnectionStatusBadge, SBorder)
						.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
						.BorderBackgroundColor(FLinearColor(0.9f, 0.8f, 0.2f, 0.15f))
						.Padding(FMargin(6.0f, 2.0f))
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0, 0, 4, 0)
							[
								SNew(SBox)
								.WidthOverride(8.0f)
								.HeightOverride(8.0f)
								[
									SAssignNew(ConnectionStatusDot, SBorder)
									.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
									.BorderBackgroundColor(FLinearColor(0.9f, 0.8f, 0.2f))
								]
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SAssignNew(StatusConnectionLabel, STextBlock)
								.Text(LOCTEXT("Starting", "Starting..."))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
							]
						]
					]
				]
			]

			// Toolbar bottom separator
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.15f, 0.15f, 0.15f, 1.0f))
				.Padding(FMargin(0, 0.5f))
				[
					SNew(SSpacer)
					.Size(FVector2D(0, 0))
				]
			]
		]

		// ── Message area (with scroll-to-bottom overlay) ──
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4.0f)
		[
			SNew(SOverlay)

			+ SOverlay::Slot()
			[
				SAssignNew(OutputScrollBox, SScrollBox)
				.ScrollBarAlwaysVisible(false)
				.ConsumeMouseWheel(EConsumeMouseWheel::Always)
				.AllowOverscroll(EAllowOverscroll::No)
			]

			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Bottom)
			.Padding(0, 0, 0, 4)
			[
				SAssignNew(ScrollToBottomButton, SButton)
				.Visibility(EVisibility::Collapsed)
				.OnClicked_Lambda([this]()
				{
					bAutoScroll = true;
					bNeedsScroll = true;
					return FReply::Handled();
				})
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(0.18f, 0.18f, 0.22f, 0.9f))
					.Padding(FMargin(10.0f, 4.0f))
					[
						SNew(STextBlock)
						.Text(FText::FromString(FString(TEXT("\x2193")) + TEXT("  New messages")))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.8f, 1.0f)))
					]
				]
			]
		]

		// ── Status bar ──
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.0f, 2.0f)
		[
			SNew(SHorizontalBox)

			// Pulsing activity dot
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 5, 0)
			[
				SNew(SBox)
				.WidthOverride(6.0f)
				.HeightOverride(6.0f)
				[
					SAssignNew(StatusDot, SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f))
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(StatusTextBlock, STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.45f, 0.45f, 0.45f)))
				.Text(FText::GetEmpty())
			]
		]

		// ── Input area ──
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 2.0f, 4.0f, 2.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(0, 0, 4, 0)
				[
					SNew(SBox)
					.MinDesiredHeight(36.0f * FontSize / 11.0f)
					.MaxDesiredHeight(120.0f * FontSize / 11.0f)
					[
						SAssignNew(InputTextBox, SMultiLineEditableTextBox)
						.Font(BodyFont)
						.HintText(LOCTEXT("InputHint", "Type a message..."))
						.OnKeyDownHandler(FOnKeyDown::CreateSP(this, &SArborChatWidget::OnInputKeyDown))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Bottom)
				.Padding(0, 0, 2, 0)
				[
					SAssignNew(SendButton, SButton)
					.ButtonColorAndOpacity(FLinearColor(0.15f, 0.4f, 0.15f, 1.0f))
					.OnClicked_Lambda([this]()
					{
						OnSendClicked();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Send", "Send"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.95f, 0.9f)))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Bottom)
				[
					SAssignNew(StopButton, SButton)
					.Visibility(EVisibility::Collapsed)
					.ButtonColorAndOpacity(FLinearColor(0.4f, 0.12f, 0.12f, 1.0f))
					.OnClicked_Lambda([this]()
					{
						InterruptClaude();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT("Stop", "Stop"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
						.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.6f, 0.6f)))
					]
				]
			]

			// Keyboard hints
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2, 2, 0, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("KeyHints", "Enter to send  \xB7  Shift+Enter for newline  \xB7  Esc to stop"))
				.Font(SmallFont)
				.ColorAndOpacity(FSlateColor(FLinearColor(0.35f, 0.35f, 0.35f)))
			]
		]
	];

	FString SavedSession = LoadSessionFromFile();
	StartClaudeProcess(SavedSession);
}

SArborChatWidget::~SArborChatWidget()
{
	SaveSessionToFile();
	CleanUpProcess();
}

// ─── Rich text helpers ───────────────────────────────────────────────────────

FString SArborChatWidget::EscapeRichText(const FString& Text)
{
	FString Result = Text;
	Result.ReplaceInline(TEXT("&"), TEXT("&amp;"));
	Result.ReplaceInline(TEXT("<"), TEXT("&lt;"));
	Result.ReplaceInline(TEXT(">"), TEXT("&gt;"));
	return Result;
}

FString SArborChatWidget::MarkupUrlsInRichText(const FString& RawText)
{
	const FRegexPattern Pattern(TEXT("https?://[^\\s<>\"\\)]+"));
	FRegexMatcher Matcher(Pattern, RawText);

	FString Result;
	int32 LastEnd = 0;

	while (Matcher.FindNext())
	{
		const int32 Start = Matcher.GetMatchBeginning();
		const int32 End = Matcher.GetMatchEnding();
		FString Url = RawText.Mid(Start, End - Start);

		// Strip trailing punctuation that's likely not part of the URL
		while (Url.Len() > 0)
		{
			TCHAR Last = Url[Url.Len() - 1];
			if (Last == TEXT('.') || Last == TEXT(',') || Last == TEXT(';') || Last == TEXT(':'))
			{
				Url = Url.Left(Url.Len() - 1);
			}
			else
			{
				break;
			}
		}
		const int32 ActualEnd = Start + Url.Len();

		// Escape text before this URL
		Result += EscapeRichText(RawText.Mid(LastEnd, Start - LastEnd));
		// Wrap URL in hyperlink tag
		Result += FString::Printf(TEXT("<a id=\"link\" href=\"%s\">%s</>"), *Url, *EscapeRichText(Url));
		LastEnd = ActualEnd;
	}

	// Escape remaining text after last URL
	Result += EscapeRichText(RawText.Mid(LastEnd));
	return Result;
}

static const FSlateStyleSet& GetChatHyperlinkStyleSet()
{
	static TSharedPtr<FSlateStyleSet> StyleSet;
	if (!StyleSet.IsValid())
	{
		StyleSet = MakeShared<FSlateStyleSet>("ArborChatLinks");

		FButtonStyle LinkButton;
		LinkButton.SetNormal(FSlateNoResource());
		LinkButton.SetHovered(FSlateNoResource());
		LinkButton.SetPressed(FSlateNoResource());

		FTextBlockStyle LinkText = FTextBlockStyle::GetDefault();
		LinkText.SetColorAndOpacity(FSlateColor(FLinearColor(0.3f, 0.6f, 1.0f)));

		FHyperlinkStyle HyperlinkStyle;
		HyperlinkStyle.SetUnderlineStyle(LinkButton);
		HyperlinkStyle.SetTextStyle(LinkText);
		HyperlinkStyle.SetPadding(FMargin(0));

		StyleSet->Set("RichTextBlock.Hyperlink", HyperlinkStyle);
	}
	return *StyleSet;
}

// ─── Message system ───────────────────────────────────────────────────────────

void SArborChatWidget::GetMessageStyle(EArborMessageType Type, FLinearColor& OutBarColor, FLinearColor& OutLabelColor, FLinearColor& OutTextColor, FLinearColor& OutBgColor, FString& OutLabel) const
{
	switch (Type)
	{
	case EArborMessageType::User:
		OutBarColor = FLinearColor(0.25f, 0.55f, 1.0f);
		OutLabelColor = FLinearColor(0.35f, 0.65f, 1.0f);
		OutTextColor = FLinearColor(0.88f, 0.88f, 0.88f);
		OutBgColor = FLinearColor(0.10f, 0.13f, 0.20f, 0.5f);
		OutLabel = TEXT("You");
		break;
	case EArborMessageType::Assistant:
		OutBarColor = FLinearColor(0.35f, 0.75f, 0.35f);
		OutLabelColor = FLinearColor(0.4f, 0.8f, 0.4f);
		OutTextColor = FLinearColor(0.88f, 0.88f, 0.88f);
		OutBgColor = FLinearColor(0.08f, 0.13f, 0.08f, 0.4f);
		OutLabel = TEXT("Claude");
		break;
	case EArborMessageType::Tool:
		OutBarColor = FLinearColor(0.9f, 0.7f, 0.2f);
		OutLabelColor = FLinearColor(0.9f, 0.7f, 0.2f);
		OutTextColor = FLinearColor(0.55f, 0.55f, 0.55f);
		OutBgColor = FLinearColor(0.16f, 0.16f, 0.16f, 0.6f);
		OutLabel = TEXT("Tool");
		break;
	case EArborMessageType::System:
	default:
		OutBarColor = FLinearColor(0.45f, 0.45f, 0.45f);
		OutLabelColor = FLinearColor(0.5f, 0.5f, 0.5f);
		OutTextColor = FLinearColor(0.5f, 0.5f, 0.5f);
		OutBgColor = FLinearColor(0.12f, 0.12f, 0.12f, 0.3f);
		OutLabel = TEXT("System");
		break;
	}
}

TSharedRef<SWidget> SArborChatWidget::CreateToolPillWidget(const FString& DisplayText)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.16f, 0.16f, 0.18f, 0.7f))
		.Padding(FMargin(8.0f, 3.0f, 10.0f, 3.0f))
		[
			SNew(SHorizontalBox)

			// Small yellow activity dot
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 5, 0)
			[
				SNew(SBox)
				.WidthOverride(5.0f)
				.HeightOverride(5.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(0.8f, 0.65f, 0.2f, 0.8f))
				]
			]

			// Tool display text
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(DisplayText))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			]
		];
}

TSharedRef<SWidget> SArborChatWidget::CreateMessageWidget(FArborChatMessage& Message)
{
	FLinearColor BarColor, LabelColor, TextColor, BgColor;
	FString Label;
	GetMessageStyle(Message.Type, BarColor, LabelColor, TextColor, BgColor, Label);

	// Tool messages render as compact pills
	if (Message.Type == EArborMessageType::Tool)
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("NoBorder"))
			.Padding(FMargin(8, 1, 4, 1))
			[
				CreateToolPillWidget(Message.Text)
			];
	}

	// Choose font: proportional for natural language, monospace for debug/system code output
	FSlateFontInfo& MessageFont = (Message.Type == EArborMessageType::System) ? MonoFont : BodyFont;

	TSharedRef<SVerticalBox> ContentBox = SNew(SVerticalBox)
		// Speaker label
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 2)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Label))
			.Font(LabelFont)
			.ColorAndOpacity(FSlateColor(LabelColor))
		];

	// Pick the persistent rich text style for this message type
	FTextBlockStyle* RichTextStyle;
	switch (Message.Type)
	{
	case EArborMessageType::User:    RichTextStyle = &RichTextStyleUser; break;
	case EArborMessageType::System:  RichTextStyle = &RichTextStyleSystem; break;
	default:                         RichTextStyle = &RichTextStyleAssistant; break;
	}

	ContentBox->AddSlot()
	.AutoHeight()
	[
		SAssignNew(Message.TextWidget, SRichTextBlock)
		.TextStyle(RichTextStyle)
		.AutoWrapText(true)
		.Text(FText::FromString(EscapeRichText(Message.Text)))
		.DecoratorStyleSet(&GetChatHyperlinkStyleSet())
		+ SRichTextBlock::HyperlinkDecorator(TEXT("link"),
			FSlateHyperlinkRun::FOnClick::CreateLambda([](const FSlateHyperlinkRun::FMetadata& Metadata)
			{
				if (const FString* Url = Metadata.Find(TEXT("href")))
				{
					FPlatformProcess::LaunchURL(**Url, nullptr, nullptr);
				}
			}))
	];

	// Add clickable screenshot link if message contains a screenshot path
	FString ImagePath;
	if (ExtractImagePath(Message.Text, ImagePath) && !ShownImageLinks.Contains(ImagePath))
	{
		ShownImageLinks.Add(ImagePath);
		FString CapturedPath = ImagePath;
		ContentBox->AddSlot()
		.AutoHeight()
		.Padding(0, 4, 0, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.ContentPadding(FMargin(0))
			.OnClicked_Lambda([this, CapturedPath]()
			{
				OpenImageWindow(CapturedPath);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("[View Screenshot]")))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.3f, 0.6f, 1.0f)))
			]
		];
	}

	FString MessageText = Message.Text;

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(BgColor)
		.Padding(FMargin(0, 3, 0, 3))
		.OnMouseButtonUp_Lambda([MessageText](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
		{
			if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
			{
				FMenuBuilder MenuBuilder(true, nullptr);
				MenuBuilder.AddMenuEntry(
					FText::FromString(TEXT("Copy Message")),
					FText::FromString(TEXT("Copy this message to clipboard")),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([MessageText]()
					{
						FPlatformApplicationMisc::ClipboardCopy(*MessageText);
					}))
				);
				FSlateApplication::Get().PushMenu(
					FSlateApplication::Get().GetActiveTopLevelWindow().ToSharedRef(),
					FWidgetPath(),
					MenuBuilder.MakeWidget(),
					MouseEvent.GetScreenSpacePosition(),
					FPopupTransitionEffect::ContextMenu
				);
				return FReply::Handled();
			}
			return FReply::Unhandled();
		})
		[
			SNew(SHorizontalBox)

			// Color indicator bar
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 2, 8, 2)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(BarColor)
				.Padding(FMargin(1.5f, 0))
				[
					SNew(SSpacer)
					.Size(FVector2D(0, 0))
				]
			]

			// Content area
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0, 2, 6, 2)
			[
				ContentBox
			]
		];
}

void SArborChatWidget::AddMessage(EArborMessageType Type, const FString& Text)
{
	// Detect turn boundaries — insert extra spacing when switching between user/assistant
	bool bNewTurn = false;
	if (Messages.Num() > 0 && Type != EArborMessageType::Tool)
	{
		EArborMessageType PrevType = Messages.Last().Type;
		// New turn: User after Assistant/Tool/System, or Assistant after User
		if ((Type == EArborMessageType::User && PrevType != EArborMessageType::User) ||
			(Type == EArborMessageType::Assistant && PrevType == EArborMessageType::User))
		{
			bNewTurn = true;
		}
	}

	FArborChatMessage Msg;
	Msg.Type = Type;
	Msg.Text = Text;

	TSharedRef<SWidget> Row = CreateMessageWidget(Msg);
	Messages.Add(MoveTemp(Msg));

	if (OutputScrollBox.IsValid())
	{
		if (bNewTurn)
		{
			// Turn separator: subtle line + spacing
			OutputScrollBox->AddSlot()
			.Padding(16, 4, 16, 4)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.2f, 0.2f, 0.2f, 0.4f))
				.Padding(FMargin(0, 0.5f))
				[
					SNew(SSpacer)
					.Size(FVector2D(0, 0))
				]
			];
		}

		float VerticalPad = (Type == EArborMessageType::Tool) ? 0.0f : 1.0f;
		OutputScrollBox->AddSlot()
		.Padding(0, VerticalPad)
		[
			Row
		];
	}

	bNeedsScroll = true;
}

void SArborChatWidget::AppendToCurrentMessage(const FString& Text)
{
	if (CurrentStreamingIndex == INDEX_NONE || CurrentStreamingIndex >= Messages.Num())
	{
		return;
	}

	FArborChatMessage& Msg = Messages[CurrentStreamingIndex];
	Msg.Text.Append(Text);

	if (Msg.TextWidget.IsValid())
	{
		// During streaming, escape markup chars but don't process URLs yet
		Msg.TextWidget->SetText(FText::FromString(EscapeRichText(Msg.Text)));
	}

	bNeedsScroll = true;
}

void SArborChatWidget::FinalizeCurrentMessage()
{
	if (CurrentStreamingIndex != INDEX_NONE && CurrentStreamingIndex < Messages.Num())
	{
		FArborChatMessage& Msg = Messages[CurrentStreamingIndex];

		// Re-process text with URL markup now that streaming is complete
		if (Msg.TextWidget.IsValid())
		{
			Msg.TextWidget->SetText(FText::FromString(MarkupUrlsInRichText(Msg.Text)));
		}

		// Check for image paths (screenshot viewer buttons)
		if (Msg.Type == EArborMessageType::Assistant && OutputScrollBox.IsValid())
		{
			FString ImagePath;
			if (ExtractImagePath(Msg.Text, ImagePath) && !ShownImageLinks.Contains(ImagePath))
			{
				ShownImageLinks.Add(ImagePath);
				FString CapturedPath = ImagePath;
				FString FileName = FPaths::GetCleanFilename(CapturedPath);
				OutputScrollBox->AddSlot()
				.Padding(8.0f, 2.0f, 4.0f, 2.0f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.ContentPadding(FMargin(0))
					.OnClicked_Lambda([this, CapturedPath]()
					{
						OpenImageWindow(CapturedPath);
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(TEXT("[View: %s]"), *FileName)))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.3f, 0.6f, 1.0f)))
					]
				];
				bNeedsScroll = true;
			}
		}
	}

	CurrentStreamingIndex = INDEX_NONE;
}

void SArborChatWidget::ClearChat()
{
	Messages.Empty();
	CurrentStreamingIndex = INDEX_NONE;
	StdOutLineBuffer.Empty();
	ShownImageLinks.Empty();
	ProcessedToolUseIds.Empty();

	if (OutputScrollBox.IsValid())
	{
		OutputScrollBox->ClearChildren();
	}
}

// ─── Toolbar actions ──────────────────────────────────────────────────────────

void SArborChatWidget::OnNewSessionClicked()
{
	IFileManager::Get().Delete(*GetSessionFilePath());
	ClearChat();
	StartClaudeProcess();
}

void SArborChatWidget::OnRestartClicked()
{
	FString ResumeId = SessionId;
	FinalizeCurrentMessage();
	CleanUpProcess();

	AddMessage(EArborMessageType::System, TEXT("Restarting Claude (MCP servers will reload)..."));
	FinalizeCurrentMessage();
	SetStatus(TEXT(""));

	StartClaudeProcess(ResumeId);
}

void SArborChatWidget::InterruptClaude()
{
	if (!bWaitingForResponse)
	{
		return;
	}

	FinalizeCurrentMessage();

	// Save session ID before killing so we can resume with context
	FString ResumeId = SessionId;
	CleanUpProcess();

	if (!ResumeId.IsEmpty())
	{
		AddMessage(EArborMessageType::System, TEXT("Interrupted. Resuming session..."));
		FinalizeCurrentMessage();
		SetStatus(TEXT(""));
		// Restart with --resume to preserve conversation context
		StartClaudeProcess(ResumeId);
	}
	else
	{
		AddMessage(EArborMessageType::System, TEXT("Interrupted. Starting new session..."));
		FinalizeCurrentMessage();
		SetStatus(TEXT(""));
		StartClaudeProcess();
	}
}

void SArborChatWidget::UpdateConnectionStatus()
{
	if (!ConnectionStatusDot.IsValid())
	{
		return;
	}

	FLinearColor DotColor;
	FLinearColor BadgeBg;
	FString Label;

	if (bProcessRunning && bInitialized)
	{
		DotColor = FLinearColor(0.2f, 0.8f, 0.2f);
		BadgeBg = FLinearColor(0.1f, 0.2f, 0.1f, 0.4f);
		Label = TEXT("Ready");
	}
	else if (bProcessRunning)
	{
		DotColor = FLinearColor(0.9f, 0.8f, 0.2f);
		BadgeBg = FLinearColor(0.2f, 0.18f, 0.08f, 0.4f);
		Label = TEXT("Starting...");
	}
	else
	{
		DotColor = FLinearColor(0.8f, 0.2f, 0.2f);
		BadgeBg = FLinearColor(0.2f, 0.1f, 0.1f, 0.4f);
		Label = TEXT("Offline");
	}

	ConnectionStatusDot->SetBorderBackgroundColor(DotColor);
	if (ConnectionStatusBadge.IsValid())
	{
		ConnectionStatusBadge->SetBorderBackgroundColor(BadgeBg);
	}
	if (StatusConnectionLabel.IsValid())
	{
		StatusConnectionLabel->SetText(FText::FromString(Label));
	}
}

// ─── Session persistence ──────────────────────────────────────────────────────

FString SArborChatWidget::GetSessionFilePath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / TEXT("Arbor") / TEXT("chat_session.txt");
}

void SArborChatWidget::SaveSessionToFile() const
{
	if (SessionId.IsEmpty())
	{
		return;
	}
	FFileHelper::SaveStringToFile(SessionId, *GetSessionFilePath());
}

FString SArborChatWidget::LoadSessionFromFile() const
{
	FString SavedId;
	if (FFileHelper::LoadFileToString(SavedId, *GetSessionFilePath()))
	{
		return SavedId.TrimStartAndEnd();
	}
	return FString();
}

// ─── Process management ───────────────────────────────────────────────────────

void SArborChatWidget::StartClaudeProcess(const FString& ResumeSessionId)
{
	CleanUpProcess();

	SetStatus(TEXT("Starting Claude..."));

	FPlatformProcess::CreatePipe(StdOutReadPipe, StdOutWritePipe, false);
	FPlatformProcess::CreatePipe(StdInReadPipe, StdInWritePipe, true);

	FString InitMsg = TEXT("{\"type\":\"control_request\",\"request_id\":\"req_init\",\"request\":{\"subtype\":\"initialize\",\"hooks\":null,\"agents\":null}}\n");
	FPlatformProcess::WritePipe(StdInWritePipe, InitMsg);

	FString WorkDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FString ClaudePath = TEXT("claude");
	FString Args = TEXT("--print --output-format stream-json --input-format stream-json --verbose");

	// Permission handling: plan mode uses restricted permissions, edit mode skips all permissions
	if (bPlanMode)
	{
		Args += TEXT(" --permission-mode plan");
		// Pre-approve read-only tools so plan mode exploration works in stream-json mode.
		// --permission-mode plan auto-denies without prompting in non-interactive mode,
		// so we must explicitly allow read tools and read-only MCP query tools (*_query).
		Args += TEXT(" --allowedTools \"Read Glob Grep mcp__ue5-bridge__ue5_ping mcp__ue5-bridge__*_query\"");
	}
	else
	{
		Args += TEXT(" --dangerously-skip-permissions");
	}

	// Append model flag if not default
	if (SelectedModel.IsValid() && *SelectedModel != TEXT("default"))
	{
		Args += FString::Printf(TEXT(" --model %s"), **SelectedModel);
	}

	// Resume session if we have an ID (preserves conversation context after interrupt)
	if (!ResumeSessionId.IsEmpty())
	{
		Args += FString::Printf(TEXT(" --resume %s"), *ResumeSessionId);
	}

	ClaudeProcess = FPlatformProcess::CreateProc(
		*ClaudePath,
		*Args,
		false,      // bLaunchDetached
		true,       // bLaunchHidden
		true,       // bLaunchReallyHidden
		&ProcessId,
		0,          // PriorityModifier
		*WorkDir,
		StdOutWritePipe,  // PipeWriteChild — child's stdout
		StdInReadPipe     // PipeReadChild  — child's stdin
	);

	if (ClaudeProcess.IsValid())
	{
		bProcessRunning = true;
		bNeedsSendInit = false;

		if (!ResumeSessionId.IsEmpty())
		{
			AddMessage(EArborMessageType::System, TEXT("Resumed previous session"));
			FinalizeCurrentMessage();
		}
	}
	else
	{
		bProcessRunning = false;
		SetStatus(TEXT(""));
		AddMessage(EArborMessageType::System, TEXT("Failed to start Claude. Make sure 'claude' is on your PATH."));
		FinalizeCurrentMessage();
	}
}

void SArborChatWidget::CleanUpProcess()
{
	if (ClaudeProcess.IsValid())
	{
		if (FPlatformProcess::IsProcRunning(ClaudeProcess))
		{
			FPlatformProcess::TerminateProc(ClaudeProcess, /*bKillTree=*/ true);
		}
		FPlatformProcess::CloseProc(ClaudeProcess);
	}

	if (StdOutReadPipe != nullptr || StdOutWritePipe != nullptr)
	{
		FPlatformProcess::ClosePipe(StdOutReadPipe, StdOutWritePipe);
		StdOutReadPipe = nullptr;
		StdOutWritePipe = nullptr;
	}

	if (StdInReadPipe != nullptr || StdInWritePipe != nullptr)
	{
		FPlatformProcess::ClosePipe(StdInReadPipe, StdInWritePipe);
		StdInReadPipe = nullptr;
		StdInWritePipe = nullptr;
	}

	bProcessRunning = false;
	bInitialized = false;
	bNeedsSendInit = false;
	bWaitingForResponse = false;
	bSuppressAutoResolve = false;
	// Note: do NOT reset bPendingPlanExecution here — it must survive across
	// CleanUpProcess → StartClaudeProcess so the resumed session auto-sends.
	ProcessId = 0;
	StdOutLineBuffer.Empty();
	CurrentToolName.Empty();
	SessionId.Empty();
	PendingToolUseId.Empty();
	PendingQuestionText.Empty();
	PendingQuestionOptions.Empty();
	PendingQuestionDescriptions.Empty();
	PendingQuestionsJson.Empty();
	PlanFilePath.Empty();
	QuestionButtonsBox.Reset();
}

void SArborChatWidget::WriteToPipe(const FString& Json)
{
	if (StdInWritePipe != nullptr)
	{
		FString Line = Json + TEXT("\n");
		FPlatformProcess::WritePipe(StdInWritePipe, Line);
	}
}

void SArborChatWidget::SendUserMessage(const FString& Message)
{
	FString Escaped = Message;
	Escaped = Escaped.Replace(TEXT("\\"), TEXT("\\\\"));
	Escaped = Escaped.Replace(TEXT("\""), TEXT("\\\""));
	Escaped = Escaped.Replace(TEXT("\n"), TEXT("\\n"));
	Escaped = Escaped.Replace(TEXT("\r"), TEXT("\\r"));
	Escaped = Escaped.Replace(TEXT("\t"), TEXT("\\t"));

	FString Json = FString::Printf(
		TEXT("{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":\"%s\"},\"parent_tool_use_id\":null,\"session_id\":\"default\"}"),
		*Escaped
	);

	WriteToPipe(Json);
	bWaitingForResponse = true;
	SetStatus(TEXT("Claude is thinking..."));
}

void SArborChatWidget::SendToolResult(const FString& ToolUseId, const FString& Content)
{
	// Escape content for embedding inside JSON string
	FString Escaped = Content;
	Escaped = Escaped.Replace(TEXT("\\"), TEXT("\\\\"));
	Escaped = Escaped.Replace(TEXT("\""), TEXT("\\\""));
	Escaped = Escaped.Replace(TEXT("\n"), TEXT("\\n"));
	Escaped = Escaped.Replace(TEXT("\r"), TEXT("\\r"));
	Escaped = Escaped.Replace(TEXT("\t"), TEXT("\\t"));

	FString Json = FString::Printf(
		TEXT("{\"type\":\"user\",\"message\":{\"role\":\"user\",\"content\":[{\"type\":\"tool_result\",\"tool_use_id\":\"%s\",\"content\":\"%s\",\"is_error\":false}]},\"parent_tool_use_id\":null,\"session_id\":\"default\"}"),
		*ToolUseId,
		*Escaped
	);

	WriteToPipe(Json);
	bWaitingForResponse = true;
	SetStatus(TEXT("Claude is thinking..."));
}

void SArborChatWidget::SendExternalMessage(const FString& Message)
{
	// Start Claude if not running
	if (!bProcessRunning)
	{
		StartClaudeProcess();
	}

	// Show the message in chat
	AddMessage(EArborMessageType::User, Message);
	FinalizeCurrentMessage();

	// Send via pipe (same as OnSendClicked)
	SendUserMessage(Message);
}

void SArborChatWidget::SendExternalMessageInPlanMode(const FString& Message)
{
	// Switch to plan mode
	bPlanMode = true;
	if (PlanCheckBox.IsValid())
	{
		PlanCheckBox->SetIsChecked(ECheckBoxState::Checked);
	}

	// If Claude is running in non-plan mode, kill and restart in plan mode
	if (bProcessRunning)
	{
		CleanUpProcess();
		AddMessage(EArborMessageType::System, TEXT("Switching to plan mode for implementation help..."));
		FinalizeCurrentMessage();
		StartClaudeProcess();
	}
	else
	{
		// Start fresh in plan mode
		StartClaudeProcess();
	}

	// Show the message in chat and send
	AddMessage(EArborMessageType::User, Message);
	FinalizeCurrentMessage();
	SendUserMessage(Message);
}

void SArborChatWidget::OnQuestionOptionClicked(const FString& OptionLabel)
{
	if (PendingToolUseId.IsEmpty())
	{
		return;
	}

	// Show the user's choice in chat
	AddMessage(EArborMessageType::User, OptionLabel);
	FinalizeCurrentMessage();

	// Remove the option buttons from chat
	if (QuestionButtonsBox.IsValid() && OutputScrollBox.IsValid())
	{
		OutputScrollBox->RemoveSlot(QuestionButtonsBox.ToSharedRef());
		QuestionButtonsBox.Reset();
	}

	// Clear pending state
	PendingToolUseId.Empty();
	PendingQuestionText.Empty();
	PendingQuestionOptions.Empty();
	PendingQuestionDescriptions.Empty();
	PendingQuestionsJson.Empty();

	// Send the user's choice as a regular user message.
	// Claude Code's --print mode auto-resolves AskUserQuestion (tool_result via stdin
	// is not supported — see anthropics/claude-code#16712). We suppressed that
	// auto-resolved turn via bSuppressAutoResolve, so now we send the answer as
	// a new conversation turn that Claude will naturally respond to.
	SendUserMessage(OptionLabel);
}

// ─── Screenshot viewer ────────────────────────────────────────────────────────

bool SArborChatWidget::ExtractImagePath(const FString& Text, FString& OutPath)
{
	// Try multiple key names for image paths in JSON
	static const TArray<FString> Keys = {
		TEXT("\"screenshot_path\""),
		TEXT("\"image_path\"")
	};

	for (const FString& Key : Keys)
	{
		int32 KeyIdx = Text.Find(Key, ESearchCase::IgnoreCase);
		if (KeyIdx == INDEX_NONE)
		{
			continue;
		}

		int32 ColonIdx = Text.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, KeyIdx + Key.Len());
		if (ColonIdx == INDEX_NONE) continue;

		int32 QuoteStart = Text.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, ColonIdx + 1);
		if (QuoteStart == INDEX_NONE) continue;

		int32 QuoteEnd = Text.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, QuoteStart + 1);
		if (QuoteEnd == INDEX_NONE) continue;

		OutPath = Text.Mid(QuoteStart + 1, QuoteEnd - QuoteStart - 1);
		// Unescape JSON backslashes and normalize
		OutPath.ReplaceInline(TEXT("\\\\"), TEXT("\\"));
		OutPath.ReplaceInline(TEXT("/"), TEXT("\\"));
		if (FPaths::FileExists(OutPath))
		{
			return true;
		}
	}

	// Fallback: look for bare file paths ending in image extensions
	// Matches patterns like D:\path\to\file.jpg or C:/path/to/file.png
	static const TArray<FString> Extensions = {
		TEXT(".jpg"), TEXT(".jpeg"), TEXT(".png"), TEXT(".bmp")
	};

	for (const FString& Ext : Extensions)
	{
		int32 ExtIdx = Text.Find(Ext, ESearchCase::IgnoreCase);
		while (ExtIdx != INDEX_NONE)
		{
			int32 PathEnd = ExtIdx + Ext.Len();

			// Walk backwards to find path start (drive letter or forward slash)
			int32 PathStart = ExtIdx;
			while (PathStart > 0)
			{
				TCHAR Ch = Text[PathStart - 1];
				if (Ch == TEXT(' ') || Ch == TEXT('\n') || Ch == TEXT('\r') ||
				    Ch == TEXT('\t') || Ch == TEXT('"') || Ch == TEXT('\'') ||
				    Ch == TEXT('`') || Ch == TEXT('(') || Ch == TEXT('['))
				{
					break;
				}
				PathStart--;
			}

			if (PathStart < ExtIdx)
			{
				OutPath = Text.Mid(PathStart, PathEnd - PathStart);
				OutPath.ReplaceInline(TEXT("/"), TEXT("\\"));
				if (FPaths::FileExists(OutPath))
				{
					return true;
				}
			}

			// Search for next occurrence
			ExtIdx = Text.Find(Ext, ESearchCase::IgnoreCase, ESearchDir::FromStart, PathEnd);
		}
	}

	return false;
}

void SArborChatWidget::OpenImageWindow(const FString& ImagePath)
{
	FGlobalTabmanager::Get()->TryInvokeTab(FArborImageViewerTab::TabId);

	TSharedPtr<SArborImageViewerWidget> Widget = FArborImageViewerTab::GetWidget();
	if (Widget.IsValid())
	{
		Widget->LoadImage(ImagePath);
	}
}

// ─── Tool display names ───────────────────────────────────────────────────────

static FString GetToolDisplayName(const FString& RawName)
{
	// Internal Claude tools — fixed set
	if (RawName == TEXT("Read"))           return TEXT("Reading files");
	if (RawName == TEXT("Bash"))           return TEXT("Running command");
	if (RawName == TEXT("Glob"))           return TEXT("Searching files");
	if (RawName == TEXT("Grep"))           return TEXT("Searching code");
	if (RawName == TEXT("Edit"))           return TEXT("Editing");
	if (RawName == TEXT("Write"))          return TEXT("Writing file");
	if (RawName == TEXT("Task"))           return TEXT("Running task");
	if (RawName == TEXT("WebFetch"))       return TEXT("Fetching web page");
	if (RawName == TEXT("WebSearch"))      return TEXT("Searching web");
	if (RawName == TEXT("NotebookEdit"))   return TEXT("Editing notebook");
	if (RawName == TEXT("ExitPlanMode"))   return TEXT("Requesting plan approval");

	// MCP tools — derive display name algorithmically
	FString Name = RawName;

	// Strip everything up to and including the last "__"
	int32 LastSep;
	if (Name.FindLastChar('_', LastSep) && LastSep > 0 && Name[LastSep - 1] == '_')
	{
		Name = Name.Mid(LastSep + 1);
	}
	else if (Name.StartsWith(TEXT("mcp_")))
	{
		int32 Pos = Name.Find(TEXT("__"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (Pos != INDEX_NONE)
		{
			Name = Name.Mid(Pos + 2);
		}
	}

	// Strip common prefixes like "ue5_"
	if (Name.StartsWith(TEXT("ue5_")))
	{
		Name = Name.Mid(4);
	}

	// Friendly overrides for specific MCP tools
	if (Name == TEXT("run_python"))        return TEXT("Building in editor");

	// Replace underscores with spaces
	Name = Name.Replace(TEXT("_"), TEXT(" "));

	// Capitalize first letter
	if (Name.Len() > 0)
	{
		Name = Name.Left(1).ToUpper() + Name.Mid(1);
	}

	return Name;
}

// ─── JSON message processing ──────────────────────────────────────────────────

void SArborChatWidget::ProcessJsonLine(const FString& Line)
{
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
	TSharedPtr<FJsonObject> JsonObj;

	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
	{
		return;
	}

	FString Type;
	if (!JsonObj->TryGetStringField(TEXT("type"), Type))
	{
		return;
	}

	if (bDebugMode)
	{
		UE_LOG(LogTemp, Log, TEXT("[ArborChat] type=%s | %s"), *Type, *Line.Left(300));
	}

	// Capture session_id for --resume on interrupt
	FString MsgSessionId;
	if (JsonObj->TryGetStringField(TEXT("session_id"), MsgSessionId) && !MsgSessionId.IsEmpty())
	{
		SessionId = MsgSessionId;
		SaveSessionToFile();
	}

	// Suppress auto-resolved AskUserQuestion results
	if (bSuppressAutoResolve)
	{
		if (Type == TEXT("user") || Type == TEXT("result"))
		{
			if (Type == TEXT("result"))
			{
				bSuppressAutoResolve = false;
			}
			return;
		}
	}

	if (Type == TEXT("system"))
	{
		if (!bInitialized)
		{
			bInitialized = true;
			SetStatus(TEXT(""));
			FString Model;
			if (!JsonObj->TryGetStringField(TEXT("model"), Model) || Model.IsEmpty())
			{
				Model = SelectedModel.IsValid() ? *SelectedModel : TEXT("default");
			}
			AddMessage(EArborMessageType::System, FString::Printf(TEXT("Claude is ready (%s)"), *Model));
			FinalizeCurrentMessage();

			if (bPendingPlanExecution)
			{
				bPendingPlanExecution = false;
				SendUserMessage(TEXT("The plan was approved. Proceed with execution."));
			}
		}
		else
		{
			FString Subtype;
			JsonObj->TryGetStringField(TEXT("subtype"), Subtype);
			if (Subtype.Contains(TEXT("compact")) || Subtype.Contains(TEXT("summarize")))
			{
				SetStatus(TEXT("Compacting conversation..."));
				AddMessage(EArborMessageType::System, TEXT("Conversation compacted"));
				FinalizeCurrentMessage();
			}
			else if (!Subtype.IsEmpty())
			{
				SetStatus(FString::Printf(TEXT("%s..."), *Subtype));
			}
		}
	}
	else if (Type == TEXT("control_response"))
	{
		if (!bInitialized)
		{
			bInitialized = true;
			SetStatus(TEXT(""));
			FString Model = SelectedModel.IsValid() ? *SelectedModel : TEXT("default");
			AddMessage(EArborMessageType::System, FString::Printf(TEXT("Claude is ready (%s)"), *Model));
			FinalizeCurrentMessage();

			if (bPendingPlanExecution)
			{
				bPendingPlanExecution = false;
				SendUserMessage(TEXT("The plan was approved. Proceed with execution."));
			}
		}
	}
	else if (Type == TEXT("assistant"))
	{
		HandleAssistantMessage(JsonObj);
	}
	else if (Type == TEXT("content_block_delta"))
	{
		const TSharedPtr<FJsonObject>* DeltaObj = nullptr;
		if (JsonObj->TryGetObjectField(TEXT("delta"), DeltaObj) && DeltaObj)
		{
			FString DeltaText;
			if ((*DeltaObj)->TryGetStringField(TEXT("text"), DeltaText) && !DeltaText.IsEmpty())
			{
				if (CurrentStreamingIndex != INDEX_NONE)
				{
					AppendToCurrentMessage(DeltaText);
				}
				else
				{
					AddMessage(EArborMessageType::Assistant, DeltaText);
					CurrentStreamingIndex = Messages.Num() - 1;
				}
			}
		}
	}
	else if (Type == TEXT("content_block_stop"))
	{
		// Content block finished — finalize handled on result
	}
	else if (Type == TEXT("user"))
	{
		HandleUserMessage(JsonObj);
	}
	else if (Type == TEXT("result"))
	{
		HandleResultMessage(JsonObj);
	}
}

// ─── Assistant message handler ───────────────────────────────────────────────

void SArborChatWidget::HandleAssistantMessage(const TSharedPtr<FJsonObject>& JsonObj)
{
	// Try message.content first, then fall back to top-level content
	const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
	const TSharedPtr<FJsonObject>* MessageObj = nullptr;
	if (JsonObj->TryGetObjectField(TEXT("message"), MessageObj) && MessageObj)
	{
		(*MessageObj)->TryGetArrayField(TEXT("content"), ContentArray);
	}
	if (!ContentArray)
	{
		JsonObj->TryGetArrayField(TEXT("content"), ContentArray);
	}
	if (!ContentArray)
	{
		return;
	}

	for (const TSharedPtr<FJsonValue>& ContentItem : *ContentArray)
	{
		const TSharedPtr<FJsonObject>* ContentObj = nullptr;
		if (!ContentItem->TryGetObject(ContentObj) || !ContentObj)
		{
			continue;
		}

		FString ContentType;
		if (!(*ContentObj)->TryGetStringField(TEXT("type"), ContentType))
		{
			continue;
		}

		if (ContentType == TEXT("text"))
		{
			HandleAssistantTextBlock(*ContentObj);
		}
		else if (ContentType == TEXT("tool_use"))
		{
			HandleToolUseBlock(*ContentObj);
		}
	}
}

void SArborChatWidget::HandleAssistantTextBlock(const TSharedPtr<FJsonObject>& ContentObj)
{
	FString Text;
	if (!ContentObj->TryGetStringField(TEXT("text"), Text))
	{
		return;
	}

	CurrentToolName.Empty();
	SetStatus(TEXT("Claude is thinking..."));

	if (CurrentStreamingIndex != INDEX_NONE)
	{
		AppendToCurrentMessage(Text);
	}
	else
	{
		AddMessage(EArborMessageType::Assistant, Text);
		CurrentStreamingIndex = Messages.Num() - 1;
	}
}

void SArborChatWidget::HandleToolUseBlock(const TSharedPtr<FJsonObject>& ContentObj)
{
	// Deduplicate: streaming may re-send the same assistant message
	FString ToolUseId;
	ContentObj->TryGetStringField(TEXT("id"), ToolUseId);
	if (!ToolUseId.IsEmpty() && ProcessedToolUseIds.Contains(ToolUseId))
	{
		return;
	}
	if (!ToolUseId.IsEmpty())
	{
		ProcessedToolUseIds.Add(ToolUseId);
	}

	FinalizeCurrentMessage();

	FString ToolName;
	ContentObj->TryGetStringField(TEXT("name"), ToolName);
	CurrentToolName = ToolName;

	// Track the last file written during plan mode so we can display it on ExitPlanMode
	if (bPlanMode && ToolName == TEXT("Write"))
	{
		const TSharedPtr<FJsonObject>* InputObj = nullptr;
		if (ContentObj->TryGetObjectField(TEXT("input"), InputObj) && InputObj)
		{
			FString FilePath;
			if ((*InputObj)->TryGetStringField(TEXT("file_path"), FilePath))
			{
				PlanFilePath = FilePath;
			}
		}
	}

	if (ToolName == TEXT("AskUserQuestion"))
	{
		HandleAskUserQuestion(ToolUseId, ContentObj);
	}
	else if (ToolName == TEXT("ExitPlanMode"))
	{
		HandleExitPlanMode(ToolUseId, ContentObj);
	}
	else
	{
		FString DisplayName = GetToolDisplayName(ToolName);
		if (ToolName.StartsWith(TEXT("mcp_")) || bDebugMode)
		{
			AddMessage(EArborMessageType::Tool, bDebugMode ? ToolName : DisplayName);
			FinalizeCurrentMessage();
		}
		SetStatus(FString::Printf(TEXT("%s..."), *DisplayName));
	}
}

void SArborChatWidget::HandleAskUserQuestion(const FString& ToolUseId, const TSharedPtr<FJsonObject>& ContentObj)
{
	const TSharedPtr<FJsonObject>* InputObj = nullptr;
	if (!ContentObj->TryGetObjectField(TEXT("input"), InputObj) || !InputObj)
	{
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* QuestionsArray = nullptr;
	if (!(*InputObj)->TryGetArrayField(TEXT("questions"), QuestionsArray) || !QuestionsArray || QuestionsArray->Num() == 0)
	{
		return;
	}

	// Save the full questions JSON for the response
	FString QuestionsStr;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&QuestionsStr);
	FJsonSerializer::Serialize(*QuestionsArray, Writer);
	Writer->Close();
	PendingQuestionsJson = QuestionsStr;

	const TSharedPtr<FJsonObject>* FirstQuestion = nullptr;
	if (!(*QuestionsArray)[0]->TryGetObject(FirstQuestion) || !FirstQuestion)
	{
		return;
	}

	FString QuestionText;
	(*FirstQuestion)->TryGetStringField(TEXT("question"), QuestionText);

	PendingToolUseId = ToolUseId;
	PendingQuestionText = QuestionText;
	PendingQuestionOptions.Empty();
	PendingQuestionDescriptions.Empty();
	bSuppressAutoResolve = true;

	AddMessage(EArborMessageType::System, QuestionText);
	FinalizeCurrentMessage();

	// Parse options
	const TArray<TSharedPtr<FJsonValue>>* OptionsArray = nullptr;
	if ((*FirstQuestion)->TryGetArrayField(TEXT("options"), OptionsArray) && OptionsArray)
	{
		for (const TSharedPtr<FJsonValue>& OptionVal : *OptionsArray)
		{
			const TSharedPtr<FJsonObject>* OptionObj = nullptr;
			if (OptionVal->TryGetObject(OptionObj) && OptionObj)
			{
				FString Label, Desc;
				(*OptionObj)->TryGetStringField(TEXT("label"), Label);
				(*OptionObj)->TryGetStringField(TEXT("description"), Desc);
				if (!Label.IsEmpty())
				{
					PendingQuestionOptions.Add(Label);
					PendingQuestionDescriptions.Add(Desc);
				}
			}
		}
	}

	// Build option buttons
	TSharedRef<SVerticalBox> OptionsVBox = SNew(SVerticalBox);
	for (int32 i = 0; i < PendingQuestionOptions.Num(); ++i)
	{
		FString CapturedOption = PendingQuestionOptions[i];
		FString OptionDesc = (i < PendingQuestionDescriptions.Num()) ? PendingQuestionDescriptions[i] : FString();

		TSharedRef<SVerticalBox> CardContent = SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(CapturedOption))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f)))
			];

		if (!OptionDesc.IsEmpty())
		{
			CardContent->AddSlot()
			.AutoHeight()
			.Padding(0, 2, 0, 0)
			[
				SNew(STextBlock)
				.Text(FText::FromString(OptionDesc))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
				.AutoWrapText(true)
			];
		}

		OptionsVBox->AddSlot()
		.AutoHeight()
		.Padding(8, 2, 8, 2)
		[
			SNew(SButton)
			.ButtonColorAndOpacity(FLinearColor(0.14f, 0.14f, 0.17f, 0.9f))
			.OnClicked_Lambda([this, CapturedOption]()
			{
				OnQuestionOptionClicked(CapturedOption);
				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("NoBorder"))
				.Padding(FMargin(10.0f, 6.0f))
				[
					CardContent
				]
			]
		];
	}

	QuestionButtonsBox = SNew(SHorizontalBox);
	QuestionButtonsBox->AddSlot()
	.FillWidth(1.0f)
	[
		OptionsVBox
	];

	if (OutputScrollBox.IsValid())
	{
		OutputScrollBox->AddSlot()
		.Padding(0, 4, 0, 4)
		[
			QuestionButtonsBox.ToSharedRef()
		];
	}

	SetStatus(TEXT("Claude is asking..."));
	bNeedsScroll = true;
}

// ─── ExitPlanMode handler ─────────────────────────────────────────────────────

void SArborChatWidget::HandleExitPlanMode(const FString& ToolUseId, const TSharedPtr<FJsonObject>& ContentObj)
{
	PendingToolUseId = ToolUseId;
	bSuppressAutoResolve = true;

	// Read the plan file contents
	FString PlanContent;
	bool bPlanRead = false;
	if (!PlanFilePath.IsEmpty() && FPaths::FileExists(PlanFilePath))
	{
		bPlanRead = FFileHelper::LoadFileToString(PlanContent, *PlanFilePath);
	}

	if (bPlanRead && !PlanContent.IsEmpty())
	{
		AddMessage(EArborMessageType::System, PlanContent);
		FinalizeCurrentMessage();
	}
	else
	{
		AddMessage(EArborMessageType::System, TEXT("Plan ready for review (plan file could not be read)."));
		FinalizeCurrentMessage();
	}

	// Build Approve / Reject buttons
	QuestionButtonsBox = SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(8, 4, 4, 4)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.ContentPadding(FMargin(12.0f, 6.0f))
			.OnClicked_Lambda([this]()
			{
				OnPlanApprovalClicked(true);
				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.15f, 0.55f, 0.15f, 0.9f))
				.Padding(FMargin(12.0f, 6.0f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Approve")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
					.ColorAndOpacity(FSlateColor(FLinearColor::White))
				]
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 4, 8, 4)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.ContentPadding(FMargin(12.0f, 6.0f))
			.OnClicked_Lambda([this]()
			{
				OnPlanApprovalClicked(false);
				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.55f, 0.15f, 0.15f, 0.9f))
				.Padding(FMargin(12.0f, 6.0f))
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Reject")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
					.ColorAndOpacity(FSlateColor(FLinearColor::White))
				]
			]
		];

	if (OutputScrollBox.IsValid())
	{
		OutputScrollBox->AddSlot()
		.Padding(0, 4, 0, 4)
		[
			QuestionButtonsBox.ToSharedRef()
		];
	}

	SetStatus(TEXT("Waiting for plan approval..."));
	bNeedsScroll = true;
}

void SArborChatWidget::OnPlanApprovalClicked(bool bApproved)
{
	// Remove the approval buttons from the UI
	if (QuestionButtonsBox.IsValid() && OutputScrollBox.IsValid())
	{
		OutputScrollBox->RemoveSlot(QuestionButtonsBox.ToSharedRef());
		QuestionButtonsBox.Reset();
	}

	FString Decision = bApproved ? TEXT("Approved") : TEXT("Rejected");
	AddMessage(EArborMessageType::User, Decision);
	FinalizeCurrentMessage();

	// Clear pending state
	PendingToolUseId.Empty();

	if (bApproved)
	{
		// Switch from plan mode to edit mode and restart the Claude process.
		// The subprocess was launched with --permission-mode plan, so we must
		// restart without it. Resuming the same session preserves conversation context.
		bPlanMode = false;
		bPendingPlanExecution = true;
		FString ResumeId = SessionId;
		CleanUpProcess();
		AddMessage(EArborMessageType::System, TEXT("Plan approved — switching to edit mode..."));
		FinalizeCurrentMessage();
		StartClaudeProcess(ResumeId);
	}
	else
	{
		// Rejected — stay in plan mode, send rejection as a user message so Claude can revise
		SendUserMessage(Decision);
	}
}

// ─── User message handler (tool results) ─────────────────────────────────────

void SArborChatWidget::HandleUserMessage(const TSharedPtr<FJsonObject>& JsonObj)
{
	const TSharedPtr<FJsonObject>* MessageObj = nullptr;
	if (!JsonObj->TryGetObjectField(TEXT("message"), MessageObj) || !MessageObj)
	{
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
	if (!(*MessageObj)->TryGetArrayField(TEXT("content"), ContentArray) || !ContentArray)
	{
		return;
	}

	for (const TSharedPtr<FJsonValue>& ContentItem : *ContentArray)
	{
		const TSharedPtr<FJsonObject>* ContentObj = nullptr;
		if (!ContentItem->TryGetObject(ContentObj) || !ContentObj)
		{
			continue;
		}

		FString ContentType;
		if (!(*ContentObj)->TryGetStringField(TEXT("type"), ContentType) || ContentType != TEXT("tool_result"))
		{
			continue;
		}

		FString ResultText;
		if (!(*ContentObj)->TryGetStringField(TEXT("content"), ResultText))
		{
			// Try array format: [{"type": "text", "text": "..."}]
			const TArray<TSharedPtr<FJsonValue>>* InnerArray = nullptr;
			if ((*ContentObj)->TryGetArrayField(TEXT("content"), InnerArray) && InnerArray)
			{
				for (const TSharedPtr<FJsonValue>& InnerItem : *InnerArray)
				{
					const TSharedPtr<FJsonObject>* InnerObj = nullptr;
					if (InnerItem->TryGetObject(InnerObj) && InnerObj)
					{
						FString InnerText;
						if ((*InnerObj)->TryGetStringField(TEXT("text"), InnerText))
						{
							ResultText += InnerText;
						}
					}
				}
			}
		}

		if (!ResultText.IsEmpty())
		{
			// Check for screenshot path in tool result
			FString ScreenshotPath;
			if (ExtractImagePath(ResultText, ScreenshotPath) && !ShownImageLinks.Contains(ScreenshotPath))
			{
				ShownImageLinks.Add(ScreenshotPath);
				FString FileName = FPaths::GetCleanFilename(ScreenshotPath);
				FString CapturedPath = ScreenshotPath;

				FinalizeCurrentMessage();
				OutputScrollBox->AddSlot()
				.Padding(8.0f, 1.0f, 4.0f, 1.0f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.ContentPadding(FMargin(0))
					.OnClicked_Lambda([this, CapturedPath]()
					{
						OpenImageWindow(CapturedPath);
						return FReply::Handled();
					})
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
						.BorderBackgroundColor(FLinearColor(0.16f, 0.16f, 0.18f, 0.7f))
						.Padding(FMargin(8.0f, 3.0f, 10.0f, 3.0f))
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0, 0, 5, 0)
							[
								SNew(SBox)
								.WidthOverride(5.0f)
								.HeightOverride(5.0f)
								[
									SNew(SBorder)
									.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
									.BorderBackgroundColor(FLinearColor(0.3f, 0.6f, 1.0f))
								]
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::FromString(FString::Printf(TEXT("View Screenshot: %s"), *FileName)))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.3f, 0.6f, 1.0f)))
							]
						]
					]
				];
				bNeedsScroll = true;
			}

			if (bDebugMode)
			{
				if (ResultText.Len() > 500)
				{
					ResultText = ResultText.Left(500) + TEXT("...");
				}
				AddMessage(EArborMessageType::System, ResultText);
				FinalizeCurrentMessage();
			}
		}
	}

	if (!CurrentToolName.IsEmpty())
	{
		SetStatus(TEXT("Claude is thinking..."));
	}
}

// ─── Result message handler ──────────────────────────────────────────────────

void SArborChatWidget::HandleResultMessage(const TSharedPtr<FJsonObject>& JsonObj)
{
	bWaitingForResponse = false;
	CurrentToolName.Empty();
	SetStatus(TEXT(""));
	FinalizeCurrentMessage();

	const TSharedPtr<FJsonObject>* UsageObj = nullptr;
	if (JsonObj->TryGetObjectField(TEXT("usage"), UsageObj) && UsageObj)
	{
		double InputTokens = 0;
		(*UsageObj)->TryGetNumberField(TEXT("input_tokens"), InputTokens);
		if (InputTokens > 0)
		{
			const UArborSettings* UsageSettings = GetDefault<UArborSettings>();
			double MaxContext = UsageSettings ? (double)UsageSettings->MaxContextTokens : 200000.0;
			double Pct = InputTokens / MaxContext * 100.0;
			if (Pct >= 90.0)
			{
				AddMessage(EArborMessageType::System,
					FString::Printf(TEXT("Context usage: %.0f%% (%dk / %dk tokens). Consider starting a new session."),
						Pct, (int32)(InputTokens / 1000.0), (int32)(MaxContext / 1000.0)));
				FinalizeCurrentMessage();
			}
		}
	}
}

// ─── Tick ─────────────────────────────────────────────────────────────────────

void SArborChatWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	UpdateConnectionStatus();

	// Toggle Send/Stop button visibility
	// When a question is pending, show Send (for freeform answer) instead of Stop
	bool bShowSend = !bWaitingForResponse || !PendingToolUseId.IsEmpty();
	if (SendButton.IsValid())
	{
		SendButton->SetVisibility(bShowSend ? EVisibility::Visible : EVisibility::Collapsed);
		SendButton->SetEnabled(bProcessRunning && bInitialized);
	}
	if (StopButton.IsValid())
	{
		StopButton->SetVisibility(bShowSend ? EVisibility::Collapsed : EVisibility::Visible);
	}

	// Pulsing status dot animation
	StatusAnimTime += InDeltaTime;
	if (StatusDot.IsValid())
	{
		bool bActive = bWaitingForResponse || !CurrentToolName.IsEmpty();
		if (bActive)
		{
			// Smooth pulse using sine wave (0.3 to 0.9 alpha over ~1s)
			float Alpha = 0.3f + 0.6f * (0.5f + 0.5f * FMath::Sin(StatusAnimTime * 4.0f));
			StatusDot->SetBorderBackgroundColor(FLinearColor(0.4f, 0.8f, 0.4f, Alpha));
		}
		else
		{
			StatusDot->SetBorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		}
	}

	// Detect whether user has scrolled away from the bottom.
	// Do this in Tick (after layout) so offsets are stable.
	if (OutputScrollBox.IsValid() && !bIsAutoScrolling)
	{
		float ScrollEnd = OutputScrollBox->GetScrollOffsetOfEnd();
		float Current = OutputScrollBox->GetScrollOffset();
		bool bAtBottom = (ScrollEnd - Current) < 30.0f;

		// Re-enable auto-scroll when user scrolls back to bottom
		if (bAtBottom && !bAutoScroll)
		{
			bAutoScroll = true;
		}
		// Disable only when genuinely scrolled up AND we're not at the bottom
		else if (!bAtBottom && bAutoScroll && !bNeedsScroll)
		{
			bAutoScroll = false;
		}
	}

	// Scroll-to-bottom button visibility
	if (ScrollToBottomButton.IsValid())
	{
		bool bShowScrollButton = !bAutoScroll && bWaitingForResponse;
		ScrollToBottomButton->SetVisibility(bShowScrollButton ? EVisibility::Visible : EVisibility::Collapsed);
	}

	// Deferred scroll
	if (bNeedsScroll)
	{
		bNeedsScroll = false;
		if (bAutoScroll && OutputScrollBox.IsValid())
		{
			bIsAutoScrolling = true;
			OutputScrollBox->ScrollToEnd();
			bIsAutoScrolling = false;
		}
	}

	if (!bProcessRunning)
	{
		return;
	}

	if (bNeedsSendInit)
	{
		bNeedsSendInit = false;
		WriteToPipe(TEXT("{\"type\":\"control_request\",\"request_id\":\"req_init\",\"request\":{\"subtype\":\"initialize\",\"hooks\":null,\"agents\":null}}"));
	}

	// Check if process died unexpectedly
	if (!FPlatformProcess::IsProcRunning(ClaudeProcess))
	{
		if (StdOutReadPipe != nullptr)
		{
			FString Output = FPlatformProcess::ReadPipe(StdOutReadPipe);
			if (!Output.IsEmpty())
			{
				StdOutLineBuffer.Append(Output);
			}
		}

		FString Remaining = StdOutLineBuffer;
		StdOutLineBuffer.Empty();
		TArray<FString> Lines;
		Remaining.ParseIntoArray(Lines, TEXT("\n"), false);
		for (const FString& ParsedLine : Lines)
		{
			FString Trimmed = ParsedLine.TrimStartAndEnd();
			if (!Trimmed.IsEmpty())
			{
				ProcessJsonLine(Trimmed);
			}
		}

		int32 ReturnCode = -1;
		FPlatformProcess::GetProcReturnCode(ClaudeProcess, &ReturnCode);
		CleanUpProcess();
		SetStatus(TEXT(""));
		FinalizeCurrentMessage();
		AddMessage(EArborMessageType::System, FString::Printf(TEXT("Claude process exited (code %d). Click 'New Session' to restart."), ReturnCode));
		FinalizeCurrentMessage();
		return;
	}

	// Read stdout (non-blocking)
	if (StdOutReadPipe != nullptr)
	{
		FString Output = FPlatformProcess::ReadPipe(StdOutReadPipe);
		if (!Output.IsEmpty())
		{
			StdOutLineBuffer.Append(Output);

			int32 NewlineIdx;
			while (StdOutLineBuffer.FindChar(TEXT('\n'), NewlineIdx))
			{
				FString ParsedLine = StdOutLineBuffer.Left(NewlineIdx).TrimStartAndEnd();
				StdOutLineBuffer = StdOutLineBuffer.Mid(NewlineIdx + 1);

				if (!ParsedLine.IsEmpty())
				{
					ProcessJsonLine(ParsedLine);
				}
			}
		}
	}
}

// ─── Input handling ───────────────────────────────────────────────────────────

void SArborChatWidget::OnSendClicked()
{
	if (!InputTextBox.IsValid())
	{
		return;
	}

	FString UserInput = InputTextBox->GetText().ToString();
	if (UserInput.IsEmpty())
	{
		return;
	}

	if (!bProcessRunning)
	{
		AddMessage(EArborMessageType::System, TEXT("Claude is not running. Click 'New Session' to restart."));
		FinalizeCurrentMessage();
		return;
	}

	if (!bInitialized)
	{
		AddMessage(EArborMessageType::System, TEXT("Waiting for Claude to initialize..."));
		FinalizeCurrentMessage();
		return;
	}

	// If there's a pending question, send the typed text as a freeform answer
	if (!PendingToolUseId.IsEmpty())
	{
		OnQuestionOptionClicked(UserInput);
		InputTextBox->SetText(FText::GetEmpty());
		return;
	}

	if (bWaitingForResponse)
	{
		return;
	}

	FinalizeCurrentMessage();
	AddMessage(EArborMessageType::User, UserInput);
	FinalizeCurrentMessage();
	InputTextBox->SetText(FText::GetEmpty());
	bAutoScroll = true;
	SendUserMessage(UserInput);
}

FReply SArborChatWidget::OnInputKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Enter && !InKeyEvent.IsShiftDown())
	{
		OnSendClicked();
		return FReply::Handled();
	}
	if (InKeyEvent.GetKey() == EKeys::Escape && bWaitingForResponse)
	{
		InterruptClaude();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SArborChatWidget::SetStatus(const FString& Text)
{
	if (StatusTextBlock.IsValid())
	{
		StatusTextBlock->SetText(FText::FromString(Text));
	}
}

#undef LOCTEXT_NAMESPACE
