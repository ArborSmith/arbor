#include "ArborRegistryHelper.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/PlatformAtomics.h"

static bool bAssetRegistryDirty = true;
static volatile int32 AssetChangeCounter = 0;
static TArray<FString> RecentlyAddedPaths;
static FCriticalSection RecentAssetsLock;

bool UArborRegistryHelper::IsAssetRegistryDirty()
{
	return bAssetRegistryDirty;
}

void UArborRegistryHelper::ClearAssetRegistryDirty()
{
	bAssetRegistryDirty = false;
}

int32 UArborRegistryHelper::GetAssetChangeCounter()
{
	return AssetChangeCounter;
}

FString UArborRegistryHelper::GetRecentlyAddedAssets()
{
	FScopeLock Lock(&RecentAssetsLock);

	FString Result = TEXT("[");
	for (int32 i = 0; i < RecentlyAddedPaths.Num(); ++i)
	{
		if (i > 0) Result += TEXT(",");
		// Escape backslashes and quotes for JSON
		FString Escaped = RecentlyAddedPaths[i].Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
		Result += TEXT("\"") + Escaped + TEXT("\"");
	}
	Result += TEXT("]");

	RecentlyAddedPaths.Empty();
	return Result;
}

bool UArborRegistryHelper::IsAssetRegistryReady()
{
	IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();
	return !AssetRegistry.IsLoadingAssets();
}

void UArborRegistryHelper::MarkAssetRegistryDirty()
{
	bAssetRegistryDirty = true;
}

void UArborRegistryHelper::OnAssetChanged(const FAssetData& AssetData)
{
	bAssetRegistryDirty = true;
	FPlatformAtomics::InterlockedIncrement(&AssetChangeCounter);

	FScopeLock Lock(&RecentAssetsLock);
	RecentlyAddedPaths.Add(AssetData.GetObjectPathString());
	if (RecentlyAddedPaths.Num() > 100)
	{
		RecentlyAddedPaths.RemoveAt(0, RecentlyAddedPaths.Num() - 100);
	}
}
