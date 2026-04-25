#pragma once

#include "CoreMinimal.h"

/**
 * Lightweight dialog for iterating on a single codex field with AI.
 * Shows the current value (read-only) and asks for user instructions,
 * then sends a focused prompt to Claude targeting only that field.
 */
namespace ArborAIFieldIterateDialog
{
	void Show(
		const FString& FieldName,
		const FString& CurrentValue,
		const FString& ContextSummary,
		const FString& AssetPath,
		const FString& FieldKey);
}
