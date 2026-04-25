#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

struct FArborCodexContext;
class STextComboBox;
class SWidgetSwitcher;
class SHorizontalBox;
class STextBlock;
class SBorder;

class SArborGameCodexWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArborGameCodexWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	~SArborGameCodexWidget();

private:
	TSharedPtr<FArborCodexContext> CodexContext;

	// Context selector
	TSharedPtr<STextComboBox> ContextSelectorCombo;
	TArray<TSharedPtr<FString>> ContextDisplayNames;
	TArray<FString> ContextAssetPaths;

	struct FTabDef
	{
		FString Label;
		TSharedRef<SWidget> Widget;
	};
	TArray<FTabDef> AllTabs;

	// Dynamic tab bar
	TSharedPtr<SHorizontalBox> TabBarBox;
	TSharedPtr<SWidgetSwitcher> SectionSwitcher;
	int32 ActiveSectionIndex = 0;
	TArray<TSharedPtr<SButton>> TabButtons;
	TArray<TSharedPtr<STextBlock>> TabTexts;
	TArray<TSharedPtr<SBorder>> TabUnderlines;
	TArray<int32> ActiveTabMapping;  // visible tab index → AllTabs index

	void ScanContextAssets();
	void OnContextSelected(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo);
	FReply OnNewContextClicked();
	FReply OnScanClicked();
	void SetActiveSection(int32 VisibleIndex);
	void UpdateTabStyles();
	void RebuildTabBar();

	// Auto-refresh on asset changes
	void OnAssetAddedOrRemoved(const FAssetData& AssetData);
	FDelegateHandle AssetAddedHandle;
	FDelegateHandle AssetRemovedHandle;

};
