#pragma once

#include "CoreMinimal.h"
#include "Framework/Docking/TabManager.h"

class SArborImageViewerWidget;

class FArborImageViewerTab
{
public:
	static const FName TabId;

	static void Register();
	static void Unregister();

	static TSharedPtr<SArborImageViewerWidget> GetWidget();

private:
	static TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);
	static TWeakPtr<SArborImageViewerWidget> ActiveWidget;
};
