#include "ArborGameCodexTab.h"
#include "ArborGameCodexWidget.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "ArborGameCodexTab"

const FName FArborGameCodexTab::TabId(TEXT("ArborGameCodex"));
TWeakPtr<SArborGameCodexWidget> FArborGameCodexTab::ActiveWidget;

void FArborGameCodexTab::Register()
{
	if (FGlobalTabmanager::Get()->HasTabSpawner(TabId))
	{
		return;
	}

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		TabId,
		FOnSpawnTab::CreateStatic(&FArborGameCodexTab::SpawnTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Game Codex"))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.ContentBrowser"));
}

void FArborGameCodexTab::Unregister()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

TSharedPtr<SArborGameCodexWidget> FArborGameCodexTab::GetWidget()
{
	TSharedPtr<SArborGameCodexWidget> Widget = ActiveWidget.Pin();
	if (!Widget.IsValid())
	{
		TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(TabId);
		if (Tab.IsValid())
		{
			Tab->RequestCloseTab();
		}
		FGlobalTabmanager::Get()->TryInvokeTab(TabId);
		Widget = ActiveWidget.Pin();
	}
	return Widget;
}

TSharedRef<SDockTab> FArborGameCodexTab::SpawnTab(const FSpawnTabArgs& Args)
{
	TSharedPtr<SArborGameCodexWidget> Widget;

	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.TabRole(NomadTab)
		.Label(LOCTEXT("TabLabel", "Game Codex"))
		[
			SAssignNew(Widget, SArborGameCodexWidget)
		];

	ActiveWidget = Widget;

	return Tab;
}

#undef LOCTEXT_NAMESPACE
