#pragma once

#include "CoreMinimal.h"
#include "Framework/Docking/TabManager.h"

class SDockTab;
class SArborGameCodexWidget;

class FArborGameCodexTab
{
public:
	static const FName TabId;

	static void Register();
	static void Unregister();

	static TSharedPtr<SArborGameCodexWidget> GetWidget();

private:
	static TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);

	static TWeakPtr<SArborGameCodexWidget> ActiveWidget;
};
