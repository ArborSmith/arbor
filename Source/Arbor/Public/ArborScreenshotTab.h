#pragma once

#include "CoreMinimal.h"
#include "Framework/Docking/TabManager.h"

class FArborScreenshotTab
{
public:
	static const FName TabId;

	static void Register();
	static void Unregister();

private:
	static TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);
};
