#include "BehaviorTreeBuilder.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Composites/BTComposite_SimpleParallel.h"
#include "BehaviorTree/BlackboardComponent.h"

#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

// For BT graph layout
#include "Kismet2/BlueprintEditorUtils.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "EdGraphSchema_BehaviorTree.h"
#include "DataProviders/AIDataProvider.h"

DEFINE_LOG_CATEGORY_STATIC(LogArbor, Log, All);

// ============================================================================
// Main Entry Point
// ============================================================================

UBehaviorTree* UBehaviorTreeBuilder::BuildBehaviorTreeFromJSON(
	const FString& JsonFilePath, const FString& AssetPath)
{
	TSharedPtr<FJsonObject> JsonRoot;
	if (!LoadAndParseJSON(JsonFilePath, JsonRoot))
	{
		return nullptr;
	}
	return BuildFromParsedJSON(JsonRoot, AssetPath);
}

UBehaviorTree* UBehaviorTreeBuilder::BuildBehaviorTreeFromJSONString(
	const FString& JsonString, const FString& AssetPath)
{
	TSharedPtr<FJsonObject> JsonRoot;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonRoot) || !JsonRoot.IsValid())
	{
		UE_LOG(LogArbor, Error, TEXT("Failed to parse JSON string"));
		return nullptr;
	}
	return BuildFromParsedJSON(JsonRoot, AssetPath);
}

UBehaviorTree* UBehaviorTreeBuilder::BuildFromParsedJSON(
	const TSharedPtr<FJsonObject>& JsonRoot, const FString& AssetPath)
{
	// Key-alias normalization: accept both "tree"/"root" and "blackboard_keys"/"blackboard"
	// so callers can use either the MCP schema or the internal C++ schema.
	if (JsonRoot->HasField(TEXT("tree")) && !JsonRoot->HasField(TEXT("root")))
	{
		JsonRoot->SetField(TEXT("root"), JsonRoot->TryGetField(TEXT("tree")));
		JsonRoot->RemoveField(TEXT("tree"));
	}
	if (JsonRoot->HasField(TEXT("blackboard_keys")) && !JsonRoot->HasField(TEXT("blackboard")))
	{
		FString TreeName = JsonRoot->GetStringField(TEXT("name"));
		FString BaseName = TreeName.StartsWith(TEXT("BT_")) ? TreeName.Mid(3) : TreeName;

		TSharedPtr<FJsonObject> BBJson = MakeShared<FJsonObject>();
		BBJson->SetStringField(TEXT("name"), FString::Printf(TEXT("BB_%s"), *BaseName));
		BBJson->SetField(TEXT("keys"), JsonRoot->TryGetField(TEXT("blackboard_keys")));
		JsonRoot->SetObjectField(TEXT("blackboard"), BBJson);
		JsonRoot->RemoveField(TEXT("blackboard_keys"));
	}

	// 1. Extract tree name
	FString TreeName;
	if (!JsonRoot->TryGetStringField(TEXT("name"), TreeName))
	{
		UE_LOG(LogArbor, Error, TEXT("JSON missing 'name' field"));
		return nullptr;
	}

	// 2. Validate root exists BEFORE creating/clearing assets
	const TSharedPtr<FJsonObject>* RootJson;
	if (!JsonRoot->TryGetObjectField(TEXT("root"), RootJson))
	{
		UE_LOG(LogArbor, Error, TEXT("JSON missing 'root' field — expected 'root' (or 'tree' alias)"));
		return nullptr;
	}

	// 3. Create Blackboard asset
	UBlackboardData* BlackboardAsset = nullptr;
	const TSharedPtr<FJsonObject>* BlackboardJson;
	if (JsonRoot->TryGetObjectField(TEXT("blackboard"), BlackboardJson))
	{
		BlackboardAsset = CreateBlackboardAsset(*BlackboardJson, AssetPath);
		if (!BlackboardAsset)
		{
			UE_LOG(LogArbor, Error, TEXT("Failed to create Blackboard asset"));
			return nullptr;
		}
	}

	// 4. Create or load Behavior Tree asset
	UBehaviorTree* BehaviorTree = CreateBehaviorTreeAsset(TreeName, AssetPath);
	if (!BehaviorTree)
	{
		UE_LOG(LogArbor, Error, TEXT("Failed to create BehaviorTree asset"));
		return nullptr;
	}

	// Save old state so we can restore on failure
	UBTCompositeNode* OldRootNode = BehaviorTree->RootNode;
	UBlackboardData* OldBlackboardAsset = BehaviorTree->BlackboardAsset;

	// If no blackboard JSON was provided, preserve the existing blackboard reference
	if (!BlackboardAsset && OldBlackboardAsset)
	{
		BlackboardAsset = OldBlackboardAsset;
		UE_LOG(LogArbor, Log, TEXT("Preserving existing blackboard reference: %s"),
			*OldBlackboardAsset->GetPathName());
	}

	BehaviorTree->BlackboardAsset = BlackboardAsset;

	UBTCompositeNode* RootNode = ProcessCompositeNode(BehaviorTree, BlackboardAsset, *RootJson, 0);
	if (!RootNode)
	{
		// Restore old state so the existing asset isn't left corrupted
		BehaviorTree->RootNode = OldRootNode;
		BehaviorTree->BlackboardAsset = OldBlackboardAsset;
		UE_LOG(LogArbor, Error, TEXT("Failed to process root node — existing tree preserved"));
		return nullptr;
	}

	BehaviorTree->RootNode = RootNode;

	// 6. Assign execution indices
	AssignExecutionIndices(RootNode, nullptr, 0, 0);

	// 7. Initialize all nodes (sets TreeAsset, which is private in UE 5.5)
	RootNode->InitializeFromAsset(*BehaviorTree);

	// 8. Save assets
	if (BlackboardAsset)
	{
		SaveAsset(BlackboardAsset);
	}
	SaveAsset(BehaviorTree);

	// 9. Auto-layout for editor graph
	LayoutBehaviorTreeInternal(BehaviorTree);

	UE_LOG(LogArbor, Log, TEXT("Successfully built behavior tree '%s' at %s"), *TreeName, *AssetPath);
	return BehaviorTree;
}

// ============================================================================
// JSON Parsing
// ============================================================================

bool UBehaviorTreeBuilder::LoadAndParseJSON(
	const FString& JsonFilePath, TSharedPtr<FJsonObject>& OutJsonObject)
{
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
	{
		UE_LOG(LogArbor, Error, TEXT("Failed to load JSON file: %s"), *JsonFilePath);
		return false;
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, OutJsonObject) || !OutJsonObject.IsValid())
	{
		UE_LOG(LogArbor, Error, TEXT("Failed to parse JSON file: %s"), *JsonFilePath);
		return false;
	}

	return true;
}

// ============================================================================
// Blackboard Creation
// ============================================================================

UBlackboardData* UBehaviorTreeBuilder::CreateBlackboardAsset(
	const TSharedPtr<FJsonObject>& BlackboardJson, const FString& AssetPath)
{
	FString BBName;
	if (!BlackboardJson->TryGetStringField(TEXT("name"), BBName))
	{
		UE_LOG(LogArbor, Error, TEXT("Blackboard JSON missing 'name' field"));
		return nullptr;
	}

	const FString PackagePath = AssetPath / BBName;
	const FString AssetObjectPath = PackagePath + TEXT(".") + BBName;
	UBlackboardData* BBAsset = nullptr;

	// Check if asset already exists — update in place to preserve references
	if (UEditorAssetLibrary::DoesAssetExist(PackagePath))
	{
		BBAsset = LoadObject<UBlackboardData>(nullptr, *AssetObjectPath);
		if (BBAsset)
		{
			BBAsset->Keys.Empty();
			BBAsset->GetPackage()->MarkPackageDirty();
			UE_LOG(LogArbor, Log, TEXT("Updating existing Blackboard asset: %s"), *PackagePath);
		}
	}

	if (!BBAsset)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		if (!Package)
		{
			UE_LOG(LogArbor, Error, TEXT("Failed to create package: %s"), *PackagePath);
			return nullptr;
		}

		BBAsset = NewObject<UBlackboardData>(
			Package, UBlackboardData::StaticClass(), *BBName,
			RF_Public | RF_Standalone | RF_Transactional);
		UE_LOG(LogArbor, Log, TEXT("Creating new Blackboard asset: %s"), *PackagePath);
	}

	const TArray<TSharedPtr<FJsonValue>>* KeysArray;
	if (BlackboardJson->TryGetArrayField(TEXT("keys"), KeysArray))
	{
		for (const TSharedPtr<FJsonValue>& KeyValue : *KeysArray)
		{
			const TSharedPtr<FJsonObject>& KeyJson = KeyValue->AsObject();
			if (!KeyJson.IsValid())
			{
				continue;
			}

			FString KeyName;
			FString KeyType;
			if (!KeyJson->TryGetStringField(TEXT("name"), KeyName) ||
				!KeyJson->TryGetStringField(TEXT("type"), KeyType))
			{
				UE_LOG(LogArbor, Warning, TEXT("Blackboard key missing 'name' or 'type', skipping"));
				continue;
			}

			FBlackboardEntry Entry;
			Entry.EntryName = FName(*KeyName);
			Entry.KeyType = CreateBlackboardKeyType(BBAsset, KeyType, KeyJson);

			if (Entry.KeyType)
			{
				BBAsset->Keys.Add(Entry);
				UE_LOG(LogArbor, Verbose, TEXT("Added blackboard key: %s (%s)"), *KeyName, *KeyType);
			}
			else
			{
				UE_LOG(LogArbor, Warning, TEXT("Failed to create key type '%s' for key '%s'"), *KeyType, *KeyName);
			}
		}
	}

	return BBAsset;
}

UBlackboardKeyType* UBehaviorTreeBuilder::CreateBlackboardKeyType(
	UBlackboardData* BlackboardAsset, const FString& TypeName,
	const TSharedPtr<FJsonObject>& KeyJson)
{
	UClass* KeyTypeClass = nullptr;

	if (TypeName == TEXT("Object"))
	{
		KeyTypeClass = UBlackboardKeyType_Object::StaticClass();
	}
	else if (TypeName == TEXT("Vector"))
	{
		KeyTypeClass = UBlackboardKeyType_Vector::StaticClass();
	}
	else if (TypeName == TEXT("Float"))
	{
		KeyTypeClass = UBlackboardKeyType_Float::StaticClass();
	}
	else if (TypeName == TEXT("Bool"))
	{
		KeyTypeClass = UBlackboardKeyType_Bool::StaticClass();
	}
	else if (TypeName == TEXT("Int"))
	{
		KeyTypeClass = UBlackboardKeyType_Int::StaticClass();
	}
	else if (TypeName == TEXT("String"))
	{
		KeyTypeClass = UBlackboardKeyType_String::StaticClass();
	}
	else if (TypeName == TEXT("Name"))
	{
		KeyTypeClass = UBlackboardKeyType_Name::StaticClass();
	}
	else if (TypeName == TEXT("Enum"))
	{
		KeyTypeClass = UBlackboardKeyType_Enum::StaticClass();
	}
	else if (TypeName == TEXT("Rotator"))
	{
		KeyTypeClass = UBlackboardKeyType_Rotator::StaticClass();
	}
	else if (TypeName == TEXT("Class"))
	{
		KeyTypeClass = UBlackboardKeyType_Class::StaticClass();
	}
	else
	{
		UE_LOG(LogArbor, Warning, TEXT("Unknown blackboard key type: %s"), *TypeName);
		return nullptr;
	}

	UBlackboardKeyType* KeyTypeInstance = NewObject<UBlackboardKeyType>(BlackboardAsset, KeyTypeClass);

	// Special handling for Object type — set BaseClass
	if (TypeName == TEXT("Object"))
	{
		FString BaseClassName;
		if (KeyJson->TryGetStringField(TEXT("base_class"), BaseClassName))
		{
			UClass* BaseClass = nullptr;

			// Try common module paths
			TArray<FString> ModulePaths = {
				TEXT("/Script/Engine"),
				TEXT("/Script/AIModule"),
				TEXT("/Script/CoreUObject"),
				TEXT("/Script/GameplayTasks"),
				TEXT("/Script/NavigationSystem")
			};

			for (const FString& ModulePath : ModulePaths)
			{
				FString FullPath = FString::Printf(TEXT("%s.%s"), *ModulePath, *BaseClassName);
				BaseClass = StaticLoadClass(UObject::StaticClass(), nullptr, *FullPath);
				if (BaseClass)
				{
					break;
				}

				// Try with A prefix for Actor types
				FullPath = FString::Printf(TEXT("%s.A%s"), *ModulePath, *BaseClassName);
				BaseClass = StaticLoadClass(UObject::StaticClass(), nullptr, *FullPath);
				if (BaseClass)
				{
					break;
				}
			}

			if (!BaseClass)
			{
				// Fallback: try FindObject across all packages
				BaseClass = FindObject<UClass>(static_cast<UObject*>(nullptr), *BaseClassName);
			}

			if (BaseClass)
			{
				UBlackboardKeyType_Object* ObjectKeyType = Cast<UBlackboardKeyType_Object>(KeyTypeInstance);
				if (ObjectKeyType)
				{
					ObjectKeyType->BaseClass = BaseClass;
				}
			}
			else
			{
				UE_LOG(LogArbor, Warning, TEXT("Could not resolve base_class '%s' for Object key"), *BaseClassName);
			}
		}
	}

	return KeyTypeInstance;
}

// ============================================================================
// Behavior Tree Asset Creation
// ============================================================================

UBehaviorTree* UBehaviorTreeBuilder::CreateBehaviorTreeAsset(
	const FString& Name, const FString& AssetPath)
{
	const FString PackagePath = AssetPath / Name;
	const FString AssetObjectPath = PackagePath + TEXT(".") + Name;

	// Check if asset already exists — return it for update in place to preserve references
	// NOTE: We no longer clear RootNode/BlackboardAsset here. The caller (BuildFromParsedJSON)
	// handles clearing only after the new tree is successfully processed, so that the old
	// state can be restored on failure.
	if (UEditorAssetLibrary::DoesAssetExist(PackagePath))
	{
		UBehaviorTree* ExistingTree = LoadObject<UBehaviorTree>(nullptr, *AssetObjectPath);
		if (ExistingTree)
		{
			ExistingTree->GetPackage()->MarkPackageDirty();
			UE_LOG(LogArbor, Log, TEXT("Updating existing BehaviorTree asset: %s"), *PackagePath);
			return ExistingTree;
		}
	}

	// Asset doesn't exist — create new
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		UE_LOG(LogArbor, Error, TEXT("Failed to create package: %s"), *PackagePath);
		return nullptr;
	}

	UBehaviorTree* BehaviorTree = NewObject<UBehaviorTree>(
		Package, UBehaviorTree::StaticClass(), *Name,
		RF_Public | RF_Standalone | RF_Transactional);

	UE_LOG(LogArbor, Log, TEXT("Creating new BehaviorTree asset: %s"), *PackagePath);
	return BehaviorTree;
}

// ============================================================================
// Class Resolution
// ============================================================================

UClass* UBehaviorTreeBuilder::ResolveNodeClass(const FString& ClassName, UClass* BaseClass)
{
	// Abstract class → concrete suggestion map
	static TMap<FString, FString> AbstractSuggestions = {
		{TEXT("BTDecorator_BlackboardBase"), TEXT("BTDecorator_Blackboard")},
		{TEXT("BTTask_BlackboardBase"), TEXT("BTTask_SetBlackboardValue")}
	};

	auto ValidateNotAbstract = [&](UClass* FoundClass) -> UClass*
	{
		if (FoundClass && FoundClass->HasAnyClassFlags(CLASS_Abstract))
		{
			const FString FoundName = FoundClass->GetName();
			if (const FString* Suggestion = AbstractSuggestions.Find(FoundName))
			{
				UE_LOG(LogArbor, Error, TEXT("Class '%s' is abstract and cannot be instantiated. Did you mean '%s'?"),
					*FoundName, **Suggestion);
			}
			else
			{
				UE_LOG(LogArbor, Error, TEXT("Class '%s' is abstract and cannot be instantiated (remove the 'Base' suffix or check UE5 docs)"),
					*FoundName);
			}
			return nullptr;
		}
		return FoundClass;
	};

	// Try common module paths with StaticLoadClass
	TArray<FString> ModulePaths = {
		TEXT("/Script/AIModule"),
		TEXT("/Script/GameplayTasks"),
		TEXT("/Script/Engine"),
		TEXT("/Script/NavigationSystem"),
		TEXT("/Script/Arbor")
	};

	for (const FString& ModulePath : ModulePaths)
	{
		FString FullPath = FString::Printf(TEXT("%s.%s"), *ModulePath, *ClassName);
		UClass* FoundClass = StaticLoadClass(BaseClass, nullptr, *FullPath);
		if (FoundClass)
		{
			return ValidateNotAbstract(FoundClass);
		}

		// Try with U prefix
		FullPath = FString::Printf(TEXT("%s.U%s"), *ModulePath, *ClassName);
		FoundClass = StaticLoadClass(BaseClass, nullptr, *FullPath);
		if (FoundClass)
		{
			return ValidateNotAbstract(FoundClass);
		}
	}

	// Fallback: iterate all UBTTaskNode / UBTDecorator / UBTService subclasses
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Candidate = *It;
		if (Candidate->IsChildOf(BaseClass) &&
			(Candidate->GetName() == ClassName || Candidate->GetName() == (TEXT("U") + ClassName)))
		{
			UE_LOG(LogArbor, Log, TEXT("Resolved '%s' via class iterator → %s"), *ClassName, *Candidate->GetPathName());
			return ValidateNotAbstract(Candidate);
		}
	}

	UE_LOG(LogArbor, Warning, TEXT("Could not resolve class '%s'"), *ClassName);
	return nullptr;
}

// ============================================================================
// Parameter Application via Reflection
// ============================================================================

void UBehaviorTreeBuilder::ApplyParamsToStruct(
	UStruct* StructDef, void* StructPtr, const TSharedPtr<FJsonObject>& ParamsJson,
	UBlackboardData* BlackboardAsset)
{
	if (!StructDef || !StructPtr || !ParamsJson.IsValid())
	{
		return;
	}

	for (const auto& Pair : ParamsJson->Values)
	{
		const FString& FieldName = Pair.Key;
		const TSharedPtr<FJsonValue>& FieldValue = Pair.Value;

		// Find the property on the struct
		FProperty* Prop = StructDef->FindPropertyByName(FName(*FieldName));
		if (!Prop)
		{
			// Try lowercase first character
			FString LowerKey = FieldName;
			if (LowerKey.Len() > 0)
			{
				LowerKey[0] = FChar::ToLower(LowerKey[0]);
			}
			Prop = StructDef->FindPropertyByName(FName(*LowerKey));
		}
		if (!Prop)
		{
			UE_LOG(LogArbor, Warning, TEXT("Struct property '%s' not found on %s"),
				*FieldName, *StructDef->GetName());
			continue;
		}

		void* PropertyAddr = Prop->ContainerPtrToValuePtr<void>(StructPtr);

		// Handle nested struct properties
		if (FStructProperty* InnerStructProp = CastField<FStructProperty>(Prop))
		{
			// BlackboardKeySelector inside a struct
			if (InnerStructProp->Struct->GetFName() == FName(TEXT("BlackboardKeySelector")))
			{
				FBlackboardKeySelector* KeySelector = static_cast<FBlackboardKeySelector*>(PropertyAddr);
				KeySelector->SelectedKeyName = FName(*FieldValue->AsString());
				if (BlackboardAsset)
				{
					KeySelector->ResolveSelectedKey(*BlackboardAsset);
				}
				continue;
			}

			// Recursion for nested structs with JSON object values
			const TSharedPtr<FJsonObject>* NestedObj;
			if (FieldValue->TryGetObject(NestedObj))
			{
				ApplyParamsToStruct(InnerStructProp->Struct, PropertyAddr, *NestedObj, BlackboardAsset);
				continue;
			}
		}

		// Object reference (e.g. QueryTemplate is a UEnvQuery*)
		if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
		{
			FString AssetPath = FieldValue->AsString();
			if (!AssetPath.IsEmpty())
			{
				UObject* LoadedObj = StaticLoadObject(ObjProp->PropertyClass, nullptr, *AssetPath);
				if (!LoadedObj && !AssetPath.Contains(TEXT(".")))
				{
					FString AssetName = FPaths::GetBaseFilename(AssetPath);
					LoadedObj = StaticLoadObject(
						ObjProp->PropertyClass, nullptr,
						*(AssetPath + TEXT(".") + AssetName));
				}
				if (LoadedObj)
				{
					ObjProp->SetObjectPropertyValue(PropertyAddr, LoadedObj);
					UE_LOG(LogArbor, Log, TEXT("Set struct field '%s' = '%s'"),
						*FieldName, *LoadedObj->GetPathName());
				}
				else
				{
					UE_LOG(LogArbor, Warning, TEXT("Could not load object '%s' for struct field '%s'"),
						*AssetPath, *FieldName);
				}
			}
			continue;
		}

		// Soft object reference
		if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Prop))
		{
			FString AssetPath = FieldValue->AsString();
			if (!AssetPath.IsEmpty())
			{
				FSoftObjectPtr* SoftPtr = static_cast<FSoftObjectPtr*>(PropertyAddr);
				*SoftPtr = FSoftObjectPath(AssetPath);
			}
			continue;
		}

		// Class reference (TSubclassOf<T>)
		if (FClassProperty* ClassProp = CastField<FClassProperty>(Prop))
		{
			FString ClassPath = FieldValue->AsString();
			if (!ClassPath.IsEmpty())
			{
				UClass* LoadedClass = StaticLoadClass(ClassProp->MetaClass, nullptr, *ClassPath);
				if (LoadedClass)
				{
					ClassProp->SetObjectPropertyValue(PropertyAddr, LoadedClass);
				}
			}
			continue;
		}

		// Primitive types
		if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
		{
			FloatProp->SetPropertyValue(PropertyAddr, static_cast<float>(FieldValue->AsNumber()));
		}
		else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
		{
			DoubleProp->SetPropertyValue(PropertyAddr, FieldValue->AsNumber());
		}
		else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
		{
			IntProp->SetPropertyValue(PropertyAddr, static_cast<int32>(FieldValue->AsNumber()));
		}
		else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			BoolProp->SetPropertyValue(PropertyAddr, FieldValue->AsBool());
		}
		else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
		{
			StrProp->SetPropertyValue(PropertyAddr, FieldValue->AsString());
		}
		else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
		{
			NameProp->SetPropertyValue(PropertyAddr, FName(*FieldValue->AsString()));
		}
		else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			UEnum* EnumDef = EnumProp->GetEnum();
			if (EnumDef)
			{
				int64 EnumValue = EnumDef->GetValueByNameString(FieldValue->AsString());
				if (EnumValue != INDEX_NONE)
				{
					EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(PropertyAddr, EnumValue);
				}
			}
		}
		else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			if (ByteProp->Enum)
			{
				int64 EnumValue = ByteProp->Enum->GetValueByNameString(FieldValue->AsString());
				if (EnumValue != INDEX_NONE)
				{
					ByteProp->SetPropertyValue(PropertyAddr, static_cast<uint8>(EnumValue));
				}
			}
			else
			{
				ByteProp->SetPropertyValue(PropertyAddr, static_cast<uint8>(FieldValue->AsNumber()));
			}
		}
		else
		{
			UE_LOG(LogArbor, Warning, TEXT("Unsupported property type for struct field '%s' on %s"),
				*FieldName, *StructDef->GetName());
		}
	}
}

void UBehaviorTreeBuilder::ApplyParameters(
	UObject* Node, const TSharedPtr<FJsonObject>& ParamsJson,
	UBlackboardData* BlackboardAsset)
{
	if (!ParamsJson.IsValid() || !Node)
	{
		return;
	}

	UClass* NodeClass = Node->GetClass();

	for (const auto& Pair : ParamsJson->Values)
	{
		const FString& Key = Pair.Key;
		const TSharedPtr<FJsonValue>& Value = Pair.Value;

		// Find the property by name
		FProperty* Prop = NodeClass->FindPropertyByName(FName(*Key));

		// If not found, try common variations
		if (!Prop)
		{
			// Try with 'b' prefix for booleans
			Prop = NodeClass->FindPropertyByName(FName(*(TEXT("b") + Key)));
		}
		if (!Prop)
		{
			// Try with lowercase first character
			FString LowerKey = Key;
			if (LowerKey.Len() > 0)
			{
				LowerKey[0] = FChar::ToLower(LowerKey[0]);
			}
			Prop = NodeClass->FindPropertyByName(FName(*LowerKey));
		}

		if (!Prop)
		{
			// Check for known C++ bitfield properties that aren't real UPROPERTYs
			static TMap<FString, FString> BitfieldHints = {
				{TEXT("bUsePathfinding"), TEXT("C++ bitfield, not a UPROPERTY. Use BTTask_MoveDirectlyToward instead of BTTask_MoveTo for non-pathfinding movement.")},
				{TEXT("UsePathfinding"), TEXT("C++ bitfield (bUsePathfinding). Use BTTask_MoveDirectlyToward instead of BTTask_MoveTo for non-pathfinding movement.")}
			};

			const FString* Hint = BitfieldHints.Find(Key);
			if (!Hint)
			{
				Hint = BitfieldHints.Find(TEXT("b") + Key);
			}

			if (Hint)
			{
				UE_LOG(LogArbor, Warning, TEXT("Property '%s' on %s: %s"), *Key, *NodeClass->GetName(), **Hint);
			}
			else
			{
				UE_LOG(LogArbor, Warning, TEXT("Property '%s' not found on %s"), *Key, *NodeClass->GetName());
			}
			continue;
		}

		void* PropertyAddr = Prop->ContainerPtrToValuePtr<void>(Node);

		// Check if this is a BlackboardKeySelector struct
		if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			if (StructProp->Struct->GetFName() == FName(TEXT("BlackboardKeySelector")))
			{
				FString KeyName = Value->AsString();
				SetBlackboardKeySelector(Node, Prop, KeyName, BlackboardAsset);
				continue;
			}

			// Handle Vector struct
			if (StructProp->Struct->GetFName() == FName(TEXT("Vector")))
			{
				const TSharedPtr<FJsonObject>* VecObj;
				if (Value->TryGetObject(VecObj))
				{
					FVector* Vec = static_cast<FVector*>(PropertyAddr);
					Vec->X = (*VecObj)->GetNumberField(TEXT("X"));
					Vec->Y = (*VecObj)->GetNumberField(TEXT("Y"));
					Vec->Z = (*VecObj)->GetNumberField(TEXT("Z"));
				}
				continue;
			}

			// Handle Rotator struct
			if (StructProp->Struct->GetFName() == FName(TEXT("Rotator")))
			{
				const TSharedPtr<FJsonObject>* RotObj;
				if (Value->TryGetObject(RotObj))
				{
					FRotator* Rot = static_cast<FRotator*>(PropertyAddr);
					Rot->Pitch = (*RotObj)->GetNumberField(TEXT("Pitch"));
					Rot->Yaw = (*RotObj)->GetNumberField(TEXT("Yaw"));
					Rot->Roll = (*RotObj)->GetNumberField(TEXT("Roll"));
				}
				continue;
			}

			// Handle FAIDataProvider*Value structs (wrappers around float/int/bool used by BT nodes)
			if (StructProp->Struct == FAIDataProviderFloatValue::StaticStruct())
			{
				FAIDataProviderFloatValue* Provider = static_cast<FAIDataProviderFloatValue*>(PropertyAddr);
				Provider->DefaultValue = static_cast<float>(Value->AsNumber());
				continue;
			}
			if (StructProp->Struct == FAIDataProviderIntValue::StaticStruct())
			{
				FAIDataProviderIntValue* Provider = static_cast<FAIDataProviderIntValue*>(PropertyAddr);
				Provider->DefaultValue = static_cast<int32>(Value->AsNumber());
				continue;
			}
			if (StructProp->Struct == FAIDataProviderBoolValue::StaticStruct())
			{
				FAIDataProviderBoolValue* Provider = static_cast<FAIDataProviderBoolValue*>(PropertyAddr);
				Provider->DefaultValue = Value->AsBool();
				continue;
			}

			// Generic struct handling: recursively apply JSON object fields to struct properties
			const TSharedPtr<FJsonObject>* InnerObj;
			if (Value->TryGetObject(InnerObj))
			{
				ApplyParamsToStruct(StructProp->Struct, PropertyAddr, *InnerObj, BlackboardAsset);
				continue;
			}
		}

		// Handle by property type
		if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
		{
			FloatProp->SetPropertyValue(PropertyAddr, static_cast<float>(Value->AsNumber()));
		}
		else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
		{
			DoubleProp->SetPropertyValue(PropertyAddr, Value->AsNumber());
		}
		else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
		{
			IntProp->SetPropertyValue(PropertyAddr, static_cast<int32>(Value->AsNumber()));
		}
		else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			BoolProp->SetPropertyValue(PropertyAddr, Value->AsBool());
		}
		else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
		{
			StrProp->SetPropertyValue(PropertyAddr, Value->AsString());
		}
		else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
		{
			NameProp->SetPropertyValue(PropertyAddr, FName(*Value->AsString()));
		}
		else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			UEnum* EnumDef = EnumProp->GetEnum();
			if (EnumDef)
			{
				int64 EnumValue = EnumDef->GetValueByNameString(Value->AsString());
				if (EnumValue != INDEX_NONE)
				{
					EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(PropertyAddr, EnumValue);
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
					ByteProp->SetPropertyValue(PropertyAddr, static_cast<uint8>(EnumValue));
				}
			}
			else
			{
				ByteProp->SetPropertyValue(PropertyAddr, static_cast<uint8>(Value->AsNumber()));
			}
		}
		else
		{
			UE_LOG(LogArbor, Warning, TEXT("Unsupported property type for '%s' on %s"),
				*Key, *NodeClass->GetName());
		}
	}
}

void UBehaviorTreeBuilder::SetBlackboardKeySelector(
	UObject* Node, FProperty* Property, const FString& KeyName,
	UBlackboardData* BlackboardAsset)
{
	FStructProperty* StructProp = CastField<FStructProperty>(Property);
	if (!StructProp)
	{
		return;
	}

	void* StructPtr = StructProp->ContainerPtrToValuePtr<void>(Node);
	FBlackboardKeySelector* KeySelector = static_cast<FBlackboardKeySelector*>(StructPtr);

	KeySelector->SelectedKeyName = FName(*KeyName);

	// Resolve the key type from the blackboard
	if (BlackboardAsset)
	{
		for (const FBlackboardEntry& Entry : BlackboardAsset->Keys)
		{
			if (Entry.EntryName == FName(*KeyName) && Entry.KeyType)
			{
				KeySelector->SelectedKeyType = Entry.KeyType->GetClass();
				break;
			}
		}

		KeySelector->ResolveSelectedKey(*BlackboardAsset);
	}
}

// ============================================================================
// Node Processing
// ============================================================================

UBTCompositeNode* UBehaviorTreeBuilder::ProcessCompositeNode(
	UBehaviorTree* BehaviorTree, UBlackboardData* BlackboardAsset,
	const TSharedPtr<FJsonObject>& NodeJson, uint8 TreeDepth)
{
	FString NodeType;
	if (!NodeJson->TryGetStringField(TEXT("type"), NodeType))
	{
		UE_LOG(LogArbor, Error, TEXT("Node missing 'type' field"));
		return nullptr;
	}

	// Determine composite class
	UClass* CompositeClass = nullptr;
	if (NodeType == TEXT("Selector"))
	{
		CompositeClass = UBTComposite_Selector::StaticClass();
	}
	else if (NodeType == TEXT("Sequence"))
	{
		CompositeClass = UBTComposite_Sequence::StaticClass();
	}
	else if (NodeType == TEXT("SimpleParallel"))
	{
		CompositeClass = UBTComposite_SimpleParallel::StaticClass();
	}
	else
	{
		UE_LOG(LogArbor, Error, TEXT("Unknown composite type: %s"), *NodeType);
		return nullptr;
	}

	UBTCompositeNode* CompositeNode = NewObject<UBTCompositeNode>(BehaviorTree, CompositeClass);
	// Apply params if any
	const TSharedPtr<FJsonObject>* ParamsJson;
	if (NodeJson->TryGetObjectField(TEXT("params"), ParamsJson))
	{
		ApplyParameters(CompositeNode, *ParamsJson, BlackboardAsset);
	}

	// Process services on this composite
	const TArray<TSharedPtr<FJsonValue>>* ServicesArray;
	if (NodeJson->TryGetArrayField(TEXT("services"), ServicesArray))
	{
		ProcessServices(BehaviorTree, BlackboardAsset, *ServicesArray, CompositeNode);
	}

	// Process children
	const TArray<TSharedPtr<FJsonValue>>* ChildrenArray;
	if (NodeJson->TryGetArrayField(TEXT("children"), ChildrenArray))
	{
		for (const TSharedPtr<FJsonValue>& ChildValue : *ChildrenArray)
		{
			const TSharedPtr<FJsonObject>& ChildJson = ChildValue->AsObject();
			if (!ChildJson.IsValid())
			{
				continue;
			}

			FBTCompositeChild ChildEntry;

			FString ChildType;
			if (!ChildJson->TryGetStringField(TEXT("type"), ChildType))
			{
				UE_LOG(LogArbor, Warning, TEXT("Child node missing 'type' field, skipping"));
				continue;
			}

			// Determine if this is a composite or task
			if (ChildType == TEXT("Selector") || ChildType == TEXT("Sequence") || ChildType == TEXT("SimpleParallel"))
			{
				UBTCompositeNode* ChildComposite = ProcessCompositeNode(
					BehaviorTree, BlackboardAsset, ChildJson, TreeDepth + 1);
				if (ChildComposite)
				{
					ChildEntry.ChildComposite = ChildComposite;
				}
				else
				{
					continue;
				}
			}
			else if (ChildType == TEXT("Task"))
			{
				UBTTaskNode* ChildTask = ProcessTaskNode(BehaviorTree, BlackboardAsset, ChildJson);
				if (ChildTask)
				{
					ChildEntry.ChildTask = ChildTask;
				}
				else
				{
					continue;
				}
			}
			else
			{
				UE_LOG(LogArbor, Warning, TEXT("Unknown child type: %s"), *ChildType);
				continue;
			}

			// Process decorators on this child
			const TArray<TSharedPtr<FJsonValue>>* DecoratorsArray;
			if (ChildJson->TryGetArrayField(TEXT("decorators"), DecoratorsArray))
			{
				ProcessDecorators(BehaviorTree, BlackboardAsset, *DecoratorsArray, ChildEntry);
			}

			CompositeNode->Children.Add(ChildEntry);
		}
	}

	return CompositeNode;
}

UBTTaskNode* UBehaviorTreeBuilder::ProcessTaskNode(
	UBehaviorTree* BehaviorTree, UBlackboardData* BlackboardAsset,
	const TSharedPtr<FJsonObject>& NodeJson)
{
	FString ClassName;
	if (!NodeJson->TryGetStringField(TEXT("class"), ClassName))
	{
		UE_LOG(LogArbor, Error, TEXT("Task node missing 'class' field"));
		return nullptr;
	}

	UClass* TaskClass = ResolveNodeClass(ClassName, UBTTaskNode::StaticClass());
	if (!TaskClass)
	{
		UE_LOG(LogArbor, Error, TEXT("Could not resolve task class: %s"), *ClassName);
		return nullptr;
	}

	UBTTaskNode* TaskNode = NewObject<UBTTaskNode>(BehaviorTree, TaskClass);

	// Apply parameters
	const TSharedPtr<FJsonObject>* ParamsJson;
	if (NodeJson->TryGetObjectField(TEXT("params"), ParamsJson))
	{
		ApplyParameters(TaskNode, *ParamsJson, BlackboardAsset);
	}

	return TaskNode;
}

// ============================================================================
// Decorators
// ============================================================================

void UBehaviorTreeBuilder::ProcessDecorators(
	UBehaviorTree* BehaviorTree, UBlackboardData* BlackboardAsset,
	const TArray<TSharedPtr<FJsonValue>>& DecoratorsJson,
	FBTCompositeChild& OutChild)
{
	for (const TSharedPtr<FJsonValue>& DecValue : DecoratorsJson)
	{
		const TSharedPtr<FJsonObject>& DecJson = DecValue->AsObject();
		if (!DecJson.IsValid())
		{
			continue;
		}

		FString DecType;
		FString DecClass;

		// Support both "type" shorthand and "class" for full class name
		if (DecJson->TryGetStringField(TEXT("class"), DecClass))
		{
			// Use class name directly
		}
		else if (DecJson->TryGetStringField(TEXT("type"), DecType))
		{
			// Map shorthand type to full class name
			if (DecType == TEXT("BlackboardBased") || DecType == TEXT("Blackboard"))
			{
				DecClass = TEXT("BTDecorator_Blackboard");
			}
			else if (DecType == TEXT("ConeCheck"))
			{
				DecClass = TEXT("BTDecorator_ConeCheck");
			}
			else if (DecType == TEXT("Cooldown"))
			{
				DecClass = TEXT("BTDecorator_Cooldown");
			}
			else if (DecType == TEXT("TimeLimit"))
			{
				DecClass = TEXT("BTDecorator_TimeLimit");
			}
			else
			{
				// Try using the type as a class name directly
				DecClass = DecType;
			}
		}
		else
		{
			UE_LOG(LogArbor, Warning, TEXT("Decorator missing 'type' or 'class' field, skipping"));
			continue;
		}

		UClass* DecoratorClass = ResolveNodeClass(DecClass, UBTDecorator::StaticClass());
		if (!DecoratorClass)
		{
			UE_LOG(LogArbor, Warning, TEXT("Could not resolve decorator class: %s"), *DecClass);
			continue;
		}

		UBTDecorator* Decorator = NewObject<UBTDecorator>(BehaviorTree, DecoratorClass);

		// Handle BlackboardBased decorator special fields
		if (DecType == TEXT("BlackboardBased") || DecType == TEXT("Blackboard"))
		{
			FString BBKey;
			if (DecJson->TryGetStringField(TEXT("blackboard_key"), BBKey))
			{
				// Find the BlackboardKey property (FBlackboardKeySelector)
				FProperty* KeyProp = DecoratorClass->FindPropertyByName(FName(TEXT("BlackboardKey")));
				if (KeyProp)
				{
					SetBlackboardKeySelector(Decorator, KeyProp, BBKey, BlackboardAsset);
				}
			}

			// Handle condition (NotifyObserver / key query)
			FString Condition;
			if (DecJson->TryGetStringField(TEXT("condition"), Condition))
			{
				// Set the key query on the decorator — try to find the relevant property
				// BTDecorator_Blackboard uses "BasicOperation" for IsSet/IsNotSet
				if (Condition == TEXT("IsSet") || Condition == TEXT("IsNotSet"))
				{
					FProperty* NotifyProp = DecoratorClass->FindPropertyByName(
						FName(TEXT("NotifyObserver")));
					if (NotifyProp)
					{
						FByteProperty* ByteProp = CastField<FByteProperty>(NotifyProp);
						if (ByteProp && ByteProp->Enum)
						{
							FString EnumValueName = (Condition == TEXT("IsSet"))
								? TEXT("OnValueChange")
								: TEXT("OnResultChange");
							int64 Val = ByteProp->Enum->GetValueByNameString(EnumValueName);
							if (Val != INDEX_NONE)
							{
								void* PropAddr = ByteProp->ContainerPtrToValuePtr<void>(Decorator);
								ByteProp->SetPropertyValue(PropAddr, static_cast<uint8>(Val));
							}
						}
					}
				}
			}
		}

		// Apply general params
		const TSharedPtr<FJsonObject>* ParamsJson;
		if (DecJson->TryGetObjectField(TEXT("params"), ParamsJson))
		{
			ApplyParameters(Decorator, *ParamsJson, BlackboardAsset);
		}

		OutChild.Decorators.Add(Decorator);
	}

	// Build decorator logic operations after all decorators are added
	if (OutChild.Decorators.Num() == 1)
	{
		// Single decorator: just test it (index 0)
		OutChild.DecoratorOps.Add(FBTDecoratorLogic(EBTDecoratorLogic::Test, 0));
	}
	else if (OutChild.Decorators.Num() > 1)
	{
		// Multiple decorators: AND them all together
		OutChild.DecoratorOps.Add(FBTDecoratorLogic(EBTDecoratorLogic::And, static_cast<uint16>(OutChild.Decorators.Num())));
		for (uint16 DecIdx = 0; DecIdx < static_cast<uint16>(OutChild.Decorators.Num()); DecIdx++)
		{
			OutChild.DecoratorOps.Add(FBTDecoratorLogic(EBTDecoratorLogic::Test, DecIdx));
		}
	}
}

// ============================================================================
// Services
// ============================================================================

void UBehaviorTreeBuilder::ProcessServices(
	UBehaviorTree* BehaviorTree, UBlackboardData* BlackboardAsset,
	const TArray<TSharedPtr<FJsonValue>>& ServicesJson,
	UBTCompositeNode* CompositeNode)
{
	for (const TSharedPtr<FJsonValue>& SvcValue : ServicesJson)
	{
		const TSharedPtr<FJsonObject>& SvcJson = SvcValue->AsObject();
		if (!SvcJson.IsValid())
		{
			continue;
		}

		FString SvcClass;
		if (!SvcJson->TryGetStringField(TEXT("class"), SvcClass))
		{
			// Try "type" as fallback
			FString SvcType;
			if (SvcJson->TryGetStringField(TEXT("type"), SvcType))
			{
				// Map shorthand types
				if (SvcType == TEXT("DefaultFocus"))
				{
					SvcClass = TEXT("BTService_DefaultFocus");
				}
				else if (SvcType == TEXT("RunEQS"))
				{
					SvcClass = TEXT("BTService_RunEQS");
				}
				else
				{
					SvcClass = SvcType;
				}
			}
			else
			{
				UE_LOG(LogArbor, Warning, TEXT("Service missing 'class' or 'type' field, skipping"));
				continue;
			}
		}

		UClass* ServiceClass = ResolveNodeClass(SvcClass, UBTService::StaticClass());
		if (!ServiceClass)
		{
			UE_LOG(LogArbor, Warning, TEXT("Could not resolve service class: %s"), *SvcClass);
			continue;
		}

		UBTService* Service = NewObject<UBTService>(BehaviorTree, ServiceClass);

		// Apply params
		const TSharedPtr<FJsonObject>* ParamsJson;
		if (SvcJson->TryGetObjectField(TEXT("params"), ParamsJson))
		{
			ApplyParameters(Service, *ParamsJson, BlackboardAsset);
		}

		CompositeNode->Services.Add(Service);
	}
}

// ============================================================================
// Execution Index Assignment
// ============================================================================

uint16 UBehaviorTreeBuilder::AssignExecutionIndices(
	UBTCompositeNode* Node, UBTCompositeNode* ParentNode,
	uint16 StartIndex, uint8 TreeDepth)
{
	uint16 CurrentIndex = StartIndex;

	// Assign index to this composite node
	Node->InitializeNode(ParentNode, CurrentIndex, 0, TreeDepth);
	CurrentIndex++;

	// Process each child
	for (int32 ChildIdx = 0; ChildIdx < Node->Children.Num(); ChildIdx++)
	{
		FBTCompositeChild& Child = Node->Children[ChildIdx];

		// Assign indices to decorators first
		for (UBTDecorator* Decorator : Child.Decorators)
		{
			if (Decorator)
			{
				Decorator->InitializeNode(Node, CurrentIndex, 0, TreeDepth + 1);
				CurrentIndex++;
			}
		}

		// Then the child node itself
		if (Child.ChildComposite)
		{
			CurrentIndex = AssignExecutionIndices(Child.ChildComposite, Node, CurrentIndex, TreeDepth + 1);
		}
		else if (Child.ChildTask)
		{
			Child.ChildTask->InitializeNode(Node, CurrentIndex, 0, TreeDepth + 1);
			CurrentIndex++;
		}
	}

	// Assign indices to services
	for (UBTService* Service : Node->Services)
	{
		if (Service)
		{
			Service->InitializeNode(Node, CurrentIndex, 0, TreeDepth);
			CurrentIndex++;
		}
	}

	// Tell the composite about its last execution index for range checks
	Node->InitializeComposite(CurrentIndex - 1);

	return CurrentIndex;
}

// ============================================================================
// Asset Saving
// ============================================================================

bool UBehaviorTreeBuilder::SaveAsset(UObject* Asset)
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
		// Notify asset registry after save (not before) to avoid duplicates
		FAssetRegistryModule::AssetCreated(Asset);
		UE_LOG(LogArbor, Log, TEXT("Saved asset: %s"), *PackageName);
	}
	else
	{
		UE_LOG(LogArbor, Error, TEXT("Failed to save asset: %s"), *PackageName);
	}

	return bSuccess;
}

// ============================================================================
// Granular BT Editing — Helpers
// ============================================================================

UBehaviorTree* UBehaviorTreeBuilder::LoadBehaviorTreeForEditing(const FString& AssetPath)
{
	// Try to load the BT asset
	UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *AssetPath);
	if (!BT)
	{
		// Try with dot suffix convention
		FString AssetName = FPaths::GetBaseFilename(AssetPath);
		FString FullPath = AssetPath + TEXT(".") + AssetName;
		BT = LoadObject<UBehaviorTree>(nullptr, *FullPath);
	}
	if (!BT)
	{
		UE_LOG(LogArbor, Error, TEXT("LoadBehaviorTreeForEditing: Could not load '%s'"), *AssetPath);
	}
	return BT;
}

UBTCompositeNode* UBehaviorTreeBuilder::ResolveNodePath(
	UBehaviorTree* BT, const FString& Path, int32& OutChildIndex)
{
	OutChildIndex = -1;

	if (!BT || !BT->RootNode)
	{
		return nullptr;
	}

	// Empty path → root composite itself
	if (Path.IsEmpty())
	{
		return BT->RootNode;
	}

	// Parse dot-delimited path segments
	TArray<FString> Segments;
	Path.ParseIntoArray(Segments, TEXT("."));

	UBTCompositeNode* Current = BT->RootNode;

	for (int32 i = 0; i < Segments.Num(); i++)
	{
		const FString& Seg = Segments[i];

		// Check for auxiliary suffix like ":decorator:0" or ":service:0"
		if (Seg.Contains(TEXT(":")))
		{
			// The auxiliary suffix is handled by the caller — return current composite
			// and set OutChildIndex from the part before the colon
			FString IndexPart;
			FString Rest;
			Seg.Split(TEXT(":"), &IndexPart, &Rest);

			if (IndexPart.IsEmpty())
			{
				// Colon at the start, e.g. ":decorator:0" on root — already at root
				OutChildIndex = -1;
				return Current;
			}

			int32 ChildIdx = FCString::Atoi(*IndexPart);
			if (ChildIdx < 0 || ChildIdx >= Current->Children.Num())
			{
				UE_LOG(LogArbor, Error, TEXT("ResolveNodePath: Child index %d out of range at '%s'"), ChildIdx, *Path);
				return nullptr;
			}
			OutChildIndex = ChildIdx;
			return Current;
		}

		int32 ChildIdx = FCString::Atoi(*Seg);
		if (ChildIdx < 0 || ChildIdx >= Current->Children.Num())
		{
			UE_LOG(LogArbor, Error, TEXT("ResolveNodePath: Child index %d out of range at segment %d of '%s'"),
				ChildIdx, i, *Path);
			return nullptr;
		}

		bool bIsLast = (i == Segments.Num() - 1);

		if (bIsLast)
		{
			// This is the target child index
			OutChildIndex = ChildIdx;
			return Current;
		}
		else
		{
			// Navigate deeper — child must be a composite
			FBTCompositeChild& Child = Current->Children[ChildIdx];
			if (Child.ChildComposite)
			{
				Current = Child.ChildComposite;
			}
			else
			{
				UE_LOG(LogArbor, Error, TEXT("ResolveNodePath: Node at index %d is a task (not composite), cannot navigate deeper. Path: '%s'"),
					ChildIdx, *Path);
				return nullptr;
			}
		}
	}

	// Shouldn't reach here, but just in case
	return Current;
}

TSharedPtr<FJsonObject> UBehaviorTreeBuilder::SerializeBTNodeToJson(
	UBTCompositeNode* Node, const FString& CurrentPath)
{
	TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();

	if (!Node)
	{
		return NodeObj;
	}

	NodeObj->SetStringField(TEXT("path"), CurrentPath);

	// Determine composite type
	FString TypeName;
	if (Node->IsA(UBTComposite_Selector::StaticClass()))
	{
		TypeName = TEXT("Selector");
	}
	else if (Node->IsA(UBTComposite_Sequence::StaticClass()))
	{
		TypeName = TEXT("Sequence");
	}
	else if (Node->IsA(UBTComposite_SimpleParallel::StaticClass()))
	{
		TypeName = TEXT("SimpleParallel");
	}
	else
	{
		TypeName = Node->GetClass()->GetName();
	}
	NodeObj->SetStringField(TEXT("type"), TypeName);
	NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());

	// Services on this composite
	if (Node->Services.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ServicesArray;
		for (int32 SvcIdx = 0; SvcIdx < Node->Services.Num(); SvcIdx++)
		{
			UBTService* Svc = Node->Services[SvcIdx];
			if (!Svc) continue;

			TSharedPtr<FJsonObject> SvcObj = MakeShared<FJsonObject>();
			FString SvcPath = CurrentPath.IsEmpty()
				? FString::Printf(TEXT(":service:%d"), SvcIdx)
				: FString::Printf(TEXT("%s:service:%d"), *CurrentPath, SvcIdx);
			SvcObj->SetStringField(TEXT("path"), SvcPath);
			SvcObj->SetStringField(TEXT("class"), Svc->GetClass()->GetName());
			SvcObj->SetNumberField(TEXT("index"), SvcIdx);
			ServicesArray.Add(MakeShared<FJsonValueObject>(SvcObj));
		}
		NodeObj->SetArrayField(TEXT("services"), ServicesArray);
	}

	// Children
	TArray<TSharedPtr<FJsonValue>> ChildrenArray;
	for (int32 ChildIdx = 0; ChildIdx < Node->Children.Num(); ChildIdx++)
	{
		const FBTCompositeChild& Child = Node->Children[ChildIdx];
		FString ChildPath = CurrentPath.IsEmpty()
			? FString::Printf(TEXT("%d"), ChildIdx)
			: FString::Printf(TEXT("%s.%d"), *CurrentPath, ChildIdx);

		TSharedPtr<FJsonObject> ChildObj;

		if (Child.ChildComposite)
		{
			// Recurse into composite
			ChildObj = SerializeBTNodeToJson(Child.ChildComposite, ChildPath);
		}
		else if (Child.ChildTask)
		{
			ChildObj = MakeShared<FJsonObject>();
			ChildObj->SetStringField(TEXT("path"), ChildPath);
			ChildObj->SetStringField(TEXT("type"), TEXT("Task"));
			ChildObj->SetStringField(TEXT("class"), Child.ChildTask->GetClass()->GetName());
		}
		else
		{
			continue;
		}

		// Decorators on this child
		if (Child.Decorators.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> DecoratorsArray;
			for (int32 DecIdx = 0; DecIdx < Child.Decorators.Num(); DecIdx++)
			{
				UBTDecorator* Dec = Child.Decorators[DecIdx];
				if (!Dec) continue;

				TSharedPtr<FJsonObject> DecObj = MakeShared<FJsonObject>();
				FString DecPath = FString::Printf(TEXT("%s:decorator:%d"), *ChildPath, DecIdx);
				DecObj->SetStringField(TEXT("path"), DecPath);
				DecObj->SetStringField(TEXT("class"), Dec->GetClass()->GetName());
				DecObj->SetNumberField(TEXT("index"), DecIdx);
				DecoratorsArray.Add(MakeShared<FJsonValueObject>(DecObj));
			}
			ChildObj->SetArrayField(TEXT("decorators"), DecoratorsArray);
		}

		ChildrenArray.Add(MakeShared<FJsonValueObject>(ChildObj));
	}
	NodeObj->SetArrayField(TEXT("children"), ChildrenArray);

	return NodeObj;
}

void UBehaviorTreeBuilder::FinalizeBehaviorTree(UBehaviorTree* BT)
{
	if (!BT || !BT->RootNode)
	{
		return;
	}

	// Reassign execution indices
	AssignExecutionIndices(BT->RootNode, nullptr, 0, 0);

	// Re-init from asset
	BT->RootNode->InitializeFromAsset(*BT);

	// Save
	SaveAsset(BT);
}

// ============================================================================
// Granular BT Editing — Public API
// ============================================================================

FString UBehaviorTreeBuilder::QueryBehaviorTree(const FString& AssetPath)
{
	UBehaviorTree* BT = LoadBehaviorTreeForEditing(AssetPath);
	if (!BT)
	{
		return TEXT("{\"error\": \"Failed to load BehaviorTree\"}");
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), AssetPath);

	// Blackboard info
	if (BT->BlackboardAsset)
	{
		TSharedPtr<FJsonObject> BBObj = MakeShared<FJsonObject>();
		BBObj->SetStringField(TEXT("name"), BT->BlackboardAsset->GetName());

		TArray<TSharedPtr<FJsonValue>> KeysArray;
		for (const FBlackboardEntry& Entry : BT->BlackboardAsset->Keys)
		{
			TSharedPtr<FJsonObject> KeyObj = MakeShared<FJsonObject>();
			KeyObj->SetStringField(TEXT("name"), Entry.EntryName.ToString());

			if (Entry.KeyType)
			{
				// Strip "BlackboardKeyType_" prefix for cleaner type names
				FString TypeName = Entry.KeyType->GetClass()->GetName();
				TypeName.RemoveFromStart(TEXT("BlackboardKeyType_"));
				KeyObj->SetStringField(TEXT("type"), TypeName);
			}

			KeysArray.Add(MakeShared<FJsonValueObject>(KeyObj));
		}
		BBObj->SetArrayField(TEXT("keys"), KeysArray);
		Result->SetObjectField(TEXT("blackboard"), BBObj);
	}

	// Tree structure
	if (BT->RootNode)
	{
		TSharedPtr<FJsonObject> TreeObj = SerializeBTNodeToJson(BT->RootNode, TEXT(""));
		Result->SetObjectField(TEXT("root"), TreeObj);
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	return OutputString;
}

FString UBehaviorTreeBuilder::AddBTNode(
	const FString& AssetPath, const FString& ParentPath,
	int32 ChildIndex, const FString& NodeJsonString)
{
	UBehaviorTree* BT = LoadBehaviorTreeForEditing(AssetPath);
	if (!BT)
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to load BehaviorTree\"}");
	}

	// Parse node JSON
	TSharedPtr<FJsonObject> NodeJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(NodeJsonString);
	if (!FJsonSerializer::Deserialize(Reader, NodeJson) || !NodeJson.IsValid())
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to parse node JSON\"}");
	}

	// Determine role: child (default), decorator, service
	FString Role = TEXT("child");
	NodeJson->TryGetStringField(TEXT("role"), Role);

	// Resolve parent composite
	int32 ResolvedChildIndex = -1;
	UBTCompositeNode* Parent = ResolveNodePath(BT, ParentPath, ResolvedChildIndex);
	if (!Parent)
	{
		return TEXT("{\"success\": false, \"error\": \"Could not resolve parent path\"}");
	}

	// For child and service roles, the parent_path points to the composite node
	// that should contain the new node. When ResolvedChildIndex >= 0, we need to
	// navigate into that child composite (ResolveNodePath returns the parent of
	// the target, not the target itself).
	if (Role != TEXT("decorator") && ResolvedChildIndex >= 0)
	{
		if (ResolvedChildIndex >= Parent->Children.Num())
		{
			return TEXT("{\"success\": false, \"error\": \"Parent path child index out of range\"}");
		}
		FBTCompositeChild& TargetChild = Parent->Children[ResolvedChildIndex];
		if (!TargetChild.ChildComposite)
		{
			return TEXT("{\"success\": false, \"error\": \"Parent path points to a task node, not a composite\"}");
		}
		Parent = TargetChild.ChildComposite;
		ResolvedChildIndex = -1;
	}

	UBlackboardData* BB = BT->BlackboardAsset;
	FString ResultPath;

	if (Role == TEXT("decorator"))
	{
		// Add decorator to a specific child
		int32 TargetChildIdx = ResolvedChildIndex >= 0 ? ResolvedChildIndex : 0;
		if (TargetChildIdx < 0 || TargetChildIdx >= Parent->Children.Num())
		{
			return TEXT("{\"success\": false, \"error\": \"Target child index out of range for decorator\"}");
		}

		FBTCompositeChild& TargetChild = Parent->Children[TargetChildIdx];

		// Resolve decorator class
		FString DecClass;
		if (!NodeJson->TryGetStringField(TEXT("class"), DecClass))
		{
			FString DecType;
			if (NodeJson->TryGetStringField(TEXT("type"), DecType))
			{
				DecClass = DecType;
			}
			else
			{
				return TEXT("{\"success\": false, \"error\": \"Decorator missing 'class' or 'type'\"}");
			}
		}

		UClass* DecoratorClass = ResolveNodeClass(DecClass, UBTDecorator::StaticClass());
		if (!DecoratorClass)
		{
			return FString::Printf(TEXT("{\"success\": false, \"error\": \"Could not resolve decorator class: %s\"}"), *DecClass);
		}

		UBTDecorator* Decorator = NewObject<UBTDecorator>(BT, DecoratorClass);
		const TSharedPtr<FJsonObject>* ParamsJson;
		if (NodeJson->TryGetObjectField(TEXT("params"), ParamsJson))
		{
			ApplyParameters(Decorator, *ParamsJson, BB);
		}

		TargetChild.Decorators.Add(Decorator);

		// Rebuild DecoratorOps
		TargetChild.DecoratorOps.Empty();
		if (TargetChild.Decorators.Num() == 1)
		{
			TargetChild.DecoratorOps.Add(FBTDecoratorLogic(EBTDecoratorLogic::Test, 0));
		}
		else if (TargetChild.Decorators.Num() > 1)
		{
			TargetChild.DecoratorOps.Add(FBTDecoratorLogic(EBTDecoratorLogic::And,
				static_cast<uint16>(TargetChild.Decorators.Num())));
			for (uint16 DecIdx = 0; DecIdx < static_cast<uint16>(TargetChild.Decorators.Num()); DecIdx++)
			{
				TargetChild.DecoratorOps.Add(FBTDecoratorLogic(EBTDecoratorLogic::Test, DecIdx));
			}
		}

		int32 DecIndex = TargetChild.Decorators.Num() - 1;
		FString ChildPath = ParentPath.IsEmpty()
			? FString::Printf(TEXT("%d"), TargetChildIdx)
			: FString::Printf(TEXT("%s.%d"), *ParentPath, TargetChildIdx);
		ResultPath = FString::Printf(TEXT("%s:decorator:%d"), *ChildPath, DecIndex);
	}
	else if (Role == TEXT("service"))
	{
		// Add service to composite
		FString SvcClass;
		if (!NodeJson->TryGetStringField(TEXT("class"), SvcClass))
		{
			return TEXT("{\"success\": false, \"error\": \"Service missing 'class'\"}");
		}

		UClass* ServiceClass = ResolveNodeClass(SvcClass, UBTService::StaticClass());
		if (!ServiceClass)
		{
			return FString::Printf(TEXT("{\"success\": false, \"error\": \"Could not resolve service class: %s\"}"), *SvcClass);
		}

		UBTService* Service = NewObject<UBTService>(BT, ServiceClass);
		const TSharedPtr<FJsonObject>* ParamsJson;
		if (NodeJson->TryGetObjectField(TEXT("params"), ParamsJson))
		{
			ApplyParameters(Service, *ParamsJson, BB);
		}

		Parent->Services.Add(Service);

		int32 SvcIndex = Parent->Services.Num() - 1;
		ResultPath = ParentPath.IsEmpty()
			? FString::Printf(TEXT(":service:%d"), SvcIndex)
			: FString::Printf(TEXT("%s:service:%d"), *ParentPath, SvcIndex);
	}
	else
	{
		// Add child node (composite or task)
		FString NodeType;
		if (!NodeJson->TryGetStringField(TEXT("type"), NodeType))
		{
			return TEXT("{\"success\": false, \"error\": \"Node missing 'type' field\"}");
		}

		FBTCompositeChild NewChild;

		if (NodeType == TEXT("Selector") || NodeType == TEXT("Sequence") || NodeType == TEXT("SimpleParallel"))
		{
			UBTCompositeNode* ChildComposite = ProcessCompositeNode(BT, BB, NodeJson, 0);
			if (!ChildComposite)
			{
				return TEXT("{\"success\": false, \"error\": \"Failed to create composite node\"}");
			}
			NewChild.ChildComposite = ChildComposite;
		}
		else if (NodeType == TEXT("Task"))
		{
			UBTTaskNode* ChildTask = ProcessTaskNode(BT, BB, NodeJson);
			if (!ChildTask)
			{
				return TEXT("{\"success\": false, \"error\": \"Failed to create task node\"}");
			}
			NewChild.ChildTask = ChildTask;
		}
		else
		{
			return FString::Printf(TEXT("{\"success\": false, \"error\": \"Unknown node type: %s\"}"), *NodeType);
		}

		// Process decorators on this new node if present
		const TArray<TSharedPtr<FJsonValue>>* DecoratorsArray;
		if (NodeJson->TryGetArrayField(TEXT("decorators"), DecoratorsArray))
		{
			ProcessDecorators(BT, BB, *DecoratorsArray, NewChild);
		}

		// Insert at ChildIndex (or append if -1 / out of range)
		int32 InsertIdx;
		if (ChildIndex < 0 || ChildIndex >= Parent->Children.Num())
		{
			InsertIdx = Parent->Children.Num();
			Parent->Children.Add(NewChild);
		}
		else
		{
			InsertIdx = ChildIndex;
			Parent->Children.Insert(NewChild, ChildIndex);
		}

		ResultPath = ParentPath.IsEmpty()
			? FString::Printf(TEXT("%d"), InsertIdx)
			: FString::Printf(TEXT("%s.%d"), *ParentPath, InsertIdx);
	}

	FinalizeBehaviorTree(BT);

	return FString::Printf(TEXT("{\"success\": true, \"node_path\": \"%s\"}"), *ResultPath);
}

FString UBehaviorTreeBuilder::RemoveBTNode(const FString& AssetPath, const FString& NodePath)
{
	UBehaviorTree* BT = LoadBehaviorTreeForEditing(AssetPath);
	if (!BT)
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to load BehaviorTree\"}");
	}

	if (NodePath.IsEmpty())
	{
		return TEXT("{\"success\": false, \"error\": \"Cannot remove root node\"}");
	}

	// Check for auxiliary suffix (:decorator:N or :service:N)
	int32 ColonIdx;
	if (NodePath.FindChar(TEXT(':'), ColonIdx))
	{
		// Parse the path before and after the colon
		FString BasePath = NodePath.Left(ColonIdx);
		FString AuxSuffix = NodePath.Mid(ColonIdx + 1);

		// Resolve the base path to get the composite
		int32 BaseChildIdx = -1;
		UBTCompositeNode* BaseParent;

		if (BasePath.IsEmpty())
		{
			// Auxiliary on root itself (e.g. ":service:0")
			BaseParent = BT->RootNode;
		}
		else
		{
			BaseParent = ResolveNodePath(BT, BasePath, BaseChildIdx);
			if (!BaseParent)
			{
				return TEXT("{\"success\": false, \"error\": \"Could not resolve base path\"}");
			}
		}

		// Parse "decorator:N" or "service:N"
		TArray<FString> AuxParts;
		AuxSuffix.ParseIntoArray(AuxParts, TEXT(":"));

		if (AuxParts.Num() < 2)
		{
			return TEXT("{\"success\": false, \"error\": \"Invalid auxiliary suffix\"}");
		}

		FString AuxType = AuxParts[0];
		int32 AuxIndex = FCString::Atoi(*AuxParts[1]);

		if (AuxType == TEXT("decorator"))
		{
			// Remove decorator from child
			if (BaseChildIdx < 0 && BasePath.IsEmpty())
			{
				return TEXT("{\"success\": false, \"error\": \"Decorators belong to children, not root directly\"}");
			}

			// Navigate to the actual composite that holds the child
			// BaseParent is the parent, BaseChildIdx is the child index in parent->Children
			int32 TargetChildIdx = BaseChildIdx;
			UBTCompositeNode* ParentComposite = BaseParent;

			// If BasePath pointed to a composite (not a child index), we need the parent above it
			if (BaseChildIdx < 0)
			{
				// The path didn't resolve to a child — means the path ended at a composite
				// For decorators, we need to navigate back up
				return TEXT("{\"success\": false, \"error\": \"Cannot resolve decorator target from path\"}");
			}

			if (TargetChildIdx < 0 || TargetChildIdx >= ParentComposite->Children.Num())
			{
				return TEXT("{\"success\": false, \"error\": \"Child index out of range\"}");
			}

			FBTCompositeChild& TargetChild = ParentComposite->Children[TargetChildIdx];
			if (AuxIndex < 0 || AuxIndex >= TargetChild.Decorators.Num())
			{
				return TEXT("{\"success\": false, \"error\": \"Decorator index out of range\"}");
			}

			TargetChild.Decorators.RemoveAt(AuxIndex);

			// Rebuild DecoratorOps
			TargetChild.DecoratorOps.Empty();
			if (TargetChild.Decorators.Num() == 1)
			{
				TargetChild.DecoratorOps.Add(FBTDecoratorLogic(EBTDecoratorLogic::Test, 0));
			}
			else if (TargetChild.Decorators.Num() > 1)
			{
				TargetChild.DecoratorOps.Add(FBTDecoratorLogic(EBTDecoratorLogic::And,
					static_cast<uint16>(TargetChild.Decorators.Num())));
				for (uint16 DecIdx = 0; DecIdx < static_cast<uint16>(TargetChild.Decorators.Num()); DecIdx++)
				{
					TargetChild.DecoratorOps.Add(FBTDecoratorLogic(EBTDecoratorLogic::Test, DecIdx));
				}
			}
		}
		else if (AuxType == TEXT("service"))
		{
			// Remove service from composite
			UBTCompositeNode* TargetComposite;
			if (BasePath.IsEmpty())
			{
				TargetComposite = BT->RootNode;
			}
			else if (BaseChildIdx >= 0 && BaseChildIdx < BaseParent->Children.Num()
				&& BaseParent->Children[BaseChildIdx].ChildComposite)
			{
				TargetComposite = BaseParent->Children[BaseChildIdx].ChildComposite;
			}
			else
			{
				return TEXT("{\"success\": false, \"error\": \"Services can only be on composites\"}");
			}

			if (AuxIndex < 0 || AuxIndex >= TargetComposite->Services.Num())
			{
				return TEXT("{\"success\": false, \"error\": \"Service index out of range\"}");
			}

			TargetComposite->Services.RemoveAt(AuxIndex);
		}
		else
		{
			return FString::Printf(TEXT("{\"success\": false, \"error\": \"Unknown auxiliary type: %s\"}"), *AuxType);
		}

		FinalizeBehaviorTree(BT);
		return TEXT("{\"success\": true}");
	}

	// Regular child removal
	int32 ChildIdx = -1;
	UBTCompositeNode* Parent = ResolveNodePath(BT, NodePath, ChildIdx);
	if (!Parent || ChildIdx < 0)
	{
		return TEXT("{\"success\": false, \"error\": \"Could not resolve node path\"}");
	}

	if (ChildIdx >= Parent->Children.Num())
	{
		return TEXT("{\"success\": false, \"error\": \"Child index out of range\"}");
	}

	Parent->Children.RemoveAt(ChildIdx);
	FinalizeBehaviorTree(BT);

	return TEXT("{\"success\": true}");
}

FString UBehaviorTreeBuilder::SetBTNodeParams(
	const FString& AssetPath, const FString& NodePath,
	const FString& ParamsJsonString)
{
	UBehaviorTree* BT = LoadBehaviorTreeForEditing(AssetPath);
	if (!BT)
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to load BehaviorTree\"}");
	}

	// Parse params JSON
	TSharedPtr<FJsonObject> ParamsJson;
	TSharedRef<TJsonReader<>> ParamsReader = TJsonReaderFactory<>::Create(ParamsJsonString);
	if (!FJsonSerializer::Deserialize(ParamsReader, ParamsJson) || !ParamsJson.IsValid())
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to parse params JSON\"}");
	}

	UBlackboardData* BB = BT->BlackboardAsset;

	// Find the target node object
	UObject* TargetNode = nullptr;

	// Check for auxiliary suffix
	int32 ColonIdx;
	if (NodePath.FindChar(TEXT(':'), ColonIdx))
	{
		FString BasePath = NodePath.Left(ColonIdx);
		FString AuxSuffix = NodePath.Mid(ColonIdx + 1);

		int32 BaseChildIdx = -1;
		UBTCompositeNode* BaseParent;

		if (BasePath.IsEmpty())
		{
			BaseParent = BT->RootNode;
		}
		else
		{
			BaseParent = ResolveNodePath(BT, BasePath, BaseChildIdx);
			if (!BaseParent)
			{
				return TEXT("{\"success\": false, \"error\": \"Could not resolve base path\"}");
			}
		}

		TArray<FString> AuxParts;
		AuxSuffix.ParseIntoArray(AuxParts, TEXT(":"));
		if (AuxParts.Num() < 2)
		{
			return TEXT("{\"success\": false, \"error\": \"Invalid auxiliary suffix\"}");
		}

		FString AuxType = AuxParts[0];
		int32 AuxIndex = FCString::Atoi(*AuxParts[1]);

		if (AuxType == TEXT("decorator"))
		{
			if (BaseChildIdx < 0 || BaseChildIdx >= BaseParent->Children.Num())
			{
				return TEXT("{\"success\": false, \"error\": \"Child index out of range for decorator\"}");
			}
			FBTCompositeChild& Child = BaseParent->Children[BaseChildIdx];
			if (AuxIndex < 0 || AuxIndex >= Child.Decorators.Num())
			{
				return TEXT("{\"success\": false, \"error\": \"Decorator index out of range\"}");
			}
			TargetNode = Child.Decorators[AuxIndex];
		}
		else if (AuxType == TEXT("service"))
		{
			UBTCompositeNode* TargetComposite;
			if (BasePath.IsEmpty())
			{
				TargetComposite = BT->RootNode;
			}
			else if (BaseChildIdx >= 0 && BaseChildIdx < BaseParent->Children.Num()
				&& BaseParent->Children[BaseChildIdx].ChildComposite)
			{
				TargetComposite = BaseParent->Children[BaseChildIdx].ChildComposite;
			}
			else
			{
				return TEXT("{\"success\": false, \"error\": \"Services can only be on composites\"}");
			}
			if (AuxIndex < 0 || AuxIndex >= TargetComposite->Services.Num())
			{
				return TEXT("{\"success\": false, \"error\": \"Service index out of range\"}");
			}
			TargetNode = TargetComposite->Services[AuxIndex];
		}
		else
		{
			return FString::Printf(TEXT("{\"success\": false, \"error\": \"Unknown auxiliary type: %s\"}"), *AuxType);
		}
	}
	else if (NodePath.IsEmpty())
	{
		// Root composite
		TargetNode = BT->RootNode;
	}
	else
	{
		// Regular child path
		int32 ChildIdx = -1;
		UBTCompositeNode* Parent = ResolveNodePath(BT, NodePath, ChildIdx);
		if (!Parent || ChildIdx < 0 || ChildIdx >= Parent->Children.Num())
		{
			return TEXT("{\"success\": false, \"error\": \"Could not resolve node path\"}");
		}

		FBTCompositeChild& Child = Parent->Children[ChildIdx];
		if (Child.ChildComposite)
		{
			TargetNode = Child.ChildComposite;
		}
		else if (Child.ChildTask)
		{
			TargetNode = Child.ChildTask;
		}
	}

	if (!TargetNode)
	{
		return TEXT("{\"success\": false, \"error\": \"Could not find target node\"}");
	}

	ApplyParameters(TargetNode, ParamsJson, BB);
	SaveAsset(BT);

	return TEXT("{\"success\": true}");
}

// ============================================================================
// BT Editor Graph Layout
// ============================================================================

namespace
{
	struct FBTLayoutResult
	{
		float MinX;
		float MaxX;
	};

	constexpr float BT_H_STEP = 300.0f;
	constexpr float BT_V_STEP = 200.0f;

	/**
	 * Recursively lay out graph nodes for a BT subtree.
	 * Returns the horizontal extent (min/max X) of this subtree.
	 * CursorX is advanced as leaves are placed left-to-right.
	 */
	FBTLayoutResult LayoutBTGraphNode(UBehaviorTreeGraphNode* GraphNode, float Depth, float& CursorX)
	{
		if (!GraphNode)
		{
			return { CursorX, CursorX };
		}

		// Find child graph nodes via output pins
		TArray<UBehaviorTreeGraphNode*> ChildGraphNodes;
		for (UEdGraphPin* Pin : GraphNode->Pins)
		{
			if (Pin->Direction == EGPD_Output && Pin->PinName == TEXT(""))
			{
				// This is the child connection pin (unnamed output on BT graph nodes)
				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (UBehaviorTreeGraphNode* ChildNode = Cast<UBehaviorTreeGraphNode>(LinkedPin->GetOwningNode()))
					{
						ChildGraphNodes.Add(ChildNode);
					}
				}
			}
		}

		if (ChildGraphNodes.Num() == 0)
		{
			// Leaf node — place at cursor
			float NodeY = Depth * BT_V_STEP;
			GraphNode->NodePosX = CursorX;
			GraphNode->NodePosY = NodeY;
			float MyX = CursorX;
			CursorX += BT_H_STEP;
			return { MyX, MyX };
		}

		// Composite/parent — lay out children first, then center
		float ChildMinX = TNumericLimits<float>::Max();
		float ChildMaxX = TNumericLimits<float>::Lowest();

		for (UBehaviorTreeGraphNode* Child : ChildGraphNodes)
		{
			FBTLayoutResult ChildResult = LayoutBTGraphNode(Child, Depth + 1.0f, CursorX);
			ChildMinX = FMath::Min(ChildMinX, ChildResult.MinX);
			ChildMaxX = FMath::Max(ChildMaxX, ChildResult.MaxX);
		}

		float CenterX = (ChildMinX + ChildMaxX) / 2.0f;
		float NodeY = Depth * BT_V_STEP;
		GraphNode->NodePosX = CenterX;
		GraphNode->NodePosY = NodeY;

		return { ChildMinX, ChildMaxX };
	}
}

bool UBehaviorTreeBuilder::LayoutBehaviorTreeInternal(UBehaviorTree* BT)
{
	if (!BT || !BT->RootNode)
	{
		return false;
	}

#if WITH_EDITORONLY_DATA
	// Clear the editor graph entirely. When the user opens the BT in the
	// editor, it will call OnCreated() → SpawnMissingNodes() to reconstruct
	// graph nodes from the runtime tree. We MUST NOT call UpdateAsset()
	// here — it compiles graph→runtime, wiping the nodes we just built.
	BT->BTGraph = nullptr;
	SaveAsset(BT);
	return true;
#else
	return false;
#endif
}

FString UBehaviorTreeBuilder::LayoutBehaviorTree(const FString& AssetPath)
{
	UBehaviorTree* BT = LoadBehaviorTreeForEditing(AssetPath);
	if (!BT)
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to load BehaviorTree\"}");
	}

	bool bSuccess = LayoutBehaviorTreeInternal(BT);
	if (bSuccess)
	{
		return TEXT("{\"success\": true}");
	}
	return TEXT("{\"success\": false, \"error\": \"Layout failed (editor-only feature)\"}");
}
