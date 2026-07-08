#include "ArborTextureReviewWidget.h"
#include "ArborAIPromptDialog.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SButton.h"
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
#include "Styling/SlateTypes.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Framework/Application/SlateApplication.h"
#include "ArborClaude.h"

static constexpr float TILE_WIDTH = 400.0f;

#define LOCTEXT_NAMESPACE "ArborTextureReview"

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void SArborTextureReviewWidget::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		// Header: prompt + source
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 8.0f, 8.0f, 4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(PromptLabel, STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
				.Text(LOCTEXT("NoImages", "No images loaded. Use Arbor.TextureReview to load images."))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SAssignNew(SourceLabel, STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 11))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
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
				SAssignNew(ImageGridContainer, SBox)
			]
		]

		// PBR preview (shown only when a texture with PBR maps is selected)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SAssignNew(PBRPreviewContainer, SBox)
			.Visibility(EVisibility::Collapsed)
		]

		// Status label
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 2.0f)
		[
			SAssignNew(StatusLabel, STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
		]

		// Comments input
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f)
		[
			SNew(SVerticalBox)

		]

		// Action buttons
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f, 8.0f, 8.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SAssignNew(UseSelectedButton, SButton)
				.IsEnabled(false)
				.OnClicked_Lambda([this]()
				{
					OnUseSelected();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("UseSelected", "Use Selected"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SAssignNew(RegenerateButton, SButton)
				.IsEnabled(false)
				.OnClicked_Lambda([this]()
				{
					OnRegenerate();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Regenerate", "Regenerate with Comments"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.OnClicked_Lambda([this]()
				{
					OnCancel();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Cancel", "Cancel"))
				]
			]
		]
	];
}

// ---------------------------------------------------------------------------
// Load images from JSON manifest
// ---------------------------------------------------------------------------

void SArborTextureReviewWidget::LoadImages(const FString& JsonManifest)
{
	ClearImages();

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonManifest);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Arbor TextureReview: Failed to parse JSON manifest"));
		return;
	}

	OriginalPrompt = Root->GetStringField(TEXT("prompt"));
	Source = Root->GetStringField(TEXT("source"));
	Root->TryGetStringField(TEXT("codex_asset_path"), CodexAssetPath);

	const TArray<TSharedPtr<FJsonValue>>* ImagesArray;
	if (!Root->TryGetArrayField(TEXT("images"), ImagesArray))
	{
		UE_LOG(LogTemp, Error, TEXT("Arbor TextureReview: No 'images' array in manifest"));
		return;
	}

	for (const auto& ImageValue : *ImagesArray)
	{
		const TSharedPtr<FJsonObject>& ImageObj = ImageValue->AsObject();
		if (!ImageObj.IsValid()) continue;

		FImageEntry Entry;
		Entry.Path = ImageObj->GetStringField(TEXT("path"));
		Entry.Label = ImageObj->GetStringField(TEXT("label"));

		// Load main image
		Entry.Texture = LoadImageFromDisk(Entry.Path);
		if (Entry.Texture)
		{
			Entry.Brush = CreateBrushFromTexture(Entry.Texture, (int32)TILE_WIDTH);
		}

		// Load PBR map paths if present
		const TSharedPtr<FJsonObject>* PBRObj;
		if (ImageObj->TryGetObjectField(TEXT("pbr"), PBRObj))
		{
			for (const auto& Pair : (*PBRObj)->Values)
			{
				FString MapPath;
				if (Pair.Value->TryGetString(MapPath))
				{
					Entry.PBRPaths.Add(FString(*Pair.Key), MapPath);
				}
			}
		}

		ImageEntries.Add(MoveTemp(Entry));
	}

	// Update UI
	if (PromptLabel.IsValid())
	{
		PromptLabel->SetText(FText::FromString(FString::Printf(TEXT("Prompt: \"%s\""), *OriginalPrompt)));
	}
	if (SourceLabel.IsValid())
	{
		SourceLabel->SetText(FText::FromString(FString::Printf(TEXT("[%s]"), *Source.ToUpper())));
	}
	if (StatusLabel.IsValid())
	{
		StatusLabel->SetText(FText::FromString(FString::Printf(
			TEXT("%d images loaded. Click to select, double-click to enlarge."), ImageEntries.Num())));
	}

	// Enable regenerate button (always available after loading)
	if (RegenerateButton.IsValid())
	{
		RegenerateButton->SetEnabled(true);
	}

	RebuildImageGrid();

	UE_LOG(LogTemp, Log, TEXT("Arbor TextureReview: Loaded %d images for prompt '%s'"),
		ImageEntries.Num(), *OriginalPrompt);
}

// ---------------------------------------------------------------------------
// Image grid
// ---------------------------------------------------------------------------

void SArborTextureReviewWidget::RebuildImageGrid()
{
	ImageBorders.Empty();

	if (!ImageGridContainer.IsValid()) return;

	TSharedPtr<SWrapBox> Grid = SNew(SWrapBox)
		.UseAllottedSize(true);

	for (int32 i = 0; i < ImageEntries.Num(); i++)
	{
		const FImageEntry& Entry = ImageEntries[i];

		TSharedPtr<SBorder> Border;

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
					.HeightOverride(Entry.Texture
						? TILE_WIDTH * Entry.Texture->GetSizeY() / FMath::Max(1, Entry.Texture->GetSizeX())
						: TILE_WIDTH)
					[
						Entry.Brush.IsValid()
						? StaticCastSharedRef<SWidget>(
							SNew(SImage).Image(Entry.Brush.Get()))
						: StaticCastSharedRef<SWidget>(
							SNew(STextBlock)
							.Text(LOCTEXT("LoadFailed", "Failed to load"))
							.Justification(ETextJustify::Center))
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
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				]
			]
		];

		ImageBorders.Add(Border);
	}

	ImageGridContainer->SetContent(Grid.ToSharedRef());
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

void SArborTextureReviewWidget::OnImageClicked(int32 Index)
{
	SelectedIndex = Index;
	UpdateSelectionHighlight();
	UpdatePBRPreview();

	if (UseSelectedButton.IsValid())
	{
		UseSelectedButton->SetEnabled(true);
	}

	if (StatusLabel.IsValid())
	{
		StatusLabel->SetText(FText::FromString(FString::Printf(
			TEXT("Selected: %s"), *ImageEntries[Index].Label)));
	}
}

void SArborTextureReviewWidget::UpdateSelectionHighlight()
{
	for (int32 i = 0; i < ImageBorders.Num(); i++)
	{
		if (ImageBorders[i].IsValid())
		{
			FLinearColor Color = (i == SelectedIndex)
				? FLinearColor(0.2f, 0.6f, 1.0f, 1.0f)   // Blue highlight
				: FLinearColor(0.15f, 0.15f, 0.15f, 1.0f); // Default dark
			ImageBorders[i]->SetBorderBackgroundColor(Color);
		}
	}
}

void SArborTextureReviewWidget::UpdatePBRPreview()
{
	if (!PBRPreviewContainer.IsValid()) return;

	if (SelectedIndex < 0 || SelectedIndex >= ImageEntries.Num())
	{
		PBRPreviewContainer->SetVisibility(EVisibility::Collapsed);
		return;
	}

	const FImageEntry& Entry = ImageEntries[SelectedIndex];
	if (Entry.PBRPaths.Num() == 0)
	{
		PBRPreviewContainer->SetVisibility(EVisibility::Collapsed);
		return;
	}

	// Build PBR preview row
	TSharedPtr<SHorizontalBox> Row = SNew(SHorizontalBox);

	// Label
	Row->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(0.0f, 0.0f, 8.0f, 0.0f)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("PBRMaps", "PBR Maps:"))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
	];

	// Order of PBR maps to display
	static const TArray<FString> MapOrder = { TEXT("albedo"), TEXT("normal"), TEXT("roughness"), TEXT("metallic"), TEXT("ao"), TEXT("height") };

	for (const FString& MapType : MapOrder)
	{
		const FString* MapPath = Entry.PBRPaths.Find(MapType);
		if (!MapPath) continue;

		UTexture2D* MapTexture = LoadImageFromDisk(*MapPath);
		if (!MapTexture) continue;

		TSharedPtr<FSlateBrush> MapBrush = CreateBrushFromTexture(MapTexture, 80);
		if (!MapBrush.IsValid()) continue;

		Row->AddSlot()
		.AutoWidth()
		.Padding(4.0f, 0.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.WidthOverride(80.0f)
				.HeightOverride(80.0f)
				[
					SNew(SImage).Image(MapBrush.Get())
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(MapType))
				.Justification(ETextJustify::Center)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
		];
	}

	PBRPreviewContainer->SetContent(Row.ToSharedRef());
	PBRPreviewContainer->SetVisibility(EVisibility::Visible);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void SArborTextureReviewWidget::OnUseSelected()
{
	if (SelectedIndex < 0 || SelectedIndex >= ImageEntries.Num()) return;

	WriteResult(TEXT("select"));

	const FImageEntry& Entry = ImageEntries[SelectedIndex];

	if (!CodexAssetPath.IsEmpty())
	{
		FString Message = FString::Printf(
			TEXT("The user selected concept art variant %d from the texture review window.\n")
			TEXT("Selected image path: %s\n")
			TEXT("Codex asset path: %s\n")
			TEXT("Import this image and set it as concept art on the codex entry."),
			SelectedIndex + 1, *Entry.Path, *CodexAssetPath);
		FArborClaude::SendMessage(Message);
	}

	if (StatusLabel.IsValid())
	{
		StatusLabel->SetText(FText::FromString(FString::Printf(
			TEXT("Selected %s. %s"), *Entry.Label,
			CodexAssetPath.IsEmpty() ? TEXT("Waiting for import...") : TEXT("Importing..."))));
	}
}

void SArborTextureReviewWidget::OnRegenerate()
{
	// Build a regeneration prompt for Claude
	FString Prompt = FString::Printf(
		TEXT("The user wants to regenerate concept art images.\n\n"
			 "Original image generation prompt:\n\"%s\"\n\n"
			 "Please:\n"
			 "1. Revise the image prompt incorporating the user's feedback\n"
			 "2. Generate 4 new images: fal_generate_image(prompt=..., num_images=4)\n"
			 "3. Show review in UE5: ue5_run_python with arbor.textures.show_texture_review(...)\n"
			 "4. Poll for selection: arbor.textures.get_texture_review_result()\n"
			 "5. If selected, import and set as concept art: ue5_run_python with arbor.concept_art.import_concept_art(...)"),
		*OriginalPrompt);

	if (!CodexAssetPath.IsEmpty())
	{
		Prompt += FString::Printf(TEXT("\n\nCodex asset path: %s"), *CodexAssetPath);
	}

	ArborAIPromptDialog::Show(Prompt, TEXT("Regenerate Concept Art"));

	if (StatusLabel.IsValid())
	{
		StatusLabel->SetText(FText::FromString(TEXT("Regeneration prompt sent to AI.")));
	}
}

void SArborTextureReviewWidget::OnCancel()
{
	WriteResult(TEXT("cancel"));

	if (StatusLabel.IsValid())
	{
		StatusLabel->SetText(LOCTEXT("Cancelled", "Cancelled."));
	}
}

void SArborTextureReviewWidget::WriteResult(const FString& Action)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetStringField(TEXT("action"), Action);
	Result->SetNumberField(TEXT("selected_index"), SelectedIndex);
	Result->SetStringField(TEXT("source"), Source);
	Result->SetStringField(TEXT("prompt"), OriginalPrompt);
	Result->SetNumberField(TEXT("timestamp"), FDateTime::UtcNow().ToUnixTimestamp());

	// Codex asset path (if provided in manifest)
	if (!CodexAssetPath.IsEmpty())
	{
		Result->SetStringField(TEXT("codex_asset_path"), CodexAssetPath);
	}

	// Selected image info
	if (SelectedIndex >= 0 && SelectedIndex < ImageEntries.Num())
	{
		const FImageEntry& Entry = ImageEntries[SelectedIndex];
		Result->SetStringField(TEXT("selected_path"), Entry.Path);

		// PBR paths
		if (Entry.PBRPaths.Num() > 0)
		{
			TSharedPtr<FJsonObject> PBRObj = MakeShareable(new FJsonObject());
			for (const auto& Pair : Entry.PBRPaths)
			{
				PBRObj->SetStringField(Pair.Key, Pair.Value);
			}
			Result->SetObjectField(TEXT("pbr"), PBRObj);
		}
	}

	// Serialize
	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

	// Write to Saved/Arbor/texture_review_result.json
	FString OutputPath = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Arbor"), TEXT("texture_review_result.json"));
	FFileHelper::SaveStringToFile(JsonString, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	UE_LOG(LogTemp, Log, TEXT("Arbor TextureReview: Wrote result to %s (action=%s)"), *OutputPath, *Action);
}

// ---------------------------------------------------------------------------
// Full-size viewer popup
// ---------------------------------------------------------------------------

void SArborTextureReviewWidget::OpenFullSizeViewer(UTexture2D* Texture)
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
// Cleanup
// ---------------------------------------------------------------------------

void SArborTextureReviewWidget::ClearImages()
{
	ImageEntries.Empty();
	ImageBorders.Empty();
	SelectedIndex = -1;

	if (UseSelectedButton.IsValid())
	{
		UseSelectedButton->SetEnabled(false);
	}
	if (RegenerateButton.IsValid())
	{
		RegenerateButton->SetEnabled(false);
	}
	if (PBRPreviewContainer.IsValid())
	{
		PBRPreviewContainer->SetVisibility(EVisibility::Collapsed);
	}
}

// ---------------------------------------------------------------------------
// Image loading
// ---------------------------------------------------------------------------

UTexture2D* SArborTextureReviewWidget::LoadImageFromDisk(const FString& FilePath)
{
	TArray<uint8> RawData;
	if (!FFileHelper::LoadFileToArray(RawData, *FilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("Arbor TextureReview: Failed to load file: %s"), *FilePath);
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
		UE_LOG(LogTemp, Warning, TEXT("Arbor TextureReview: Failed to decode image: %s"), *FilePath);
		return nullptr;
	}

	TArray<uint8> UncompressedBGRA;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, UncompressedBGRA))
	{
		UE_LOG(LogTemp, Warning, TEXT("Arbor TextureReview: Failed to decompress image: %s"), *FilePath);
		return nullptr;
	}

	const int32 Width = ImageWrapper->GetWidth();
	const int32 Height = ImageWrapper->GetHeight();

	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!Texture)
	{
		UE_LOG(LogTemp, Warning, TEXT("Arbor TextureReview: Failed to create transient texture"));
		return nullptr;
	}

	Texture->AddToRoot(); // Prevent GC while the widget is alive

	void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, UncompressedBGRA.GetData(), UncompressedBGRA.Num());
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture->UpdateResource();

	return Texture;
}

TSharedPtr<FSlateBrush> SArborTextureReviewWidget::CreateBrushFromTexture(UTexture2D* Texture, int32 Size)
{
	if (!Texture) return nullptr;

	TSharedPtr<FSlateBrush> Brush = MakeShareable(new FSlateBrush());
	Brush->SetResourceObject(Texture);
	Brush->ImageSize = FVector2D(Size, Size);
	Brush->DrawAs = ESlateBrushDrawType::Image;
	return Brush;
}

#undef LOCTEXT_NAMESPACE
