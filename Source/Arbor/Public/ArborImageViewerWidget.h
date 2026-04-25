#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SImage;

class SArborImageViewerWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArborImageViewerWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void LoadImage(const FString& FilePath);

private:
	TSharedPtr<FSlateBrush> ImageBrush;
	TSharedPtr<SImage> ImageWidget;
	UTexture2D* ImageTexture = nullptr;

	static UTexture2D* LoadImageFromDisk(const FString& FilePath);
};
