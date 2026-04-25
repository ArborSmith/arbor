#include "ArborImageViewerWidget.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "ArborImageViewer"

void SArborImageViewerWidget::Construct(const FArguments& InArgs)
{
	ImageBrush = MakeShareable(new FSlateBrush());
	ImageBrush->DrawAs = ESlateBrushDrawType::NoDrawType;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.02f))
		[
			SNew(SScaleBox)
			.Stretch(EStretch::ScaleToFit)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SAssignNew(ImageWidget, SImage)
				.Image(ImageBrush.Get())
			]
		]
	];
}

void SArborImageViewerWidget::LoadImage(const FString& FilePath)
{
	ImageTexture = LoadImageFromDisk(FilePath);
	if (!ImageTexture)
	{
		ImageBrush->DrawAs = ESlateBrushDrawType::NoDrawType;
		ImageBrush->SetResourceObject(nullptr);
		return;
	}

	int32 W = ImageTexture->GetSizeX();
	int32 H = ImageTexture->GetSizeY();

	ImageBrush->SetResourceObject(ImageTexture);
	ImageBrush->ImageSize = FVector2D(W, H);
	ImageBrush->DrawAs = ESlateBrushDrawType::Image;
}

UTexture2D* SArborImageViewerWidget::LoadImageFromDisk(const FString& FilePath)
{
	TArray<uint8> RawData;
	if (!FFileHelper::LoadFileToArray(RawData, *FilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("Arbor ImageViewer: Failed to load file: %s"), *FilePath);
		return nullptr;
	}

	IImageWrapperModule& ImageWrapperModule =
		FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

	EImageFormat Format = EImageFormat::PNG;
	if (FilePath.EndsWith(TEXT(".jpg")) || FilePath.EndsWith(TEXT(".jpeg")))
	{
		Format = EImageFormat::JPEG;
	}
	else if (FilePath.EndsWith(TEXT(".bmp")))
	{
		Format = EImageFormat::BMP;
	}

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(Format);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(RawData.GetData(), RawData.Num()))
	{
		UE_LOG(LogTemp, Warning, TEXT("Arbor ImageViewer: Failed to decode image: %s"), *FilePath);
		return nullptr;
	}

	TArray<uint8> UncompressedBGRA;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, UncompressedBGRA))
	{
		UE_LOG(LogTemp, Warning, TEXT("Arbor ImageViewer: Failed to decompress image: %s"), *FilePath);
		return nullptr;
	}

	int32 Width = ImageWrapper->GetWidth();
	int32 Height = ImageWrapper->GetHeight();

	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!Texture)
	{
		return nullptr;
	}

	void* MipData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(MipData, UncompressedBGRA.GetData(), UncompressedBGRA.Num());
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture->UpdateResource();

	return Texture;
}

#undef LOCTEXT_NAMESPACE
