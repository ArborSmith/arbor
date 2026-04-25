#include "ArborConceptArtStudioTab.h"
#include "ArborConceptArtStudioWidget.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "ArborConceptArtStudioTab"

const FName FArborConceptArtStudioTab::TabId(TEXT("ArborConceptArtStudio"));
TWeakPtr<SArborConceptArtStudioWidget> FArborConceptArtStudioTab::ActiveWidget;

void FArborConceptArtStudioTab::Register()
{
	if (FGlobalTabmanager::Get()->HasTabSpawner(TabId))
	{
		return;
	}

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		TabId,
		FOnSpawnTab::CreateStatic(&FArborConceptArtStudioTab::SpawnTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Concept Art Studio"))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));
}

void FArborConceptArtStudioTab::Unregister()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

TSharedPtr<SArborConceptArtStudioWidget> FArborConceptArtStudioTab::GetWidget()
{
	return ActiveWidget.Pin();
}

TSharedRef<SDockTab> FArborConceptArtStudioTab::SpawnTab(const FSpawnTabArgs& Args)
{
	TSharedPtr<SArborConceptArtStudioWidget> Widget;

	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.TabRole(NomadTab)
		.Label(LOCTEXT("TabLabel", "Concept Art Studio"))
		[
			SAssignNew(Widget, SArborConceptArtStudioWidget)
		];

	ActiveWidget = Widget;

	return Tab;
}

#undef LOCTEXT_NAMESPACE
