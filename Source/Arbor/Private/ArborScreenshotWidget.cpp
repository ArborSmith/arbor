#include "ArborScreenshotWidget.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "ArborScreenshots"

static constexpr int32 THUMB_SIZE = 200;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void SArborScreenshotWidget::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		// Toolbar
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SButton)
				.OnClicked_Lambda([this]()
				{
					ScanScreenshotDirectory();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Refresh", "Refresh"))
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SAssignNew(StatusLabel, STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			]
		]

		// Scrollable image grid
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8.0f, 0.0f, 8.0f, 8.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(ImageGrid, SWrapBox)
				.UseAllottedSize(true)
			]
		]
	];

	ScanScreenshotDirectory();
}

// ---------------------------------------------------------------------------
// Directory scanning
// ---------------------------------------------------------------------------

FString SArborScreenshotWidget::GetScreenshotDirectory()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Arbor"), TEXT("Screenshots"));
}

void SArborScreenshotWidget::ScanScreenshotDirectory()
{
	// Clean up old textures
	for (auto& Entry : Entries)
	{
		if (Entry.Texture && Entry.Texture->IsRooted())
		{
			Entry.Texture->RemoveFromRoot();
		}
	}
	Entries.Empty();

	const FString Dir = GetScreenshotDirectory();
	IFileManager& FM = IFileManager::Get();

	if (!FM.DirectoryExists(*Dir))
	{
		if (StatusLabel.IsValid())
		{
			StatusLabel->SetText(LOCTEXT("NoDir", "No screenshots directory found."));
		}
		RebuildGrid();
		return;
	}

	// Find all image files
	TArray<FString> JpgFiles, PngFiles;
	FM.FindFiles(JpgFiles, *(Dir / TEXT("*.jpg")), true, false);
	FM.FindFiles(PngFiles, *(Dir / TEXT("*.png")), true, false);

	TArray<FString> AllFiles;
	AllFiles.Append(JpgFiles);
	AllFiles.Append(PngFiles);

	for (const FString& Filename : AllFiles)
	{
		FScreenshotEntry Entry;
		Entry.Filename = Filename;
		Entry.Path = Dir / Filename;

		// Get modification time for sorting
		Entry.ModifiedTime = FM.GetTimeStamp(*Entry.Path);

		// Load thumbnail
		Entry.Texture = LoadImageFromDisk(Entry.Path);
		if (Entry.Texture)
		{
			Entry.Brush = CreateBrushFromTexture(Entry.Texture, THUMB_SIZE);
		}

		Entries.Add(MoveTemp(Entry));
	}

	// Sort newest first
	Entries.Sort([](const FScreenshotEntry& A, const FScreenshotEntry& B)
	{
		return A.ModifiedTime > B.ModifiedTime;
	});

	if (StatusLabel.IsValid())
	{
		StatusLabel->SetText(FText::FromString(FString::Printf(
			TEXT("%d screenshot(s) in %s"), Entries.Num(), *Dir)));
	}

	RebuildGrid();
}

// ---------------------------------------------------------------------------
// Grid layout
// ---------------------------------------------------------------------------

void SArborScreenshotWidget::RebuildGrid()
{
	if (!ImageGrid.IsValid()) return;

	ImageGrid->ClearChildren();

	for (int32 i = 0; i < Entries.Num(); i++)
	{
		const FScreenshotEntry& Entry = Entries[i];

		ImageGrid->AddSlot()
		.Padding(4.0f)
		[
			SNew(SBox)
			.WidthOverride(THUMB_SIZE + 8.0f)
			[
				SNew(SVerticalBox)

				// Thumbnail (clickable)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("Border"))
					.BorderBackgroundColor(FLinearColor(0.15f, 0.15f, 0.15f, 1.0f))
					.Padding(3.0f)
					.Cursor(EMouseCursor::Hand)
					.OnMouseButtonDown_Lambda([this, i](const FGeometry&, const FPointerEvent& MouseEvent) -> FReply
					{
						if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
						{
							OnOpenClicked(i);
							return FReply::Handled();
						}
						return FReply::Unhandled();
					})
					[
						SNew(SBox)
						.WidthOverride(THUMB_SIZE)
						.HeightOverride(THUMB_SIZE * 9.0f / 16.0f) // 16:9 aspect ratio
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

				// Filename + delete button row
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Entry.Filename))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.OnClicked_Lambda([this, i]()
						{
							OnDeleteClicked(i);
							return FReply::Handled();
						})
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("X")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.3f, 0.3f)))
						]
					]
				]
			]
		];
	}
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void SArborScreenshotWidget::OnOpenClicked(int32 Index)
{
	if (!Entries.IsValidIndex(Index)) return;

	const FString& Path = Entries[Index].Path;
	FPlatformProcess::LaunchFileInDefaultExternalApplication(*Path);

	UE_LOG(LogTemp, Log, TEXT("Arbor Screenshots: Opened %s"), *Path);
}

void SArborScreenshotWidget::OnDeleteClicked(int32 Index)
{
	if (!Entries.IsValidIndex(Index)) return;

	const FString Filename = Entries[Index].Filename;
	const FString Path = Entries[Index].Path;

	// Clean up texture
	if (Entries[Index].Texture && Entries[Index].Texture->IsRooted())
	{
		Entries[Index].Texture->RemoveFromRoot();
	}

	// Delete file
	IFileManager::Get().Delete(*Path);

	// Remove from array
	Entries.RemoveAt(Index);

	// Update UI
	if (StatusLabel.IsValid())
	{
		StatusLabel->SetText(FText::FromString(FString::Printf(
			TEXT("Deleted %s. %d screenshot(s) remaining."), *Filename, Entries.Num())));
	}

	RebuildGrid();

	UE_LOG(LogTemp, Log, TEXT("Arbor Screenshots: Deleted %s"), *Path);
}

// ---------------------------------------------------------------------------
// Image loading (duplicated from ArborTextureReviewWidget for independence)
// ---------------------------------------------------------------------------

UTexture2D* SArborScreenshotWidget::LoadImageFromDisk(const FString& FilePath)
{
	TArray<uint8> RawData;
	if (!FFileHelper::LoadFileToArray(RawData, *FilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("Arbor Screenshots: Failed to load file: %s"), *FilePath);
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
		UE_LOG(LogTemp, Warning, TEXT("Arbor Screenshots: Failed to decode image: %s"), *FilePath);
		return nullptr;
	}

	TArray<uint8> UncompressedBGRA;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, UncompressedBGRA))
	{
		UE_LOG(LogTemp, Warning, TEXT("Arbor Screenshots: Failed to decompress image: %s"), *FilePath);
		return nullptr;
	}

	const int32 Width = ImageWrapper->GetWidth();
	const int32 Height = ImageWrapper->GetHeight();

	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!Texture)
	{
		UE_LOG(LogTemp, Warning, TEXT("Arbor Screenshots: Failed to create transient texture"));
		return nullptr;
	}

	Texture->AddToRoot(); // Prevent GC while the widget is alive

	void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(TextureData, UncompressedBGRA.GetData(), UncompressedBGRA.Num());
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture->UpdateResource();

	return Texture;
}

TSharedPtr<FSlateBrush> SArborScreenshotWidget::CreateBrushFromTexture(UTexture2D* Texture, int32 Size)
{
	if (!Texture) return nullptr;

	TSharedPtr<FSlateBrush> Brush = MakeShareable(new FSlateBrush());
	Brush->SetResourceObject(Texture);
	Brush->ImageSize = FVector2D(Size, Size * 9.0 / 16.0); // 16:9
	Brush->DrawAs = ESlateBrushDrawType::Image;
	return Brush;
}

#undef LOCTEXT_NAMESPACE
