#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborLayoutSolver.generated.h"

UCLASS()
class ARBOR_API UArborLayoutSolver : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Resolve an environment graph into world transforms via BFS anchor alignment.
	 * Seeds the first node at the graph origin, then walks edges to place remaining nodes.
	 * Supports "adjacent" (anchors touch) and "facing" (face each other with gap) relationships.
	 *
	 * @param GraphJson  JSON environment graph: {id, origin?, nodes:{...}, edges:[...]}
	 * @return JSON: {success, transforms:{node_id:{location:{x,y,z}, rotation:{pitch,yaw,roll}, scale:{x,y,z}},...}}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Environment")
	static FString ResolveGraph(const FString& GraphJson);

private:
	struct FAnchor
	{
		FString Id;
		FVector Position;
		FVector Direction;
	};

	struct FNodeTransform
	{
		FVector Location;
		FRotator Rotation;
		FVector Scale;
	};

	static bool LoadAnchorsForAsset(const FString& AssetPath, TArray<FAnchor>& OutAnchors);
	static const FAnchor* FindAnchor(const TArray<FAnchor>& Anchors, const FString& AnchorId);
	static FNodeTransform AlignNodes(
		const FNodeTransform& FromTransform, const FAnchor& FromAnchor,
		const FAnchor& ToAnchor, const FString& Relationship, double Gap,
		double NodeYawHint = 0.0);
};
