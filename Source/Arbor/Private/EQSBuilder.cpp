#include "EQSBuilder.h"

// JSON
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

// EQS core
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "EnvironmentQuery/EnvQueryTest.h"

// Generators
#include "EnvironmentQuery/Generators/EnvQueryGenerator_SimpleGrid.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_OnCircle.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_Donut.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_PathingGrid.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_CurrentLocation.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_ActorsOfClass.h"

// Built-in EnvQueryTest_* classes are resolved by path at runtime rather than
// compile-time StaticClass() references. Some of them (notably
// UEnvQueryTest_Distance in UE 5.4) don't export GetPrivateStaticClass with
// AIMODULE_API, so direct StaticClass() refs cause LNK2019 in dependent
// modules. Runtime lookup via StaticLoadClass("/Script/AIModule.*") works
// regardless of UHT export state.

// Contexts
#include "EnvironmentQuery/Contexts/EnvQueryContext_Querier.h"
#include "EnvironmentQuery/Contexts/EnvQueryContext_Item.h"

// Data providers
#include "DataProviders/AIDataProvider.h"

// Asset infrastructure
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogArborEQS, Log, All);

// ============================================================================
// Main Entry Point
// ============================================================================

UEnvQuery* UEQSBuilder::BuildEQSFromJSON(
	const FString& JsonFilePath, const FString& AssetPath)
{
	TSharedPtr<FJsonObject> JsonRoot;
	if (!LoadAndParseJSON(JsonFilePath, JsonRoot))
	{
		return nullptr;
	}
	return BuildFromParsedJSON(JsonRoot, AssetPath);
}

UEnvQuery* UEQSBuilder::BuildEQSFromJSONString(
	const FString& JsonString, const FString& AssetPath)
{
	TSharedPtr<FJsonObject> JsonRoot;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonRoot) || !JsonRoot.IsValid())
	{
		UE_LOG(LogArborEQS, Error, TEXT("Failed to parse JSON string"));
		return nullptr;
	}
	return BuildFromParsedJSON(JsonRoot, AssetPath);
}

UEnvQuery* UEQSBuilder::BuildFromParsedJSON(
	const TSharedPtr<FJsonObject>& JsonRoot, const FString& AssetPath)
{
	// 1. Extract query name
	FString QueryName;
	if (!JsonRoot->TryGetStringField(TEXT("name"), QueryName))
	{
		UE_LOG(LogArborEQS, Error, TEXT("JSON missing 'name' field"));
		return nullptr;
	}

	// 2. Create EQS asset
	UEnvQuery* Query = CreateEQSAsset(QueryName, AssetPath);
	if (!Query)
	{
		UE_LOG(LogArborEQS, Error, TEXT("Failed to create EQS asset"));
		return nullptr;
	}

	// Clear any existing options — we're rebuilding from scratch
	Query->GetOptionsMutable().Empty();

	// 3. Parse generators array
	const TArray<TSharedPtr<FJsonValue>>* GeneratorsArray;
	if (!JsonRoot->TryGetArrayField(TEXT("generators"), GeneratorsArray) || GeneratorsArray->Num() == 0)
	{
		UE_LOG(LogArborEQS, Error, TEXT("JSON missing or empty 'generators' array"));
		return nullptr;
	}

	// 4. Parse tests array (applied to every generator/option)
	const TArray<TSharedPtr<FJsonValue>>* TestsArray = nullptr;
	JsonRoot->TryGetArrayField(TEXT("tests"), TestsArray);

	// 5. Build options: one per generator, each with all tests
	for (const TSharedPtr<FJsonValue>& GenValue : *GeneratorsArray)
	{
		const TSharedPtr<FJsonObject>& GenJson = GenValue->AsObject();
		if (!GenJson.IsValid())
		{
			continue;
		}

		FString GenType;
		if (!GenJson->TryGetStringField(TEXT("type"), GenType))
		{
			UE_LOG(LogArborEQS, Warning, TEXT("Generator missing 'type' field, skipping"));
			continue;
		}

		// Resolve generator class
		UClass* GenClass = ResolveGeneratorClass(GenType);
		if (!GenClass)
		{
			UE_LOG(LogArborEQS, Error, TEXT("Unknown generator type: %s"), *GenType);
			continue;
		}

		// Create option
		UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Query);

		// Create generator
		UEnvQueryGenerator* Generator = NewObject<UEnvQueryGenerator>(Option, GenClass);

		// Apply generator params
		const TSharedPtr<FJsonObject>* GenParams;
		if (GenJson->TryGetObjectField(TEXT("params"), GenParams))
		{
			ApplyGeneratorParams(Generator, *GenParams);
		}

		Option->Generator = Generator;

		// Add tests to this option
		if (TestsArray)
		{
			for (const TSharedPtr<FJsonValue>& TestValue : *TestsArray)
			{
				const TSharedPtr<FJsonObject>& TestJson = TestValue->AsObject();
				if (!TestJson.IsValid())
				{
					continue;
				}

				FString TestType;
				if (!TestJson->TryGetStringField(TEXT("type"), TestType))
				{
					UE_LOG(LogArborEQS, Warning, TEXT("Test missing 'type' field, skipping"));
					continue;
				}

				UClass* TestClass = ResolveTestClass(TestType);
				if (!TestClass)
				{
					UE_LOG(LogArborEQS, Error, TEXT("Unknown test type: %s"), *TestType);
					continue;
				}

				UEnvQueryTest* Test = NewObject<UEnvQueryTest>(Option, TestClass);

				const TSharedPtr<FJsonObject>* TestParams;
				if (TestJson->TryGetObjectField(TEXT("params"), TestParams))
				{
					ApplyTestParams(Test, *TestParams);
				}

				Option->Tests.Add(Test);
			}
		}

		Query->GetOptionsMutable().Add(Option);
	}

	// 6. Fail if no generators were successfully added
	if (Query->GetOptionsMutable().Num() == 0)
	{
		UE_LOG(LogArborEQS, Error, TEXT("No generators were added to EQS '%s' — check that type names are valid"), *QueryName);
		return nullptr;
	}

	// 7. Save asset
	SaveAsset(Query);

	UE_LOG(LogArborEQS, Log, TEXT("Successfully built EQS query '%s' at %s"), *QueryName, *AssetPath);
	return Query;
}

// ============================================================================
// JSON Parsing
// ============================================================================

bool UEQSBuilder::LoadAndParseJSON(
	const FString& JsonFilePath, TSharedPtr<FJsonObject>& OutJsonObject)
{
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
	{
		UE_LOG(LogArborEQS, Error, TEXT("Failed to load JSON file: %s"), *JsonFilePath);
		return false;
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, OutJsonObject) || !OutJsonObject.IsValid())
	{
		UE_LOG(LogArborEQS, Error, TEXT("Failed to parse JSON file: %s"), *JsonFilePath);
		return false;
	}

	return true;
}

// ============================================================================
// Asset Creation
// ============================================================================

UEnvQuery* UEQSBuilder::CreateEQSAsset(const FString& Name, const FString& AssetPath)
{
	const FString PackagePath = AssetPath / Name;
	const FString AssetObjectPath = PackagePath + TEXT(".") + Name;

	// Check if asset already exists — update in place to preserve references
	if (UEditorAssetLibrary::DoesAssetExist(PackagePath))
	{
		UEnvQuery* ExistingQuery = LoadObject<UEnvQuery>(nullptr, *AssetObjectPath);
		if (ExistingQuery)
		{
			ExistingQuery->GetOptionsMutable().Empty();
			ExistingQuery->GetPackage()->MarkPackageDirty();
			UE_LOG(LogArborEQS, Log, TEXT("Updating existing EQS asset: %s"), *PackagePath);
			return ExistingQuery;
		}
	}

	// Asset doesn't exist — create new
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		UE_LOG(LogArborEQS, Error, TEXT("Failed to create package: %s"), *PackagePath);
		return nullptr;
	}

	UEnvQuery* Query = NewObject<UEnvQuery>(
		Package, UEnvQuery::StaticClass(), *Name,
		RF_Public | RF_Standalone | RF_Transactional);

	UE_LOG(LogArborEQS, Log, TEXT("Creating new EQS asset: %s"), *PackagePath);
	return Query;
}

// ============================================================================
// Class Resolution
// ============================================================================

UClass* UEQSBuilder::ResolveGeneratorClass(const FString& TypeName)
{
	// Accept both full class names (EnvQueryGenerator_OnCircle) and friendly names (OnCircle)
	FString NormalizedName = TypeName;
	NormalizedName.RemoveFromStart(TEXT("EnvQueryGenerator_"));

	static const TMap<FString, UClass*> GeneratorMap = {
		{TEXT("SimpleGrid"),      UEnvQueryGenerator_SimpleGrid::StaticClass()},
		{TEXT("OnCircle"),        UEnvQueryGenerator_OnCircle::StaticClass()},
		{TEXT("Donut"),           UEnvQueryGenerator_Donut::StaticClass()},
		{TEXT("PathingGrid"),     UEnvQueryGenerator_PathingGrid::StaticClass()},
		{TEXT("CurrentLocation"), UEnvQueryGenerator_CurrentLocation::StaticClass()},
		{TEXT("ActorsOfClass"),   UEnvQueryGenerator_ActorsOfClass::StaticClass()},
	};

	if (UClass* const* Found = GeneratorMap.Find(NormalizedName))
	{
		return *Found;
	}

	// Try loading as a full class path
	UClass* LoadedClass = StaticLoadClass(
		UEnvQueryGenerator::StaticClass(), nullptr, *TypeName);
	if (LoadedClass)
	{
		return LoadedClass;
	}

	// Try /Script/AIModule prefix
	FString FullPath = FString::Printf(TEXT("/Script/AIModule.EnvQueryGenerator_%s"), *NormalizedName);
	LoadedClass = StaticLoadClass(UEnvQueryGenerator::StaticClass(), nullptr, *FullPath);

	return LoadedClass;
}

UClass* UEQSBuilder::ResolveTestClass(const FString& TypeName)
{
	// Accept both full class names (EnvQueryTest_Pathfinding) and friendly names (Pathfinding)
	FString NormalizedName = TypeName;
	NormalizedName.RemoveFromStart(TEXT("EnvQueryTest_"));

	// Shortname → /Script/ path map for built-in AIModule tests.
	static const TMap<FString, FString> TestShortnameToPath = {
		{TEXT("Distance"),     TEXT("/Script/AIModule.EnvQueryTest_Distance")},
		{TEXT("Trace"),        TEXT("/Script/AIModule.EnvQueryTest_Trace")},
		{TEXT("PathExist"),    TEXT("/Script/AIModule.EnvQueryTest_Pathfinding")},
		{TEXT("Pathfinding"),  TEXT("/Script/AIModule.EnvQueryTest_Pathfinding")},
		{TEXT("Dot"),          TEXT("/Script/AIModule.EnvQueryTest_Dot")},
		{TEXT("GameplayTags"), TEXT("/Script/AIModule.EnvQueryTest_GameplayTags")},
		{TEXT("Overlap"),      TEXT("/Script/AIModule.EnvQueryTest_Overlap")},
		{TEXT("Project"),      TEXT("/Script/AIModule.EnvQueryTest_Project")},
	};

	if (const FString* MappedPath = TestShortnameToPath.Find(NormalizedName))
	{
		return StaticLoadClass(UEnvQueryTest::StaticClass(), nullptr, **MappedPath);
	}

	// Try loading as a full class path (user-provided)
	UClass* LoadedClass = StaticLoadClass(
		UEnvQueryTest::StaticClass(), nullptr, *TypeName);
	if (LoadedClass)
	{
		return LoadedClass;
	}

	// Fallback: try /Script/AIModule prefix with the normalized shortname
	FString FullPath = FString::Printf(TEXT("/Script/AIModule.EnvQueryTest_%s"), *NormalizedName);
	LoadedClass = StaticLoadClass(UEnvQueryTest::StaticClass(), nullptr, *FullPath);

	return LoadedClass;
}

TSubclassOf<UEnvQueryContext> UEQSBuilder::ResolveContextClass(const FString& ContextName)
{
	// Built-in contexts
	if (ContextName == TEXT("Querier") || ContextName == TEXT("Self"))
	{
		return UEnvQueryContext_Querier::StaticClass();
	}
	if (ContextName == TEXT("Item") || ContextName == TEXT("Items"))
	{
		return UEnvQueryContext_Item::StaticClass();
	}

	// Try loading from AIModule
	FString FullPath = FString::Printf(TEXT("/Script/AIModule.EnvQueryContext_%s"), *ContextName);
	UClass* Found = StaticLoadClass(UEnvQueryContext::StaticClass(), nullptr, *FullPath);
	if (Found)
	{
		return Found;
	}

	// Try as a direct content/class path
	Found = StaticLoadClass(UEnvQueryContext::StaticClass(), nullptr, *ContextName);
	if (Found)
	{
		return Found;
	}

	UE_LOG(LogArborEQS, Warning, TEXT("Unknown context '%s', defaulting to Querier"), *ContextName);
	return UEnvQueryContext_Querier::StaticClass();
}

// ============================================================================
// Parameter Application
// ============================================================================

void UEQSBuilder::ApplyGeneratorParams(
	UObject* Generator, const TSharedPtr<FJsonObject>& ParamsJson)
{
	if (!ParamsJson.IsValid() || !Generator)
	{
		return;
	}

	// Context-type params — these are TSubclassOf<UEnvQueryContext> properties
	static const TArray<FString> ContextParamNames = {
		TEXT("GenerateAround"), TEXT("CircleCenter"), TEXT("PathToItem"),
	};

	for (const auto& Pair : ParamsJson->Values)
	{
		const FString& Key = Pair.Key;
		const TSharedPtr<FJsonValue>& Value = Pair.Value;

		// Check if this is a context reference
		if (ContextParamNames.Contains(Key))
		{
			TSubclassOf<UEnvQueryContext> ContextClass = ResolveContextClass(Value->AsString());
			SetContextParam(Generator, Key, ContextClass);
			continue;
		}

		// Handle SearchedActorClass specially (it's a TSubclassOf<AActor>)
		if (Key == TEXT("SearchedActorClass"))
		{
			FString ClassName = Value->AsString();
			FString ClassPath = FString::Printf(TEXT("/Script/Engine.%s"), *ClassName);
			UClass* ActorClass = StaticLoadClass(AActor::StaticClass(), nullptr, *ClassPath);
			if (!ActorClass)
			{
				ActorClass = StaticLoadClass(AActor::StaticClass(), nullptr, *ClassName);
			}
			if (ActorClass)
			{
				FProperty* Prop = Generator->GetClass()->FindPropertyByName(FName(*Key));
				if (FClassProperty* ClassProp = CastField<FClassProperty>(Prop))
				{
					void* PropAddr = ClassProp->ContainerPtrToValuePtr<void>(Generator);
					ClassProp->SetPropertyValue(PropAddr, ActorClass);
				}
			}
			else
			{
				UE_LOG(LogArborEQS, Warning, TEXT("Could not resolve actor class: %s"), *ClassName);
			}
			continue;
		}

		// Numeric params (handles FAIDataProviderFloatValue, FAIDataProviderIntValue, plain float/int)
		if (Value->Type == EJson::Number)
		{
			SetNumericParam(Generator, Key, Value->AsNumber());
			continue;
		}

		// Bool params
		if (Value->Type == EJson::Boolean)
		{
			FProperty* Prop = Generator->GetClass()->FindPropertyByName(FName(*Key));
			if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
			{
				void* PropAddr = BoolProp->ContainerPtrToValuePtr<void>(Generator);
				BoolProp->SetPropertyValue(PropAddr, Value->AsBool());
			}
			continue;
		}

		// String params (enum or plain string)
		if (Value->Type == EJson::String)
		{
			FProperty* Prop = Generator->GetClass()->FindPropertyByName(FName(*Key));
			if (!Prop)
			{
				UE_LOG(LogArborEQS, Warning, TEXT("Generator property '%s' not found"), *Key);
				continue;
			}

			void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Generator);

			if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
			{
				UEnum* EnumDef = EnumProp->GetEnum();
				if (EnumDef)
				{
					int64 EnumValue = EnumDef->GetValueByNameString(Value->AsString());
					if (EnumValue != INDEX_NONE)
					{
						EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(PropAddr, EnumValue);
					}
				}
			}
			else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
			{
				if (ByteProp->Enum)
				{
					int64 EnumValue = ByteProp->Enum->GetValueByNameString(Value->AsString());
					if (EnumValue != INDEX_NONE)
					{
						ByteProp->SetPropertyValue(PropAddr, static_cast<uint8>(EnumValue));
					}
				}
			}
			else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
			{
				StrProp->SetPropertyValue(PropAddr, Value->AsString());
			}
			else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
			{
				NameProp->SetPropertyValue(PropAddr, FName(*Value->AsString()));
			}
		}
	}
}

void UEQSBuilder::ApplyTestParams(
	UObject* Test, const TSharedPtr<FJsonObject>& ParamsJson)
{
	if (!ParamsJson.IsValid() || !Test)
	{
		return;
	}

	UEnvQueryTest* EQSTest = Cast<UEnvQueryTest>(Test);
	if (!EQSTest)
	{
		return;
	}

	// Context-type params on tests
	static const TArray<FString> ContextParamNames = {
		TEXT("DistanceTo"), TEXT("TraceFrom"), TEXT("PathFrom"),
		TEXT("LineA"), TEXT("LineB"), TEXT("Context"),
	};

	// Handle TestPurpose
	FString TestPurpose;
	if (ParamsJson->TryGetStringField(TEXT("TestPurpose"), TestPurpose))
	{
		if (TestPurpose == TEXT("FilterOnly") || TestPurpose == TEXT("Filter"))
		{
			EQSTest->TestPurpose = EEnvTestPurpose::Filter;
		}
		else if (TestPurpose == TEXT("ScoreOnly") || TestPurpose == TEXT("Score"))
		{
			EQSTest->TestPurpose = EEnvTestPurpose::Score;
		}
		else if (TestPurpose == TEXT("FilterAndScore"))
		{
			EQSTest->TestPurpose = EEnvTestPurpose::FilterAndScore;
		}
	}

	// Handle ScoringEquation
	FString ScoringEq;
	if (ParamsJson->TryGetStringField(TEXT("ScoringEquation"), ScoringEq))
	{
		if (ScoringEq == TEXT("Linear"))
		{
			EQSTest->ScoringEquation = EEnvTestScoreEquation::Linear;
		}
		else if (ScoringEq == TEXT("Square"))
		{
			EQSTest->ScoringEquation = EEnvTestScoreEquation::Square;
		}
		else if (ScoringEq == TEXT("InverseLinear"))
		{
			EQSTest->ScoringEquation = EEnvTestScoreEquation::InverseLinear;
		}
		else if (ScoringEq == TEXT("SquareRoot"))
		{
			EQSTest->ScoringEquation = EEnvTestScoreEquation::SquareRoot;
		}
		else if (ScoringEq == TEXT("Constant"))
		{
			EQSTest->ScoringEquation = EEnvTestScoreEquation::Constant;
		}
	}

	// Handle FilterType
	FString FilterType;
	if (ParamsJson->TryGetStringField(TEXT("FilterType"), FilterType))
	{
		if (FilterType == TEXT("Minimum"))
		{
			EQSTest->FilterType = EEnvTestFilterType::Minimum;
		}
		else if (FilterType == TEXT("Maximum"))
		{
			EQSTest->FilterType = EEnvTestFilterType::Maximum;
		}
		else if (FilterType == TEXT("Range"))
		{
			EQSTest->FilterType = EEnvTestFilterType::Range;
		}
	}

	// Process remaining params
	for (const auto& Pair : ParamsJson->Values)
	{
		const FString& Key = Pair.Key;
		const TSharedPtr<FJsonValue>& Value = Pair.Value;

		// Skip already-handled special keys
		if (Key == TEXT("TestPurpose") || Key == TEXT("ScoringEquation") || Key == TEXT("FilterType"))
		{
			continue;
		}

		// Context references
		if (ContextParamNames.Contains(Key))
		{
			TSubclassOf<UEnvQueryContext> ContextClass = ResolveContextClass(Value->AsString());
			SetContextParam(Test, Key, ContextClass);
			continue;
		}

		// Numeric params
		if (Value->Type == EJson::Number)
		{
			SetNumericParam(Test, Key, Value->AsNumber());
			continue;
		}

		// Bool params (handles FAIDataProviderBoolValue too)
		if (Value->Type == EJson::Boolean)
		{
			bool BoolVal = Value->AsBool();
			FProperty* Prop = Test->GetClass()->FindPropertyByName(FName(*Key));
			if (!Prop)
			{
				// Try with 'b' prefix
				Prop = Test->GetClass()->FindPropertyByName(FName(*(TEXT("b") + Key)));
			}
			if (Prop)
			{
				void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Test);
				if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
				{
					BoolProp->SetPropertyValue(PropAddr, BoolVal);
				}
				else if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
				{
					if (StructProp->Struct == FAIDataProviderBoolValue::StaticStruct())
					{
						FAIDataProviderBoolValue* Provider = static_cast<FAIDataProviderBoolValue*>(PropAddr);
						Provider->DefaultValue = BoolVal;
					}
				}
			}
			continue;
		}

		// String params (enum or plain)
		if (Value->Type == EJson::String)
		{
			FProperty* Prop = Test->GetClass()->FindPropertyByName(FName(*Key));
			if (!Prop)
			{
				UE_LOG(LogArborEQS, Warning, TEXT("Test property '%s' not found"), *Key);
				continue;
			}

			void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Test);

			if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
			{
				UEnum* EnumDef = EnumProp->GetEnum();
				if (EnumDef)
				{
					int64 EnumValue = EnumDef->GetValueByNameString(Value->AsString());
					if (EnumValue != INDEX_NONE)
					{
						EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(PropAddr, EnumValue);
					}
				}
			}
			else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
			{
				if (ByteProp->Enum)
				{
					int64 EnumValue = ByteProp->Enum->GetValueByNameString(Value->AsString());
					if (EnumValue != INDEX_NONE)
					{
						ByteProp->SetPropertyValue(PropAddr, static_cast<uint8>(EnumValue));
					}
				}
			}
			else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
			{
				StrProp->SetPropertyValue(PropAddr, Value->AsString());
			}
		}
	}
}

// ============================================================================
// Property Helpers
// ============================================================================

void UEQSBuilder::SetNumericParam(
	UObject* Object, const FString& PropertyName, double Value)
{
	FProperty* Prop = Object->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		UE_LOG(LogArborEQS, Warning, TEXT("Property '%s' not found on %s"),
			*PropertyName, *Object->GetClass()->GetName());
		return;
	}

	void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Object);

	// FAIDataProviderFloatValue — the most common case for EQS numeric params
	if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		if (StructProp->Struct == FAIDataProviderFloatValue::StaticStruct())
		{
			FAIDataProviderFloatValue* Provider = static_cast<FAIDataProviderFloatValue*>(PropAddr);
			Provider->DefaultValue = static_cast<float>(Value);
			return;
		}
		if (StructProp->Struct == FAIDataProviderIntValue::StaticStruct())
		{
			FAIDataProviderIntValue* Provider = static_cast<FAIDataProviderIntValue*>(PropAddr);
			Provider->DefaultValue = static_cast<int32>(Value);
			return;
		}
	}

	// Plain float/double/int
	if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
	{
		FloatProp->SetPropertyValue(PropAddr, static_cast<float>(Value));
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
	{
		DoubleProp->SetPropertyValue(PropAddr, Value);
	}
	else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
	{
		IntProp->SetPropertyValue(PropAddr, static_cast<int32>(Value));
	}
	else
	{
		UE_LOG(LogArborEQS, Warning, TEXT("Property '%s' on %s is not a numeric type"),
			*PropertyName, *Object->GetClass()->GetName());
	}
}

void UEQSBuilder::SetContextParam(
	UObject* Object, const FString& PropertyName,
	TSubclassOf<UEnvQueryContext> ContextClass)
{
	FProperty* Prop = Object->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		UE_LOG(LogArborEQS, Warning, TEXT("Context property '%s' not found on %s"),
			*PropertyName, *Object->GetClass()->GetName());
		return;
	}

	void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Object);

	if (FClassProperty* ClassProp = CastField<FClassProperty>(Prop))
	{
		ClassProp->SetPropertyValue(PropAddr, ContextClass.Get());
	}
	else if (FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Prop))
	{
		SoftClassProp->SetPropertyValue(PropAddr, FSoftObjectPtr(ContextClass.Get()));
	}
	else
	{
		UE_LOG(LogArborEQS, Warning, TEXT("Property '%s' on %s is not a class reference type"),
			*PropertyName, *Object->GetClass()->GetName());
	}
}

// ============================================================================
// Asset Saving
// ============================================================================

bool UEQSBuilder::SaveAsset(UObject* Asset)
{
	if (!Asset)
	{
		return false;
	}

	UPackage* Package = Asset->GetPackage();
	FString PackageName = Package->GetName();
	FString PackageFileName = FPackageName::LongPackageNameToFilename(
		PackageName, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;

	bool bSuccess = UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs);

	if (bSuccess)
	{
		FAssetRegistryModule::AssetCreated(Asset);
		UE_LOG(LogArborEQS, Log, TEXT("Saved asset: %s"), *PackageName);
	}
	else
	{
		UE_LOG(LogArborEQS, Error, TEXT("Failed to save asset: %s"), *PackageName);
	}

	return bSuccess;
}

// ============================================================================
// Loading Helper
// ============================================================================

UEnvQuery* UEQSBuilder::LoadEQSForEditing(const FString& AssetPath)
{
	// AssetPath is e.g. "/Game/AI/EQS_FindPatrolPoint"
	FString AssetName = FPaths::GetBaseFilename(AssetPath);
	FString ObjectPath = AssetPath + TEXT(".") + AssetName;

	UEnvQuery* Query = LoadObject<UEnvQuery>(nullptr, *ObjectPath);
	if (!Query)
	{
		UE_LOG(LogArborEQS, Error, TEXT("Failed to load EQS asset: %s"), *AssetPath);
	}
	return Query;
}

// ============================================================================
// Query — Serialization Helpers
// ============================================================================

FString UEQSBuilder::GetContextName(TSubclassOf<UEnvQueryContext> ContextClass)
{
	if (!ContextClass)
	{
		return TEXT("Querier");
	}
	if (ContextClass == UEnvQueryContext_Querier::StaticClass())
	{
		return TEXT("Querier");
	}
	if (ContextClass == UEnvQueryContext_Item::StaticClass())
	{
		return TEXT("Item");
	}

	// Strip prefix for custom contexts
	FString ClassName = ContextClass->GetName();
	ClassName.RemoveFromStart(TEXT("EnvQueryContext_"));
	return ClassName;
}

FString UEQSBuilder::GetGeneratorTypeName(UObject* Generator)
{
	if (!Generator)
	{
		return TEXT("Unknown");
	}
	FString ClassName = Generator->GetClass()->GetName();
	ClassName.RemoveFromStart(TEXT("EnvQueryGenerator_"));
	return ClassName;
}

FString UEQSBuilder::GetTestTypeName(UObject* Test)
{
	if (!Test)
	{
		return TEXT("Unknown");
	}
	FString ClassName = Test->GetClass()->GetName();
	ClassName.RemoveFromStart(TEXT("EnvQueryTest_"));
	// Normalize Pathfinding → PathExist for consistency with input schema
	if (ClassName == TEXT("Pathfinding"))
	{
		ClassName = TEXT("PathExist");
	}
	return ClassName;
}

TSharedPtr<FJsonObject> UEQSBuilder::SerializeGeneratorParams(UObject* Generator)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	if (!Generator)
	{
		return Params;
	}

	UClass* Class = Generator->GetClass();

	// Context params on generators
	static const TArray<FString> ContextParamNames = {
		TEXT("GenerateAround"), TEXT("CircleCenter"), TEXT("PathToItem"),
	};

	for (TFieldIterator<FProperty> It(Class); It; ++It)
	{
		FProperty* Prop = *It;
		// Skip properties from the base generator class (internal UE stuff)
		if (Prop->GetOwnerClass() == UEnvQueryGenerator::StaticClass())
		{
			continue;
		}

		const FString PropName = Prop->GetName();
		void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Generator);

		// TSubclassOf<UEnvQueryContext> — context reference
		if (FClassProperty* ClassProp = CastField<FClassProperty>(Prop))
		{
			if (ClassProp->MetaClass && ClassProp->MetaClass->IsChildOf(UEnvQueryContext::StaticClass()))
			{
				UClass* ContextClass = Cast<UClass>(ClassProp->GetPropertyValue(PropAddr));
				if (ContextClass)
				{
					Params->SetStringField(PropName, GetContextName(ContextClass));
				}
				continue;
			}
			// TSubclassOf<AActor> — actor class reference (e.g. SearchedActorClass)
			UClass* ActorClass = Cast<UClass>(ClassProp->GetPropertyValue(PropAddr));
			if (ActorClass)
			{
				FString Name = ActorClass->GetName();
				Params->SetStringField(PropName, Name);
			}
			continue;
		}

		// FAIDataProviderFloatValue
		if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			if (StructProp->Struct == FAIDataProviderFloatValue::StaticStruct())
			{
				const FAIDataProviderFloatValue* Provider = static_cast<const FAIDataProviderFloatValue*>(PropAddr);
				Params->SetNumberField(PropName, Provider->DefaultValue);
				continue;
			}
			if (StructProp->Struct == FAIDataProviderIntValue::StaticStruct())
			{
				const FAIDataProviderIntValue* Provider = static_cast<const FAIDataProviderIntValue*>(PropAddr);
				Params->SetNumberField(PropName, Provider->DefaultValue);
				continue;
			}
		}

		// Plain numeric
		if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
		{
			Params->SetNumberField(PropName, FloatProp->GetPropertyValue(PropAddr));
			continue;
		}
		if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
		{
			Params->SetNumberField(PropName, DoubleProp->GetPropertyValue(PropAddr));
			continue;
		}
		if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
		{
			Params->SetNumberField(PropName, IntProp->GetPropertyValue(PropAddr));
			continue;
		}

		// Bool
		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			Params->SetBoolField(PropName, BoolProp->GetPropertyValue(PropAddr));
			continue;
		}

		// Enum
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			UEnum* EnumDef = EnumProp->GetEnum();
			if (EnumDef)
			{
				int64 EnumValue = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(PropAddr);
				FString EnumName = EnumDef->GetNameStringByValue(EnumValue);
				Params->SetStringField(PropName, EnumName);
			}
			continue;
		}
		if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			if (ByteProp->Enum)
			{
				uint8 ByteValue = ByteProp->GetPropertyValue(PropAddr);
				FString EnumName = ByteProp->Enum->GetNameStringByValue(ByteValue);
				Params->SetStringField(PropName, EnumName);
			}
			continue;
		}
	}

	return Params;
}

TSharedPtr<FJsonObject> UEQSBuilder::SerializeTestParams(UEnvQueryTest* Test)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	if (!Test)
	{
		return Params;
	}

	// TestPurpose
	switch (Test->TestPurpose)
	{
	case EEnvTestPurpose::Filter:
		Params->SetStringField(TEXT("TestPurpose"), TEXT("FilterOnly"));
		break;
	case EEnvTestPurpose::Score:
		Params->SetStringField(TEXT("TestPurpose"), TEXT("ScoreOnly"));
		break;
	case EEnvTestPurpose::FilterAndScore:
		Params->SetStringField(TEXT("TestPurpose"), TEXT("FilterAndScore"));
		break;
	}

	// ScoringEquation (only relevant for scoring)
	if (Test->TestPurpose != EEnvTestPurpose::Filter)
	{
		switch (Test->ScoringEquation)
		{
		case EEnvTestScoreEquation::Linear:
			Params->SetStringField(TEXT("ScoringEquation"), TEXT("Linear"));
			break;
		case EEnvTestScoreEquation::Square:
			Params->SetStringField(TEXT("ScoringEquation"), TEXT("Square"));
			break;
		case EEnvTestScoreEquation::InverseLinear:
			Params->SetStringField(TEXT("ScoringEquation"), TEXT("InverseLinear"));
			break;
		case EEnvTestScoreEquation::SquareRoot:
			Params->SetStringField(TEXT("ScoringEquation"), TEXT("SquareRoot"));
			break;
		case EEnvTestScoreEquation::Constant:
			Params->SetStringField(TEXT("ScoringEquation"), TEXT("Constant"));
			break;
		}
	}

	// FilterType (only relevant for filtering)
	if (Test->TestPurpose != EEnvTestPurpose::Score)
	{
		switch (Test->FilterType)
		{
		case EEnvTestFilterType::Minimum:
			Params->SetStringField(TEXT("FilterType"), TEXT("Minimum"));
			break;
		case EEnvTestFilterType::Maximum:
			Params->SetStringField(TEXT("FilterType"), TEXT("Maximum"));
			break;
		case EEnvTestFilterType::Range:
			Params->SetStringField(TEXT("FilterType"), TEXT("Range"));
			break;
		}
	}

	// Context params on tests
	static const TArray<FString> ContextParamNames = {
		TEXT("DistanceTo"), TEXT("TraceFrom"), TEXT("PathFrom"),
		TEXT("LineA"), TEXT("LineB"), TEXT("Context"),
	};

	UClass* Class = Test->GetClass();
	for (TFieldIterator<FProperty> It(Class); It; ++It)
	{
		FProperty* Prop = *It;
		// Skip base UEnvQueryTest properties (already handled above)
		if (Prop->GetOwnerClass() == UEnvQueryTest::StaticClass())
		{
			continue;
		}

		const FString PropName = Prop->GetName();
		void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Test);

		// TSubclassOf<UEnvQueryContext>
		if (FClassProperty* ClassProp = CastField<FClassProperty>(Prop))
		{
			if (ClassProp->MetaClass && ClassProp->MetaClass->IsChildOf(UEnvQueryContext::StaticClass()))
			{
				UClass* ContextClass = Cast<UClass>(ClassProp->GetPropertyValue(PropAddr));
				if (ContextClass)
				{
					Params->SetStringField(PropName, GetContextName(ContextClass));
				}
			}
			continue;
		}

		// FAIDataProviderFloatValue / IntValue
		if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			if (StructProp->Struct == FAIDataProviderFloatValue::StaticStruct())
			{
				const FAIDataProviderFloatValue* Provider = static_cast<const FAIDataProviderFloatValue*>(PropAddr);
				Params->SetNumberField(PropName, Provider->DefaultValue);
				continue;
			}
			if (StructProp->Struct == FAIDataProviderIntValue::StaticStruct())
			{
				const FAIDataProviderIntValue* Provider = static_cast<const FAIDataProviderIntValue*>(PropAddr);
				Params->SetNumberField(PropName, Provider->DefaultValue);
				continue;
			}
			if (StructProp->Struct == FAIDataProviderBoolValue::StaticStruct())
			{
				const FAIDataProviderBoolValue* Provider = static_cast<const FAIDataProviderBoolValue*>(PropAddr);
				Params->SetBoolField(PropName, Provider->DefaultValue);
				continue;
			}
		}

		// Plain numeric
		if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
		{
			Params->SetNumberField(PropName, FloatProp->GetPropertyValue(PropAddr));
			continue;
		}
		if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
		{
			Params->SetNumberField(PropName, DoubleProp->GetPropertyValue(PropAddr));
			continue;
		}
		if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
		{
			Params->SetNumberField(PropName, IntProp->GetPropertyValue(PropAddr));
			continue;
		}

		// Bool
		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			Params->SetBoolField(PropName, BoolProp->GetPropertyValue(PropAddr));
			continue;
		}

		// Enum
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			UEnum* EnumDef = EnumProp->GetEnum();
			if (EnumDef)
			{
				int64 EnumValue = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(PropAddr);
				Params->SetStringField(PropName, EnumDef->GetNameStringByValue(EnumValue));
			}
			continue;
		}
		if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			if (ByteProp->Enum)
			{
				uint8 ByteValue = ByteProp->GetPropertyValue(PropAddr);
				Params->SetStringField(PropName, ByteProp->Enum->GetNameStringByValue(ByteValue));
			}
			continue;
		}
	}

	return Params;
}

// ============================================================================
// Query — Main Function
// ============================================================================

FString UEQSBuilder::QueryEQS(const FString& AssetPath)
{
	UEnvQuery* Query = LoadEQSForEditing(AssetPath);
	if (!Query)
	{
		return TEXT("{\"error\": \"Failed to load EQS asset\"}");
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), Query->GetName());
	Result->SetStringField(TEXT("asset_path"), AssetPath);

	// Serialize options (one per generator)
	TArray<TSharedPtr<FJsonValue>> GeneratorsArray;
	TArray<TSharedPtr<FJsonValue>> TestsArray;

	// Collect tests from first option (they're shared across all options)
	bool bTestsCollected = false;

	const TArray<UEnvQueryOption*>& Options = Query->GetOptions();
	for (int32 i = 0; i < Options.Num(); ++i)
	{
		UEnvQueryOption* Option = Options[i];
		if (!Option)
		{
			continue;
		}

		// Generator
		if (Option->Generator)
		{
			TSharedPtr<FJsonObject> GenObj = MakeShared<FJsonObject>();
			GenObj->SetStringField(TEXT("type"), GetGeneratorTypeName(Option->Generator));
			GenObj->SetNumberField(TEXT("option_index"), i);
			GenObj->SetObjectField(TEXT("params"), SerializeGeneratorParams(Option->Generator));
			GeneratorsArray.Add(MakeShared<FJsonValueObject>(GenObj));
		}

		// Tests — collect from first option only (they're duplicated across options)
		if (!bTestsCollected && Option->Tests.Num() > 0)
		{
			for (int32 j = 0; j < Option->Tests.Num(); ++j)
			{
				UEnvQueryTest* Test = Option->Tests[j];
				if (!Test)
				{
					continue;
				}

				TSharedPtr<FJsonObject> TestObj = MakeShared<FJsonObject>();
				TestObj->SetStringField(TEXT("type"), GetTestTypeName(Test));
				TestObj->SetNumberField(TEXT("test_index"), j);
				TestObj->SetObjectField(TEXT("params"), SerializeTestParams(Test));
				TestsArray.Add(MakeShared<FJsonValueObject>(TestObj));
			}
			bTestsCollected = true;
		}
	}

	Result->SetArrayField(TEXT("generators"), GeneratorsArray);
	Result->SetArrayField(TEXT("tests"), TestsArray);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	return OutputString;
}

// ============================================================================
// Granular Editing
// ============================================================================

FString UEQSBuilder::AddEQSGenerator(
	const FString& AssetPath, int32 OptionIndex, const FString& GeneratorJsonString)
{
	UEnvQuery* Query = LoadEQSForEditing(AssetPath);
	if (!Query)
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to load EQS asset\"}");
	}

	// Parse generator JSON
	TSharedPtr<FJsonObject> GenJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(GeneratorJsonString);
	if (!FJsonSerializer::Deserialize(Reader, GenJson) || !GenJson.IsValid())
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to parse generator JSON\"}");
	}

	FString GenType;
	if (!GenJson->TryGetStringField(TEXT("type"), GenType))
	{
		return TEXT("{\"success\": false, \"error\": \"Generator JSON missing 'type'\"}");
	}

	UClass* GenClass = ResolveGeneratorClass(GenType);
	if (!GenClass)
	{
		return FString::Printf(TEXT("{\"success\": false, \"error\": \"Unknown generator type: %s\"}"), *GenType);
	}

	// Create option + generator
	UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Query);
	UEnvQueryGenerator* Generator = NewObject<UEnvQueryGenerator>(Option, GenClass);

	const TSharedPtr<FJsonObject>* GenParams;
	if (GenJson->TryGetObjectField(TEXT("params"), GenParams))
	{
		ApplyGeneratorParams(Generator, *GenParams);
	}
	Option->Generator = Generator;

	// Copy tests from existing options (tests are shared)
	auto& Options = Query->GetOptionsMutable();
	if (Options.Num() > 0 && Options[0])
	{
		for (UEnvQueryTest* ExistingTest : Options[0]->Tests)
		{
			if (ExistingTest)
			{
				UEnvQueryTest* TestCopy = DuplicateObject(ExistingTest, Option);
				Option->Tests.Add(TestCopy);
			}
		}
	}

	// Insert at specified index or append
	if (OptionIndex >= 0 && OptionIndex < Options.Num())
	{
		Options.Insert(Option, OptionIndex);
	}
	else
	{
		OptionIndex = Options.Num();
		Options.Add(Option);
	}

	SaveAsset(Query);
	return FString::Printf(TEXT("{\"success\": true, \"option_index\": %d}"), OptionIndex);
}

FString UEQSBuilder::RemoveEQSGenerator(const FString& AssetPath, int32 OptionIndex)
{
	UEnvQuery* Query = LoadEQSForEditing(AssetPath);
	if (!Query)
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to load EQS asset\"}");
	}

	auto& Options = Query->GetOptionsMutable();
	if (OptionIndex < 0 || OptionIndex >= Options.Num())
	{
		return TEXT("{\"success\": false, \"error\": \"Option index out of range\"}");
	}

	Options.RemoveAt(OptionIndex);
	SaveAsset(Query);
	return TEXT("{\"success\": true}");
}

FString UEQSBuilder::SetEQSGeneratorParams(
	const FString& AssetPath, int32 OptionIndex, const FString& ParamsJsonString)
{
	UEnvQuery* Query = LoadEQSForEditing(AssetPath);
	if (!Query)
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to load EQS asset\"}");
	}

	const TArray<UEnvQueryOption*>& Options = Query->GetOptions();
	if (OptionIndex < 0 || OptionIndex >= Options.Num())
	{
		return TEXT("{\"success\": false, \"error\": \"Option index out of range\"}");
	}

	UEnvQueryOption* Option = Options[OptionIndex];
	if (!Option || !Option->Generator)
	{
		return TEXT("{\"success\": false, \"error\": \"Option has no generator\"}");
	}

	TSharedPtr<FJsonObject> ParamsJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ParamsJsonString);
	if (!FJsonSerializer::Deserialize(Reader, ParamsJson) || !ParamsJson.IsValid())
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to parse params JSON\"}");
	}

	ApplyGeneratorParams(Option->Generator, ParamsJson);
	SaveAsset(Query);
	return TEXT("{\"success\": true}");
}

FString UEQSBuilder::AddEQSTest(
	const FString& AssetPath, int32 OptionIndex, int32 TestIndex,
	const FString& TestJsonString)
{
	UEnvQuery* Query = LoadEQSForEditing(AssetPath);
	if (!Query)
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to load EQS asset\"}");
	}

	// Parse test JSON
	TSharedPtr<FJsonObject> TestJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TestJsonString);
	if (!FJsonSerializer::Deserialize(Reader, TestJson) || !TestJson.IsValid())
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to parse test JSON\"}");
	}

	FString TestType;
	if (!TestJson->TryGetStringField(TEXT("type"), TestType))
	{
		return TEXT("{\"success\": false, \"error\": \"Test JSON missing 'type'\"}");
	}

	UClass* TestClass = ResolveTestClass(TestType);
	if (!TestClass)
	{
		return FString::Printf(TEXT("{\"success\": false, \"error\": \"Unknown test type: %s\"}"), *TestType);
	}

	auto& Options = Query->GetOptionsMutable();
	if (Options.Num() == 0)
	{
		return TEXT("{\"success\": false, \"error\": \"EQS has no options/generators\"}");
	}

	// Determine which options to add the test to
	int32 StartIdx = 0;
	int32 EndIdx = Options.Num();
	if (OptionIndex >= 0)
	{
		if (OptionIndex >= Options.Num())
		{
			return TEXT("{\"success\": false, \"error\": \"Option index out of range\"}");
		}
		StartIdx = OptionIndex;
		EndIdx = OptionIndex + 1;
	}

	int32 ActualTestIndex = -1;
	for (int32 i = StartIdx; i < EndIdx; ++i)
	{
		UEnvQueryOption* Option = Options[i];
		if (!Option)
		{
			continue;
		}

		UEnvQueryTest* Test = NewObject<UEnvQueryTest>(Option, TestClass);

		const TSharedPtr<FJsonObject>* TestParams;
		if (TestJson->TryGetObjectField(TEXT("params"), TestParams))
		{
			ApplyTestParams(Test, *TestParams);
		}

		if (TestIndex >= 0 && TestIndex < Option->Tests.Num())
		{
			Option->Tests.Insert(Test, TestIndex);
			ActualTestIndex = TestIndex;
		}
		else
		{
			ActualTestIndex = Option->Tests.Num();
			Option->Tests.Add(Test);
		}
	}

	SaveAsset(Query);
	return FString::Printf(TEXT("{\"success\": true, \"test_index\": %d}"), ActualTestIndex);
}

FString UEQSBuilder::RemoveEQSTest(
	const FString& AssetPath, int32 OptionIndex, int32 TestIndex)
{
	UEnvQuery* Query = LoadEQSForEditing(AssetPath);
	if (!Query)
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to load EQS asset\"}");
	}

	auto& Options = Query->GetOptionsMutable();
	if (Options.Num() == 0)
	{
		return TEXT("{\"success\": false, \"error\": \"EQS has no options\"}");
	}

	// Determine which options to remove the test from
	int32 StartIdx = 0;
	int32 EndIdx = Options.Num();
	if (OptionIndex >= 0)
	{
		if (OptionIndex >= Options.Num())
		{
			return TEXT("{\"success\": false, \"error\": \"Option index out of range\"}");
		}
		StartIdx = OptionIndex;
		EndIdx = OptionIndex + 1;
	}

	for (int32 i = StartIdx; i < EndIdx; ++i)
	{
		UEnvQueryOption* Option = Options[i];
		if (!Option)
		{
			continue;
		}
		if (TestIndex < 0 || TestIndex >= Option->Tests.Num())
		{
			return TEXT("{\"success\": false, \"error\": \"Test index out of range\"}");
		}
		Option->Tests.RemoveAt(TestIndex);
	}

	SaveAsset(Query);
	return TEXT("{\"success\": true}");
}

FString UEQSBuilder::SetEQSTestParams(
	const FString& AssetPath, int32 OptionIndex, int32 TestIndex,
	const FString& ParamsJsonString)
{
	UEnvQuery* Query = LoadEQSForEditing(AssetPath);
	if (!Query)
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to load EQS asset\"}");
	}

	TSharedPtr<FJsonObject> ParamsJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ParamsJsonString);
	if (!FJsonSerializer::Deserialize(Reader, ParamsJson) || !ParamsJson.IsValid())
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to parse params JSON\"}");
	}

	auto& Options = Query->GetOptionsMutable();
	if (Options.Num() == 0)
	{
		return TEXT("{\"success\": false, \"error\": \"EQS has no options\"}");
	}

	// Determine which options to update
	int32 StartIdx = 0;
	int32 EndIdx = Options.Num();
	if (OptionIndex >= 0)
	{
		if (OptionIndex >= Options.Num())
		{
			return TEXT("{\"success\": false, \"error\": \"Option index out of range\"}");
		}
		StartIdx = OptionIndex;
		EndIdx = OptionIndex + 1;
	}

	for (int32 i = StartIdx; i < EndIdx; ++i)
	{
		UEnvQueryOption* Option = Options[i];
		if (!Option)
		{
			continue;
		}
		if (TestIndex < 0 || TestIndex >= Option->Tests.Num())
		{
			return TEXT("{\"success\": false, \"error\": \"Test index out of range\"}");
		}
		ApplyTestParams(Option->Tests[TestIndex], ParamsJson);
	}

	SaveAsset(Query);
	return TEXT("{\"success\": true}");
}
