#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AnimBlueprintBuilder.generated.h"

class UAnimBlueprint;
class UEdGraph;
class UEdGraphNode;
class UAnimGraphNode_Base;

UCLASS()
class ARBOR_API UAnimBlueprintBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Set up a locomotion AnimGraph: BlendSpace -> OutputPose with variable binding(s).
	 * The ABP must already exist (created via BlueprintBuilder with parent_class "AnimInstance").
	 *
	 * ParamsJsonString format:
	 * {
	 *   "blendspace_path": "/Game/AI/BS_Ghost",
	 *
	 *   // Option A: single variable (backward compat)
	 *   "variable_name": "Speed",    // optional, default "Speed"
	 *   "variable_axis": "X"         // optional, "X" or "Y", default "X"
	 *
	 *   // Option B: multiple variables for 2D BlendSpaces
	 *   "variable_bindings": [
	 *     {"variable_name": "Direction", "variable_axis": "X"},
	 *     {"variable_name": "Speed", "variable_axis": "Y"}
	 *   ]
	 * }
	 *
	 * Returns JSON: {"success":true, "asset_path":"...", "blendspace_node_guid":"...",
	 *   "pose_wired":true, "variable_wired":true,
	 *   "variables_wired":[{"name":"Speed","axis":"X","wired":true}, ...]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString SetupLocomotionGraph(const FString& AssetPath, const FString& ParamsJsonString);

	/**
	 * Query the AnimGraph structure of an Animation Blueprint.
	 * Returns JSON with nodes, connections, variables, and skeleton info.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString QueryAnimGraph(const FString& AssetPath);

private:
	static UAnimBlueprint* LoadAnimBlueprintForEditing(const FString& AssetPath);
	static UEdGraph* GetAnimGraph(UAnimBlueprint* AnimBP);
	static UAnimGraphNode_Base* FindRootNode(UEdGraph* AnimGraph);
	static UEdGraphNode* FindNodeByGuid(UEdGraph* Graph, const FString& GuidString);
	static TSharedPtr<FJsonObject> SerializeAnimGraphNodeToJson(UEdGraphNode* Node);
	static bool SaveAsset(UObject* Asset);
};
