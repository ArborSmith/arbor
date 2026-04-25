#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FabHelper.generated.h"

/**
 * Helper class that accesses the Fab plugin's private APIs via UE runtime reflection.
 * No compile-time dependency on the Fab module — all calls go through FindObject + ProcessEvent.
 */
UCLASS()
class ARBOR_API UFabHelper : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Returns EOS Bearer token from the Fab plugin. Empty string if not logged in or Fab plugin not loaded. */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Fab")
	static FString GetFabAuthToken();

	/** Check if user is logged into Fab (token is non-empty). */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Fab")
	static bool IsFabLoggedIn();

	/**
	 * Trigger the Fab plugin's native import workflow via reflection.
	 * MetadataJson should contain:
	 *   download_url (string), asset_id (string), asset_name (string),
	 *   asset_type (string), listing_type (string), is_quixel (bool),
	 *   distribution_urls (array of strings)
	 * Returns true if the call was dispatched successfully.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Fab")
	static bool FabImportAsset(const FString& MetadataJson);

	/**
	 * List all UFunction names available on UFabBrowserApi via reflection.
	 * Returns a JSON array of function names, useful for runtime discovery.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Fab")
	static FString ListFabApiFunctions();

	/**
	 * Retrieve the user's Fab library (owned/claimed assets) via the Fab plugin.
	 * Tries to call UFabBrowserApi library-related methods via reflection.
	 * Returns JSON with the library data, or error info if the method is not available.
	 * ParamsJson may contain: page (int), per_page (int).
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Fab")
	static FString GetFabLibrary(const FString& ParamsJson);

private:
	/** Find the singleton UFabBrowserApi instance via reflection. Returns nullptr if Fab plugin is not loaded. */
	static UObject* FindFabBrowserApi();
};
