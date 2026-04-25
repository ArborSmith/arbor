#pragma once

#include "CoreMinimal.h"
#include "Framework/Docking/TabManager.h"

class SDockTab;
class SArborTextVariationWidget;

class FArborTextVariationTab
{
public:
	static const FName TabId;

	static void Register();
	static void Unregister();

	/** Get the active widget instance (may be null if the tab hasn't been opened). */
	static TSharedPtr<SArborTextVariationWidget> GetWidget();

private:
	static TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);

	static TWeakPtr<SArborTextVariationWidget> ActiveWidget;
};
