#include "ArborGameCodexWidget.h"
#include "ArborCodexContext.h"
#include "ArborCodexStyle.h"
#include "ArborCodexWidgetHelpers.h"
#include "ArborCodexOverviewWidget.h"
#include "ArborCodexLocationsWidget.h"
#include "ArborCharacterSheetWidget.h"
#include "ArborCodexFeaturesWidget.h"
#include "ArborCodexPillarsWidget.h"
#include "ArborGameContextTypes.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SBorder.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "UObject/SavePackage.h"

#define LOCTEXT_NAMESPACE "ArborGameCodexWidget"

void SArborGameCodexWidget::Construct(const FArguments& InArgs)
{
	CodexContext = MakeShared<FArborCodexContext>();

	AllTabs = {
		{TEXT("Overview"),   SNew(SArborCodexOverviewWidget).CodexContext(CodexContext)},
		{TEXT("Pillars"),    SNew(SArborCodexPillarsWidget).CodexContext(CodexContext)},
		{TEXT("World"),      SNew(SArborCodexLocationsWidget).CodexContext(CodexContext)},
		{TEXT("Features"),   SNew(SArborCodexFeaturesWidget).CodexContext(CodexContext)},
		{TEXT("Characters"), SNew(SArborCharacterSheetWidget).CodexContext(CodexContext)},
	};

	ChildSlot
	[
		// Panel background
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(ArborCodexStyle::Bg::Panel)
		.Padding(0.0f)
		[
			SNew(SVerticalBox)

			// ---- Context selector bar ----
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(ArborCodexStyle::Bg::Surface)
				.Padding(FMargin(12.0f, 10.0f, 12.0f, 10.0f))
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 10.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("GameContext", "Game Context:"))
						.Font(ArborCodexStyle::Font::FieldLabel())
						.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Secondary))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(0.0f, 0.0f, 10.0f, 0.0f)
					[
						SAssignNew(ContextSelectorCombo, STextComboBox)
						.OptionsSource(&ContextDisplayNames)
						.OnSelectionChanged(this, &SArborGameCodexWidget::OnContextSelected)
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						ArborCodexHelpers::MakeSecondaryButton(
							LOCTEXT("NewContext", "+ New"),
							FOnClicked::CreateSP(this, &SArborGameCodexWidget::OnNewContextClicked))
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						ArborCodexHelpers::MakeSecondaryButton(
							LOCTEXT("Scan", "Scan"),
							FOnClicked::CreateSP(this, &SArborGameCodexWidget::OnScanClicked))
					]
				]
			]

			// ---- Tab bar ----
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(ArborCodexStyle::Bg::TabBar)
				.Padding(FMargin(10.0f, 4.0f, 10.0f, 0.0f))
				[
					SAssignNew(TabBarBox, SHorizontalBox)
				]
			]

			// ---- Thin separator ----
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(ArborCodexStyle::Border::Subtle)
				.Padding(0.0f)
				[
					SNew(SBox).HeightOverride(1.0f)
				]
			]

			// ---- Section content ----
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SAssignNew(SectionSwitcher, SWidgetSwitcher)
				.WidgetIndex(0)
			]
		]
	];

	// Add all tab widgets to the switcher
	for (const FTabDef& Tab : AllTabs)
	{
		SectionSwitcher->AddSlot()
		[
			Tab.Widget
		];
	}

	// Build tab bar with all tabs visible initially (empty EnabledCollections = show all)
	RebuildTabBar();

	ScanContextAssets();

	// Auto-refresh when codex assets are added or removed
	IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();
	AssetAddedHandle = AssetRegistry.OnAssetAdded().AddSP(this, &SArborGameCodexWidget::OnAssetAddedOrRemoved);
	AssetRemovedHandle = AssetRegistry.OnAssetRemoved().AddSP(this, &SArborGameCodexWidget::OnAssetAddedOrRemoved);
}

void SArborGameCodexWidget::RebuildTabBar()
{
	TabBarBox->ClearChildren();
	TabButtons.Empty();
	TabTexts.Empty();
	TabUnderlines.Empty();
	ActiveTabMapping.Empty();

	// All tabs are permanent — build them all
	for (int32 i = 0; i < AllTabs.Num(); i++)
	{
		const FTabDef& Tab = AllTabs[i];

		int32 VisibleIdx = ActiveTabMapping.Num();
		ActiveTabMapping.Add(i);

		TSharedPtr<SButton> Btn;
		TSharedPtr<STextBlock> Txt;
		TSharedPtr<SBorder> Underline;

		TabBarBox->AddSlot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, ArborCodexStyle::Spacing::TabGap, 0.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(Btn, SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.ContentPadding(ArborCodexStyle::Spacing::TabPadding)
				.OnClicked_Lambda([this, VisibleIdx]()
				{
					SetActiveSection(VisibleIdx);
					return FReply::Handled();
				})
				[
					SAssignNew(Txt, STextBlock)
					.Text(FText::FromString(Tab.Label))
					.Font(ArborCodexStyle::Font::TabInactive())
					.ColorAndOpacity(FSlateColor(ArborCodexStyle::Text::Secondary))
				]
			]

			// Bottom accent bar
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(Underline, SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor::Transparent)
				.Padding(0.0f)
				[
					SNew(SBox).HeightOverride(2.0f)
				]
			]
		];

		TabButtons.Add(Btn);
		TabTexts.Add(Txt);
		TabUnderlines.Add(Underline);
	}

	// Reset to first tab if current selection is invalid
	if (ActiveSectionIndex >= ActiveTabMapping.Num())
	{
		ActiveSectionIndex = 0;
	}

	UpdateTabStyles();

	// Set the switcher to the correct widget
	if (ActiveTabMapping.Num() > 0 && SectionSwitcher.IsValid())
	{
		SectionSwitcher->SetActiveWidgetIndex(ActiveTabMapping[ActiveSectionIndex]);
	}
}

void SArborGameCodexWidget::ScanContextAssets()
{
	ContextDisplayNames.Empty();
	ContextAssetPaths.Empty();

	// Add "(None)" option
	ContextDisplayNames.Add(MakeShared<FString>(TEXT("(None)")));
	ContextAssetPaths.Add(TEXT(""));

	IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();
	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssetsByClass(UArborGameContextAsset::StaticClass()->GetClassPathName(), AssetList);

	for (const FAssetData& AssetData : AssetList)
	{
		UObject* Obj = AssetData.GetAsset();
		UArborGameContextAsset* Ctx = Cast<UArborGameContextAsset>(Obj);
		if (!Ctx)
		{
			continue;
		}

		FString DisplayName = Ctx->GameTitle.IsEmpty()
			? AssetData.AssetName.ToString()
			: Ctx->GameTitle;
		ContextDisplayNames.Add(MakeShared<FString>(DisplayName));
		ContextAssetPaths.Add(AssetData.GetObjectPathString());
	}

	if (ContextSelectorCombo.IsValid())
	{
		ContextSelectorCombo->RefreshOptions();

		// Try to keep current selection, otherwise select "(None)"
		FString CurrentPath = CodexContext.IsValid() ? CodexContext->SelectedContextPath : TEXT("");
		int32 SelectIndex = 0;
		for (int32 i = 0; i < ContextAssetPaths.Num(); i++)
		{
			if (ContextAssetPaths[i] == CurrentPath)
			{
				SelectIndex = i;
				break;
			}
		}
		if (ContextDisplayNames.Num() > SelectIndex)
		{
			ContextSelectorCombo->SetSelectedItem(ContextDisplayNames[SelectIndex]);
		}
	}
}

void SArborGameCodexWidget::OnContextSelected(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
{
	if (!NewValue.IsValid() || !CodexContext.IsValid())
	{
		return;
	}

	for (int32 i = 0; i < ContextDisplayNames.Num(); i++)
	{
		if (*ContextDisplayNames[i] == *NewValue)
		{
			CodexContext->SetContext(ContextAssetPaths[i]);
			return;
		}
	}
}

FReply SArborGameCodexWidget::OnNewContextClicked()
{
	FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	FString AssetName = FString::Printf(TEXT("GC_%s"), *Timestamp);
	FString PackagePath = FString::Printf(TEXT("/Game/GameCodex/%s"), *AssetName);

	UPackage* Package = CreatePackage(*PackagePath);
	UArborGameContextAsset* NewCtx = NewObject<UArborGameContextAsset>(Package, *AssetName, RF_Public | RF_Standalone);
	NewCtx->GameTitle = TEXT("New Game Context");

	FAssetRegistryModule::AssetCreated(NewCtx);
	Package->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, NewCtx, *PackageFilename, SaveArgs);

	ScanContextAssets();

	// Select the new context
	FString NewPath = NewCtx->GetPathName();
	for (int32 i = 0; i < ContextAssetPaths.Num(); i++)
	{
		if (ContextAssetPaths[i] == NewPath)
		{
			if (ContextSelectorCombo.IsValid())
			{
				ContextSelectorCombo->SetSelectedItem(ContextDisplayNames[i]);
			}
			CodexContext->SetContext(ContextAssetPaths[i]);
			break;
		}
	}

	// Switch to Overview tab to edit
	SetActiveSection(0);

	return FReply::Handled();
}

FReply SArborGameCodexWidget::OnScanClicked()
{
	ScanContextAssets();
	return FReply::Handled();
}

void SArborGameCodexWidget::SetActiveSection(int32 VisibleIndex)
{
	ActiveSectionIndex = VisibleIndex;
	if (SectionSwitcher.IsValid() && ActiveTabMapping.IsValidIndex(VisibleIndex))
	{
		SectionSwitcher->SetActiveWidgetIndex(ActiveTabMapping[VisibleIndex]);
	}
	UpdateTabStyles();
}

void SArborGameCodexWidget::UpdateTabStyles()
{
	for (int32 i = 0; i < TabButtons.Num(); i++)
	{
		const bool bActive = (i == ActiveSectionIndex);

		if (TabTexts.IsValidIndex(i) && TabTexts[i].IsValid())
		{
			TabTexts[i]->SetFont(bActive ? ArborCodexStyle::Font::TabActive() : ArborCodexStyle::Font::TabInactive());
			TabTexts[i]->SetColorAndOpacity(FSlateColor(bActive ? ArborCodexStyle::Accent::Primary : ArborCodexStyle::Text::Secondary));
		}

		if (TabUnderlines.IsValidIndex(i) && TabUnderlines[i].IsValid())
		{
			TabUnderlines[i]->SetBorderBackgroundColor(bActive ? ArborCodexStyle::Accent::Primary : FLinearColor::Transparent);
		}
	}
}

SArborGameCodexWidget::~SArborGameCodexWidget()
{
	if (FAssetRegistryModule* Module = FModuleManager::GetModulePtr<FAssetRegistryModule>("AssetRegistry"))
	{
		IAssetRegistry& AssetRegistry = Module->Get();
		AssetRegistry.OnAssetAdded().Remove(AssetAddedHandle);
		AssetRegistry.OnAssetRemoved().Remove(AssetRemovedHandle);
	}
}

void SArborGameCodexWidget::OnAssetAddedOrRemoved(const FAssetData& AssetData)
{
	if (!CodexContext.IsValid() || !CodexContext->HasContext()) return;

	// Check if asset is a codex type (all live under /Game/GameCodex/)
	const FString AssetPath = AssetData.GetObjectPathString();
	if (!AssetPath.StartsWith(TEXT("/Game/GameCodex/"))) return;

	// Check if it's a new GameContext (refresh the combo box)
	if (AssetData.AssetClassPath == UArborGameContextAsset::StaticClass()->GetClassPathName())
	{
		ScanContextAssets();
		return;
	}

	// For other codex entries, re-broadcast context changed to refresh category widgets
	CodexContext->OnContextChanged.Broadcast();
}

#undef LOCTEXT_NAMESPACE
