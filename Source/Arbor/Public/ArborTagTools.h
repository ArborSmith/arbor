#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborTagTools.generated.h"

/**
 * Gameplay tag helpers for the bridge.
 *
 * UE5's stock Python bindings can't construct an FGameplayTag from a string —
 * `make_literal_gameplay_tag` requires an already-valid FGameplayTag,
 * `GameplayTag.tag_name` is read-only, and `UGameplayTagsManager::RequestGameplayTag`
 * is not exposed via reflection. These UFUNCTIONs go through the manager so
 * Python callers can resolve, validate, and assign tags by name.
 *
 * Pythonic wrappers live in `arbor.tags`.
 */
UCLASS()
class ARBOR_API UArborTagTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Resolve a registered gameplay tag by name.
	 *
	 * @param TagName            The tag's full path, e.g. "Quest.Gym.Main".
	 * @param bErrorIfNotFound   If true and the tag isn't registered, returns an empty tag and logs an error.
	 *                           If false, just returns an empty tag silently.
	 * @return                   The resolved FGameplayTag (empty if not registered).
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Tags")
	static FGameplayTag RequestGameplayTag(FName TagName, bool bErrorIfNotFound);

	/** Returns true iff TagName is registered with the gameplay tags manager. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Arbor|Tags")
	static bool IsTagRegistered(FName TagName);

	/**
	 * Set a FGameplayTag UPROPERTY on an object by name.
	 *
	 * Walks dotted property paths (e.g. "Branches.0.BranchTag") so callers can
	 * reach into nested USTRUCTs and array elements without bouncing through
	 * Python's struct introspection.
	 *
	 * @param Target         The object whose property to set.
	 * @param PropertyPath   Dotted path: "Field" or "StructField.SubField" or "ArrayField.0.SubField".
	 * @param TagName        Tag to assign. Must be registered.
	 * @return JSON: {success, message, resolved_tag}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Tags")
	static FString SetGameplayTagOnObject(UObject* Target, const FString& PropertyPath, FName TagName);

	/**
	 * Set a FGameplayTagContainer UPROPERTY on an object by name list.
	 * Replaces (does not append to) the existing container.
	 *
	 * @return JSON: {success, message, resolved_tags: [...], unresolved: [...]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Tags")
	static FString SetGameplayTagContainerOnObject(UObject* Target, const FString& PropertyPath, const TArray<FName>& TagNames);

	/**
	 * List every registered gameplay tag, optionally filtered by prefix.
	 *
	 * @param Prefix   Empty = all tags. Otherwise returns only tags that start with Prefix (e.g. "Quest.Gym").
	 * @return JSON: {count, tags: [...]}
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Arbor|Tags")
	static FString ListGameplayTags(const FString& Prefix);
};
