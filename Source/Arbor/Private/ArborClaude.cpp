#include "ArborClaude.h"
#include "ArborChatTab.h"
#include "ArborChatWidget.h"
#include "Framework/Docking/TabManager.h"

bool FArborClaude::SendMessage(const FString& Message)
{
	FGlobalTabmanager::Get()->TryInvokeTab(FArborChatTab::TabId);

	TSharedPtr<SArborChatWidget> Widget = FArborChatTab::GetWidget();
	if (!Widget.IsValid())
	{
		return false;
	}

	Widget->SendExternalMessage(Message);
	return true;
}

bool FArborClaude::SendMessageInPlanMode(const FString& Message)
{
	FGlobalTabmanager::Get()->TryInvokeTab(FArborChatTab::TabId);

	TSharedPtr<SArborChatWidget> Widget = FArborChatTab::GetWidget();
	if (!Widget.IsValid())
	{
		return false;
	}

	Widget->SendExternalMessageInPlanMode(Message);
	return true;
}

bool FArborClaude::IsProcessRunning()
{
	TSharedPtr<SArborChatWidget> Widget = FArborChatTab::FindWidget();
	return Widget.IsValid() && Widget->IsClaudeRunning();
}

bool FArborClaude::IsWaitingForResponse()
{
	TSharedPtr<SArborChatWidget> Widget = FArborChatTab::FindWidget();
	return Widget.IsValid() && Widget->IsClaudeWaiting();
}
