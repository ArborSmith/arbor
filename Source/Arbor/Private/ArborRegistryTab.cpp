#include "ArborRegistryTab.h"
#include "ArborRegistryWidget.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "ArborRegistryTab"

const FName FArborRegistryTab::TabId(TEXT("ArborRegistry"));

void FArborRegistryTab::Register()
{
	if (FGlobalTabmanager::Get()->HasTabSpawner(TabId))
	{
		return;
	}

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		TabId,
		FOnSpawnTab::CreateStatic(&FArborRegistryTab::SpawnTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Arbor Registry"))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.ContentBrowser"));
}

void FArborRegistryTab::Unregister()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

TSharedRef<SDockTab> FArborRegistryTab::SpawnTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(NomadTab)
		.Label(LOCTEXT("TabLabel", "Arbor Registry"))
		[
			SNew(SArborRegistryWidget)
		];
}

#undef LOCTEXT_NAMESPACE
