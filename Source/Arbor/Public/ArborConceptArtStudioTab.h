#pragma once

#include "CoreMinimal.h"
#include "Framework/Docking/TabManager.h"

class SDockTab;
class SArborConceptArtStudioWidget;

class FArborConceptArtStudioTab
{
public:
	static const FName TabId;

	static void Register();
	static void Unregister();

	/** Get the active widget instance (may be null if the tab hasn't been opened). */
	static TSharedPtr<SArborConceptArtStudioWidget> GetWidget();

private:
	static TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);

	static TWeakPtr<SArborConceptArtStudioWidget> ActiveWidget;
};
