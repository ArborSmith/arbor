#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PCGBuilder.generated.h"

class UPCGGraph;
class UPCGNode;
class UPCGSettings;
class UPCGComponent;

UCLASS()
class ARBOR_API UPCGBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ---- Full Rebuild from JSON ----

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static UPCGGraph* BuildPCGGraphFromJSON(const FString& JsonFilePath, const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static UPCGGraph* BuildPCGGraphFromJSONString(const FString& JsonString, const FString& AssetPath);

	// ---- Query ----

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString QueryPCGGraph(const FString& AssetPath);

	// ---- Granular Node Editing ----

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString AddPCGNode(const FString& AssetPath, const FString& NodeJsonString);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString RemovePCGNode(const FString& AssetPath, const FString& NodeIdString);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString SetPCGNodeParams(const FString& AssetPath, const FString& NodeIdString,
	                                 const FString& ParamsJsonString);

	// ---- Pin Wiring ----

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString ConnectPCGPins(const FString& AssetPath,
		const FString& FromNodeId, const FString& FromPinLabel,
		const FString& ToNodeId, const FString& ToPinLabel);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString DisconnectPCGPin(const FString& AssetPath,
		const FString& NodeIdString, const FString& PinLabel);

	// ---- Execution ----

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString ExecutePCGOnActor(const FString& GraphAssetPath, const FString& ActorLabel);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString AddPCGComponentToActor(const FString& ActorLabel, const FString& GraphAssetPath);

private:
	static UPCGGraph* BuildFromParsedJSON(const TSharedPtr<FJsonObject>& JsonRoot, const FString& AssetPath);

	static bool LoadAndParseJSON(const FString& JsonFilePath, TSharedPtr<FJsonObject>& OutJsonObject);

	static UPCGGraph* CreatePCGGraphAsset(const FString& Name, const FString& AssetPath);

	static UClass* ResolveSettingsClass(const FString& NodeType);

	static void ApplyNodeParams(UPCGSettings* Settings, const TSharedPtr<FJsonObject>& ParamsJson);

	static void SetNumericParam(UObject* Object, const FString& PropertyName, double Value);

	static bool SaveAsset(UObject* Asset);

	// Query helpers
	static UPCGGraph* LoadPCGGraphForEditing(const FString& AssetPath);

	static TSharedPtr<FJsonObject> SerializeNodeToJson(UPCGNode* Node);

	static TSharedPtr<FJsonObject> SerializeSettingsParams(UPCGSettings* Settings);

	static FString GetNodeTypeName(UPCGSettings* Settings);

	static UPCGNode* FindNodeById(UPCGGraph* Graph, const FString& NodeIdString);

	// Actor helpers
	static AActor* FindActorByLabel(const FString& Label);
};
