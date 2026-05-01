#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborLevelTools.generated.h"

/**
 * Editor-only level (map) navigation utilities.
 *
 * Fills the gap that public Arbor's `ue5_actors` only operates on the
 * currently-open level — no way to navigate between maps or check which
 * one is active. Pair: `LoadLevel` → `SaveCurrentLevel` for the round
 * trip; `GetCurrentLevel` for orientation.
 *
 * MCP family: `ue5_level` (writes) + `ue5_level_query` (reads).
 *
 * NOTE: Arbitrary FProperty edits on actors live in
 * `UArborActorTools::SetActorProperty` (next to `ModifyActor`) — that's
 * where users look for actor-modify ops.
 */
UCLASS()
class ARBOR_API UArborLevelTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Load a different map in the editor.
	 * @param AssetPath  e.g. "/Game/Maps/MyLevel"
	 * @param bForce     When false, fails if the current level has unsaved changes.
	 *                   When true, discards unsaved changes and proceeds.
	 * @return JSON: {success, asset_path, level_name, error?}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Level")
	static FString LoadLevel(const FString& AssetPath, bool bForce = false);

	/**
	 * Save the currently-active level + all its dirty packages.
	 * @return JSON: {success, asset_path, level_name, error?}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Level")
	static FString SaveCurrentLevel();

	/**
	 * Inspect the editor's currently-active level.
	 * @return JSON: {success, asset_path, level_name, is_dirty, actor_count}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Level")
	static FString GetCurrentLevel();
};
