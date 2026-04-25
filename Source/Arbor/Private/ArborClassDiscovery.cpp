#include "ArborClassDiscovery.h"

#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

// BT types
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTCompositeNode.h"

// EQS types
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "EnvironmentQuery/EnvQueryTest.h"

// PCG types
#include "PCGSettings.h"

// Blueprint node types
#include "K2Node.h"

// Component types
#include "Components/ActorComponent.h"

// AI data providers
#include "DataProviders/AIDataProvider.h"

DEFINE_LOG_CATEGORY_STATIC(LogArborDiscovery, Log, All);

// ============================================================================
// Friendly Name Maps
// ============================================================================

const TMap<FString, FString>& UArborClassDiscovery::GetBTFriendlyNames()
{
	static TMap<FString, FString> Map = {
		{TEXT("BTDecorator_Blackboard"), TEXT("Blackboard")},
		{TEXT("BTTask_SetBlackboardValue"), TEXT("SetBlackboardValue")},
		{TEXT("BTTask_Wait"), TEXT("Wait")},
		{TEXT("BTTask_MoveTo"), TEXT("MoveTo")},
		{TEXT("BTTask_RunBehavior"), TEXT("RunBehavior")},
		{TEXT("BTTask_RunEQSQuery"), TEXT("RunEQSQuery")},
		{TEXT("BTTask_MoveDirectlyToward"), TEXT("MoveDirectlyToward")},
		{TEXT("BTTask_PlayAnimation"), TEXT("PlayAnimation")},
		{TEXT("BTTask_PlaySound"), TEXT("PlaySound")},
		{TEXT("BTTask_RotateToFaceBBEntry"), TEXT("RotateToFaceBBEntry")},
		{TEXT("BTDecorator_Cooldown"), TEXT("Cooldown")},
		{TEXT("BTDecorator_TimeLimit"), TEXT("TimeLimit")},
		{TEXT("BTDecorator_Loop"), TEXT("Loop")},
		{TEXT("BTDecorator_ForceSuccess"), TEXT("ForceSuccess")},
		{TEXT("BTDecorator_ConditionalLoop"), TEXT("ConditionalLoop")},
		{TEXT("BTService_DefaultFocus"), TEXT("DefaultFocus")},
		{TEXT("BTService_RunEQS"), TEXT("RunEQS")},
	};
	return Map;
}

const TMap<FString, FString>& UArborClassDiscovery::GetEQSGeneratorFriendlyNames()
{
	// Reverse map: class name → friendly name
	static TMap<FString, FString> Map = {
		{TEXT("EnvQueryGenerator_SimpleGrid"), TEXT("SimpleGrid")},
		{TEXT("EnvQueryGenerator_OnCircle"), TEXT("OnCircle")},
		{TEXT("EnvQueryGenerator_Donut"), TEXT("Donut")},
		{TEXT("EnvQueryGenerator_PathingGrid"), TEXT("PathingGrid")},
		{TEXT("EnvQueryGenerator_CurrentLocation"), TEXT("CurrentLocation")},
		{TEXT("EnvQueryGenerator_ActorsOfClass"), TEXT("ActorsOfClass")},
	};
	return Map;
}

const TMap<FString, FString>& UArborClassDiscovery::GetEQSTestFriendlyNames()
{
	static TMap<FString, FString> Map = {
		{TEXT("EnvQueryTest_Distance"), TEXT("Distance")},
		{TEXT("EnvQueryTest_Trace"), TEXT("Trace")},
		{TEXT("EnvQueryTest_Pathfinding"), TEXT("Pathfinding")},
		{TEXT("EnvQueryTest_Dot"), TEXT("Dot")},
		{TEXT("EnvQueryTest_GameplayTags"), TEXT("GameplayTags")},
		{TEXT("EnvQueryTest_Overlap"), TEXT("Overlap")},
		{TEXT("EnvQueryTest_Project"), TEXT("Project")},
	};
	return Map;
}

const TMap<FString, FString>& UArborClassDiscovery::GetPCGFriendlyNames()
{
	// Reverse map: settings class name → friendly name
	static TMap<FString, FString> Map = {
		{TEXT("PCGSurfaceSamplerSettings"), TEXT("SurfaceSampler")},
		{TEXT("PCGVolumeSamplerSettings"), TEXT("VolumeSampler")},
		{TEXT("PCGGetActorDataSettings"), TEXT("GetActorData")},
		{TEXT("PCGGetLandscapeDataSettings"), TEXT("GetLandscapeData")},
		{TEXT("PCGGetSplineDataSettings"), TEXT("GetSplineData")},
		{TEXT("PCGTransformPointsSettings"), TEXT("TransformPoints")},
		{TEXT("PCGCopyPointsSettings"), TEXT("CopyPoints")},
		{TEXT("PCGCreatePointsGridSettings"), TEXT("CreatePointsGrid")},
		{TEXT("PCGBoundsModifierSettings"), TEXT("BoundsModifier")},
		{TEXT("PCGSelfPruningSettings"), TEXT("SelfPruning")},
		{TEXT("PCGProjectionSettings"), TEXT("Projection")},
		{TEXT("PCGDensityFilterSettings"), TEXT("DensityFilter")},
		{TEXT("PCGDensityRemapSettings"), TEXT("DensityRemap")},
		{TEXT("PCGDensityNoiseSettings"), TEXT("DensityNoise")},
		{TEXT("PCGPointFilterSettings"), TEXT("PointFilter")},
		{TEXT("PCGStaticMeshSpawnerSettings"), TEXT("StaticMeshSpawner")},
		{TEXT("PCGSpawnActorSettings"), TEXT("SpawnActor")},
		{TEXT("PCGSpatialNoiseSettings"), TEXT("SpatialNoise")},
		{TEXT("PCGSubgraphSettings"), TEXT("Subgraph")},
	};
	return Map;
}

// ============================================================================
// Helpers
// ============================================================================

FString UArborClassDiscovery::GetModuleName(UClass* Class)
{
	if (!Class) return TEXT("");
	FString PathName = Class->GetPathName();
	// e.g. /Script/AIModule.BTTask_Wait → AIModule
	int32 DotIndex;
	if (PathName.FindChar('.', DotIndex))
	{
		FString ModulePart = PathName.Left(DotIndex);
		int32 LastSlash;
		if (ModulePart.FindLastChar('/', LastSlash))
		{
			return ModulePart.Mid(LastSlash + 1);
		}
	}
	return TEXT("");
}

UClass* UArborClassDiscovery::ResolveClassByName(const FString& ClassName, UClass* BaseClass)
{
	if (ClassName.IsEmpty()) return nullptr;

	// Try direct load from common module paths
	TArray<FString> ModulePaths = {
		TEXT("/Script/AIModule"),
		TEXT("/Script/GameplayTasks"),
		TEXT("/Script/Engine"),
		TEXT("/Script/NavigationSystem"),
		TEXT("/Script/PCG"),
		TEXT("/Script/Arbor"),
		TEXT("/Script/CoreUObject"),
		TEXT("/Script/UMG"),
	};

	UClass* SearchBase = BaseClass ? BaseClass : UObject::StaticClass();

	for (const FString& ModulePath : ModulePaths)
	{
		FString FullPath = FString::Printf(TEXT("%s.%s"), *ModulePath, *ClassName);
		UClass* Found = StaticLoadClass(SearchBase, nullptr, *FullPath);
		if (Found) return Found;

		// Try with U prefix
		FullPath = FString::Printf(TEXT("%s.U%s"), *ModulePath, *ClassName);
		Found = StaticLoadClass(SearchBase, nullptr, *FullPath);
		if (Found) return Found;
	}

	// Fallback: iterate all loaded classes
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Candidate = *It;
		const FString& CandidateName = Candidate->GetName();
		if (CandidateName == ClassName ||
			CandidateName == (TEXT("U") + ClassName) ||
			CandidateName == (TEXT("A") + ClassName))
		{
			if (!BaseClass || Candidate->IsChildOf(BaseClass))
			{
				return Candidate;
			}
		}
	}

	return nullptr;
}

TArray<UArborClassDiscovery::FClassInfo> UArborClassDiscovery::EnumerateSubclasses(
	UClass* Base, const FString& Filter, bool ExcludeAbstract)
{
	TArray<FClassInfo> Results;
	if (!Base) return Results;

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Candidate = *It;
		if (!Candidate->IsChildOf(Base)) continue;
		if (Candidate == Base) continue;
		if (ExcludeAbstract && Candidate->HasAnyClassFlags(CLASS_Abstract)) continue;

		const FString Name = Candidate->GetName();

		// Apply substring filter
		if (!Filter.IsEmpty() && !Name.Contains(Filter))
		{
			continue;
		}

		FClassInfo Info;
		Info.ClassName = Name;
		Info.Module = GetModuleName(Candidate);
		Results.Add(MoveTemp(Info));
	}

	// Sort alphabetically
	Results.Sort([](const FClassInfo& A, const FClassInfo& B)
	{
		return A.ClassName < B.ClassName;
	});

	return Results;
}

TSharedPtr<FJsonObject> UArborClassDiscovery::SerializePropertyDescriptor(FProperty* Prop, UObject* CDO)
{
	if (!Prop) return nullptr;

	auto Desc = MakeShared<FJsonObject>();
	Desc->SetStringField(TEXT("name"), Prop->GetName());

	// Determine type string
	FString TypeStr;
	if (CastField<FFloatProperty>(Prop) || CastField<FDoubleProperty>(Prop))
	{
		TypeStr = TEXT("float");
	}
	else if (CastField<FIntProperty>(Prop) || CastField<FInt64Property>(Prop))
	{
		TypeStr = TEXT("int");
	}
	else if (CastField<FBoolProperty>(Prop))
	{
		TypeStr = TEXT("bool");
	}
	else if (CastField<FStrProperty>(Prop))
	{
		TypeStr = TEXT("string");
	}
	else if (CastField<FNameProperty>(Prop))
	{
		TypeStr = TEXT("name");
	}
	else if (CastField<FTextProperty>(Prop))
	{
		TypeStr = TEXT("text");
	}
	else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
	{
		TypeStr = TEXT("enum");
		if (UEnum* Enum = EnumProp->GetEnum())
		{
			Desc->SetStringField(TEXT("enum_type"), Enum->GetName());
			TArray<TSharedPtr<FJsonValue>> EnumValues;
			for (int32 i = 0; i < Enum->NumEnums() - 1; ++i) // -1 to skip _MAX
			{
				EnumValues.Add(MakeShared<FJsonValueString>(Enum->GetNameStringByIndex(i)));
			}
			Desc->SetArrayField(TEXT("enum_values"), EnumValues);
		}
	}
	else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
	{
		if (ByteProp->Enum)
		{
			TypeStr = TEXT("enum");
			Desc->SetStringField(TEXT("enum_type"), ByteProp->Enum->GetName());
			TArray<TSharedPtr<FJsonValue>> EnumValues;
			for (int32 i = 0; i < ByteProp->Enum->NumEnums() - 1; ++i)
			{
				EnumValues.Add(MakeShared<FJsonValueString>(ByteProp->Enum->GetNameStringByIndex(i)));
			}
			Desc->SetArrayField(TEXT("enum_values"), EnumValues);
		}
		else
		{
			TypeStr = TEXT("byte");
		}
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		FName StructName = StructProp->Struct->GetFName();
		if (StructName == TEXT("Vector"))
			TypeStr = TEXT("Vector");
		else if (StructName == TEXT("Rotator"))
			TypeStr = TEXT("Rotator");
		else if (StructName == TEXT("BlackboardKeySelector"))
			TypeStr = TEXT("BlackboardKeySelector");
		else if (StructProp->Struct == FAIDataProviderFloatValue::StaticStruct())
			TypeStr = TEXT("AIDataProviderFloat");
		else if (StructProp->Struct == FAIDataProviderIntValue::StaticStruct())
			TypeStr = TEXT("AIDataProviderInt");
		else if (StructProp->Struct == FAIDataProviderBoolValue::StaticStruct())
			TypeStr = TEXT("AIDataProviderBool");
		else if (StructName == TEXT("LinearColor"))
			TypeStr = TEXT("LinearColor");
		else if (StructName == TEXT("Transform"))
			TypeStr = TEXT("Transform");
		else if (StructName == TEXT("SoftObjectPath"))
			TypeStr = TEXT("SoftObjectPath");
		else
			TypeStr = FString::Printf(TEXT("struct:%s"), *StructProp->Struct->GetName());
	}
	else if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
	{
		if (ObjProp->PropertyClass)
		{
			TypeStr = FString::Printf(TEXT("object:%s"), *ObjProp->PropertyClass->GetName());
		}
		else
		{
			TypeStr = TEXT("object");
		}
	}
	else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
	{
		TypeStr = TEXT("array");
	}
	else if (FMapProperty* MapProp = CastField<FMapProperty>(Prop))
	{
		TypeStr = TEXT("map");
	}
	else if (FSetProperty* SetProp = CastField<FSetProperty>(Prop))
	{
		TypeStr = TEXT("set");
	}
	else
	{
		TypeStr = TEXT("unknown");
	}

	Desc->SetStringField(TEXT("type"), TypeStr);

	// Editability
	bool bEditable = Prop->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible);
	Desc->SetBoolField(TEXT("editable"), bEditable);

	// Default value from CDO
	if (CDO)
	{
		void* ValueAddr = Prop->ContainerPtrToValuePtr<void>(CDO);
		FString DefaultStr;
		Prop->ExportTextItem_Direct(DefaultStr, ValueAddr, nullptr, CDO, PPF_None);
		if (!DefaultStr.IsEmpty())
		{
			Desc->SetStringField(TEXT("default"), DefaultStr);
		}
	}

	// Tooltip from metadata
#if WITH_EDITORONLY_DATA
	if (Prop->HasMetaData(TEXT("ToolTip")))
	{
		FString Tooltip = Prop->GetMetaData(TEXT("ToolTip"));
		if (!Tooltip.IsEmpty() && Tooltip.Len() < 200)
		{
			Desc->SetStringField(TEXT("tooltip"), Tooltip);
		}
	}
#endif

	return Desc;
}

// ============================================================================
// JSON Serialization Helper
// ============================================================================

static FString SerializeJsonObject(const TSharedRef<FJsonObject>& Root)
{
	FString OutputString;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
	FJsonSerializer::Serialize(Root, Writer);
	return OutputString;
}

// ============================================================================
// ListBTNodeTypes
// ============================================================================

FString UArborClassDiscovery::ListBTNodeTypes(const FString& Category, const FString& Filter)
{
	auto Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ClassesArray;

	auto AddClasses = [&](UClass* Base, const FString& Cat)
	{
		TArray<FClassInfo> Infos = EnumerateSubclasses(Base, Filter);
		const TMap<FString, FString>& FriendlyNames = GetBTFriendlyNames();

		for (const FClassInfo& Info : Infos)
		{
			auto Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("class"), Info.ClassName);
			Obj->SetStringField(TEXT("category"), Cat);
			Obj->SetStringField(TEXT("module"), Info.Module);

			if (const FString* Friendly = FriendlyNames.Find(Info.ClassName))
			{
				Obj->SetStringField(TEXT("friendly_name"), *Friendly);
			}

			ClassesArray.Add(MakeShared<FJsonValueObject>(Obj));
		}
	};

	FString Cat = Category.ToLower();

	if (Cat.IsEmpty() || Cat == TEXT("task"))
		AddClasses(UBTTaskNode::StaticClass(), TEXT("task"));
	if (Cat.IsEmpty() || Cat == TEXT("decorator"))
		AddClasses(UBTDecorator::StaticClass(), TEXT("decorator"));
	if (Cat.IsEmpty() || Cat == TEXT("service"))
		AddClasses(UBTService::StaticClass(), TEXT("service"));
	if (Cat.IsEmpty() || Cat == TEXT("composite"))
		AddClasses(UBTCompositeNode::StaticClass(), TEXT("composite"));

	Root->SetArrayField(TEXT("classes"), ClassesArray);
	Root->SetNumberField(TEXT("count"), ClassesArray.Num());

	UE_LOG(LogArborDiscovery, Log, TEXT("ListBTNodeTypes(category='%s', filter='%s') → %d classes"),
		*Category, *Filter, ClassesArray.Num());

	return SerializeJsonObject(Root);
}

// ============================================================================
// ListEQSGeneratorTypes
// ============================================================================

FString UArborClassDiscovery::ListEQSGeneratorTypes(const FString& Filter)
{
	auto Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> GeneratorsArray;

	TArray<FClassInfo> Infos = EnumerateSubclasses(UEnvQueryGenerator::StaticClass(), Filter);
	const TMap<FString, FString>& FriendlyNames = GetEQSGeneratorFriendlyNames();

	for (const FClassInfo& Info : Infos)
	{
		auto Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("class"), Info.ClassName);
		Obj->SetStringField(TEXT("module"), Info.Module);

		if (const FString* Friendly = FriendlyNames.Find(Info.ClassName))
		{
			Obj->SetStringField(TEXT("friendly_name"), *Friendly);
		}

		GeneratorsArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	Root->SetArrayField(TEXT("generators"), GeneratorsArray);
	Root->SetNumberField(TEXT("count"), GeneratorsArray.Num());

	UE_LOG(LogArborDiscovery, Log, TEXT("ListEQSGeneratorTypes(filter='%s') → %d generators"),
		*Filter, GeneratorsArray.Num());

	return SerializeJsonObject(Root);
}

// ============================================================================
// ListEQSTestTypes
// ============================================================================

FString UArborClassDiscovery::ListEQSTestTypes(const FString& Filter)
{
	auto Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> TestsArray;

	TArray<FClassInfo> Infos = EnumerateSubclasses(UEnvQueryTest::StaticClass(), Filter);
	const TMap<FString, FString>& FriendlyNames = GetEQSTestFriendlyNames();

	for (const FClassInfo& Info : Infos)
	{
		auto Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("class"), Info.ClassName);
		Obj->SetStringField(TEXT("module"), Info.Module);

		if (const FString* Friendly = FriendlyNames.Find(Info.ClassName))
		{
			Obj->SetStringField(TEXT("friendly_name"), *Friendly);
		}

		TestsArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	Root->SetArrayField(TEXT("tests"), TestsArray);
	Root->SetNumberField(TEXT("count"), TestsArray.Num());

	UE_LOG(LogArborDiscovery, Log, TEXT("ListEQSTestTypes(filter='%s') → %d tests"),
		*Filter, TestsArray.Num());

	return SerializeJsonObject(Root);
}

// ============================================================================
// ListPCGNodeTypes
// ============================================================================

FString UArborClassDiscovery::ListPCGNodeTypes(const FString& Filter)
{
	auto Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> NodeTypesArray;

	TArray<FClassInfo> Infos = EnumerateSubclasses(UPCGSettings::StaticClass(), Filter);
	const TMap<FString, FString>& FriendlyNames = GetPCGFriendlyNames();

	for (const FClassInfo& Info : Infos)
	{
		auto Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("class"), Info.ClassName);
		Obj->SetStringField(TEXT("module"), Info.Module);

		if (const FString* Friendly = FriendlyNames.Find(Info.ClassName))
		{
			Obj->SetStringField(TEXT("friendly_name"), *Friendly);
		}

		NodeTypesArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	Root->SetArrayField(TEXT("node_types"), NodeTypesArray);
	Root->SetNumberField(TEXT("count"), NodeTypesArray.Num());

	UE_LOG(LogArborDiscovery, Log, TEXT("ListPCGNodeTypes(filter='%s') → %d types"),
		*Filter, NodeTypesArray.Num());

	return SerializeJsonObject(Root);
}

// ============================================================================
// ListBlueprintNodeTypes
// ============================================================================

FString UArborClassDiscovery::ListBlueprintNodeTypes(const FString& Filter)
{
	auto Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> NodeTypesArray;

	TArray<FClassInfo> Infos = EnumerateSubclasses(UK2Node::StaticClass(), Filter);

	for (const FClassInfo& Info : Infos)
	{
		auto Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("class"), Info.ClassName);
		Obj->SetStringField(TEXT("module"), Info.Module);

		bool bIsBuiltin = Info.Module == TEXT("BlueprintGraph") ||
		                  Info.Module == TEXT("Engine") ||
		                  Info.Module == TEXT("KismetCompiler");
		Obj->SetBoolField(TEXT("is_builtin"), bIsBuiltin);

		NodeTypesArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	Root->SetArrayField(TEXT("node_types"), NodeTypesArray);
	Root->SetNumberField(TEXT("count"), NodeTypesArray.Num());

	UE_LOG(LogArborDiscovery, Log, TEXT("ListBlueprintNodeTypes(filter='%s') → %d types"),
		*Filter, NodeTypesArray.Num());

	return SerializeJsonObject(Root);
}

// ============================================================================
// ListComponentTypes
// ============================================================================

FString UArborClassDiscovery::ListComponentTypes(const FString& Filter)
{
	auto Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ComponentsArray;

	TArray<FClassInfo> Infos = EnumerateSubclasses(UActorComponent::StaticClass(), Filter);

	for (const FClassInfo& Info : Infos)
	{
		auto Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("class"), Info.ClassName);
		Obj->SetStringField(TEXT("module"), Info.Module);
		ComponentsArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	Root->SetArrayField(TEXT("component_types"), ComponentsArray);
	Root->SetNumberField(TEXT("count"), ComponentsArray.Num());

	UE_LOG(LogArborDiscovery, Log, TEXT("ListComponentTypes(filter='%s') → %d types"),
		*Filter, ComponentsArray.Num());

	return SerializeJsonObject(Root);
}

// ============================================================================
// ListClassFunctions
// ============================================================================

FString UArborClassDiscovery::ListClassFunctions(const FString& ClassName)
{
	auto Root = MakeShared<FJsonObject>();

	UClass* Class = ResolveClassByName(ClassName);
	if (!Class)
	{
		Root->SetBoolField(TEXT("success"), false);
		Root->SetStringField(TEXT("error"), FString::Printf(TEXT("Class '%s' not found"), *ClassName));
		return SerializeJsonObject(Root);
	}

	TArray<TSharedPtr<FJsonValue>> FunctionsArray;

	for (TFieldIterator<UFunction> It(Class); It; ++It)
	{
		UFunction* Func = *It;
		if (!Func->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
			continue;

		auto FuncObj = MakeShared<FJsonObject>();
		FuncObj->SetStringField(TEXT("name"), Func->GetName());
		FuncObj->SetBoolField(TEXT("is_pure"), Func->HasAnyFunctionFlags(FUNC_BlueprintPure));

		// Params
		TArray<TSharedPtr<FJsonValue>> ParamsArray;
		FProperty* ReturnProp = nullptr;

		for (TFieldIterator<FProperty> PIt(Func); PIt; ++PIt)
		{
			FProperty* Param = *PIt;
			if (Param->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				ReturnProp = Param;
				continue;
			}

			auto ParamObj = MakeShared<FJsonObject>();
			ParamObj->SetStringField(TEXT("name"), Param->GetName());

			// Simple type string
			FString TypeStr = Param->GetCPPType();
			ParamObj->SetStringField(TEXT("type"), TypeStr);

			bool bIsOutput = Param->HasAnyPropertyFlags(CPF_OutParm);
			if (bIsOutput)
			{
				ParamObj->SetBoolField(TEXT("is_output"), true);
			}

			ParamsArray.Add(MakeShared<FJsonValueObject>(ParamObj));
		}

		FuncObj->SetArrayField(TEXT("params"), ParamsArray);

		if (ReturnProp)
		{
			FuncObj->SetStringField(TEXT("return_type"), ReturnProp->GetCPPType());
		}

		FunctionsArray.Add(MakeShared<FJsonValueObject>(FuncObj));
	}

	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("class"), Class->GetName());
	Root->SetArrayField(TEXT("functions"), FunctionsArray);
	Root->SetNumberField(TEXT("count"), FunctionsArray.Num());

	UE_LOG(LogArborDiscovery, Log, TEXT("ListClassFunctions('%s') → %d functions"),
		*ClassName, FunctionsArray.Num());

	return SerializeJsonObject(Root);
}

// ============================================================================
// GetClassProperties
// ============================================================================

FString UArborClassDiscovery::GetClassProperties(const FString& ClassName, const FString& BaseClassName)
{
	auto Root = MakeShared<FJsonObject>();

	// Resolve optional base class constraint
	UClass* BaseClass = nullptr;
	if (!BaseClassName.IsEmpty())
	{
		BaseClass = ResolveClassByName(BaseClassName);
	}

	UClass* Class = ResolveClassByName(ClassName, BaseClass);
	if (!Class)
	{
		Root->SetBoolField(TEXT("success"), false);
		Root->SetStringField(TEXT("error"), FString::Printf(TEXT("Class '%s' not found"), *ClassName));
		return SerializeJsonObject(Root);
	}

	// Try to get CDO for default values
	UObject* CDO = nullptr;
	if (!Class->HasAnyClassFlags(CLASS_Abstract))
	{
		CDO = Class->GetDefaultObject(false);
	}

	TArray<TSharedPtr<FJsonValue>> PropsArray;

	for (TFieldIterator<FProperty> It(Class); It; ++It)
	{
		FProperty* Prop = *It;

		// Only include editable properties (CPF_Edit) or blueprint-visible ones
		if (!Prop->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
			continue;

		TSharedPtr<FJsonObject> PropDesc = SerializePropertyDescriptor(Prop, CDO);
		if (PropDesc.IsValid())
		{
			PropsArray.Add(MakeShared<FJsonValueObject>(PropDesc));
		}
	}

	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("class"), Class->GetName());
	Root->SetStringField(TEXT("module"), GetModuleName(Class));
	Root->SetBoolField(TEXT("is_abstract"), Class->HasAnyClassFlags(CLASS_Abstract));
	Root->SetArrayField(TEXT("properties"), PropsArray);
	Root->SetNumberField(TEXT("count"), PropsArray.Num());

	UE_LOG(LogArborDiscovery, Log, TEXT("GetClassProperties('%s') → %d properties"),
		*ClassName, PropsArray.Num());

	return SerializeJsonObject(Root);
}
