#include "ArborTextVariationTab.h"
#include "ArborTextVariationWidget.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "ArborTextVariationTab"

const FName FArborTextVariationTab::TabId(TEXT("ArborTextVariation"));
TWeakPtr<SArborTextVariationWidget> FArborTextVariationTab::ActiveWidget;

void FArborTextVariationTab::Register()
{
	if (FGlobalTabmanager::Get()->HasTabSpawner(TabId))
	{
		return;
	}

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		TabId,
		FOnSpawnTab::CreateStatic(&FArborTextVariationTab::SpawnTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Arbor Text Variation Review"))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));
}

void FArborTextVariationTab::Unregister()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

TSharedPtr<SArborTextVariationWidget> FArborTextVariationTab::GetWidget()
{
	return ActiveWidget.Pin();
}

TSharedRef<SDockTab> FArborTextVariationTab::SpawnTab(const FSpawnTabArgs& Args)
{
	TSharedPtr<SArborTextVariationWidget> Widget;

	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.TabRole(NomadTab)
		.Label(LOCTEXT("TabLabel", "Arbor Text Variation Review"))
		[
			SAssignNew(Widget, SArborTextVariationWidget)
		];

	ActiveWidget = Widget;

	return Tab;
}

#undef LOCTEXT_NAMESPACE
