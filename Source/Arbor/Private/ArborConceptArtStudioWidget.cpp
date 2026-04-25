#include "ArborConceptArtStudioWidget.h"
#include "ArborClaude.h"
#include "ArborCodexStyle.h"
#include "ArborCodexWidgetHelpers.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Images/SImage.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Styling/AppStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "DesktopPlatformModule.h"
#include "UObject/UnrealType.h"

static constexpr float TILE_WIDTH = 360.0f;
static constexpr float STYLE_THUMB_SIZE = 80.0f;

#define LOCTEXT_NAMESPACE "ArborConceptArtStudio"

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void SArborConceptArtStudioWidget::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(ArborCodexStyle::Bg::Panel)
		.Padding(0.0f)
		[
			SAssignNew(StepSwitcher, SWidgetSwitcher)
			.WidgetIndex(0)

			// Slot 0: Main (Context + Prompt Review combined)
			+ SWidgetSwitcher::Slot()
			[
				BuildMainPanel()
			]

			// Slot 1: Generating
			+ SWidgetSwitcher::Slot()
			[
				BuildGeneratingPanel()
			]

			// Slot 2: Results
			+ SWidgetSwitcher::Slot()
			[
				BuildResultsPanel()
			]

			// Slot 3: Done
			+ SWidgetSwitcher::Slot()
			[
				BuildDonePanel()
			]
		]
	];
}

// ---------------------------------------------------------------------------
// Step panels
// ---------------------------------------------------------------------------

TSharedRef<SWidget> SArborConceptArtStudioWidget::BuildMainPanel()
{
	// Build the prompt section (initially hidden, shown when AI sends prompt)
	PromptSection = SNew(SVerticalBox)
		.Visibility(EVisibility::Collapsed)

		// Separator
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 12.0f, 0.0f, 8.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(ArborCodexStyle::Border::Default)
			.Padding(0.0f)
			[
				SNew(SBox).HeightOverride(1.0f)
			]
		]

		// Prompt label
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PromptLabel", "Generation Prompt"))
			.Font(ArborCodexStyle::Font::SectionHeader())
			.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PromptHint", "Edit the prompt below if needed, then approve to start generation."))
			.Font(ArborCodexStyle::Font::Caption())
			.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Muted))
			.AutoWrapText(true)
		]

		// Prompt editor
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.MinDesiredHeight(120.0f)
			.MaxDesiredHeight(300.0f)
			[
				SAssignNew(PromptEditor, SMultiLineEditableTextBox)
				.Font(ArborCodexStyle::Font::Input())
				.AutoWrapText(true)
			]
		]

		// Approve / Cancel buttons
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				ArborCodexHelpers::MakeAIButton(
					LOCTEXT("ApproveGenerate", "Approve & Generate"),
					FOnClicked::CreateSP(this, &SArborConceptArtStudioWidget::OnApprovePrompt)
				)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				ArborCodexHelpers::MakeSecondaryButton(
					LOCTEXT("CancelPrompt", "Cancel"),
					FOnClicked::CreateSP(this, &SArborConceptArtStudioWidget::OnCancel)
				)
			]
		];

	return SNew(SScrollBox)
		+ SScrollBox::Slot()
		.Padding(ArborCodexStyle::Spacing::PagePadding)
		[
			SNew(SVerticalBox)

			// Title
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("StudioTitle", "Concept Art Studio"))
				.Font(ArborCodexStyle::Font::PageHeader())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
			]

			// Codex name
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("EntryLabel", "Codex Entry"))
					.Font(ArborCodexStyle::Font::FieldLabel())
					.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Secondary))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					SAssignNew(ContextNameLabel, STextBlock)
					.Text(LOCTEXT("NoEntry", "No codex entry loaded"))
					.Font(ArborCodexStyle::Font::SectionHeader())
					.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
				]
			]

			// Description
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("DescLabel", "Description"))
					.Font(ArborCodexStyle::Font::FieldLabel())
					.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Secondary))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					SAssignNew(ContextDescLabel, STextBlock)
					.AutoWrapText(true)
					.Font(ArborCodexStyle::Font::Body())
					.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
				]
			]

			// Style images
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 4.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("StyleImagesLabel", "Style Reference Images"))
						.Font(ArborCodexStyle::Font::FieldLabel())
						.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Secondary))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						ArborCodexHelpers::MakeSecondaryButton(
							LOCTEXT("BrowseStyle", "Browse..."),
							FOnClicked::CreateSP(this, &SArborConceptArtStudioWidget::OnAddStyleImageFromDisk)
						)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SAssignNew(StyleImageGrid, SBox)
				]
			]

			// Num images slider
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NumImagesLabel", "Number of images: "))
					.Font(ArborCodexStyle::Font::FieldLabel())
					.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Secondary))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(80.0f)
					[
						SAssignNew(NumImagesSpinBox, SSpinBox<int32>)
						.Value(4)
						.MinValue(1)
						.MaxValue(8)
						.MinSliderValue(1)
						.MaxSliderValue(8)
						.OnValueChanged_Lambda([this](int32 Value) { NumImages = Value; })
					]
				]
			]

			// Generate Prompt button (shown initially, hidden while waiting / after prompt arrives)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 16.0f, 0.0f, 0.0f)
			[
				SAssignNew(GeneratePromptSection, SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					ArborCodexHelpers::MakeAIButton(
						LOCTEXT("GeneratePrompt", "Generate Prompt with AI"),
						FOnClicked::CreateSP(this, &SArborConceptArtStudioWidget::OnGeneratePrompt)
					)
				]
			]

			// Status (shown while waiting for AI to generate prompt)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				SAssignNew(ContextStatusLabel, STextBlock)
				.Text(LOCTEXT("WaitingForAI", "Generating prompt..."))
				.Font(ArborCodexStyle::Font::Caption())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Muted))
				.Visibility(EVisibility::Collapsed)
			]

			// Prompt section (revealed when AI sends prompt)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				PromptSection.ToSharedRef()
			]
		];
}

void SArborConceptArtStudioWidget::SetPromptSectionVisible(bool bVisible)
{
	if (PromptSection.IsValid())
	{
		PromptSection->SetVisibility(bVisible ? EVisibility::Visible : EVisibility::Collapsed);
	}
	// When prompt is visible, hide both the generate button and status label
	if (GeneratePromptSection.IsValid())
	{
		GeneratePromptSection->SetVisibility(bVisible ? EVisibility::Collapsed : EVisibility::Visible);
	}
	if (ContextStatusLabel.IsValid())
	{
		ContextStatusLabel->SetVisibility(EVisibility::Collapsed);
	}
}

TSharedRef<SWidget> SArborConceptArtStudioWidget::BuildGeneratingPanel()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SAssignNew(GeneratingStatusLabel, STextBlock)
				.Text(LOCTEXT("Generating", "Generating concept art..."))
				.Font(ArborCodexStyle::Font::SectionHeader())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 16.0f, 0.0f, 0.0f)
			[
				ArborCodexHelpers::MakeDangerButton(
					LOCTEXT("CancelGenerate", "Cancel"),
					FOnClicked::CreateSP(this, &SArborConceptArtStudioWidget::OnCancel)
				)
			]
		];
}

TSharedRef<SWidget> SArborConceptArtStudioWidget::BuildResultsPanel()
{
	return SNew(SVerticalBox)

		// Title + status
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(ArborCodexStyle::Spacing::SectionPadding)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ResultsTitle", "Select Concept Art"))
				.Font(ArborCodexStyle::Font::PageHeader())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SAssignNew(ResultsStatusLabel, STextBlock)
				.Font(ArborCodexStyle::Font::Caption())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Muted))
			]
		]

		// Image grid (scrollable)
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8.0f, 4.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(ResultsGridContainer, SBox)
			]
		]

		// Feedback text box
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(ArborCodexStyle::Spacing::SectionPadding)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FeedbackLabel", "Feedback for regeneration"))
				.Font(ArborCodexStyle::Font::FieldLabel())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Secondary))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.MinDesiredHeight(60.0f)
				.MaxDesiredHeight(120.0f)
				[
					SAssignNew(FeedbackEditor, SMultiLineEditableTextBox)
					.HintText(LOCTEXT("FeedbackHint", "Optional: describe changes you'd like..."))
					.Font(ArborCodexStyle::Font::Input())
					.AutoWrapText(true)
				]
			]
		]

		// Buttons
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(ArborCodexStyle::Spacing::SectionPadding)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				ArborCodexHelpers::MakePrimaryButton(
					LOCTEXT("UseSelected", "Use Selected"),
					FOnClicked::CreateSP(this, &SArborConceptArtStudioWidget::OnUseSelected)
				)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				ArborCodexHelpers::MakeAIButton(
					LOCTEXT("Regenerate", "Regenerate"),
					FOnClicked::CreateSP(this, &SArborConceptArtStudioWidget::OnRegenerate)
				)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				ArborCodexHelpers::MakeSecondaryButton(
					LOCTEXT("CancelResults", "Cancel"),
					FOnClicked::CreateSP(this, &SArborConceptArtStudioWidget::OnCancel)
				)
			]
		];
}

TSharedRef<SWidget> SArborConceptArtStudioWidget::BuildDonePanel()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(ArborCodexStyle::Spacing::PagePadding)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SAssignNew(DoneImageContainer, SBox)
				.WidthOverride(400.0f)
				.HeightOverride(400.0f)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 12.0f, 0.0f, 0.0f)
			[
				SAssignNew(DoneStatusLabel, STextBlock)
				.Text(LOCTEXT("ImportDone", "Concept art imported successfully!"))
				.Font(ArborCodexStyle::Font::SectionHeader())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 16.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					ArborCodexHelpers::MakeAIButton(
						LOCTEXT("GenerateMore", "Generate More"),
						FOnClicked::CreateSP(this, &SArborConceptArtStudioWidget::OnGenerateMore)
					)
				]
			]
		];
}

// ---------------------------------------------------------------------------
// State management
// ---------------------------------------------------------------------------

void SArborConceptArtStudioWidget::SetStep(EStep Step)
{
	CurrentStep = Step;
	if (StepSwitcher.IsValid())
	{
		StepSwitcher->SetActiveWidgetIndex(static_cast<int32>(Step));
	}
}

void SArborConceptArtStudioWidget::UpdateFromState(const FString& JsonManifest)
{
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonManifest);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Arbor ConceptArtStudio: Failed to parse JSON state"));
		return;
	}

	// Read data fields
	Root->TryGetStringField(TEXT("codex_asset_path"), CodexAssetPath);
	Root->TryGetStringField(TEXT("codex_name"), CodexName);
	Root->TryGetStringField(TEXT("codex_description"), CodexDescription);
	Root->TryGetStringField(TEXT("prompt"), Prompt);
	Root->TryGetStringField(TEXT("user_feedback"), UserFeedback);
	Root->TryGetNumberField(TEXT("num_images"), NumImages);
	Root->TryGetNumberField(TEXT("selected_index"), SelectedIndex);

	// Style images
	const TArray<TSharedPtr<FJsonValue>>* StyleArr;
	if (Root->TryGetArrayField(TEXT("style_images"), StyleArr))
	{
		StyleImagePaths.Empty();
		for (const auto& Val : *StyleArr)
		{
			FString Path;
			if (Val->TryGetString(Path))
			{
				StyleImagePaths.Add(Path);
			}
		}
	}

	// Result images
	const TArray<TSharedPtr<FJsonValue>>* ImagesArr;
	if (Root->TryGetArrayField(TEXT("images"), ImagesArr))
	{
		// Clear old
		ImageEntries.Empty();
		ImageBorders.Empty();

		for (const auto& ImageValue : *ImagesArr)
		{
			const TSharedPtr<FJsonObject>& ImageObj = ImageValue->AsObject();
			if (!ImageObj.IsValid()) continue;

			FImageEntry Entry;
			Entry.Path = ImageObj->GetStringField(TEXT("path"));
			Entry.Label = ImageObj->GetStringField(TEXT("label"));
			Entry.Texture = LoadImageFromDisk(Entry.Path);
			if (Entry.Texture)
			{
				Entry.Brush = CreateBrushFromTexture(Entry.Texture, (int32)TILE_WIDTH);
				PinnedTextures.AddUnique(Entry.Texture);
			}
			ImageEntries.Add(MoveTemp(Entry));
		}
	}

	// Determine step
	FString StepStr;
	Root->TryGetStringField(TEXT("step"), StepStr);

	if (StepStr == TEXT("context"))
	{
		SetStep(EStep::Main);
		SetPromptSectionVisible(false);

		// This comes from the MCP bridge after Claude was asked to generate — show waiting status
		if (GeneratePromptSection.IsValid())
		{
			GeneratePromptSection->SetVisibility(EVisibility::Collapsed);
		}
		if (ContextStatusLabel.IsValid())
		{
			ContextStatusLabel->SetVisibility(EVisibility::Visible);
			ContextStatusLabel->SetText(LOCTEXT("GeneratingPrompt", "Generating prompt with AI..."));
		}

		// Update context panel
		if (ContextNameLabel.IsValid())
		{
			ContextNameLabel->SetText(FText::FromString(CodexName));
		}
		if (ContextDescLabel.IsValid())
		{
			ContextDescLabel->SetText(FText::FromString(CodexDescription));
		}
		if (NumImagesSpinBox.IsValid())
		{
			NumImagesSpinBox->SetValue(NumImages);
		}

		// Load style images
		RebuildStyleImageGrid();
	}
	else if (StepStr == TEXT("prompt_review"))
	{
		SetStep(EStep::Main);
		SetPromptSectionVisible(true);

		// Update context info too
		if (ContextNameLabel.IsValid())
		{
			ContextNameLabel->SetText(FText::FromString(CodexName));
		}
		if (ContextDescLabel.IsValid())
		{
			ContextDescLabel->SetText(FText::FromString(CodexDescription));
		}
		if (NumImagesSpinBox.IsValid())
		{
			NumImagesSpinBox->SetValue(NumImages);
		}

		RebuildStyleImageGrid();

		if (PromptEditor.IsValid())
		{
			PromptEditor->SetText(FText::FromString(Prompt));
		}
	}
	else if (StepStr == TEXT("generating"))
	{
		SetStep(EStep::Generating);

		if (GeneratingStatusLabel.IsValid())
		{
			GeneratingStatusLabel->SetText(LOCTEXT("Generating", "Generating concept art..."));
		}
	}
	else if (StepStr == TEXT("results"))
	{
		SetStep(EStep::Results);
		SelectedIndex = -1;

		if (ResultsStatusLabel.IsValid())
		{
			ResultsStatusLabel->SetText(FText::FromString(FString::Printf(
				TEXT("%d images generated. Click to select, double-click to enlarge."),
				ImageEntries.Num())));
		}
		if (FeedbackEditor.IsValid())
		{
			FeedbackEditor->SetText(FText::GetEmpty());
		}

		RebuildResultsGrid();
	}
	else if (StepStr == TEXT("done"))
	{
		SetStep(EStep::Done);

		if (DoneStatusLabel.IsValid())
		{
			DoneStatusLabel->SetText(LOCTEXT("ImportDone", "Concept art imported successfully!"));
		}

		// Show selected image in done panel
		if (SelectedIndex >= 0 && SelectedIndex < ImageEntries.Num() && DoneImageContainer.IsValid())
		{
			const FImageEntry& Entry = ImageEntries[SelectedIndex];
			if (Entry.Brush.IsValid())
			{
				DoneImageContainer->SetContent(
					SNew(SImage).Image(Entry.Brush.Get())
				);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Arbor ConceptArtStudio: Updated to step '%s' for '%s'"),
		*StepStr, *CodexName);
}

// ---------------------------------------------------------------------------
// Style image grid
// ---------------------------------------------------------------------------

void SArborConceptArtStudioWidget::RebuildStyleImageGrid()
{
	if (!StyleImageGrid.IsValid()) return;

	// Build entries from paths
	StyleImageEntries.Empty();
	for (const FString& Path : StyleImagePaths)
	{
		FStyleImageEntry Entry;
		Entry.Path = Path;
		Entry.bSelected = true;

		// Try to load from disk (for absolute paths to generated images)
		Entry.Texture = LoadImageFromDisk(Path);
		if (Entry.Texture)
		{
			Entry.Brush = CreateBrushFromTexture(Entry.Texture, (int32)STYLE_THUMB_SIZE);
			PinnedTextures.AddUnique(Entry.Texture);
		}
		StyleImageEntries.Add(MoveTemp(Entry));
	}

	if (StyleImageEntries.Num() == 0)
	{
		StyleImageGrid->SetContent(
			SNew(STextBlock)
			.Text(LOCTEXT("NoStyleImages", "No style images. Use Browse to add reference images."))
			.Font(ArborCodexStyle::Font::Caption())
			.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Muted))
		);
		return;
	}

	TSharedPtr<SWrapBox> Grid = SNew(SWrapBox).UseAllottedSize(true);

	for (int32 i = 0; i < StyleImageEntries.Num(); i++)
	{
		const FStyleImageEntry& Entry = StyleImageEntries[i];

		const FLinearColor BorderColor = Entry.bSelected
			? ArborCodexStyle::Accent::Primary
			: ArborCodexStyle::Border::Default;

		TSharedRef<SWidget> ImageContent = Entry.Brush.IsValid()
			? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Entry.Brush.Get()))
			: StaticCastSharedRef<SWidget>(
				SNew(STextBlock)
				.Text(FText::FromString(FPaths::GetCleanFilename(Entry.Path)))
				.Justification(ETextJustify::Center)
				.Font(ArborCodexStyle::Font::Caption()));

		Grid->AddSlot()
		.Padding(4.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(BorderColor)
			.Padding(2.0f)
			.Cursor(EMouseCursor::Hand)
			.OnMouseButtonDown_Lambda([this, i](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
			{
				if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && i < StyleImageEntries.Num())
				{
					StyleImageEntries[i].bSelected = !StyleImageEntries[i].bSelected;
					RebuildStyleImageGrid();
					return FReply::Handled();
				}
				return FReply::Unhandled();
			})
			[
				SNew(SBox)
				.WidthOverride(STYLE_THUMB_SIZE)
				.HeightOverride(STYLE_THUMB_SIZE)
				[
					ImageContent
				]
			]
		];
	}

	StyleImageGrid->SetContent(Grid.ToSharedRef());
}

// ---------------------------------------------------------------------------
// Results image grid (pattern from SArborTextureReviewWidget)
// ---------------------------------------------------------------------------

void SArborConceptArtStudioWidget::RebuildResultsGrid()
{
	ImageBorders.Empty();

	if (!ResultsGridContainer.IsValid()) return;

	TSharedPtr<SWrapBox> Grid = SNew(SWrapBox).UseAllottedSize(true);

	for (int32 i = 0; i < ImageEntries.Num(); i++)
	{
		const FImageEntry& Entry = ImageEntries[i];
		TSharedPtr<SBorder> Border;

		const float TileHeight = Entry.Texture
			? TILE_WIDTH * Entry.Texture->GetSizeY() / FMath::Max(1, Entry.Texture->GetSizeX())
			: TILE_WIDTH;

		TSharedRef<SWidget> ImageContent = Entry.Brush.IsValid()
			? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Entry.Brush.Get()))
			: StaticCastSharedRef<SWidget>(
				SNew(STextBlock)
				.Text(LOCTEXT("LoadFailed", "Failed to load"))
				.Justification(ETextJustify::Center));

		Grid->AddSlot()
		.Padding(4.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(Border, SBorder)
				.BorderImage(FAppStyle::GetBrush("Border"))
				.BorderBackgroundColor(FLinearColor(0.15f, 0.15f, 0.15f, 1.0f))
				.Padding(3.0f)
				.Cursor(EMouseCursor::Hand)
				.OnMouseButtonDown_Lambda([this, i](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
				{
					if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
					{
						OnImageClicked(i);
						return FReply::Handled();
					}
					return FReply::Unhandled();
				})
				.OnMouseDoubleClick_Lambda([this, i](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
				{
					if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
					{
						if (i >= 0 && i < ImageEntries.Num() && ImageEntries[i].Texture)
						{
							OpenFullSizeViewer(ImageEntries[i].Texture);
						}
						return FReply::Handled();
					}
					return FReply::Unhandled();
				})
				[
					SNew(SBox)
					.WidthOverride(TILE_WIDTH)
					.HeightOverride(TileHeight)
					[
						ImageContent
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(TILE_WIDTH)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Entry.Label))
					.Justification(ETextJustify::Center)
					.Font(ArborCodexStyle::Font::Body())
					.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Secondary))
				]
			]
		];

		ImageBorders.Add(Border);
	}

	ResultsGridContainer->SetContent(Grid.ToSharedRef());
}

void SArborConceptArtStudioWidget::UpdateSelectionHighlight()
{
	for (int32 i = 0; i < ImageBorders.Num(); i++)
	{
		if (ImageBorders[i].IsValid())
		{
			FLinearColor Color = (i == SelectedIndex)
				? ArborCodexStyle::Accent::Primary
				: FLinearColor(0.15f, 0.15f, 0.15f, 1.0f);
			ImageBorders[i]->SetBorderBackgroundColor(Color);
		}
	}
}

// ---------------------------------------------------------------------------
// OpenForAsset — called from image panel to set up without triggering Claude
// ---------------------------------------------------------------------------

void SArborConceptArtStudioWidget::OpenForAsset(const FString& InAssetPath)
{
	CodexAssetPath = InAssetPath;

	// Extract name from asset path (e.g. "Pillar_Build_Synergy" → "Build Synergy")
	CodexName = FPaths::GetBaseFilename(InAssetPath);
	CodexName.ReplaceInline(TEXT("_"), TEXT(" "));

	// Try to load description from asset
	CodexDescription.Empty();
	if (UObject* Obj = StaticLoadObject(UObject::StaticClass(), nullptr, *InAssetPath))
	{
		// Try common description field names
		for (const FName& FieldName : { FName("Description"), FName("WorldDescription"), FName("Setting") })
		{
			if (FProperty* Prop = Obj->GetClass()->FindPropertyByName(FieldName))
			{
				if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
				{
					CodexDescription = StrProp->GetPropertyValue_InContainer(Obj);
					if (!CodexDescription.IsEmpty()) break;
				}
			}
		}
	}

	SetStep(EStep::Main);
	SetPromptSectionVisible(false);

	// Show the Generate Prompt button, hide status
	if (GeneratePromptSection.IsValid())
	{
		GeneratePromptSection->SetVisibility(EVisibility::Visible);
	}
	if (ContextStatusLabel.IsValid())
	{
		ContextStatusLabel->SetVisibility(EVisibility::Collapsed);
	}

	if (ContextNameLabel.IsValid())
	{
		ContextNameLabel->SetText(FText::FromString(CodexName));
	}
	if (ContextDescLabel.IsValid())
	{
		ContextDescLabel->SetText(FText::FromString(CodexDescription));
	}
	if (NumImagesSpinBox.IsValid())
	{
		NumImagesSpinBox->SetValue(NumImages);
	}

	RebuildStyleImageGrid();
}

// ---------------------------------------------------------------------------
// User actions
// ---------------------------------------------------------------------------

FReply SArborConceptArtStudioWidget::OnGeneratePrompt()
{
	// Hide Generate Prompt button, show waiting status
	if (GeneratePromptSection.IsValid())
	{
		GeneratePromptSection->SetVisibility(EVisibility::Collapsed);
	}
	if (ContextStatusLabel.IsValid())
	{
		ContextStatusLabel->SetVisibility(EVisibility::Visible);
		ContextStatusLabel->SetText(LOCTEXT("GeneratingPrompt", "Generating prompt with AI..."));
	}

	// Collect selected style image paths for the prompt
	StyleImagePaths.Empty();
	for (const FStyleImageEntry& Entry : StyleImageEntries)
	{
		if (Entry.bSelected)
		{
			StyleImagePaths.Add(Entry.Path);
		}
	}
	if (NumImagesSpinBox.IsValid())
	{
		NumImages = NumImagesSpinBox->GetValue();
	}

	// Send message to Claude to generate a prompt
	FString AiPrompt = FString::Printf(
		TEXT("Generate concept art for this codex entry using the Concept Art Studio. Follow these steps:\n"
			 "1. Open the studio: ue5_run_python with arbor.concept_art_studio.open_studio(\"%s\")\n"
			 "2. Read the codex entry: ue5_codex_query(action=\"get\", asset_path=\"%s\")\n"
			 "3. Craft a detailed image generation prompt from the entry data (name, description, category, tags, etc.)\n"
			 "4. Send prompt to studio for user review: ue5_run_python with arbor.concept_art_studio.set_prompt(prompt)\n"
			 "5. Poll for user approval: ue5_run_python with arbor.concept_art_studio.get_approval()\n"
			 "   - If action is empty, the user hasn't responded yet — poll again after a moment\n"
			 "   - If action is \"approve_prompt\", proceed with the (possibly edited) prompt and num_images\n"
			 "   - If action is \"cancel\", stop\n"
			 "6. Generate images: fal_generate_image(prompt=approved_prompt, num_images=num_images)\n"
			 "7. Send results to studio: ue5_run_python with arbor.concept_art_studio.set_results(images)\n"
			 "   where images is a list of {{\"path\": \"...\", \"label\": \"Variant N\"}}\n"
			 "8. Poll for user selection: ue5_run_python with arbor.concept_art_studio.get_selection()\n"
			 "   - If action is empty, poll again\n"
			 "   - If action is \"select\", import the selected image\n"
			 "   - If action is \"regenerate\", go back to step 6 with user_feedback incorporated into the prompt\n"
			 "   - If action is \"cancel\", stop\n"
			 "9. Import selected image: ue5_run_python with arbor.concept_art.import_concept_art(selected_path, \"%s\", image_name)"),
		*CodexAssetPath, *CodexAssetPath, *CodexAssetPath);

	FArborClaude::SendMessage(AiPrompt);

	return FReply::Handled();
}

void SArborConceptArtStudioWidget::OnImageClicked(int32 Index)
{
	SelectedIndex = Index;
	UpdateSelectionHighlight();

	if (ResultsStatusLabel.IsValid())
	{
		ResultsStatusLabel->SetText(FText::FromString(FString::Printf(
			TEXT("Selected: %s"), *ImageEntries[Index].Label)));
	}
}

FReply SArborConceptArtStudioWidget::OnApprovePrompt()
{
	// Read possibly-edited prompt from the editor
	if (PromptEditor.IsValid())
	{
		Prompt = PromptEditor->GetText().ToString();
	}
	if (NumImagesSpinBox.IsValid())
	{
		NumImages = NumImagesSpinBox->GetValue();
	}

	// Collect selected style image paths
	StyleImagePaths.Empty();
	for (const FStyleImageEntry& Entry : StyleImageEntries)
	{
		if (Entry.bSelected)
		{
			StyleImagePaths.Add(Entry.Path);
		}
	}

	// Write state with approve action
	SetStep(EStep::Generating);
	PendingAction = TEXT("approve_prompt");
	WriteState();
	PendingAction.Empty();

	// Notify Claude to proceed with generation
	FString AiPrompt = FString::Printf(
		TEXT("The user approved the concept art prompt in the Concept Art Studio.\n"
			 "Poll for approval: ue5_run_python with arbor.concept_art_studio.get_approval()\n"
			 "The action should be \"approve_prompt\". Proceed with generation using the approved prompt and num_images.\n"
			 "Codex entry: %s"),
		*CodexAssetPath);
	FArborClaude::SendMessage(AiPrompt);

	return FReply::Handled();
}

FReply SArborConceptArtStudioWidget::OnRegenerate()
{
	if (FeedbackEditor.IsValid())
	{
		UserFeedback = FeedbackEditor->GetText().ToString();
	}

	// Write state with regenerate action
	SetStep(EStep::Generating);

	// Write state file with action = "regenerate"
	TSharedPtr<FJsonObject> State = MakeShareable(new FJsonObject());
	State->SetStringField(TEXT("step"), TEXT("generating"));
	State->SetStringField(TEXT("codex_asset_path"), CodexAssetPath);
	State->SetStringField(TEXT("codex_name"), CodexName);
	State->SetStringField(TEXT("codex_description"), CodexDescription);
	State->SetNumberField(TEXT("num_images"), NumImages);
	State->SetStringField(TEXT("prompt"), Prompt);
	State->SetStringField(TEXT("user_feedback"), UserFeedback);
	State->SetNumberField(TEXT("selected_index"), SelectedIndex);
	State->SetStringField(TEXT("action"), TEXT("regenerate"));
	State->SetNumberField(TEXT("timestamp"), FDateTime::UtcNow().ToUnixTimestamp());

	// Style images
	TArray<TSharedPtr<FJsonValue>> StyleArr;
	for (const FString& Path : StyleImagePaths)
	{
		StyleArr.Add(MakeShareable(new FJsonValueString(Path)));
	}
	State->SetArrayField(TEXT("style_images"), StyleArr);

	// Images (keep current)
	TArray<TSharedPtr<FJsonValue>> ImagesArr;
	for (const FImageEntry& Entry : ImageEntries)
	{
		TSharedPtr<FJsonObject> ImgObj = MakeShareable(new FJsonObject());
		ImgObj->SetStringField(TEXT("path"), Entry.Path);
		ImgObj->SetStringField(TEXT("label"), Entry.Label);
		ImagesArr.Add(MakeShareable(new FJsonValueObject(ImgObj)));
	}
	State->SetArrayField(TEXT("images"), ImagesArr);

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(State.ToSharedRef(), Writer);
	FFileHelper::SaveStringToFile(JsonString, *GetStatePath(),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	// Notify Claude to regenerate
	FString AiPrompt = FString::Printf(
		TEXT("The user wants to regenerate concept art in the Concept Art Studio.\n"
			 "Poll for selection: ue5_run_python with arbor.concept_art_studio.get_selection()\n"
			 "The action should be \"regenerate\". Incorporate the user feedback into the prompt and generate new images.\n"
			 "User feedback: %s\nCodex entry: %s"),
		*UserFeedback, *CodexAssetPath);
	FArborClaude::SendMessage(AiPrompt);

	UE_LOG(LogTemp, Log, TEXT("Arbor ConceptArtStudio: Regenerate requested with feedback: %s"), *UserFeedback);
	return FReply::Handled();
}

FReply SArborConceptArtStudioWidget::OnUseSelected()
{
	if (SelectedIndex < 0 || SelectedIndex >= ImageEntries.Num()) return FReply::Handled();

	// Write state with select action
	TSharedPtr<FJsonObject> State = MakeShareable(new FJsonObject());
	State->SetStringField(TEXT("step"), TEXT("results"));
	State->SetStringField(TEXT("codex_asset_path"), CodexAssetPath);
	State->SetStringField(TEXT("codex_name"), CodexName);
	State->SetNumberField(TEXT("num_images"), NumImages);
	State->SetStringField(TEXT("prompt"), Prompt);
	State->SetStringField(TEXT("user_feedback"), TEXT(""));
	State->SetNumberField(TEXT("selected_index"), SelectedIndex);
	State->SetStringField(TEXT("action"), TEXT("select"));
	State->SetNumberField(TEXT("timestamp"), FDateTime::UtcNow().ToUnixTimestamp());

	TArray<TSharedPtr<FJsonValue>> StyleArr;
	for (const FString& Path : StyleImagePaths)
	{
		StyleArr.Add(MakeShareable(new FJsonValueString(Path)));
	}
	State->SetArrayField(TEXT("style_images"), StyleArr);

	TArray<TSharedPtr<FJsonValue>> ImagesArr;
	for (const FImageEntry& Entry : ImageEntries)
	{
		TSharedPtr<FJsonObject> ImgObj = MakeShareable(new FJsonObject());
		ImgObj->SetStringField(TEXT("path"), Entry.Path);
		ImgObj->SetStringField(TEXT("label"), Entry.Label);
		ImagesArr.Add(MakeShareable(new FJsonValueObject(ImgObj)));
	}
	State->SetArrayField(TEXT("images"), ImagesArr);

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(State.ToSharedRef(), Writer);
	FFileHelper::SaveStringToFile(JsonString, *GetStatePath(),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	if (ResultsStatusLabel.IsValid())
	{
		ResultsStatusLabel->SetText(FText::FromString(FString::Printf(
			TEXT("Selected %s. Importing..."), *ImageEntries[SelectedIndex].Label)));
	}

	UE_LOG(LogTemp, Log, TEXT("Arbor ConceptArtStudio: Selected image %d for import"), SelectedIndex);
	return FReply::Handled();
}

FReply SArborConceptArtStudioWidget::OnCancel()
{
	// Write cancel to state file
	TSharedPtr<FJsonObject> State = MakeShareable(new FJsonObject());
	State->SetStringField(TEXT("step"), TEXT("context"));
	State->SetStringField(TEXT("action"), TEXT("cancel"));
	State->SetNumberField(TEXT("timestamp"), FDateTime::UtcNow().ToUnixTimestamp());
	State->SetStringField(TEXT("codex_asset_path"), CodexAssetPath);

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(State.ToSharedRef(), Writer);
	FFileHelper::SaveStringToFile(JsonString, *GetStatePath(),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	SetStep(EStep::Main);
	SetPromptSectionVisible(false);
	if (GeneratePromptSection.IsValid())
	{
		GeneratePromptSection->SetVisibility(EVisibility::Visible);
	}
	if (ContextStatusLabel.IsValid())
	{
		ContextStatusLabel->SetVisibility(EVisibility::Collapsed);
	}

	return FReply::Handled();
}

FReply SArborConceptArtStudioWidget::OnAddStyleImageFromDisk()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return FReply::Handled();

	TArray<FString> OutFiles;
	DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Select Style Reference Image"),
		FPaths::ProjectDir(),
		TEXT(""),
		TEXT("Image Files (*.png;*.jpg;*.jpeg)|*.png;*.jpg;*.jpeg"),
		EFileDialogFlags::Multiple,
		OutFiles
	);

	if (OutFiles.Num() > 0)
	{
		for (const FString& File : OutFiles)
		{
			StyleImagePaths.AddUnique(File);
		}
		RebuildStyleImageGrid();
	}

	return FReply::Handled();
}

FReply SArborConceptArtStudioWidget::OnGenerateMore()
{
	SetStep(EStep::Main);
	SetPromptSectionVisible(false);
	if (GeneratePromptSection.IsValid())
	{
		GeneratePromptSection->SetVisibility(EVisibility::Visible);
	}
	if (ContextStatusLabel.IsValid())
	{
		ContextStatusLabel->SetVisibility(EVisibility::Collapsed);
	}

	// Clear results
	ImageEntries.Empty();
	ImageBorders.Empty();
	SelectedIndex = -1;

	// Write state to signal ready for new generation
	WriteState();

	return FReply::Handled();
}

// ---------------------------------------------------------------------------
// State file I/O
// ---------------------------------------------------------------------------

void SArborConceptArtStudioWidget::WriteState()
{
	TSharedPtr<FJsonObject> State = MakeShareable(new FJsonObject());

	FString StepStr;
	switch (CurrentStep)
	{
	case EStep::Main:          StepStr = TEXT("context"); break;
	case EStep::Generating:    StepStr = TEXT("generating"); break;
	case EStep::Results:       StepStr = TEXT("results"); break;
	case EStep::Done:          StepStr = TEXT("done"); break;
	}

	State->SetStringField(TEXT("step"), StepStr);
	State->SetStringField(TEXT("codex_asset_path"), CodexAssetPath);
	State->SetStringField(TEXT("codex_name"), CodexName);
	State->SetStringField(TEXT("codex_description"), CodexDescription);
	State->SetNumberField(TEXT("num_images"), NumImages);
	State->SetStringField(TEXT("prompt"), Prompt);
	State->SetStringField(TEXT("user_feedback"), UserFeedback);
	State->SetNumberField(TEXT("selected_index"), SelectedIndex);
	State->SetStringField(TEXT("action"), PendingAction);
	State->SetNumberField(TEXT("timestamp"), FDateTime::UtcNow().ToUnixTimestamp());

	// Style images
	TArray<TSharedPtr<FJsonValue>> StyleArr;
	for (const FString& Path : StyleImagePaths)
	{
		StyleArr.Add(MakeShareable(new FJsonValueString(Path)));
	}
	State->SetArrayField(TEXT("style_images"), StyleArr);

	// Images
	TArray<TSharedPtr<FJsonValue>> ImagesArr;
	for (const FImageEntry& Entry : ImageEntries)
	{
		TSharedPtr<FJsonObject> ImgObj = MakeShareable(new FJsonObject());
		ImgObj->SetStringField(TEXT("path"), Entry.Path);
		ImgObj->SetStringField(TEXT("label"), Entry.Label);
		ImagesArr.Add(MakeShareable(new FJsonValueObject(ImgObj)));
	}
	State->SetArrayField(TEXT("images"), ImagesArr);

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(State.ToSharedRef(), Writer);
	FFileHelper::SaveStringToFile(JsonString, *GetStatePath(),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

FString SArborConceptArtStudioWidget::GetStatePath() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Arbor"), TEXT("concept_art_studio_state.json"));
}

// ---------------------------------------------------------------------------
// Full-size viewer popup
// ---------------------------------------------------------------------------

void SArborConceptArtStudioWidget::OpenFullSizeViewer(UTexture2D* Texture)
{
	if (!Texture) return;

	PinnedTextures.AddUnique(Texture);
	Texture->AddToRoot();

	FullSizeViewerBrush.SetResourceObject(Texture);
	FullSizeViewerBrush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
	FullSizeViewerBrush.DrawAs = ESlateBrushDrawType::Image;

	TSharedRef<SWindow> ViewerWindow = SNew(SWindow)
		.Title(FText::FromString(Texture->GetName()))
		.ClientSize(FVector2D(800, 600))
		.SupportsMaximize(true)
		.SupportsMinimize(true)
		[
			SNew(SScaleBox)
			.Stretch(EStretch::ScaleToFit)
			[
				SNew(SImage)
				.Image(&FullSizeViewerBrush)
			]
		];

	FSlateApplication::Get().AddWindow(ViewerWindow);
}

// ---------------------------------------------------------------------------
// Image loading (same as SArborTextureReviewWidget)
// ---------------------------------------------------------------------------

UTexture2D* SArborConceptArtStudioWidget::LoadImageFromDisk(const FString& FilePath)
{
	TArray<uint8> RawData;
	if (!FFileHelper::LoadFileToArray(RawData, *FilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("Arbor ConceptArtStudio: Failed to load file: %s"), *FilePath);
		return nullptr;
	}

	IImageWrapperModule& ImageWrapperModule =
		FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

	EImageFormat Format = EImageFormat::PNG;
	if (FilePath.EndsWith(TEXT(".jpg")) || FilePath.EndsWith(TEXT(".jpeg")))
	{
		Format = EImageFormat::JPEG;
	}

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(Format);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(RawData.GetData(), RawData.Num()))
	{
		UE_LOG(LogTemp, Warning, TEXT("Arbor ConceptArtStudio: Failed to decode image: %s"), *FilePath);
		return nullptr;
	}

	TArray<uint8> UncompressedBGRA;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, UncompressedBGRA))
	{
		UE_LOG(LogTemp, Warning, TEXT("Arbor ConceptArtStudio: Failed to decompress image: %s"), *FilePath);
		return nullptr;
	}

	const int32 Width = ImageWrapper->GetWidth();
	const int32 Height = ImageWrapper->GetHeight();

	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!Texture)
	{
		UE_LOG(LogTemp, Warning, TEXT("Arbor ConceptArtStudio: Failed to create transient texture"));
		return nullptr;
	}

	Texture->AddToRoot();

	void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, UncompressedBGRA.GetData(), UncompressedBGRA.Num());
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture->UpdateResource();

	return Texture;
}

TSharedPtr<FSlateBrush> SArborConceptArtStudioWidget::CreateBrushFromTexture(UTexture2D* Texture, int32 Size)
{
	if (!Texture) return nullptr;

	TSharedPtr<FSlateBrush> Brush = MakeShareable(new FSlateBrush());
	Brush->SetResourceObject(Texture);
	Brush->ImageSize = FVector2D(Size, Size);
	Brush->DrawAs = ESlateBrushDrawType::Image;
	return Brush;
}

#undef LOCTEXT_NAMESPACE
