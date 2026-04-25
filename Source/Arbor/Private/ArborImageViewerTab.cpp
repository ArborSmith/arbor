#include "ArborImageViewerTab.h"
#include "ArborImageViewerWidget.h"
#include "Widgets/Docking/SDockTab.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "ArborImageViewerTab"

const FName FArborImageViewerTab::TabId(TEXT("ArborImageViewer"));
TWeakPtr<SArborImageViewerWidget> FArborImageViewerTab::ActiveWidget;

void FArborImageViewerTab::Register()
{
	if (FGlobalTabmanager::Get()->HasTabSpawner(TabId))
	{
		return;
	}

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		TabId,
		FOnSpawnTab::CreateStatic(&FArborImageViewerTab::SpawnTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Arbor Image Viewer"))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));
}

void FArborImageViewerTab::Unregister()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

TSharedPtr<SArborImageViewerWidget> FArborImageViewerTab::GetWidget()
{
	return ActiveWidget.Pin();
}

TSharedRef<SDockTab> FArborImageViewerTab::SpawnTab(const FSpawnTabArgs& Args)
{
	TSharedPtr<SArborImageViewerWidget> Widget;

	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.TabRole(NomadTab)
		.Label(LOCTEXT("TabLabel", "Image Viewer"))
		[
			SAssignNew(Widget, SArborImageViewerWidget)
		];

	ActiveWidget = Widget;

	return Tab;
}

#undef LOCTEXT_NAMESPACE
