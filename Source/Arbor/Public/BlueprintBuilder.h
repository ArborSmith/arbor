#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BlueprintBuilder.generated.h"

class UBlueprint;
class USCS_Node;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class UK2Node_Timeline;

UCLASS()
class ARBOR_API UBlueprintBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static UBlueprint* BuildBlueprintFromJSON(const FString& JsonFilePath, const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static UBlueprint* BuildBlueprintFromJSONString(const FString& JsonString, const FString& AssetPath);

	// ---- Granular Blueprint Editing ----

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString QueryBlueprint(const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString AddEventGraphNode(const FString& AssetPath, const FString& NodeJsonString);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString RemoveEventGraphNode(const FString& AssetPath, const FString& NodeGuidString);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString SetPinDefault(const FString& AssetPath,
		const FString& NodeGuidString, const FString& PinName, const FString& DefaultValue);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString ConnectPins(const FString& AssetPath,
		const FString& FromNodeGuid, const FString& FromPinName,
		const FString& ToNodeGuid, const FString& ToPinName);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString DisconnectPin(const FString& AssetPath,
		const FString& NodeGuidString, const FString& PinName);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString CompileAndSaveBlueprint(const FString& AssetPath);

	/** Introspect a node type's pins without modifying any asset.
	 *  @param NodeJsonString  Same JSON format as AddEventGraphNode node_spec
	 *  @param ContextAssetPath  Optional: existing Blueprint for function/variable resolution
	 *  @return JSON string with pin listing or error
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString GetNodePins(const FString& NodeJsonString, const FString& ContextAssetPath = TEXT(""));

	/** Query perception sense configs on an AIController Blueprint.
	 *  Returns JSON array of sense configs with all properties (affiliation, dominant, etc.)
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString QuerySenseConfig(const FString& AssetPath);

	// ---- Granular Component Editing ----

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString AddSCSComponent(const FString& AssetPath, const FString& ComponentJsonString);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString RemoveSCSComponent(const FString& AssetPath, const FString& ComponentName);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString SetComponentProperty(const FString& AssetPath,
		const FString& ComponentName, const FString& PropertyJsonString);

private:
	static UBlueprint* BuildFromParsedJSON(const TSharedPtr<FJsonObject>& JsonRoot, const FString& AssetPath);

	static bool LoadAndParseJSON(const FString& JsonFilePath, TSharedPtr<FJsonObject>& OutJsonObject);

	static UClass* ResolveParentClass(const FString& ClassName);

	static UClass* ResolveComponentClass(const FString& TypeName);

	static UBlueprint* CreateBlueprintAsset(
		const FString& Name,
		const FString& AssetPath,
		UClass* ParentClass);

	static void AddComponents(
		UBlueprint* Blueprint,
		const TArray<TSharedPtr<FJsonValue>>& ComponentsJson,
		TArray<TPair<FString, TSharedPtr<FJsonObject>>>& OutInheritedOverrides);

	static USCS_Node* AddComponent(
		UBlueprint* Blueprint,
		const TSharedPtr<FJsonObject>& ComponentJson,
		TArray<TPair<FString, TSharedPtr<FJsonObject>>>& OutInheritedOverrides);

	static void ApplyComponentProperties(
		UObject* ComponentTemplate,
		const TSharedPtr<FJsonObject>& PropertiesJson);

	static void ApplyClassDefaults(
		UBlueprint* Blueprint,
		const TSharedPtr<FJsonObject>& DefaultsJson);

	static void AddVariables(
		UBlueprint* Blueprint,
		const TArray<TSharedPtr<FJsonValue>>& VariablesJson);

	static void SetPropertyFromJson(
		UObject* Object,
		const FString& PropertyName,
		const TSharedPtr<FJsonValue>& JsonValue);

	static void HandleSensesConfig(
		UObject* PerceptionComponent,
		const TArray<TSharedPtr<FJsonValue>>& SensesArray);

	static bool SaveAsset(UObject* Asset);

	// ---- Event Graph ----

	static void ClearEventGraph(UBlueprint* Blueprint);

	static void AddTimelines(
		UBlueprint* Blueprint,
		const TArray<TSharedPtr<FJsonValue>>& TimelinesJson);

	static void AddEventGraph(
		UBlueprint* Blueprint,
		const TSharedPtr<FJsonObject>& EventGraphJson);

	static UEdGraphNode* CreateNodeFromJson(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		const TSharedPtr<FJsonObject>& NodeJson,
		int32& PosX,
		int32& PosY);

	static UEdGraphNode* CreateEventNode(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		const TSharedPtr<FJsonObject>& NodeJson,
		int32 PosX, int32 PosY);

	static UEdGraphNode* CreateComponentEventNode(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		const TSharedPtr<FJsonObject>& NodeJson,
		int32 PosX, int32 PosY);

	static UEdGraphNode* CreateCallFunctionNode(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		const TSharedPtr<FJsonObject>& NodeJson,
		int32 PosX, int32 PosY);

	static UEdGraphNode* CreateVariableGetNode(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		const TSharedPtr<FJsonObject>& NodeJson,
		int32 PosX, int32 PosY);

	static UEdGraphNode* CreateVariableSetNode(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		const TSharedPtr<FJsonObject>& NodeJson,
		int32 PosX, int32 PosY);

	static UEdGraphNode* CreateBranchNode(
		UEdGraph* Graph,
		int32 PosX, int32 PosY);

	static UEdGraphNode* CreateTimelineNode(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		const TSharedPtr<FJsonObject>& NodeJson,
		int32 PosX, int32 PosY);

	static UEdGraphNode* CreateCastNode(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		const TSharedPtr<FJsonObject>& NodeJson,
		int32 PosX, int32 PosY);

	static UEdGraphNode* CreateGenericNode(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		const TSharedPtr<FJsonObject>& NodeJson,
		const FString& NodeType,
		int32 PosX, int32 PosY);

	static void WireConnections(
		UEdGraph* Graph,
		const TArray<TSharedPtr<FJsonValue>>& ConnectionsJson,
		const TMap<FString, UEdGraphNode*>& NodeMap);

	static UFunction* ResolveFunctionByName(
		const FString& FunctionName,
		UBlueprint* Blueprint = nullptr,
		const FString& TargetComponentName = TEXT(""),
		const FString& OwnerClassName = TEXT(""));

	static UEdGraphPin* CreateComponentRefNode(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		const FString& ComponentName,
		int32 PosX, int32 PosY);

	static void SetPinDefaultFromJson(
		UEdGraphPin* Pin,
		const TSharedPtr<FJsonValue>& JsonValue);

	// ---- Granular Editing Helpers ----

	static UBlueprint* LoadBlueprintForEditing(const FString& AssetPath);

	static UEdGraph* GetEventGraph(UBlueprint* Blueprint, bool bCreateIfMissing = false);

	static UEdGraphNode* FindNodeByGuid(UEdGraph* Graph, const FString& GuidString);

	/** Search all graphs (UbergraphPages + FunctionGraphs) for a node by GUID.
	 *  Returns the node and optionally sets OutGraph to the graph containing it. */
	static UEdGraphNode* FindNodeInAllGraphs(UBlueprint* Blueprint,
		const FString& GuidString, UEdGraph** OutGraph = nullptr);

	static TSharedPtr<FJsonObject> SerializeNodeToJson(UEdGraphNode* Node);

	static TArray<TSharedPtr<FJsonValue>> SerializePinsToJsonArray(UEdGraphNode* Node);

	static bool IsHiddenComponentRefNode(UEdGraphNode* Node);

	/** Check if a node is an auto-created MakeLiteral* helper for a by-ref pin. */
	static bool IsHiddenLiteralNode(UEdGraphNode* Node);

	/** Resolve a string asset path to a UObject* for object/class/interface pins.
	 *  Handles PC_Object, PC_Interface, PC_Class, PC_SoftClass with retry logic.
	 *  Returns the loaded object (or UClass*), or nullptr on failure. */
	static UObject* ResolveObjectForPin(UEdGraphPin* Pin, const FString& AssetPath);

	/** For by-ref FName/FString/FText pins, create a MakeLiteral* node, set its Value pin,
	 *  and wire its ReturnValue to the target pin. Returns true if a literal node was created. */
	static bool CreateLiteralNodeForByRefPin(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		UEdGraphPin* TargetPin,
		const FString& Value,
		int32 PosX, int32 PosY);

	/** Set a pin default from JSON. If the pin is by-ref FName/FString/FText,
	 *  auto-creates a MakeLiteral* helper node and wires it instead. */
	static void SetPinDefaultOrCreateLiteral(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		UEdGraphPin* Pin,
		const TSharedPtr<FJsonValue>& JsonValue,
		int32 NodePosX, int32 NodePosY);

	static void CompileAndSave(UBlueprint* Blueprint);

	/** Collect compile errors/warnings from graph nodes after compilation. */
	static void CollectCompileDiagnostics(UBlueprint* Blueprint,
		TArray<FString>& OutErrors, TArray<FString>& OutWarnings);

	/** Build a JSON array fragment: ,"compile_errors":[...],"compile_warnings":[...] */
	static FString FormatCompileDiagnosticsJson(const TArray<FString>& Errors,
		const TArray<FString>& Warnings);
};
