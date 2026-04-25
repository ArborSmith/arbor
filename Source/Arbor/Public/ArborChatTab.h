#pragma once

#include "CoreMinimal.h"
#include "Framework/Docking/TabManager.h"

class SDockTab;
class SArborChatWidget;

class FArborChatTab
{
public:
	static const FName TabId;

	static void Register();
	static void Unregister();

	/** Get the active chat widget instance. Creates the tab if needed. */
	static TSharedPtr<SArborChatWidget> GetWidget();

	/** Get the active chat widget if it exists. Does NOT create the tab. */
	static TSharedPtr<SArborChatWidget> FindWidget();

private:
	static TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& Args);

	static TWeakPtr<SArborChatWidget> ActiveWidget;
};
