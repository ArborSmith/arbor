#include "ArborMaterialCatalogTab.h"
#include "ArborMaterialCatalogWidget.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "ArborMaterialCatalogTab"

const FName FArborMaterialCatalogTab::TabId(TEXT("ArborMaterialCatalog"));

void FArborMaterialCatalogTab::Register()
{
	if (FGlobalTabmanager::Get()->HasTabSpawner(TabId))
	{
		return;
	}

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		TabId,
		FOnSpawnTab::CreateStatic(&FArborMaterialCatalogTab::SpawnTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Material Catalog"))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.ContentBrowser"));
}

void FArborMaterialCatalogTab::Unregister()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

TSharedRef<SDockTab> FArborMaterialCatalogTab::SpawnTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(NomadTab)
		.Label(LOCTEXT("TabLabel", "Material Catalog"))
		[
			SNew(SArborMaterialCatalogWidget)
		];
}

#undef LOCTEXT_NAMESPACE
