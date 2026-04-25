#include "ArborChatTab.h"
#include "ArborChatWidget.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "ArborChatTab"

const FName FArborChatTab::TabId(TEXT("ArborChat"));
TWeakPtr<SArborChatWidget> FArborChatTab::ActiveWidget;

void FArborChatTab::Register()
{
	if (FGlobalTabmanager::Get()->HasTabSpawner(TabId))
	{
		return;
	}

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		TabId,
		FOnSpawnTab::CreateStatic(&FArborChatTab::SpawnTab))
		.SetDisplayName(LOCTEXT("ChatTabTitle", "Arbor Chat"))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment"));
}

void FArborChatTab::Unregister()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

TSharedPtr<SArborChatWidget> FArborChatTab::GetWidget()
{
	TSharedPtr<SArborChatWidget> Widget = ActiveWidget.Pin();
	if (!Widget.IsValid())
	{
		// Tab may exist from before ActiveWidget tracking was added (e.g. after Live Coding).
		// Close and reopen so the new SpawnTab sets ActiveWidget.
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

TSharedPtr<SArborChatWidget> FArborChatTab::FindWidget()
{
	return ActiveWidget.Pin();
}

TSharedRef<SDockTab> FArborChatTab::SpawnTab(const FSpawnTabArgs& Args)
{
	TSharedPtr<SArborChatWidget> Widget;

	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.TabRole(NomadTab)
		.Label(LOCTEXT("ChatTabLabel", "Arbor Chat"))
		[
			SAssignNew(Widget, SArborChatWidget)
		];

	ActiveWidget = Widget;

	return Tab;
}

#undef LOCTEXT_NAMESPACE
