#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborActorTools.generated.h"

UCLASS()
class ARBOR_API UArborActorTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Snap a single actor so its bounding-box bottom sits on the ground.
	 *
	 * @param ActorLabel       Label of the actor to snap.
	 * @param Offset           Extra Z offset after snapping (positive = higher).
	 * @param PreserveRotation If true, restore original rotation after snap.
	 * @param IgnoreLabels     Comma-separated actor labels to ignore in the trace.
	 * @return JSON: {success, name, old_z, new_z, ground_z}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Actors")
	static FString SnapToGround(
		const FString& ActorLabel, float Offset,
		bool PreserveRotation, const FString& IgnoreLabels);

	/**
	 * Snap multiple actors to the ground. All targets ignore each other.
	 *
	 * @param FilterLabels  Comma-separated substrings to filter actors by label.
	 *                      Empty = snap all non-system actors.
	 * @param Offset        Extra Z offset after snapping.
	 * @return JSON: {snapped: [...], failed: [...]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Actors")
	static FString SnapAllToGround(const FString& FilterLabels, float Offset);

	/**
	 * Snap currently selected actors to the ground.
	 *
	 * @param Offset  Extra Z offset after snapping.
	 * @return JSON: {snapped: [...], failed: [...]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Actors")
	static FString SnapSelectedToGround(float Offset);

	/**
	 * Sample terrain height at a world XY coordinate via line trace.
	 *
	 * @param X  World X coordinate.
	 * @param Y  World Y coordinate.
	 * @return JSON: {success, z}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Actors")
	static FString SampleTerrainHeight(float X, float Y);

	// ----- Actor Operations -----

	/**
	 * Find an actor by path, object name, or label (case-insensitive).
	 * Public so other Arbor classes can reuse.
	 */
	static AActor* FindActorByAnyIdentifier(const FString& Identifier);

	/**
	 * Delete actors by name, label, or path.
	 *
	 * @param ActorNamesJson  JSON array of identifier strings.
	 * @return JSON: {deleted:[], not_found:[]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Actors")
	static FString DeleteActors(const FString& ActorNamesJson);

	/**
	 * Get scene info with optional class/prefix filters.
	 *
	 * @param FilterClass   Only return actors of this class (empty = all).
	 * @param FilterPrefix  Only return actors whose label starts with this prefix.
	 * @return JSON: {actors:[{name, class, position, rotation, scale}]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Actors")
	static FString GetSceneInfo(const FString& FilterClass, const FString& FilterPrefix);

	/**
	 * List all actors in the level.
	 *
	 * @return JSON: {actors:[{name, class, path}]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Actors")
	static FString ListAllActors();

	/**
	 * Modify an actor's transform, visibility, or label.
	 *
	 * @param ParamsJson  JSON: {actor_name, position?, rotation?, scale?, visible?, label?}
	 * @return JSON: {success, actor_path, changes_applied:[]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Actors")
	static FString ModifyActor(const FString& ParamsJson);

	/**
	 * Set an arbitrary UPROPERTY on an actor by reflection. Complements
	 * ModifyActor (which is limited to transform/visibility/label) for
	 * cases like brush extents, gameplay-tag fields, soft-class refs,
	 * component overrides on the actor itself, etc.
	 *
	 * Marks the level package dirty on success — caller should follow up
	 * with `level.save_current` to persist.
	 *
	 * @param ActorName     Display label (preferred) or internal FName.
	 * @param PropertyName  UPROPERTY name on the actor.
	 * @param ValueJson     JSON-encoded value. Supported types:
	 *                        scalars (bool/int32/float/double/FString/FName),
	 *                        FGameplayTag (string), FGameplayTagContainer (array of strings),
	 *                        FObjectProperty/FSoftObjectProperty (asset path string),
	 *                        FClassProperty/FSoftClassProperty (class path string).
	 * @return JSON: {success, actor_path, actor_label, property_name, error?}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Actors")
	static FString SetActorProperty(const FString& ActorName, const FString& PropertyName, const FString& ValueJson);

	/**
	 * Override a material slot on an actor's mesh component.
	 *
	 * Goes through UMeshComponent::SetMaterial (the canonical UE path), which
	 * routes the assignment into the component's OverrideMaterials array.
	 * Reliable for already-placed actors — avoids the
	 * `set_editor_property("override_materials", [mat])` workaround that
	 * Python callers sometimes need when set_material silently no-ops on
	 * uncommitted assignments.
	 *
	 * Targets the actor's primary mesh component (UStaticMeshComponent first,
	 * falling back to any UMeshComponent).
	 *
	 * @param ActorName     Display label (preferred) or internal FName.
	 * @param Slot          Material element index (0-based).
	 * @param MaterialPath  Asset path of UMaterialInterface (material or instance).
	 * @return JSON: {success, actor_path, component, slot, material_path, error?}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Actors")
	static FString SetMeshMaterial(const FString& ActorName, int32 Slot, const FString& MaterialPath);

	/**
	 * Trace straight down at a world XY coordinate to find the ground Z.
	 *
	 * @param ParamsJson  JSON: {x, y, start_z?, trace_distance?, ignore_actors?:[label,...]}
	 * @return JSON: {hit:bool, z:float, impact_point:{x,y,z}}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Actors")
	static FString TraceGroundZ(const FString& ParamsJson);

	/**
	 * Batch trace ground Z for multiple XY points in one call.
	 * Much faster than calling TraceGroundZ per-point from Python.
	 *
	 * @param ParamsJson  JSON: {points:[[x,y],...], start_z?, trace_distance?, ignore_actors?:[label,...]}
	 * @return JSON: {results:[{hit:bool, z:float},...]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Actors")
	static FString BatchTraceGround(const FString& ParamsJson);

	/**
	 * Inspect an actor's UPROPERTY values on itself and its components.
	 *
	 * @param ParamsJson  JSON: {actor_name, property_filter?, component_filter?, include_defaults?}
	 * @return JSON: {success, actor_name, actor_class, properties:[{name,type,value,category}],
	 *               components:[{name,class,properties:[{name,type,value,category}]}]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Actors")
	static FString InspectActor(const FString& ParamsJson);

	/**
	 * Trigger Live Coding compile.
	 *
	 * @return JSON: {success, message}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Editor")
	static FString LiveCompile();

private:
	static AActor* FindActorByLabel(const FString& Label);
	static TArray<AActor*> GetAllLevelActors();
	static bool LineTraceGroundZ(UWorld* World, AActor* Actor,
		const TArray<AActor*>& IgnoreActors, float& OutGroundZ);
	static TSharedPtr<FJsonObject> SnapActorInternal(UWorld* World, AActor* Actor,
		float Offset, bool PreserveRotation, const TArray<AActor*>& IgnoreActors);
};
