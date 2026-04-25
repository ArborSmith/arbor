#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborCodexSearch.generated.h"

/**
 * Provides full-text search and retrieval across all Game Codex entries
 * (locations, enemies, items, factions, systems, lore, abilities, characters).
 * Called by the ue5_codex MCP tool via Remote Control API.
 */
UCLASS()
class ARBOR_API UArborCodexSearch : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Full-text search across all Game Codex entries.
	 * Tokenizes query, scores each entry by name/description/field matches.
	 *
	 * @param Query     Space-separated search terms.
	 * @param Category  Optional: location, enemy, item, faction, system, lore, ability, character. Empty = all.
	 * @param Limit     Maximum results to return.
	 * @return JSON array: [{_category, _path, _name, _score, ...fields}, ...]
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Codex")
	static FString SearchCodex(const FString& Query, const FString& Category, int32 Limit, const FString& StatusFilter);

	/**
	 * List all entries of a given codex category.
	 *
	 * @param Category      One of: location, enemy, item, faction, system, lore, ability, character.
	 * @param StatusFilter  Optional: only return entries matching this status (display name). Empty = all.
	 * @return JSON array: [{_path, _name}, ...]
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Codex")
	static FString ListCodexEntries(const FString& Category, const FString& StatusFilter);

	/**
	 * Get full details of a specific codex entry by asset path.
	 *
	 * @param AssetPath  UE5 asset path (e.g. "/Game/GameCodex/Loc_Forest").
	 * @return JSON object: {_category, _path, _name, ...all fields}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Codex")
	static FString GetCodexEntry(const FString& AssetPath);

	/**
	 * Get all Game Context assets (title, genre, setting, world description, etc.).
	 *
	 * @return JSON array: [{_path, GameTitle, Genre, Setting, ...}, ...]
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Codex")
	static FString GetGameContext();

	/**
	 * Create a new codex entry or update an existing one (idempotent).
	 *
	 * @param Category        One of: location, enemy, item, faction, system, lore, ability, character.
	 * @param AssetName       Display name for the entry (also used for the asset name).
	 * @param ContentPath     Package path prefix (e.g. "/Game/WorldBuilding"). Empty = category default.
	 * @param GameContextPath Soft object path to a GameContext asset. Empty = don't set.
	 * @param PropertiesJson  JSON object of UPROPERTY field name → value.
	 * @return JSON: {success, asset_path, _name, _category} or {success:false, error}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Codex")
	static FString CreateCodexEntry(const FString& Category, const FString& AssetName, const FString& ContentPath, const FString& GameContextPath, const FString& PropertiesJson);

	/**
	 * Update fields on an existing codex entry (partial update).
	 *
	 * @param AssetPath      UE5 asset path of the entry to update.
	 * @param PropertiesJson JSON object of UPROPERTY field name → value (only provided fields are changed).
	 * @return JSON: updated entry object or {success:false, error}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Codex")
	static FString UpdateCodexEntry(const FString& AssetPath, const FString& PropertiesJson);

	/**
	 * Delete a codex entry.
	 *
	 * @param AssetPath UE5 asset path of the entry to delete.
	 * @return JSON: {success, deleted_path} or {success:false, error}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Codex")
	static FString DeleteCodexEntry(const FString& AssetPath);

	/**
	 * Get all codex entries that reference a given GameContext, grouped by category.
	 *
	 * @param GameContextPath  Asset path of the GameContext (with or without .ObjectName suffix).
	 * @return JSON: {success, game_context, location:[...], enemy:[...], ...} or {success:false, error}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Codex")
	static FString GetEntriesForContext(const FString& GameContextPath);
};
