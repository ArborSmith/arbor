#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Dom/JsonObject.h"

class SArborRegistryWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArborRegistryWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	struct FAssetEntry
	{
		FString Path;
		FString Name;
		FString Type;
		TArray<FString> Tags;
	};

	// Data
	TArray<FAssetEntry> AllAssets;
	TArray<int32> FilteredIndices;
	FString ScanTime;

	// Filter state
	FString SearchText;
	TSharedPtr<FString> SelectedType;
	TArray<TSharedPtr<FString>> TypeOptions;

	// UI elements
	TSharedPtr<STextBlock> StatsLabel;
	TSharedPtr<STextBlock> DirtyLabel;
	TSharedPtr<STextBlock> ScanTimeLabel;
	TSharedPtr<STextBlock> FilteredCountLabel;
	TSharedPtr<SVerticalBox> AssetListBox;

	void LoadRegistryFromDisk();
	void ApplyFilter();
	void RebuildTypeOptions();

	FReply OnRefreshClicked();
	FReply OnScanClicked();
	void OnSearchTextChanged(const FText& NewText);
	void OnTypeFilterChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
};
