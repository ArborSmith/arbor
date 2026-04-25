#pragma once

#include "CoreMinimal.h"

/**
 * Static facade for sending messages to Claude from any Arbor widget.
 * Internally routes through the Chat tab/widget — callers don't need
 * to know about FArborChatTab or SArborChatWidget.
 */
class FArborClaude
{
public:
	/** Send a message to Claude. Opens/creates the Chat tab if needed. Returns true on success. */
	static bool SendMessage(const FString& Message);

	/** Send a message to Claude in plan mode. Opens/creates the Chat tab, switches to plan mode, then sends. */
	static bool SendMessageInPlanMode(const FString& Message);

	/** Returns true if the Claude subprocess is running. Does NOT create the tab. */
	static bool IsProcessRunning();

	/** Returns true if Claude is processing a request. Does NOT create the tab. */
	static bool IsWaitingForResponse();
};
