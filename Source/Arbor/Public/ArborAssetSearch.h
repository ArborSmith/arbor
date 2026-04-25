#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborAssetSearch.generated.h"

UCLASS()
class ARBOR_API UArborAssetSearch : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Search the project asset index for matching assets.
	 *
	 * Splits Query into space-separated tokens and scores each indexed
	 * asset by name, tags, and path relevance.
	 *
	 * @param Query        Space-separated search terms (e.g. "mossy rock").
	 * @param TypeFilter   Comma-separated type names to restrict results
	 *                     (e.g. "StaticMesh,SkeletalMesh"). Empty = all types.
	 * @param Limit        Maximum results to return.
	 * @return JSON string: [{path, name, type, tags, score}, ...]
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Assets")
	static FString FindAsset(const FString& Query, const FString& TypeFilter, int32 Limit);

	/**
	 * Force a rescan of the project content and rebuild the in-memory index.
	 *
	 * @return JSON string: {success, stats: {type: count}, total}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Assets")
	static FString ScanProject();

	/**
	 * Return per-type asset counts from the current index (no rescan).
	 *
	 * @return JSON string: {type: count, ...}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Assets")
	static FString GetRegistryStats();

private:
	struct FAssetEntry
	{
		FString Path;
		FString Name;
		FString Type;
		TArray<FString> Tags;
	};

	static TArray<FAssetEntry> CachedAssets;
	static bool bCacheValid;

	static void EnsureCache();
	static void RebuildCache();
	static TArray<FString> BuildTags(const FString& Path, const FString& Name);
	static int32 ScoreAsset(const FAssetEntry& Entry, const TArray<FString>& Tokens, const TSet<FString>& TypeSet);
	static bool IsExcluded(const FString& Path);
};
