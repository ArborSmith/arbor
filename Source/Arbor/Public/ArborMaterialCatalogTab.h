// Tab spawner for the Material Catalog editor window. Mirrors the pattern
// used by FArborRegistryTab / FArborGameCodexTab.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Docking/TabManager.h"

class FArborMaterialCatalogTab
{
public:
	static const FName TabId;

	static void Register();
	static void Unregister();

private:
	static TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);
};
