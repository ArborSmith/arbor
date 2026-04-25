#pragma once

#include "CoreMinimal.h"

/**
 * Modal dialog for reviewing/editing the AI prompt before sending.
 * Opens a non-modal SWindow with the auto-generated prompt (editable)
 * and a separate text box for additional user instructions.
 */
namespace ArborAIPromptDialog
{
	/**
	 * Opens a prompt review window. On Send, combines prompt + instructions
	 * and calls FArborClaude::SendMessage() (or SendMessageInPlanMode if bUsePlanMode).
	 * On Cancel, closes with no effect.
	 *
	 * @param GeneratedPrompt  Pre-filled prompt text (editable by user)
	 * @param WindowTitle      Title for the window (e.g. "Improve Enemy with AI")
	 * @param bUsePlanMode     If true, Send button calls SendMessageInPlanMode instead of SendMessage
	 */
	void Show(const FString& GeneratedPrompt, const FString& WindowTitle = TEXT("Improve with AI"), bool bUsePlanMode = false);
}
