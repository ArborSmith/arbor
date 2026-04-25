#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "EQSBuilder.generated.h"

class UEnvQuery;
class UEnvQueryTest;

UCLASS()
class ARBOR_API UEQSBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static UEnvQuery* BuildEQSFromJSON(const FString& JsonFilePath, const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static UEnvQuery* BuildEQSFromJSONString(const FString& JsonString, const FString& AssetPath);

	// Query
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString QueryEQS(const FString& AssetPath);

	// Granular editing
	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString AddEQSGenerator(const FString& AssetPath, int32 OptionIndex, const FString& GeneratorJsonString);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString RemoveEQSGenerator(const FString& AssetPath, int32 OptionIndex);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString SetEQSGeneratorParams(const FString& AssetPath, int32 OptionIndex, const FString& ParamsJsonString);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString AddEQSTest(const FString& AssetPath, int32 OptionIndex, int32 TestIndex, const FString& TestJsonString);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString RemoveEQSTest(const FString& AssetPath, int32 OptionIndex, int32 TestIndex);

	UFUNCTION(BlueprintCallable, Category = "Arbor")
	static FString SetEQSTestParams(const FString& AssetPath, int32 OptionIndex, int32 TestIndex, const FString& ParamsJsonString);

private:
	static UEnvQuery* BuildFromParsedJSON(const TSharedPtr<FJsonObject>& JsonRoot, const FString& AssetPath);

	static bool LoadAndParseJSON(const FString& JsonFilePath, TSharedPtr<FJsonObject>& OutJsonObject);

	static UEnvQuery* CreateEQSAsset(const FString& Name, const FString& AssetPath);

	static UClass* ResolveGeneratorClass(const FString& TypeName);

	static UClass* ResolveTestClass(const FString& TypeName);

	static TSubclassOf<UEnvQueryContext> ResolveContextClass(const FString& ContextName);

	static void ApplyGeneratorParams(
		UObject* Generator,
		const TSharedPtr<FJsonObject>& ParamsJson);

	static void ApplyTestParams(
		UObject* Test,
		const TSharedPtr<FJsonObject>& ParamsJson);

	static void SetNumericParam(
		UObject* Object,
		const FString& PropertyName,
		double Value);

	static void SetContextParam(
		UObject* Object,
		const FString& PropertyName,
		TSubclassOf<UEnvQueryContext> ContextClass);

	static bool SaveAsset(UObject* Asset);

	// Query helpers
	static UEnvQuery* LoadEQSForEditing(const FString& AssetPath);
	static TSharedPtr<FJsonObject> SerializeGeneratorParams(UObject* Generator);
	static TSharedPtr<FJsonObject> SerializeTestParams(UEnvQueryTest* Test);
	static FString GetContextName(TSubclassOf<UEnvQueryContext> ContextClass);
	static FString GetGeneratorTypeName(UObject* Generator);
	static FString GetTestTypeName(UObject* Test);
};
