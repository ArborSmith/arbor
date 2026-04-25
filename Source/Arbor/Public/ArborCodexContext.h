#pragma once

#include "CoreMinimal.h"
#include "ArborGameContextTypes.h"

struct FArborCodexContext
{
	TSoftObjectPtr<UArborGameContextAsset> SelectedContext;
	FString SelectedContextPath;

	DECLARE_MULTICAST_DELEGATE(FOnContextChanged);
	FOnContextChanged OnContextChanged;

	void SetContext(const FString& AssetPath)
	{
		SelectedContextPath = AssetPath;
		if (!AssetPath.IsEmpty())
		{
			SelectedContext = TSoftObjectPtr<UArborGameContextAsset>(FSoftObjectPath(AssetPath));
		}
		else
		{
			SelectedContext.Reset();
		}
		OnContextChanged.Broadcast();
	}

	bool HasContext() const { return !SelectedContextPath.IsEmpty(); }
};
