#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SScrollBox;
class SWrapBox;
class STextBlock;

class SArborScreenshotWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArborScreenshotWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	struct FScreenshotEntry
	{
		FString Path;
		FString Filename;
		FDateTime ModifiedTime;
		UTexture2D* Texture = nullptr;
		TSharedPtr<FSlateBrush> Brush;
	};

	TArray<FScreenshotEntry> Entries;

	// UI elements
	TSharedPtr<STextBlock> StatusLabel;
	TSharedPtr<SWrapBox> ImageGrid;

	// Actions
	void ScanScreenshotDirectory();
	void RebuildGrid();
	void OnOpenClicked(int32 Index);
	void OnDeleteClicked(int32 Index);

	// Helpers
	static FString GetScreenshotDirectory();
	static UTexture2D* LoadImageFromDisk(const FString& FilePath);
	static TSharedPtr<FSlateBrush> CreateBrushFromTexture(UTexture2D* Texture, int32 Size);
};
