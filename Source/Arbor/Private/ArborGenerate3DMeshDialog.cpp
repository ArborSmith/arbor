#include "ArborGenerate3DMeshDialog.h"
#include "ArborClaude.h"
#include "ArborCodexStyle.h"
#include "ArborCodexWidgetHelpers.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "ArborGenerate3DMeshDialog"

void ArborGenerate3DMeshDialog::Show(const FMeshGenOptions& Options)
{
	TSharedPtr<SMultiLineEditableTextBox> InstructionsBox;
	TSharedPtr<SCheckBox> AnimationCheckBox;
	TSharedPtr<STextComboBox> APICombo;
	TSharedPtr<STextComboBox> ImageCombo;

	// Build image selection options
	TArray<TSharedPtr<FString>>* ImageOptions = new TArray<TSharedPtr<FString>>();
	TArray<UTexture2D*> AllTextures;

	if (Options.PrimaryConceptArt)
	{
		ImageOptions->Add(MakeShared<FString>(TEXT("Primary Concept Art")));
		AllTextures.Add(Options.PrimaryConceptArt);
	}

	for (int32 i = 0; i < Options.GalleryTextures.Num(); i++)
	{
		if (Options.GalleryTextures[i])
		{
			ImageOptions->Add(MakeShared<FString>(FString::Printf(TEXT("Gallery Image %d"), i + 1)));
			AllTextures.Add(Options.GalleryTextures[i]);
		}
	}

	if (ImageOptions->Num() == 0)
	{
		ImageOptions->Add(MakeShared<FString>(TEXT("(No concept art available)")));
	}

	// Build API options
	TArray<TSharedPtr<FString>>* APIOptions = new TArray<TSharedPtr<FString>>();
	APIOptions->Add(MakeShared<FString>(TEXT("Meshy")));
	// Future APIs can be added here

	// Preview brush for selected image
	TSharedPtr<FSlateBrush> PreviewBrush = MakeShared<FSlateBrush>();
	if (AllTextures.Num() > 0)
	{
		PreviewBrush->SetResourceObject(AllTextures[0]);
		PreviewBrush->ImageSize = FVector2D(AllTextures[0]->GetSizeX(), AllTextures[0]->GetSizeY());
		PreviewBrush->DrawAs = ESlateBrushDrawType::Image;
	}

	FString CharacterName = Options.CharacterName;
	FString AssetPath = Options.AssetPath;

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::FromString(FString::Printf(TEXT("Generate 3D Mesh — %s"), *CharacterName)))
		.ClientSize(FVector2D(650, 680))
		.SupportsMaximize(true)
		.SupportsMinimize(true)
		.SizingRule(ESizingRule::UserSized);

	TWeakPtr<SWindow> WeakWindow = Window;

	Window->SetContent(
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(ArborCodexStyle::Bg::Panel)
		.Padding(ArborCodexStyle::Spacing::SectionPadding)
		[
			SNew(SVerticalBox)

			// Title
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Title", "Generate 3D Mesh"))
				.Font(ArborCodexStyle::Font::PageHeader())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(
					TEXT("Generate a 3D mesh for \"%s\" from concept art reference"), *CharacterName)))
				.Font(ArborCodexStyle::Font::Body())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Muted))
			]

			// Reference Image section
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RefImage", "Reference Image"))
				.Font(ArborCodexStyle::Font::SectionHeader())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SAssignNew(ImageCombo, STextComboBox)
				.OptionsSource(ImageOptions)
				.OnSelectionChanged_Lambda([ImageOptions, AllTextures, PreviewBrush](TSharedPtr<FString> NewValue, ESelectInfo::Type)
				{
					if (!NewValue.IsValid()) return;
					for (int32 i = 0; i < ImageOptions->Num(); i++)
					{
						if (*(*ImageOptions)[i] == *NewValue && i < AllTextures.Num())
						{
							PreviewBrush->SetResourceObject(AllTextures[i]);
							PreviewBrush->ImageSize = FVector2D(AllTextures[i]->GetSizeX(), AllTextures[i]->GetSizeY());
							PreviewBrush->DrawAs = ESlateBrushDrawType::Image;
							break;
						}
					}
				})
			]

			// Image preview
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 8.0f)
			[
				SNew(SBox)
				.HeightOverride(180.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(ArborCodexStyle::Bg::Elevated)
					.Padding(4.0f)
					.HAlign(HAlign_Center)
					[
						SNew(SScaleBox)
						.Stretch(EStretch::ScaleToFit)
						[
							SNew(SImage)
							.Image_Lambda([PreviewBrush]() -> const FSlateBrush*
							{
								return PreviewBrush->GetResourceObject() ? PreviewBrush.Get() : nullptr;
							})
						]
					]
				]
			]

			// T-Pose note
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.15f, 0.12f, 0.05f, 0.6f))
				.Padding(FMargin(10.0f, 6.0f))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("TPoseNote",
						"Tip: For best results, character concept art should be in a T-Pose, "
						"with a clean/transparent background, and no extra elements. "
						"Front-facing with arms extended works best for mesh generation."))
					.Font(ArborCodexStyle::Font::BodySmall())
					.ColorAndOpacity(FSlateColor(ArborCodexStyle::State::Locked))
					.AutoWrapText(true)
				]
			]

			// Options row: Animation + API
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 12.0f)
			[
				SNew(SHorizontalBox)

				// Animation checkbox
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 24.0f, 0.0f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SAssignNew(AnimationCheckBox, SCheckBox)
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("WithAnimation", "Include Animation (Rig + Animate)"))
						.Font(ArborCodexStyle::Font::FieldLabel())
						.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Primary))
					]
				]

				// API selector
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("APILabel", "API:"))
					.Font(ArborCodexStyle::Font::FieldLabel())
					.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Secondary))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SAssignNew(APICombo, STextComboBox)
					.OptionsSource(APIOptions)
				]
			]

			// Additional instructions
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ExtraInstructions", "Additional Instructions"))
				.Font(ArborCodexStyle::Font::SectionHeader())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Secondary))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ExtraHint", "Optional — style preferences, polygon count, detail level, etc."))
				.Font(ArborCodexStyle::Font::Body())
				.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Muted))
			]

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
					.HintText(LOCTEXT("InstructionsHint",
						"e.g. Low-poly style, focus on face detail, game-ready topology..."))
					.Font(ArborCodexStyle::Font::Input())
					.AutoWrapText(true)
				]
			]

			// Buttons
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
						LOCTEXT("Generate", "Generate 3D Mesh"),
						FOnClicked::CreateLambda([WeakWindow, CharacterName, AssetPath,
							ImageCombo, ImageOptions, AllTextures,
							AnimationCheckBox, APICombo, APIOptions,
							InstructionsBox]()
						{
							// Determine selected image index
							int32 SelectedImageIdx = 0;
							if (ImageCombo.IsValid())
							{
								TSharedPtr<FString> SelItem = ImageCombo->GetSelectedItem();
								if (SelItem.IsValid())
								{
									for (int32 i = 0; i < ImageOptions->Num(); i++)
									{
										if (*(*ImageOptions)[i] == *SelItem)
										{
											SelectedImageIdx = i;
											break;
										}
									}
								}
							}

							bool bWithAnimation = AnimationCheckBox.IsValid() && AnimationCheckBox->IsChecked();

							FString SelectedAPI = TEXT("Meshy");
							if (APICombo.IsValid())
							{
								TSharedPtr<FString> APIItem = APICombo->GetSelectedItem();
								if (APIItem.IsValid())
								{
									SelectedAPI = *APIItem;
								}
							}

							FString ExtraInstructions = InstructionsBox.IsValid()
								? InstructionsBox->GetText().ToString().TrimStartAndEnd()
								: TEXT("");

							// Determine image reference info
							FString ImageRef;
							if (SelectedImageIdx < AllTextures.Num() && AllTextures[SelectedImageIdx])
							{
								ImageRef = AllTextures[SelectedImageIdx]->GetPathName();
							}

							// Build the prompt for Claude
							FString Prompt = FString::Printf(
								TEXT("Generate a 3D mesh for the character \"%s\" using the %s API.\n\n"),
								*CharacterName, *SelectedAPI);

							Prompt += TEXT("## Instructions\n\n");

							if (SelectedAPI == TEXT("Meshy"))
							{
								if (!ImageRef.IsEmpty())
								{
									Prompt += FString::Printf(
										TEXT("1. First, get the concept art image from the character asset at: %s\n"
											 "   The selected reference image UE asset path is: %s\n"
											 "   Use `ue5_run_python` to export the texture to disk as PNG, then use `meshy_image_to_3d` with that image file.\n\n"),
										*AssetPath, *ImageRef);
								}
								else
								{
									Prompt += FString::Printf(
										TEXT("1. No concept art is available. Use `meshy_text_to_3d` with a detailed description of the character \"%s\".\n\n"),
										*CharacterName);
								}

								Prompt += TEXT("2. Poll the task with `meshy_get_task` until it completes.\n\n");

								if (bWithAnimation)
								{
									Prompt += TEXT("3. Once the mesh is ready, use `meshy_rig` to rig the model for animation.\n"
												   "4. After rigging completes, use `meshy_animate` to generate a basic idle animation.\n"
												   "5. Download the final rigged+animated model and import it into UE5.\n\n");
								}
								else
								{
									Prompt += TEXT("3. Once the mesh is ready, download and import it into UE5.\n\n");
								}
							}

							Prompt += TEXT("## Character Concept Art Guidelines\n"
										   "For best 3D mesh generation results, the reference image should be:\n"
										   "- Character in a T-Pose (arms extended horizontally)\n"
										   "- Clean or transparent background (no environment elements)\n"
										   "- Front-facing view preferred\n"
										   "- No text, watermarks, or overlapping objects\n\n");

							if (!ExtraInstructions.IsEmpty())
							{
								Prompt += FString::Printf(
									TEXT("## Additional User Instructions\n%s\n\n"), *ExtraInstructions);
							}

							Prompt += TEXT("Proceed with the generation. Report progress as you go.");

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

	// Set initial selections
	if (ImageCombo.IsValid() && ImageOptions->Num() > 0)
	{
		ImageCombo->SetSelectedItem((*ImageOptions)[0]);
	}
	if (APICombo.IsValid() && APIOptions->Num() > 0)
	{
		APICombo->SetSelectedItem((*APIOptions)[0]);
	}

	FSlateApplication::Get().AddWindow(Window);
}

#undef LOCTEXT_NAMESPACE
