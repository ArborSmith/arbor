#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborClassDiscovery.generated.h"

UCLASS()
class ARBOR_API UArborClassDiscovery : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ---- BT Discovery ----

	UFUNCTION(BlueprintCallable, Category = "Arbor|Discovery")
	static FString ListBTNodeTypes(const FString& Category, const FString& Filter);

	// ---- EQS Discovery ----

	UFUNCTION(BlueprintCallable, Category = "Arbor|Discovery")
	static FString ListEQSGeneratorTypes(const FString& Filter);

	UFUNCTION(BlueprintCallable, Category = "Arbor|Discovery")
	static FString ListEQSTestTypes(const FString& Filter);

	// ---- PCG Discovery ----

	UFUNCTION(BlueprintCallable, Category = "Arbor|Discovery")
	static FString ListPCGNodeTypes(const FString& Filter);

	// ---- Blueprint Discovery ----

	UFUNCTION(BlueprintCallable, Category = "Arbor|Discovery")
	static FString ListBlueprintNodeTypes(const FString& Filter);

	UFUNCTION(BlueprintCallable, Category = "Arbor|Discovery")
	static FString ListComponentTypes(const FString& Filter);

	// ---- Class Introspection ----

	UFUNCTION(BlueprintCallable, Category = "Arbor|Discovery")
	static FString ListClassFunctions(const FString& ClassName);

	UFUNCTION(BlueprintCallable, Category = "Arbor|Discovery")
	static FString GetClassProperties(const FString& ClassName, const FString& BaseClassName);

private:
	struct FClassInfo
	{
		FString ClassName;
		FString FriendlyName;
		FString Module;
		FString Category;
		bool bIsBuiltin = false;
	};

	static TArray<FClassInfo> EnumerateSubclasses(UClass* Base, const FString& Filter, bool ExcludeAbstract = true);

	static TSharedPtr<FJsonObject> SerializePropertyDescriptor(FProperty* Prop, UObject* CDO);

	static UClass* ResolveClassByName(const FString& ClassName, UClass* BaseClass = nullptr);

	static FString GetModuleName(UClass* Class);

	// Friendly name maps
	static const TMap<FString, FString>& GetBTFriendlyNames();
	static const TMap<FString, FString>& GetEQSGeneratorFriendlyNames();
	static const TMap<FString, FString>& GetEQSTestFriendlyNames();
	static const TMap<FString, FString>& GetPCGFriendlyNames();
};
