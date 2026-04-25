#include "ArborTextureReviewTab.h"
#include "ArborTextureReviewWidget.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "ArborTextureReviewTab"

const FName FArborTextureReviewTab::TabId(TEXT("ArborTextureReview"));
TWeakPtr<SArborTextureReviewWidget> FArborTextureReviewTab::ActiveWidget;

void FArborTextureReviewTab::Register()
{
	if (FGlobalTabmanager::Get()->HasTabSpawner(TabId))
	{
		return;
	}

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		TabId,
		FOnSpawnTab::CreateStatic(&FArborTextureReviewTab::SpawnTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Arbor Texture Review"))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));
}

void FArborTextureReviewTab::Unregister()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

TSharedPtr<SArborTextureReviewWidget> FArborTextureReviewTab::GetWidget()
{
	return ActiveWidget.Pin();
}

TSharedRef<SDockTab> FArborTextureReviewTab::SpawnTab(const FSpawnTabArgs& Args)
{
	TSharedPtr<SArborTextureReviewWidget> Widget;

	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.TabRole(NomadTab)
		.Label(LOCTEXT("TabLabel", "Arbor Texture Review"))
		[
			SAssignNew(Widget, SArborTextureReviewWidget)
		];

	ActiveWidget = Widget;

	return Tab;
}

#undef LOCTEXT_NAMESPACE
