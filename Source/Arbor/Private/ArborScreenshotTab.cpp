#include "ArborScreenshotTab.h"
#include "ArborScreenshotWidget.h"
#include "Widgets/Docking/SDockTab.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "ArborScreenshotTab"

const FName FArborScreenshotTab::TabId(TEXT("ArborScreenshots"));

void FArborScreenshotTab::Register()
{
	if (FGlobalTabmanager::Get()->HasTabSpawner(TabId))
	{
		return;
	}

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		TabId,
		FOnSpawnTab::CreateStatic(&FArborScreenshotTab::SpawnTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Arbor Screenshots"))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));
}

void FArborScreenshotTab::Unregister()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

TSharedRef<SDockTab> FArborScreenshotTab::SpawnTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(NomadTab)
		.Label(LOCTEXT("TabLabel", "Arbor Screenshots"))
		[
			SNew(SArborScreenshotWidget)
		];
}

#undef LOCTEXT_NAMESPACE
