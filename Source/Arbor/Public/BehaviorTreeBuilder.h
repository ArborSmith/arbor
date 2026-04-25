#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTreeBuilder.generated.h"

class UBehaviorTree;
class UBlackboardData;
class UBlackboardKeyType;

UCLASS()
class ARBOR_API UBehaviorTreeBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static UBehaviorTree* BuildBehaviorTreeFromJSON(const FString& JsonFilePath, const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static UBehaviorTree* BuildBehaviorTreeFromJSONString(const FString& JsonString, const FString& AssetPath);

	// Granular editing
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString QueryBehaviorTree(const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString AddBTNode(const FString& AssetPath, const FString& ParentPath,
	                         int32 ChildIndex, const FString& NodeJsonString);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString RemoveBTNode(const FString& AssetPath, const FString& NodePath);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString SetBTNodeParams(const FString& AssetPath, const FString& NodePath,
	                               const FString& ParamsJsonString);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString LayoutBehaviorTree(const FString& AssetPath);

private:
	static UBehaviorTree* BuildFromParsedJSON(const TSharedPtr<FJsonObject>& JsonRoot, const FString& AssetPath);

	static bool LoadAndParseJSON(const FString& JsonFilePath, TSharedPtr<FJsonObject>& OutJsonObject);

	static UBlackboardData* CreateBlackboardAsset(
		const TSharedPtr<FJsonObject>& BlackboardJson,
		const FString& AssetPath);

	static UBlackboardKeyType* CreateBlackboardKeyType(
		UBlackboardData* BlackboardAsset,
		const FString& TypeName,
		const TSharedPtr<FJsonObject>& KeyJson);

	static UBehaviorTree* CreateBehaviorTreeAsset(
		const FString& Name,
		const FString& AssetPath);

	static UBTCompositeNode* ProcessCompositeNode(
		UBehaviorTree* BehaviorTree,
		UBlackboardData* BlackboardAsset,
		const TSharedPtr<FJsonObject>& NodeJson,
		uint8 TreeDepth);

	static UBTTaskNode* ProcessTaskNode(
		UBehaviorTree* BehaviorTree,
		UBlackboardData* BlackboardAsset,
		const TSharedPtr<FJsonObject>& NodeJson);

	static void ProcessDecorators(
		UBehaviorTree* BehaviorTree,
		UBlackboardData* BlackboardAsset,
		const TArray<TSharedPtr<FJsonValue>>& DecoratorsJson,
		FBTCompositeChild& OutChild);

	static void ProcessServices(
		UBehaviorTree* BehaviorTree,
		UBlackboardData* BlackboardAsset,
		const TArray<TSharedPtr<FJsonValue>>& ServicesJson,
		UBTCompositeNode* CompositeNode);

	static UClass* ResolveNodeClass(const FString& ClassName, UClass* BaseClass);

	static void ApplyParameters(
		UObject* Node,
		const TSharedPtr<FJsonObject>& ParamsJson,
		UBlackboardData* BlackboardAsset);

	static void ApplyParamsToStruct(
		UStruct* StructDef, void* StructPtr,
		const TSharedPtr<FJsonObject>& ParamsJson,
		UBlackboardData* BlackboardAsset);

	static void SetBlackboardKeySelector(
		UObject* Node,
		FProperty* Property,
		const FString& KeyName,
		UBlackboardData* BlackboardAsset);

	static uint16 AssignExecutionIndices(
		UBTCompositeNode* Node,
		UBTCompositeNode* ParentNode,
		uint16 StartIndex,
		uint8 TreeDepth);

	static bool SaveAsset(UObject* Asset);

	// Granular editing helpers
	static UBehaviorTree* LoadBehaviorTreeForEditing(const FString& AssetPath);

	/**
	 * Resolve a dot-delimited child-index path to a parent composite + child index.
	 * "" → root composite (OutChildIndex = -1)
	 * "0" → first child of root (OutParent = root, OutChildIndex = 0)
	 * "1.2" → third child of second child of root
	 * Returns the parent composite. OutChildIndex is the index within parent->Children.
	 */
	static UBTCompositeNode* ResolveNodePath(UBehaviorTree* BT, const FString& Path,
	                                         int32& OutChildIndex);

	static TSharedPtr<FJsonObject> SerializeBTNodeToJson(
		UBTCompositeNode* Node, const FString& CurrentPath);

	static void FinalizeBehaviorTree(UBehaviorTree* BT);

	static bool LayoutBehaviorTreeInternal(UBehaviorTree* BT);
};
