#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborRegistryHelper.generated.h"

/**
 * Exposes a dirty flag driven by IAssetRegistry delegates so that
 * Python (arbor.registry) can detect mid-session asset additions
 * and rescan automatically.
 *
 * Also provides an asset change counter and recently-added path buffer
 * for Fab import status tracking (arbor.fab.get_import_status).
 */
UCLASS()
class ARBOR_API UArborRegistryHelper : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static bool IsAssetRegistryDirty();

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static void ClearAssetRegistryDirty();

	/** Monotonically increasing counter — increments on every OnAssetAdded/OnAssetRemoved. */
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static int32 GetAssetChangeCounter();

	/** Drain-on-read: returns JSON array of content paths added since last call, then clears the buffer. */
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString GetRecentlyAddedAssets();

	/** Returns true if UE5's Asset Registry has finished its initial async scan. */
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static bool IsAssetRegistryReady();

	/** Callback bound to IAssetRegistry::OnAssetAdded / OnAssetRemoved. */
	static void OnAssetChanged(const FAssetData& AssetData);

	/** Force-set the dirty flag (used by OnFilesLoaded callback). */
	static void MarkAssetRegistryDirty();
};
