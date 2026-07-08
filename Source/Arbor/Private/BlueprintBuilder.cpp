#include "BlueprintBuilder.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Kismet2/KismetEditorUtilities.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Components/CapsuleComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "Components/LightComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Damage.h"

#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/Skeleton.h"

#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// Event graph node includes
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Timeline.h"
#include "K2Node_Event.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_FormatText.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Curves/CurveFloat.h"
#include "Engine/TimelineTemplate.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogArborBP, Log, All);

// ============================================================================
// Event Graph Layout
// ============================================================================

namespace
{

struct FBPLayoutCursor
{
	float X = 0.f;
	float Y = 0.f;
	float MaxY = 0.f;
	float RowStartY = 0.f;

	static constexpr float H_STEP = 400.f;
	static constexpr float V_STEP = 250.f;
	static constexpr float ROW_STEP = 500.f;
	static constexpr float PURE_NODE_Y_OFFSET = -150.f;
	static constexpr float PURE_NODE_X_OFFSET = -200.f;

	void AdvanceExec() { X += H_STEP; }

	void NewEventRow()
	{
		X = 0.f;
		RowStartY = MaxY + ROW_STEP;
		Y = RowStartY;
	}

	void ApplyToNode(UEdGraphNode* Node)
	{
		Node->NodePosX = FMath::RoundToInt(X);
		Node->NodePosY = FMath::RoundToInt(Y);
	}
};

static void LayoutExecChain(
	const FString& NodeId,
	FBPLayoutCursor& Cursor,
	const TMap<FString, UEdGraphNode*>& NodeMap,
	const TMap<FString, TArray<TPair<FString, FString>>>& ExecAdjacency,
	TSet<FString>& Visited)
{
	if (Visited.Contains(NodeId)) return;
	Visited.Add(NodeId);

	UEdGraphNode* const* NodePtr = NodeMap.Find(NodeId);
	if (!NodePtr || !*NodePtr) return;

	Cursor.ApplyToNode(*NodePtr);
	Cursor.MaxY = FMath::Max(Cursor.MaxY, Cursor.Y);

	const TArray<TPair<FString, FString>>* Successors = ExecAdjacency.Find(NodeId);
	if (!Successors || Successors->Num() == 0) return;

	if (Successors->Num() == 1)
	{
		Cursor.AdvanceExec();
		LayoutExecChain((*Successors)[0].Key, Cursor, NodeMap, ExecAdjacency, Visited);
	}
	else
	{
		float BranchX = Cursor.X + FBPLayoutCursor::H_STEP;
		float BranchY = Cursor.Y;

		for (int32 i = 0; i < Successors->Num(); ++i)
		{
			if (i > 0)
			{
				BranchY = Cursor.MaxY + FBPLayoutCursor::V_STEP;
			}

			Cursor.X = BranchX;
			Cursor.Y = BranchY;

			LayoutExecChain((*Successors)[i].Key, Cursor, NodeMap, ExecAdjacency, Visited);
		}
	}
}

static void LayoutEventGraph(
	const TMap<FString, UEdGraphNode*>& NodeMap,
	const TMap<FString, FString>& NodeTypes,
	const TArray<TSharedPtr<FJsonValue>>& ConnectionsArray,
	UEdGraph* Graph)
{
	// Phase 1: Build exec adjacency and data consumer maps from connections
	TMap<FString, TArray<TPair<FString, FString>>> ExecAdjacency;
	TMap<FString, TArray<FString>> DataConsumers;

	for (const TSharedPtr<FJsonValue>& ConnValue : ConnectionsArray)
	{
		const TSharedPtr<FJsonObject>& ConnJson = ConnValue->AsObject();
		if (!ConnJson.IsValid()) continue;

		FString FromId, FromPinName, ToId, ToPinName;
		if (!ConnJson->TryGetStringField(TEXT("from"), FromId) ||
			!ConnJson->TryGetStringField(TEXT("from_pin"), FromPinName) ||
			!ConnJson->TryGetStringField(TEXT("to"), ToId) ||
			!ConnJson->TryGetStringField(TEXT("to_pin"), ToPinName))
		{
			continue;
		}

		UEdGraphNode* const* FromNodePtr = NodeMap.Find(FromId);
		if (!FromNodePtr || !*FromNodePtr) continue;

		// Check if the from_pin is an exec pin
		UEdGraphPin* FromPin = (*FromNodePtr)->FindPin(FName(*FromPinName), EGPD_Output);
		if (!FromPin)
		{
			FromPin = (*FromNodePtr)->FindPin(FName(*FromPinName));
		}

		if (FromPin && FromPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			ExecAdjacency.FindOrAdd(FromId).Add(TPair<FString, FString>(ToId, FromPinName));
		}
		else
		{
			DataConsumers.FindOrAdd(FromId).Add(ToId);
		}
	}

	// Phase 2: Identify event/root nodes
	TArray<FString> RootNodeIds;
	for (const auto& Pair : NodeTypes)
	{
		if (Pair.Value == TEXT("Event") || Pair.Value == TEXT("ComponentEvent"))
		{
			RootNodeIds.Add(Pair.Key);
		}
	}

	// Phase 3: Walk exec chains from each root
	FBPLayoutCursor Cursor;
	TSet<FString> Visited;

	for (int32 i = 0; i < RootNodeIds.Num(); ++i)
	{
		if (i > 0)
		{
			Cursor.NewEventRow();
		}

		LayoutExecChain(RootNodeIds[i], Cursor, NodeMap, ExecAdjacency, Visited);
	}

	// Phase 4: Position pure/data-only nodes above their consumer
	for (const auto& Pair : NodeMap)
	{
		if (Visited.Contains(Pair.Key)) continue;

		UEdGraphNode* PureNode = Pair.Value;
		if (!PureNode) continue;

		const TArray<FString>* Consumers = DataConsumers.Find(Pair.Key);
		if (Consumers)
		{
			for (const FString& ConsumerId : *Consumers)
			{
				UEdGraphNode* const* ConsumerPtr = NodeMap.Find(ConsumerId);
				if (ConsumerPtr && *ConsumerPtr && Visited.Contains(ConsumerId))
				{
					PureNode->NodePosX = (*ConsumerPtr)->NodePosX
						+ FMath::RoundToInt(FBPLayoutCursor::PURE_NODE_X_OFFSET);
					PureNode->NodePosY = (*ConsumerPtr)->NodePosY
						+ FMath::RoundToInt(FBPLayoutCursor::PURE_NODE_Y_OFFSET);
					break;
				}
			}
		}
	}

	// Phase 5: Reposition component ref nodes (not in NodeMap)
	TSet<UEdGraphNode*> MappedNodes;
	for (const auto& Pair : NodeMap)
	{
		MappedNodes.Add(Pair.Value);
	}

	for (UEdGraphNode* GraphNode : Graph->Nodes)
	{
		if (!GraphNode || MappedNodes.Contains(GraphNode)) continue;

		UK2Node_VariableGet* VarGet = Cast<UK2Node_VariableGet>(GraphNode);
		if (!VarGet) continue;

		// Find the node this component ref is connected to
		for (UEdGraphPin* Pin : VarGet->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && Pin->LinkedTo.Num() > 0)
			{
				UEdGraphNode* Consumer = Pin->LinkedTo[0]->GetOwningNode();
				if (Consumer)
				{
					VarGet->NodePosX = Consumer->NodePosX
						+ FMath::RoundToInt(FBPLayoutCursor::PURE_NODE_X_OFFSET);
					VarGet->NodePosY = Consumer->NodePosY + 80;
				}
				break;
			}
		}
	}
}

} // anonymous namespace

// ============================================================================
// Main Entry Point
// ============================================================================

UBlueprint* UBlueprintBuilder::BuildBlueprintFromJSON(
	const FString& JsonFilePath, const FString& AssetPath)
{
	TSharedPtr<FJsonObject> JsonRoot;
	if (!LoadAndParseJSON(JsonFilePath, JsonRoot))
	{
		return nullptr;
	}
	return BuildFromParsedJSON(JsonRoot, AssetPath);
}

UBlueprint* UBlueprintBuilder::BuildBlueprintFromJSONString(
	const FString& JsonString, const FString& AssetPath)
{
	TSharedPtr<FJsonObject> JsonRoot;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonRoot) || !JsonRoot.IsValid())
	{
		UE_LOG(LogArborBP, Error, TEXT("Failed to parse JSON string"));
		return nullptr;
	}
	return BuildFromParsedJSON(JsonRoot, AssetPath);
}

UBlueprint* UBlueprintBuilder::BuildFromParsedJSON(
	const TSharedPtr<FJsonObject>& JsonRoot, const FString& AssetPath)
{
	// 1. Extract name
	FString BPName;
	if (!JsonRoot->TryGetStringField(TEXT("name"), BPName))
	{
		UE_LOG(LogArborBP, Error, TEXT("JSON missing 'name' field"));
		return nullptr;
	}

	// 2. Resolve parent class
	FString ParentClassName;
	if (!JsonRoot->TryGetStringField(TEXT("parent_class"), ParentClassName))
	{
		ParentClassName = TEXT("Actor");
	}

	UClass* ParentClass = ResolveParentClass(ParentClassName);
	if (!ParentClass)
	{
		UE_LOG(LogArborBP, Error, TEXT("Could not resolve parent class: %s"), *ParentClassName);
		return nullptr;
	}

	// 3. Create Blueprint asset
	UBlueprint* Blueprint = CreateBlueprintAsset(BPName, AssetPath, ParentClass);
	if (!Blueprint)
	{
		UE_LOG(LogArborBP, Error, TEXT("Failed to create Blueprint asset"));
		return nullptr;
	}

	// 3b. AnimBlueprint: set target skeleton if provided
	if (UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint))
	{
		FString SkeletonPath;
		if (JsonRoot->TryGetStringField(TEXT("skeleton"), SkeletonPath))
		{
			USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
			if (!Skeleton)
			{
				// Try with .AssetName suffix
				FString BaseName = FPaths::GetBaseFilename(SkeletonPath);
				Skeleton = LoadObject<USkeleton>(nullptr, *(SkeletonPath + TEXT(".") + BaseName));
			}
			if (Skeleton)
			{
				AnimBP->TargetSkeleton = Skeleton;
				UE_LOG(LogArborBP, Log, TEXT("Set AnimBlueprint skeleton: %s"), *Skeleton->GetPathName());
			}
			else
			{
				UE_LOG(LogArborBP, Warning, TEXT("Skeleton not found: %s"), *SkeletonPath);
			}
		}
	}

	// 4. Add components (collects inherited component overrides for post-compile)
	// Only clear existing SCS components if the JSON specifies a "components" key.
	TArray<TPair<FString, TSharedPtr<FJsonObject>>> InheritedOverrides;
	const TArray<TSharedPtr<FJsonValue>>* ComponentsArray;
	if (JsonRoot->TryGetArrayField(TEXT("components"), ComponentsArray))
	{
		// Clear existing SCS nodes before rebuilding
		USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
		if (SCS)
		{
			TArray<USCS_Node*> AllNodes = SCS->GetAllNodes();
			for (USCS_Node* Node : AllNodes)
			{
				SCS->RemoveNode(Node);
			}
		}
		AddComponents(Blueprint, *ComponentsArray, InheritedOverrides);
	}

	// 5. Add variables
	// Only clear existing variables if the JSON specifies a "variables" key.
	const TArray<TSharedPtr<FJsonValue>>* VariablesArray;
	if (JsonRoot->TryGetArrayField(TEXT("variables"), VariablesArray))
	{
		Blueprint->NewVariables.Empty();
		AddVariables(Blueprint, *VariablesArray);
	}

	// 5b. Add timelines (must precede event graph — timeline nodes reference templates)
	// Only clear existing timelines if the JSON specifies a "timelines" key.
	const TArray<TSharedPtr<FJsonValue>>* TimelinesArray;
	if (JsonRoot->TryGetArrayField(TEXT("timelines"), TimelinesArray))
	{
		Blueprint->Timelines.Empty();
		AddTimelines(Blueprint, *TimelinesArray);
	}

	// 5c. Add event graph nodes and connections
	// Only clear existing event graph if the JSON specifies an "event_graph" key.
	const TSharedPtr<FJsonObject>* EventGraphObj;
	if (JsonRoot->TryGetObjectField(TEXT("event_graph"), EventGraphObj))
	{
		ClearEventGraph(Blueprint);
		AddEventGraph(Blueprint, *EventGraphObj);
	}

	// 6. Compile once to generate the class, then a second time to finalise
	// structural changes (components, variables).  CDO property values and
	// component sub-object overrides are applied AFTER the last compile so
	// they cannot be wiped by CDO reconstruction during compilation.
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	// 7. Apply class defaults on the CDO — AFTER the final compile so that
	// object-reference properties (UBehaviorTree*, UBlackboardData*, etc.)
	// are set on the definitive CDO that gets serialised to disk.
	bool bNeedsPostCDOCompile = false;
	const TSharedPtr<FJsonObject>* DefaultsJson;
	if (JsonRoot->TryGetObjectField(TEXT("defaults"), DefaultsJson))
	{
		ApplyClassDefaults(Blueprint, *DefaultsJson);
		bNeedsPostCDOCompile = true;
	}

	// 8. Recompile to propagate class-default changes (e.g. AIControllerClass)
	// to already-placed level instances.  This must happen BEFORE inherited
	// component overrides because CDO reconstruction during compilation wipes
	// component sub-object modifications that aren't registered through the
	// InheritableComponentHandler.
	if (bNeedsPostCDOCompile)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
	}

	// 9. Apply inherited component overrides on the CDO's components.
	// This must happen AFTER the very last compilation because component
	// sub-objects are recreated from scratch during CDO construction — any
	// modifications applied before a compile would be wiped.  We intentionally
	// do NOT recompile after this step; SaveAsset serialises the overridden
	// values and they survive future compiles via CDO deserialisation.
	if (InheritedOverrides.Num() > 0 && Blueprint->GeneratedClass)
	{
		UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
		if (CDO)
		{
			for (const auto& Override : InheritedOverrides)
			{
				const FString& CompName = Override.Key;
				const TSharedPtr<FJsonObject>& PropsJson = Override.Value;

				// Find the component on the CDO by iterating default sub-objects
				TArray<UObject*> DefaultSubObjects;
				CDO->GetDefaultSubobjects(DefaultSubObjects);
				bool bFound = false;
				for (UObject* SubObj : DefaultSubObjects)
				{
					if (SubObj && SubObj->GetName().Contains(CompName))
					{
						ApplyComponentProperties(SubObj, PropsJson);
						UE_LOG(LogArborBP, Log, TEXT("Applied inherited override to '%s'"),
							*SubObj->GetName());
						bFound = true;
						break;
					}
				}
				if (!bFound)
				{
					UE_LOG(LogArborBP, Warning,
						TEXT("Inherited override target '%s' not found on CDO — component may be missing"),
						*CompName);
				}
			}
			CDO->MarkPackageDirty();
		}
	}

	// 10. Save
	SaveAsset(Blueprint);

	UE_LOG(LogArborBP, Log, TEXT("Successfully built Blueprint '%s' at %s (Parent: %s)"),
		*BPName, *AssetPath, *ParentClass->GetName());
	return Blueprint;
}

// ============================================================================
// JSON Parsing
// ============================================================================

bool UBlueprintBuilder::LoadAndParseJSON(
	const FString& JsonFilePath, TSharedPtr<FJsonObject>& OutJsonObject)
{
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
	{
		UE_LOG(LogArborBP, Error, TEXT("Failed to load JSON file: %s"), *JsonFilePath);
		return false;
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, OutJsonObject) || !OutJsonObject.IsValid())
	{
		UE_LOG(LogArborBP, Error, TEXT("Failed to parse JSON file: %s"), *JsonFilePath);
		return false;
	}

	return true;
}

// ============================================================================
// Parent Class Resolution
// ============================================================================

UClass* UBlueprintBuilder::ResolveParentClass(const FString& ClassName)
{
	// Shorthand map for common parent classes
	static const TMap<FString, FString> ShorthandMap = {
		{TEXT("Character"),        TEXT("/Script/Engine.Character")},
		{TEXT("Pawn"),             TEXT("/Script/Engine.Pawn")},
		{TEXT("Actor"),            TEXT("/Script/Engine.Actor")},
		{TEXT("AIController"),     TEXT("/Script/AIModule.AIController")},
		{TEXT("GameMode"),         TEXT("/Script/Engine.GameModeBase")},
		{TEXT("GameModeBase"),     TEXT("/Script/Engine.GameModeBase")},
		{TEXT("PlayerController"), TEXT("/Script/Engine.PlayerController")},
		{TEXT("AnimInstance"),     TEXT("/Script/Engine.AnimInstance")},
	};

	// Check shorthand map first
	if (const FString* MappedPath = ShorthandMap.Find(ClassName))
	{
		UClass* FoundClass = StaticLoadClass(UObject::StaticClass(), nullptr, **MappedPath);
		if (FoundClass)
		{
			return FoundClass;
		}
	}

	// Try as a full content path (e.g. /Game/BP/BP_BaseEnemy)
	if (ClassName.StartsWith(TEXT("/")))
	{
		UBlueprint* BP = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *ClassName));
		if (BP && BP->GeneratedClass)
		{
			return BP->GeneratedClass;
		}

		// Also try as a native class path
		UClass* FoundClass = StaticLoadClass(UObject::StaticClass(), nullptr, *ClassName);
		if (FoundClass)
		{
			return FoundClass;
		}
	}

	// Try common module paths with A/U prefix
	TArray<FString> ModulePaths = {
		TEXT("/Script/Engine"),
		TEXT("/Script/AIModule"),
		TEXT("/Script/GameplayTasks"),
		TEXT("/Script/NavigationSystem")
	};

	for (const FString& ModulePath : ModulePaths)
	{
		// Try as-is
		FString FullPath = FString::Printf(TEXT("%s.%s"), *ModulePath, *ClassName);
		UClass* FoundClass = StaticLoadClass(UObject::StaticClass(), nullptr, *FullPath);
		if (FoundClass)
		{
			return FoundClass;
		}

		// Try with A prefix (actors)
		FullPath = FString::Printf(TEXT("%s.A%s"), *ModulePath, *ClassName);
		FoundClass = StaticLoadClass(UObject::StaticClass(), nullptr, *FullPath);
		if (FoundClass)
		{
			return FoundClass;
		}

		// Try with U prefix (objects)
		FullPath = FString::Printf(TEXT("%s.U%s"), *ModulePath, *ClassName);
		FoundClass = StaticLoadClass(UObject::StaticClass(), nullptr, *FullPath);
		if (FoundClass)
		{
			return FoundClass;
		}
	}

	UE_LOG(LogArborBP, Warning, TEXT("Could not resolve parent class: %s"), *ClassName);
	return nullptr;
}

// ============================================================================
// Component Class Resolution
// ============================================================================

UClass* UBlueprintBuilder::ResolveComponentClass(const FString& TypeName)
{
	// Shorthand map for common component types
	static const TMap<FString, UClass*> ComponentMap = {
		{TEXT("SkeletalMeshComponent"),      USkeletalMeshComponent::StaticClass()},
		{TEXT("StaticMeshComponent"),         UStaticMeshComponent::StaticClass()},
		{TEXT("CapsuleComponent"),            UCapsuleComponent::StaticClass()},
		{TEXT("AIPerceptionComponent"),       UAIPerceptionComponent::StaticClass()},
		{TEXT("CharacterMovementComponent"),  UCharacterMovementComponent::StaticClass()},
		{TEXT("AudioComponent"),              UAudioComponent::StaticClass()},
		{TEXT("SphereCollision"),             USphereComponent::StaticClass()},
		{TEXT("SphereComponent"),             USphereComponent::StaticClass()},
		{TEXT("BoxCollision"),                UBoxComponent::StaticClass()},
		{TEXT("BoxComponent"),                UBoxComponent::StaticClass()},
	};

	if (UClass* const* Found = ComponentMap.Find(TypeName))
	{
		return *Found;
	}

	// Fallback: try loading from common modules
	TArray<FString> ModulePaths = {
		TEXT("/Script/Engine"),
		TEXT("/Script/AIModule"),
		TEXT("/Script/HeadMountedDisplay"),
		TEXT("/Script/NavigationSystem"),
		TEXT("/Script/Niagara"),
		TEXT("/Script/UMG"),
		TEXT("/Script/PhysicsCore"),
	};

	for (const FString& ModulePath : ModulePaths)
	{
		FString FullPath = FString::Printf(TEXT("%s.U%s"), *ModulePath, *TypeName);
		UClass* FoundClass = StaticLoadClass(UActorComponent::StaticClass(), nullptr, *FullPath);
		if (FoundClass)
		{
			return FoundClass;
		}

		// Try without U prefix
		FullPath = FString::Printf(TEXT("%s.%s"), *ModulePath, *TypeName);
		FoundClass = StaticLoadClass(UActorComponent::StaticClass(), nullptr, *FullPath);
		if (FoundClass)
		{
			return FoundClass;
		}
	}

	// Final fallback: iterate all loaded UActorComponent subclasses
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Candidate = *It;
		if (Candidate->IsChildOf(UActorComponent::StaticClass()) &&
			!Candidate->HasAnyClassFlags(CLASS_Abstract) &&
			(Candidate->GetName() == TypeName || Candidate->GetName() == (TEXT("U") + TypeName)))
		{
			UE_LOG(LogArborBP, Log, TEXT("Resolved component '%s' via class iterator -> %s"), *TypeName, *Candidate->GetPathName());
			return Candidate;
		}
	}

	UE_LOG(LogArborBP, Warning, TEXT("Could not resolve component class: %s"), *TypeName);
	return nullptr;
}

// ============================================================================
// Blueprint Asset Creation
// ============================================================================

UBlueprint* UBlueprintBuilder::CreateBlueprintAsset(
	const FString& Name, const FString& AssetPath, UClass* ParentClass)
{
	const FString PackagePath = AssetPath / Name;
	const FString AssetObjectPath = PackagePath + TEXT(".") + Name;

	// Check if asset already exists — update in place to preserve references
	if (UEditorAssetLibrary::DoesAssetExist(PackagePath))
	{
		UBlueprint* ExistingBP = LoadObject<UBlueprint>(nullptr, *AssetObjectPath);
		if (ExistingBP)
		{
			// NOTE: We no longer clear components, variables, event graph, or
			// timelines here.  BuildFromParsedJSON() selectively clears only the
			// sections that the incoming JSON specifies, so properties set by a
			// prior create_character_bp() (or manual edits) survive when
			// build_bp() is called with just an event_graph.

			// Warn if parent class differs
			if (ExistingBP->ParentClass && ParentClass &&
				ExistingBP->ParentClass != ParentClass)
			{
				UE_LOG(LogArborBP, Warning,
					TEXT("Existing Blueprint '%s' has parent class '%s' but JSON specifies '%s'. Keeping existing parent class."),
					*Name, *ExistingBP->ParentClass->GetName(), *ParentClass->GetName());
			}

			ExistingBP->GetPackage()->MarkPackageDirty();
			UE_LOG(LogArborBP, Log, TEXT("Updating existing Blueprint asset: %s"), *PackagePath);
			return ExistingBP;
		}
	}

	// Asset doesn't exist — create new
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		UE_LOG(LogArborBP, Error, TEXT("Failed to create package: %s"), *PackagePath);
		return nullptr;
	}

	// Detect AnimInstance parents → create UAnimBlueprint instead of UBlueprint
	bool bIsAnimBP = ParentClass && ParentClass->IsChildOf(UAnimInstance::StaticClass());

	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		Package,
		FName(*Name),
		BPTYPE_Normal,
		bIsAnimBP ? UAnimBlueprint::StaticClass() : UBlueprint::StaticClass(),
		bIsAnimBP ? UAnimBlueprintGeneratedClass::StaticClass() : UBlueprintGeneratedClass::StaticClass()
	);

	if (!Blueprint)
	{
		UE_LOG(LogArborBP, Error, TEXT("FKismetEditorUtilities::CreateBlueprint failed for '%s'"), *Name);
		return nullptr;
	}

	Blueprint->SetFlags(RF_Public | RF_Standalone | RF_Transactional);

	UE_LOG(LogArborBP, Log, TEXT("Creating new %s asset: %s"),
		bIsAnimBP ? TEXT("AnimBlueprint") : TEXT("Blueprint"), *PackagePath);
	return Blueprint;
}

// ============================================================================
// Component Addition
// ============================================================================

void UBlueprintBuilder::AddComponents(
	UBlueprint* Blueprint, const TArray<TSharedPtr<FJsonValue>>& ComponentsJson,
	TArray<TPair<FString, TSharedPtr<FJsonObject>>>& OutInheritedOverrides)
{
	for (const TSharedPtr<FJsonValue>& CompValue : ComponentsJson)
	{
		const TSharedPtr<FJsonObject>& CompJson = CompValue->AsObject();
		if (!CompJson.IsValid())
		{
			continue;
		}

		AddComponent(Blueprint, CompJson, OutInheritedOverrides);
	}
}

USCS_Node* UBlueprintBuilder::AddComponent(
	UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& ComponentJson,
	TArray<TPair<FString, TSharedPtr<FJsonObject>>>& OutInheritedOverrides)
{
	FString CompName;
	if (!ComponentJson->TryGetStringField(TEXT("name"), CompName))
	{
		UE_LOG(LogArborBP, Warning, TEXT("Component missing 'name' field, skipping"));
		return nullptr;
	}

	FString CompType;
	if (!ComponentJson->TryGetStringField(TEXT("type"), CompType))
	{
		UE_LOG(LogArborBP, Warning, TEXT("Component '%s' missing 'type' field, skipping"), *CompName);
		return nullptr;
	}

	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	if (!SCS)
	{
		UE_LOG(LogArborBP, Error, TEXT("Blueprint has no SimpleConstructionScript"));
		return nullptr;
	}

	// Check if the parent class already has an inherited component that matches.
	// If so, defer property application to post-compile (CDO override) instead
	// of creating a duplicate SCS node.
	// Require BOTH name AND class match to avoid false positives (e.g. config-
	// dependent components like AIPerceptionComponent on AAIController).
	UClass* ParentClass = Blueprint->ParentClass;
	if (ParentClass)
	{
		UObject* ParentCDO = ParentClass->GetDefaultObject();
		if (ParentCDO)
		{
			TArray<UObject*> DefaultSubObjects;
			ParentCDO->GetDefaultSubobjects(DefaultSubObjects);

			UClass* WantedClass = ResolveComponentClass(CompType);
			for (UObject* SubObj : DefaultSubObjects)
			{
				if (!SubObj) continue;

				bool bNameMatch = SubObj->GetName().Contains(CompName);
				bool bClassMatch = WantedClass && SubObj->IsA(WantedClass);

				if (bNameMatch && bClassMatch)
				{
					// This is an inherited component — defer properties to post-compile
					const TSharedPtr<FJsonObject>* PropertiesJson;
					if (ComponentJson->TryGetObjectField(TEXT("properties"), PropertiesJson))
					{
						OutInheritedOverrides.Add(
							TPair<FString, TSharedPtr<FJsonObject>>(CompName, *PropertiesJson));
					}
					UE_LOG(LogArborBP, Log,
						TEXT("Component '%s' (%s) matches inherited '%s' — deferring to post-compile override"),
						*CompName, *CompType, *SubObj->GetName());
					return nullptr;
				}
			}
		}
	}

	UClass* ComponentClass = ResolveComponentClass(CompType);
	if (!ComponentClass)
	{
		UE_LOG(LogArborBP, Error, TEXT("Could not resolve component type: %s"), *CompType);
		return nullptr;
	}

	USCS_Node* Node = SCS->CreateNode(ComponentClass, FName(*CompName));
	if (!Node)
	{
		UE_LOG(LogArborBP, Error, TEXT("Failed to create SCS node for component: %s"), *CompName);
		return nullptr;
	}

	SCS->AddNode(Node);

	// Apply properties on the component template
	const TSharedPtr<FJsonObject>* PropertiesJson;
	if (ComponentJson->TryGetObjectField(TEXT("properties"), PropertiesJson))
	{
		if (Node->ComponentTemplate)
		{
			ApplyComponentProperties(Node->ComponentTemplate, *PropertiesJson);
		}
	}

	UE_LOG(LogArborBP, Verbose, TEXT("Added component: %s (%s)"), *CompName, *CompType);
	return Node;
}

// ============================================================================
// Component Property Application
// ============================================================================

void UBlueprintBuilder::ApplyComponentProperties(
	UObject* ComponentTemplate, const TSharedPtr<FJsonObject>& PropertiesJson)
{
	if (!PropertiesJson.IsValid() || !ComponentTemplate)
	{
		return;
	}

	for (const auto& Pair : PropertiesJson->Values)
	{
		const FString Key = FString(*Pair.Key);
		const TSharedPtr<FJsonValue>& Value = Pair.Value;

		// Special handling: SensesConfig on AIPerceptionComponent
		if (Key == TEXT("SensesConfig"))
		{
			const TArray<TSharedPtr<FJsonValue>>* SensesArray;
			if (Value->TryGetArray(SensesArray))
			{
				HandleSensesConfig(ComponentTemplate, *SensesArray);
			}
			continue;
		}

		// Use PreEditChange / PostEditChangeProperty to trigger all necessary
		// side effects (render state recreation, InitAnim, etc.) for any
		// property type on components. This replaces per-type special cases.
		// Resolve the top-level property name (handle dot notation, b-prefix,
		// and lowercase variations to match FindPropertyDeep's lookup).
		FString TopLevelName = Key;
		if (Key.Contains(TEXT(".")))
		{
			Key.Split(TEXT("."), &TopLevelName, nullptr);
		}
		UClass* CompClass = ComponentTemplate->GetClass();
		FProperty* EditProp = CompClass->FindPropertyByName(FName(*TopLevelName));
		if (!EditProp)
		{
			EditProp = CompClass->FindPropertyByName(FName(*(TEXT("b") + TopLevelName)));
		}
		if (!EditProp && TopLevelName.Len() > 0)
		{
			FString LowerName = TopLevelName;
			LowerName[0] = FChar::ToLower(LowerName[0]);
			EditProp = CompClass->FindPropertyByName(FName(*LowerName));
		}

		if (EditProp)
		{
			ComponentTemplate->PreEditChange(EditProp);
		}

		SetPropertyFromJson(ComponentTemplate, Key, Value);

		if (EditProp)
		{
			FPropertyChangedEvent ChangedEvent(EditProp);
			ComponentTemplate->PostEditChangeProperty(ChangedEvent);
		}
	}
}

// ============================================================================
// AI Perception SensesConfig
// ============================================================================

void UBlueprintBuilder::HandleSensesConfig(
	UObject* PerceptionComponent, const TArray<TSharedPtr<FJsonValue>>& SensesArray)
{
	UAIPerceptionComponent* Perception = Cast<UAIPerceptionComponent>(PerceptionComponent);
	if (!Perception)
	{
		UE_LOG(LogArborBP, Warning, TEXT("HandleSensesConfig: object is not an AIPerceptionComponent"));
		return;
	}

	for (const TSharedPtr<FJsonValue>& SenseValue : SensesArray)
	{
		const TSharedPtr<FJsonObject>& SenseJson = SenseValue->AsObject();
		if (!SenseJson.IsValid())
		{
			continue;
		}

		FString SenseType;
		if (!SenseJson->TryGetStringField(TEXT("type"), SenseType))
		{
			UE_LOG(LogArborBP, Warning, TEXT("Sense config missing 'type', skipping"));
			continue;
		}

		UAISenseConfig* SenseConfig = nullptr;

		if (SenseType == TEXT("AISense_Sight") || SenseType == TEXT("Sight"))
		{
			UAISenseConfig_Sight* SightConfig = NewObject<UAISenseConfig_Sight>(Perception);

			// Apply sight-specific properties
			double SightRadius;
			if (SenseJson->TryGetNumberField(TEXT("SightRadius"), SightRadius))
			{
				SightConfig->SightRadius = static_cast<float>(SightRadius);
			}
			double LoseSightRadius;
			if (SenseJson->TryGetNumberField(TEXT("LoseSightRadius"), LoseSightRadius))
			{
				SightConfig->LoseSightRadius = static_cast<float>(LoseSightRadius);
			}
			double PeripheralVision;
			if (SenseJson->TryGetNumberField(TEXT("PeripheralVisionAngleDegrees"), PeripheralVision))
			{
				SightConfig->PeripheralVisionAngleDegrees = static_cast<float>(PeripheralVision);
			}

			// Enable detection of all affiliations by default so perception works
			// out of the box without a team/affiliation system.
			FAISenseAffiliationFilter Affiliation;
			Affiliation.bDetectEnemies = true;
			Affiliation.bDetectNeutrals = true;
			Affiliation.bDetectFriendlies = true;

			// Allow JSON overrides
			bool bVal;
			if (SenseJson->TryGetBoolField(TEXT("DetectEnemies"), bVal))
			{
				Affiliation.bDetectEnemies = bVal;
			}
			if (SenseJson->TryGetBoolField(TEXT("DetectNeutrals"), bVal))
			{
				Affiliation.bDetectNeutrals = bVal;
			}
			if (SenseJson->TryGetBoolField(TEXT("DetectFriendlies"), bVal))
			{
				Affiliation.bDetectFriendlies = bVal;
			}
			SightConfig->DetectionByAffiliation = Affiliation;

			SenseConfig = SightConfig;
		}
		else if (SenseType == TEXT("AISense_Hearing") || SenseType == TEXT("Hearing"))
		{
			UAISenseConfig_Hearing* HearingConfig = NewObject<UAISenseConfig_Hearing>(Perception);

			double HearingRange;
			if (SenseJson->TryGetNumberField(TEXT("HearingRange"), HearingRange))
			{
				HearingConfig->HearingRange = static_cast<float>(HearingRange);
			}

			SenseConfig = HearingConfig;
		}
		else if (SenseType == TEXT("AISense_Damage") || SenseType == TEXT("Damage"))
		{
			SenseConfig = NewObject<UAISenseConfig_Damage>(Perception);
		}
		else
		{
			UE_LOG(LogArborBP, Warning, TEXT("Unknown sense type: %s"), *SenseType);
			continue;
		}

		if (SenseConfig)
		{
			Perception->ConfigureSense(*SenseConfig);
			UE_LOG(LogArborBP, Verbose, TEXT("Added sense config: %s"), *SenseType);

			// Set this sense as dominant on the perception component
			bool bDominant;
			if (SenseJson->TryGetBoolField(TEXT("dominant"), bDominant) && bDominant)
			{
				Perception->SetDominantSense(SenseConfig->GetSenseImplementation());
			}
		}
	}
}

// ============================================================================
// QuerySenseConfig — read perception sense configs from a Blueprint
// ============================================================================

FString UBlueprintBuilder::QuerySenseConfig(const FString& AssetPath)
{
	UBlueprint* Blueprint = LoadBlueprintForEditing(AssetPath);
	if (!Blueprint)
	{
		return TEXT("{\"success\":false,\"error\":\"Blueprint not found\"}");
	}

	// Find AIPerceptionComponent template in SCS
	UAIPerceptionComponent* Perception = nullptr;
	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	if (SCS)
	{
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (Node && Node->ComponentTemplate &&
				Node->ComponentTemplate->IsA(UAIPerceptionComponent::StaticClass()))
			{
				Perception = Cast<UAIPerceptionComponent>(Node->ComponentTemplate);
				break;
			}
		}
	}

	if (!Perception)
	{
		return TEXT("{\"success\":false,\"error\":\"No AIPerceptionComponent found\"}");
	}

	// Get the dominant sense class for comparison
	TSubclassOf<UAISense> DominantSense = Perception->GetDominantSense();

	// Access the protected SensesConfig array via reflection
	FArrayProperty* SensesProp = CastField<FArrayProperty>(
		UAIPerceptionComponent::StaticClass()->FindPropertyByName(TEXT("SensesConfig")));
	if (!SensesProp)
	{
		return TEXT("{\"success\":false,\"error\":\"Could not find SensesConfig property\"}");
	}

	TArray<TSharedPtr<FJsonValue>> SensesArray;
	FScriptArrayHelper ArrayHelper(SensesProp, SensesProp->ContainerPtrToValuePtr<void>(Perception));
	for (int32 i = 0; i < ArrayHelper.Num(); ++i)
	{
		FObjectProperty* InnerProp = CastField<FObjectProperty>(SensesProp->Inner);
		if (!InnerProp) continue;
		UAISenseConfig* Config = Cast<UAISenseConfig>(InnerProp->GetObjectPropertyValue(ArrayHelper.GetRawPtr(i)));
		if (!Config) continue;

		auto SenseObj = MakeShared<FJsonObject>();
		SenseObj->SetStringField(TEXT("class"), Config->GetClass()->GetName());
		SenseObj->SetBoolField(TEXT("dominant_sense"),
			DominantSense && Config->GetSenseImplementation() == DominantSense);

		// Sight-specific properties
		if (UAISenseConfig_Sight* Sight = Cast<UAISenseConfig_Sight>(Config))
		{
			SenseObj->SetNumberField(TEXT("sight_radius"), Sight->SightRadius);
			SenseObj->SetNumberField(TEXT("lose_sight_radius"), Sight->LoseSightRadius);
			SenseObj->SetNumberField(TEXT("peripheral_vision_angle"), Sight->PeripheralVisionAngleDegrees);
			SenseObj->SetBoolField(TEXT("detect_enemies"), Sight->DetectionByAffiliation.bDetectEnemies != 0);
			SenseObj->SetBoolField(TEXT("detect_neutrals"), Sight->DetectionByAffiliation.bDetectNeutrals != 0);
			SenseObj->SetBoolField(TEXT("detect_friendlies"), Sight->DetectionByAffiliation.bDetectFriendlies != 0);
		}
		// Hearing-specific properties
		else if (UAISenseConfig_Hearing* Hearing = Cast<UAISenseConfig_Hearing>(Config))
		{
			SenseObj->SetNumberField(TEXT("hearing_range"), Hearing->HearingRange);
		}

		SensesArray.Add(MakeShared<FJsonValueObject>(SenseObj));
	}

	auto Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetArrayField(TEXT("senses"), SensesArray);

	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

// ============================================================================
// Generic Property Setting via Reflection
// ============================================================================

// Recursive helper: find a property by name, searching nested structs if needed.
// Supports dot notation ("BodyInstance.CollisionProfileName") and auto-search.
// Returns the property and its memory address, or nullptr if not found.
// Deprecated → current property name aliases for backward compatibility.
// UE 5.1 renamed SkeletalMesh to SkeletalMeshAsset on USkeletalMeshComponent.
static const TMap<FString, FString> PropertyAliases = {
	{TEXT("SkeletalMesh"), TEXT("SkeletalMeshAsset")},
};

static FProperty* FindPropertyDeep(
	UStruct* Struct, void* ContainerPtr, const FString& PropertyName, void*& OutAddr)
{
	// 1. Try dot notation: "StructProp.NestedProp"
	FString Left, Right;
	if (PropertyName.Split(TEXT("."), &Left, &Right))
	{
		FProperty* OuterProp = Struct->FindPropertyByName(FName(*Left));
		if (FStructProperty* SP = CastField<FStructProperty>(OuterProp))
		{
			void* StructAddr = SP->ContainerPtrToValuePtr<void>(ContainerPtr);
			return FindPropertyDeep(SP->Struct, StructAddr, Right, OutAddr);
		}
	}

	// 2. Direct lookup on this struct (with b-prefix and lowercase variations)
	FProperty* Prop = Struct->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		Prop = Struct->FindPropertyByName(FName(*(TEXT("b") + PropertyName)));
	}
	if (!Prop)
	{
		FString LowerKey = PropertyName;
		if (LowerKey.Len() > 0)
		{
			LowerKey[0] = FChar::ToLower(LowerKey[0]);
		}
		Prop = Struct->FindPropertyByName(FName(*LowerKey));
	}
	if (Prop)
	{
		OutAddr = Prop->ContainerPtrToValuePtr<void>(ContainerPtr);
		return Prop;
	}

	// 3. Try deprecated property name aliases
	if (const FString* Alias = PropertyAliases.Find(PropertyName))
	{
		Prop = Struct->FindPropertyByName(FName(**Alias));
		if (Prop)
		{
			OutAddr = Prop->ContainerPtrToValuePtr<void>(ContainerPtr);
			return Prop;
		}
	}

	// 4. Recursive search into nested structs
	for (TFieldIterator<FStructProperty> It(Struct); It; ++It)
	{
		FStructProperty* SP = *It;
		void* StructAddr = SP->ContainerPtrToValuePtr<void>(ContainerPtr);
		FProperty* Found = FindPropertyDeep(SP->Struct, StructAddr, PropertyName, OutAddr);
		if (Found)
		{
			return Found;
		}
	}

	return nullptr;
}

void UBlueprintBuilder::SetPropertyFromJson(
	UObject* Object, const FString& PropertyName, const TSharedPtr<FJsonValue>& JsonValue)
{
	if (!Object || !JsonValue.IsValid())
	{
		return;
	}

	UClass* ObjClass = Object->GetClass();

	void* PropertyAddr = nullptr;
	FProperty* Prop = FindPropertyDeep(ObjClass, Object, PropertyName, PropertyAddr);

	if (!Prop)
	{
		UE_LOG(LogArborBP, Warning, TEXT("Property '%s' not found on %s"), *PropertyName, *ObjClass->GetName());
		return;
	}

	// Handle struct properties
	if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		if (StructProp->Struct->GetFName() == FName(TEXT("Vector")))
		{
			const TSharedPtr<FJsonObject>* VecObj;
			if (JsonValue->TryGetObject(VecObj))
			{
				FVector* Vec = static_cast<FVector*>(PropertyAddr);
				Vec->X = (*VecObj)->GetNumberField(TEXT("X"));
				Vec->Y = (*VecObj)->GetNumberField(TEXT("Y"));
				Vec->Z = (*VecObj)->GetNumberField(TEXT("Z"));
			}
			return;
		}

		if (StructProp->Struct->GetFName() == FName(TEXT("Rotator")))
		{
			const TSharedPtr<FJsonObject>* RotObj;
			if (JsonValue->TryGetObject(RotObj))
			{
				FRotator* Rot = static_cast<FRotator*>(PropertyAddr);
				Rot->Pitch = (*RotObj)->GetNumberField(TEXT("Pitch"));
				Rot->Yaw = (*RotObj)->GetNumberField(TEXT("Yaw"));
				Rot->Roll = (*RotObj)->GetNumberField(TEXT("Roll"));
			}
			return;
		}
	}

	// Handle TArray properties
	if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
	{
		const TArray<TSharedPtr<FJsonValue>>* JsonArray;
		if (JsonValue->TryGetArray(JsonArray))
		{
			FScriptArrayHelper ArrayHelper(ArrayProp, PropertyAddr);
			ArrayHelper.EmptyValues();

			FProperty* InnerProp = ArrayProp->Inner;

			for (const TSharedPtr<FJsonValue>& Elem : *JsonArray)
			{
				int32 Idx = ArrayHelper.AddValue();
				void* ElemAddr = ArrayHelper.GetRawPtr(Idx);

				if (FObjectProperty* InnerObjProp = CastField<FObjectProperty>(InnerProp))
				{
					FString AssetPath = Elem->AsString();
					if (!AssetPath.IsEmpty())
					{
						UObject* LoadedObj = StaticLoadObject(
							InnerObjProp->PropertyClass, nullptr, *AssetPath);
						if (!LoadedObj && !AssetPath.Contains(TEXT(".")))
						{
							FString AssetName = FPaths::GetBaseFilename(AssetPath);
							LoadedObj = StaticLoadObject(
								InnerObjProp->PropertyClass, nullptr,
								*(AssetPath + TEXT(".") + AssetName));
						}
						if (LoadedObj)
						{
							InnerObjProp->SetObjectPropertyValue(ElemAddr, LoadedObj);
						}
						else
						{
							UE_LOG(LogArborBP, Warning,
								TEXT("Array[%d]: could not load '%s'"), Idx, *AssetPath);
						}
					}
				}
				else if (CastField<FFloatProperty>(InnerProp))
				{
					*static_cast<float*>(ElemAddr) = static_cast<float>(Elem->AsNumber());
				}
				else if (CastField<FDoubleProperty>(InnerProp))
				{
					*static_cast<double*>(ElemAddr) = Elem->AsNumber();
				}
				else if (CastField<FIntProperty>(InnerProp))
				{
					*static_cast<int32*>(ElemAddr) = static_cast<int32>(Elem->AsNumber());
				}
				else if (CastField<FBoolProperty>(InnerProp))
				{
					CastField<FBoolProperty>(InnerProp)->SetPropertyValue(ElemAddr, Elem->AsBool());
				}
				else if (CastField<FStrProperty>(InnerProp))
				{
					*static_cast<FString*>(ElemAddr) = Elem->AsString();
				}
				else if (CastField<FNameProperty>(InnerProp))
				{
					*static_cast<FName*>(ElemAddr) = FName(*Elem->AsString());
				}
				else
				{
					UE_LOG(LogArborBP, Warning,
						TEXT("Array[%d]: unsupported inner type for '%s'"), Idx, *PropertyName);
				}
			}

			UE_LOG(LogArborBP, Log, TEXT("Set array property '%s' with %d elements"),
				*PropertyName, JsonArray->Num());
		}
		return;
	}

	// Handle class reference properties (TSubclassOf<T>) — must come before FObjectProperty
	// because FClassProperty inherits from FObjectProperty and CastField would match both
	if (FClassProperty* ClassProp = CastField<FClassProperty>(Prop))
	{
		FString ClassPath = JsonValue->AsString();
		if (!ClassPath.IsEmpty())
		{
			UClass* LoadedClass = StaticLoadClass(ClassProp->MetaClass, nullptr, *ClassPath);

			// Try _C suffix class path (e.g. /Game/AI/ABP_Ghost → ABP_Ghost_C)
			if (!LoadedClass && !ClassPath.EndsWith(TEXT("_C")))
			{
				FString AssetName = FPaths::GetBaseFilename(ClassPath);
				FString ClassSuffixPath = ClassPath + TEXT(".") + AssetName + TEXT("_C");
				LoadedClass = StaticLoadClass(ClassProp->MetaClass, nullptr, *ClassSuffixPath);
			}

			// Try loading a Blueprint asset and getting its GeneratedClass
			if (!LoadedClass)
			{
				UBlueprint* BP = Cast<UBlueprint>(
					StaticLoadObject(UBlueprint::StaticClass(), nullptr, *ClassPath));

				// Retry with full object path if short form failed
				if (!BP && !ClassPath.Contains(TEXT(".")))
				{
					FString AssetName = FPaths::GetBaseFilename(ClassPath);
					FString FullPath = ClassPath + TEXT(".") + AssetName;
					BP = Cast<UBlueprint>(
						StaticLoadObject(UBlueprint::StaticClass(), nullptr, *FullPath));
				}

				if (BP && BP->GeneratedClass)
				{
					LoadedClass = BP->GeneratedClass;
				}
			}

			if (LoadedClass)
			{
				ClassProp->SetObjectPropertyValue(PropertyAddr, LoadedClass);
				UE_LOG(LogArborBP, Log, TEXT("Set class property '%s' = '%s'"),
					*PropertyName, *LoadedClass->GetPathName());
			}
			else
			{
				UE_LOG(LogArborBP, Warning, TEXT("Could not load class '%s' for property '%s'"),
					*ClassPath, *PropertyName);
			}
		}
		return;
	}

	// Handle object reference properties — load from path string
	if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
	{
		FString AssetPath = JsonValue->AsString();
		if (!AssetPath.IsEmpty())
		{
			UObject* LoadedObj = StaticLoadObject(ObjProp->PropertyClass, nullptr, *AssetPath);

			// Retry with full object path (PackageName.AssetName) if short form failed
			if (!LoadedObj && !AssetPath.Contains(TEXT(".")))
			{
				FString AssetName = FPaths::GetBaseFilename(AssetPath);
				FString FullPath = AssetPath + TEXT(".") + AssetName;
				LoadedObj = StaticLoadObject(ObjProp->PropertyClass, nullptr, *FullPath);
			}

			if (LoadedObj)
			{
				ObjProp->SetObjectPropertyValue(PropertyAddr, LoadedObj);
				UE_LOG(LogArborBP, Log, TEXT("Set object property '%s' = '%s'"),
					*PropertyName, *LoadedObj->GetPathName());
			}
			else
			{
				UE_LOG(LogArborBP, Warning, TEXT("Could not load object '%s' for property '%s'"),
					*AssetPath, *PropertyName);
			}
		}
		return;
	}

	if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Prop))
	{
		FString AssetPath = JsonValue->AsString();
		if (!AssetPath.IsEmpty())
		{
			FSoftObjectPtr* SoftPtr = static_cast<FSoftObjectPtr*>(PropertyAddr);
			*SoftPtr = FSoftObjectPath(AssetPath);
		}
		return;
	}

	if (FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Prop))
	{
		FString ClassPath = JsonValue->AsString();
		if (!ClassPath.IsEmpty())
		{
			FSoftObjectPtr* SoftPtr = static_cast<FSoftObjectPtr*>(PropertyAddr);
			*SoftPtr = FSoftObjectPath(ClassPath);
		}
		return;
	}

	// Primitive types
	if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
	{
		FloatProp->SetPropertyValue(PropertyAddr, static_cast<float>(JsonValue->AsNumber()));
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
	{
		DoubleProp->SetPropertyValue(PropertyAddr, JsonValue->AsNumber());
	}
	else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
	{
		IntProp->SetPropertyValue(PropertyAddr, static_cast<int32>(JsonValue->AsNumber()));
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
	{
		BoolProp->SetPropertyValue(PropertyAddr, JsonValue->AsBool());
	}
	else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
	{
		StrProp->SetPropertyValue(PropertyAddr, JsonValue->AsString());
	}
	else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
	{
		NameProp->SetPropertyValue(PropertyAddr, FName(*JsonValue->AsString()));
	}
	else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
	{
		UEnum* EnumDef = EnumProp->GetEnum();
		if (EnumDef)
		{
			int64 EnumValue = EnumDef->GetValueByNameString(JsonValue->AsString());
			if (EnumValue != INDEX_NONE)
			{
				EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(PropertyAddr, EnumValue);
			}
			else
			{
				UE_LOG(LogArborBP, Warning, TEXT("Enum value '%s' not found for property '%s'"),
					*JsonValue->AsString(), *PropertyName);
			}
		}
	}
	else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
	{
		if (ByteProp->Enum)
		{
			int64 EnumValue = ByteProp->Enum->GetValueByNameString(JsonValue->AsString());
			if (EnumValue != INDEX_NONE)
			{
				ByteProp->SetPropertyValue(PropertyAddr, static_cast<uint8>(EnumValue));
			}
		}
		else
		{
			ByteProp->SetPropertyValue(PropertyAddr, static_cast<uint8>(JsonValue->AsNumber()));
		}
	}
	else
	{
		UE_LOG(LogArborBP, Warning, TEXT("Unsupported property type for '%s' on %s"),
			*PropertyName, *ObjClass->GetName());
	}
}

// ============================================================================
// Class Defaults (CDO)
// ============================================================================

void UBlueprintBuilder::ApplyClassDefaults(
	UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& DefaultsJson)
{
	if (!DefaultsJson.IsValid() || !Blueprint)
	{
		return;
	}

	if (!Blueprint->GeneratedClass)
	{
		UE_LOG(LogArborBP, Warning, TEXT("Blueprint has no GeneratedClass — compile first"));
		return;
	}

	UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
	if (!CDO)
	{
		UE_LOG(LogArborBP, Warning, TEXT("Could not get CDO for %s"), *Blueprint->GetName());
		return;
	}

	for (const auto& Pair : DefaultsJson->Values)
	{
		SetPropertyFromJson(CDO, FString(*Pair.Key), Pair.Value);
	}

	CDO->MarkPackageDirty();
	UE_LOG(LogArborBP, Verbose, TEXT("Applied class defaults to CDO"));
}

// ============================================================================
// Blueprint Variables
// ============================================================================

void UBlueprintBuilder::AddVariables(
	UBlueprint* Blueprint, const TArray<TSharedPtr<FJsonValue>>& VariablesJson)
{
	for (const TSharedPtr<FJsonValue>& VarValue : VariablesJson)
	{
		const TSharedPtr<FJsonObject>& VarJson = VarValue->AsObject();
		if (!VarJson.IsValid())
		{
			continue;
		}

		FString VarName;
		FString VarType;
		if (!VarJson->TryGetStringField(TEXT("name"), VarName) ||
			!VarJson->TryGetStringField(TEXT("type"), VarType))
		{
			UE_LOG(LogArborBP, Warning, TEXT("Variable missing 'name' or 'type', skipping"));
			continue;
		}

		FBPVariableDescription NewVar;
		NewVar.VarName = FName(*VarName);
		NewVar.FriendlyName = VarName;
		NewVar.Category = FText::FromString(TEXT("Default"));
		NewVar.PropertyFlags = CPF_Edit | CPF_BlueprintVisible;

		// Map type string to FEdGraphPinType
		if (VarType == TEXT("Float") || VarType == TEXT("float"))
		{
			NewVar.VarType.PinCategory = UEdGraphSchema_K2::PC_Real;
			NewVar.VarType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		}
		else if (VarType == TEXT("Int") || VarType == TEXT("int") || VarType == TEXT("Integer"))
		{
			NewVar.VarType.PinCategory = UEdGraphSchema_K2::PC_Int;
		}
		else if (VarType == TEXT("Bool") || VarType == TEXT("bool") || VarType == TEXT("Boolean"))
		{
			NewVar.VarType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		}
		else if (VarType == TEXT("String") || VarType == TEXT("string"))
		{
			NewVar.VarType.PinCategory = UEdGraphSchema_K2::PC_String;
		}
		else if (VarType == TEXT("Name") || VarType == TEXT("name"))
		{
			NewVar.VarType.PinCategory = UEdGraphSchema_K2::PC_Name;
		}
		else if (VarType == TEXT("Vector") || VarType == TEXT("vector"))
		{
			NewVar.VarType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			NewVar.VarType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
		}
		else if (VarType == TEXT("Rotator") || VarType == TEXT("rotator"))
		{
			NewVar.VarType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			NewVar.VarType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
		}
		else if (VarType == TEXT("Object") || VarType == TEXT("object"))
		{
			NewVar.VarType.PinCategory = UEdGraphSchema_K2::PC_Object;
			NewVar.VarType.PinSubCategoryObject = UObject::StaticClass();
		}
		else
		{
			UE_LOG(LogArborBP, Warning, TEXT("Unknown variable type '%s' for '%s', defaulting to Float"),
				*VarType, *VarName);
			NewVar.VarType.PinCategory = UEdGraphSchema_K2::PC_Real;
			NewVar.VarType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		}

		// Set default value as string
		FString DefaultStr;
		if (VarJson->TryGetStringField(TEXT("default"), DefaultStr))
		{
			NewVar.DefaultValue = DefaultStr;
		}
		else
		{
			double DefaultNum;
			if (VarJson->TryGetNumberField(TEXT("default"), DefaultNum))
			{
				NewVar.DefaultValue = FString::SanitizeFloat(DefaultNum);
			}
			else
			{
				bool DefaultBool;
				if (VarJson->TryGetBoolField(TEXT("default"), DefaultBool))
				{
					NewVar.DefaultValue = DefaultBool ? TEXT("true") : TEXT("false");
				}
			}
		}

		Blueprint->NewVariables.Add(NewVar);
		UE_LOG(LogArborBP, Verbose, TEXT("Added variable: %s (%s) default='%s'"),
			*VarName, *VarType, *NewVar.DefaultValue);
	}
}

// ============================================================================
// Asset Saving
// ============================================================================

bool UBlueprintBuilder::SaveAsset(UObject* Asset)
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
		UE_LOG(LogArborBP, Log, TEXT("Saved asset: %s"), *PackageName);
	}
	else
	{
		UE_LOG(LogArborBP, Error, TEXT("Failed to save asset: %s"), *PackageName);
	}

	return bSuccess;
}

// ============================================================================
// Event Graph — Clear
// ============================================================================

void UBlueprintBuilder::ClearEventGraph(UBlueprint* Blueprint)
{
	if (!Blueprint || Blueprint->UbergraphPages.Num() == 0)
	{
		return;
	}

	UEdGraph* Graph = Blueprint->UbergraphPages[0];
	TArray<UEdGraphNode*> NodesToRemove;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node)
		{
			NodesToRemove.Add(Node);
		}
	}
	for (UEdGraphNode* Node : NodesToRemove)
	{
		Graph->RemoveNode(Node);
	}
	UE_LOG(LogArborBP, Verbose, TEXT("Cleared %d event graph nodes"), NodesToRemove.Num());
}

// ============================================================================
// Timelines
// ============================================================================

void UBlueprintBuilder::AddTimelines(
	UBlueprint* Blueprint,
	const TArray<TSharedPtr<FJsonValue>>& TimelinesJson)
{
	for (const TSharedPtr<FJsonValue>& TLValue : TimelinesJson)
	{
		const TSharedPtr<FJsonObject>& TLJson = TLValue->AsObject();
		if (!TLJson.IsValid()) continue;

		FString TLName;
		if (!TLJson->TryGetStringField(TEXT("name"), TLName))
		{
			UE_LOG(LogArborBP, Warning, TEXT("Timeline missing 'name', skipping"));
			continue;
		}

		check(Blueprint->GeneratedClass);
		FName TimelineTemplateName = *UTimelineTemplate::TimelineVariableNameToTemplateName(FName(*TLName));
		UTimelineTemplate* Timeline = NewObject<UTimelineTemplate>(
			Blueprint->GeneratedClass, TimelineTemplateName, RF_Transactional);

		double Length = 1.0;
		TLJson->TryGetNumberField(TEXT("length"), Length);
		Timeline->TimelineLength = static_cast<float>(Length);

		bool bLoop = false;
		TLJson->TryGetBoolField(TEXT("loop"), bLoop);
		Timeline->bLoop = bLoop;

		// Process float tracks
		const TArray<TSharedPtr<FJsonValue>>* TracksArray;
		if (TLJson->TryGetArrayField(TEXT("tracks"), TracksArray))
		{
			for (const TSharedPtr<FJsonValue>& TrackValue : *TracksArray)
			{
				const TSharedPtr<FJsonObject>& TrackJson = TrackValue->AsObject();
				if (!TrackJson.IsValid()) continue;

				FString TrackName;
				if (!TrackJson->TryGetStringField(TEXT("name"), TrackName)) continue;

				FString TrackType;
				TrackJson->TryGetStringField(TEXT("type"), TrackType);

				if (TrackType.IsEmpty() || TrackType == TEXT("Float"))
				{
					FTTFloatTrack& FloatTrack = Timeline->FloatTracks.AddDefaulted_GetRef();
					FloatTrack.SetTrackName(FName(*TrackName), Timeline);
					FloatTrack.CurveFloat = NewObject<UCurveFloat>(Blueprint, NAME_None, RF_Public);

					const TArray<TSharedPtr<FJsonValue>>* KeysArray;
					if (TrackJson->TryGetArrayField(TEXT("keys"), KeysArray))
					{
						for (const TSharedPtr<FJsonValue>& KeyValue : *KeysArray)
						{
							const TArray<TSharedPtr<FJsonValue>>* KeyPair;
							if (KeyValue->TryGetArray(KeyPair) && KeyPair->Num() >= 2)
							{
								float Time = static_cast<float>((*KeyPair)[0]->AsNumber());
								float Value = static_cast<float>((*KeyPair)[1]->AsNumber());
								FloatTrack.CurveFloat->FloatCurve.AddKey(Time, Value);
							}
						}
					}

					// Register in display order so AllocateDefaultPins creates the output pin
					Timeline->AddDisplayTrack(FTTTrackId(FTTTrackBase::TT_FloatInterp, Timeline->FloatTracks.Num() - 1));

					UE_LOG(LogArborBP, Verbose, TEXT("Added float track '%s' to timeline '%s'"),
						*TrackName, *TLName);
				}
				else
				{
					UE_LOG(LogArborBP, Warning,
						TEXT("Unsupported timeline track type '%s' for track '%s'"),
						*TrackType, *TrackName);
				}
			}
		}

		Blueprint->Timelines.Add(Timeline);
		UE_LOG(LogArborBP, Log, TEXT("Added timeline: %s (%.1fs, %s)"),
			*TLName, Timeline->TimelineLength, bLoop ? TEXT("looping") : TEXT("one-shot"));
	}

	if (TimelinesJson.Num() > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
}

// ============================================================================
// Event Graph — Main Entry Point
// ============================================================================

void UBlueprintBuilder::AddEventGraph(
	UBlueprint* Blueprint,
	const TSharedPtr<FJsonObject>& EventGraphJson)
{
	if (!EventGraphJson.IsValid())
	{
		return;
	}

	// Get or create the ubergraph
	UEdGraph* Graph = nullptr;
	if (Blueprint->UbergraphPages.Num() > 0)
	{
		Graph = Blueprint->UbergraphPages[0];
	}
	else
	{
		Graph = FBlueprintEditorUtils::CreateNewGraph(
			Blueprint,
			UEdGraphSchema_K2::GN_EventGraph,
			UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass());
		Blueprint->UbergraphPages.Add(Graph);
	}

	// 1. Create all nodes and collect into a map
	TMap<FString, UEdGraphNode*> NodeMap;
	TMap<FString, FString> NodeTypes;
	int32 PosX = 0;
	int32 PosY = 0;

	const TArray<TSharedPtr<FJsonValue>>* NodesArray;
	if (EventGraphJson->TryGetArrayField(TEXT("nodes"), NodesArray))
	{
		for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArray)
		{
			const TSharedPtr<FJsonObject>& NodeJson = NodeValue->AsObject();
			if (!NodeJson.IsValid()) continue;

			FString NodeId;
			if (!NodeJson->TryGetStringField(TEXT("id"), NodeId))
			{
				UE_LOG(LogArborBP, Warning, TEXT("Event graph node missing 'id', skipping"));
				continue;
			}

			FString NodeType;
			NodeJson->TryGetStringField(TEXT("type"), NodeType);

			UEdGraphNode* CreatedNode = CreateNodeFromJson(Blueprint, Graph, NodeJson, PosX, PosY);
			if (CreatedNode)
			{
				NodeMap.Add(NodeId, CreatedNode);
				if (!NodeType.IsEmpty())
				{
					NodeTypes.Add(NodeId, NodeType);
				}
			}
		}
	}

	// 2. Layout nodes based on exec flow
	const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray = nullptr;
	if (EventGraphJson->TryGetArrayField(TEXT("connections"), ConnectionsArray))
	{
		LayoutEventGraph(NodeMap, NodeTypes, *ConnectionsArray, Graph);
	}

	// 3. Wire connections
	int32 NumConnections = 0;
	if (ConnectionsArray)
	{
		NumConnections = ConnectionsArray->Num();
		WireConnections(Graph, *ConnectionsArray, NodeMap);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	UE_LOG(LogArborBP, Log, TEXT("Added event graph: %d nodes, %d connections"),
		NodeMap.Num(), NumConnections);
}

// ============================================================================
// Event Graph — Node Factory
// ============================================================================

UEdGraphNode* UBlueprintBuilder::CreateNodeFromJson(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	const TSharedPtr<FJsonObject>& NodeJson,
	int32& PosX,
	int32& PosY)
{
	FString NodeType;
	if (!NodeJson->TryGetStringField(TEXT("type"), NodeType))
	{
		UE_LOG(LogArborBP, Warning, TEXT("Event graph node missing 'type', skipping"));
		return nullptr;
	}

	UEdGraphNode* Node = nullptr;

	if (NodeType == TEXT("Event"))
	{
		Node = CreateEventNode(Blueprint, Graph, NodeJson, PosX, PosY);
	}
	else if (NodeType == TEXT("ComponentEvent") || NodeType == TEXT("ComponentBoundEvent"))
	{
		Node = CreateComponentEventNode(Blueprint, Graph, NodeJson, PosX, PosY);
	}
	else if (NodeType == TEXT("CallFunction"))
	{
		Node = CreateCallFunctionNode(Blueprint, Graph, NodeJson, PosX, PosY);
	}
	else if (NodeType == TEXT("VariableGet"))
	{
		Node = CreateVariableGetNode(Blueprint, Graph, NodeJson, PosX, PosY);
	}
	else if (NodeType == TEXT("VariableSet"))
	{
		Node = CreateVariableSetNode(Blueprint, Graph, NodeJson, PosX, PosY);
	}
	else if (NodeType == TEXT("Branch"))
	{
		Node = CreateBranchNode(Graph, PosX, PosY);
	}
	else if (NodeType == TEXT("Timeline"))
	{
		Node = CreateTimelineNode(Blueprint, Graph, NodeJson, PosX, PosY);
	}
	else if (NodeType == TEXT("CastTo") || NodeType == TEXT("Cast"))
	{
		Node = CreateCastNode(Blueprint, Graph, NodeJson, PosX, PosY);
	}
	else if (NodeType == TEXT("FormatText"))
	{
		// UK2Node_FormatText generates one argument pin per {placeholder} in the format string,
		// so set the Format text and let the node regenerate its arg pins.
		UK2Node_FormatText* FmtNode = NewObject<UK2Node_FormatText>(Graph);
		Graph->AddNode(FmtNode, false, false);
		FmtNode->AllocateDefaultPins();
		FmtNode->NodePosX = PosX;
		FmtNode->NodePosY = PosY;
		FString Format;
		if (NodeJson->TryGetStringField(TEXT("format"), Format))
		{
			if (UEdGraphPin* FormatPin = FmtNode->GetFormatPin())
			{
				FormatPin->DefaultTextValue = FText::FromString(Format);
				FmtNode->PinDefaultValueChanged(FormatPin);
			}
		}
		Node = FmtNode;
	}
	else
	{
		// Generic fallback: treat type as a UE5 class name (e.g. "UK2Node_ExecutionSequence")
		Node = CreateGenericNode(Blueprint, Graph, NodeJson, NodeType, PosX, PosY);
		if (!Node)
		{
			UE_LOG(LogArborBP, Warning, TEXT("Unknown event graph node type: %s"), *NodeType);
		}
	}

	// Ensure the node has a valid GUID (AddNode with bFromUI=false does not call CreateNewGuid)
	if (Node)
	{
		Node->CreateNewGuid();
	}

	// Advance position for the next node
	PosY += 300;

	return Node;
}

// ============================================================================
// Event Graph — Event Node (BeginPlay, Tick, etc.)
// ============================================================================

UEdGraphNode* UBlueprintBuilder::CreateEventNode(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	const TSharedPtr<FJsonObject>& NodeJson,
	int32 PosX, int32 PosY)
{
	FString EventName;
	if (!NodeJson->TryGetStringField(TEXT("event"), EventName))
	{
		UE_LOG(LogArborBP, Warning, TEXT("Event node missing 'event' field"));
		return nullptr;
	}

	// Find the event function on AActor (or parent class)
	UClass* ParentClass = Blueprint->ParentClass ? Blueprint->ParentClass.Get() : AActor::StaticClass();
	UFunction* EventFunc = ParentClass->FindFunctionByName(FName(*EventName));

	// Try common event name mappings
	if (!EventFunc && EventName == TEXT("BeginPlay"))
	{
		EventFunc = AActor::StaticClass()->FindFunctionByName(FName(TEXT("ReceiveBeginPlay")));
	}
	if (!EventFunc && EventName == TEXT("Tick"))
	{
		EventFunc = AActor::StaticClass()->FindFunctionByName(FName(TEXT("ReceiveTick")));
	}
	if (!EventFunc && EventName == TEXT("EndPlay"))
	{
		EventFunc = AActor::StaticClass()->FindFunctionByName(FName(TEXT("ReceiveEndPlay")));
	}
	if (!EventFunc && EventName == TEXT("OnPossess"))
	{
		// AController exposes the BP-overridable "On Possess" event (display name)
		// as UFUNCTION(BlueprintImplementableEvent) void ReceivePossess(APawn*).
		// See Engine/Source/Runtime/Engine/Classes/GameFramework/Controller.h.
		EventFunc = ParentClass->FindFunctionByName(FName(TEXT("ReceivePossess")));
	}

	if (!EventFunc)
	{
		UE_LOG(LogArborBP, Error, TEXT("Could not find event function: %s"), *EventName);
		return nullptr;
	}

	UK2Node_Event* EventNode = NewObject<UK2Node_Event>(Graph);
	EventNode->EventReference.SetExternalMember(EventFunc->GetFName(), ParentClass);
	EventNode->bOverrideFunction = true;
	Graph->AddNode(EventNode, false, false);
	EventNode->AllocateDefaultPins();
	EventNode->NodePosX = PosX;
	EventNode->NodePosY = PosY;

	UE_LOG(LogArborBP, Verbose, TEXT("Created event node: %s"), *EventName);
	return EventNode;
}

// ============================================================================
// Event Graph — Component Bound Event (Overlap, Hit, etc.)
// ============================================================================

UEdGraphNode* UBlueprintBuilder::CreateComponentEventNode(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	const TSharedPtr<FJsonObject>& NodeJson,
	int32 PosX, int32 PosY)
{
	FString ComponentName, EventName;
	if (!NodeJson->TryGetStringField(TEXT("component"), ComponentName))
	{
		UE_LOG(LogArborBP, Warning,
			TEXT("ComponentEvent node missing 'component' field"));
		return nullptr;
	}
	// Accept both "event" and "delegate" as the delegate property field name
	if (!NodeJson->TryGetStringField(TEXT("event"), EventName) &&
		!NodeJson->TryGetStringField(TEXT("delegate"), EventName))
	{
		UE_LOG(LogArborBP, Warning,
			TEXT("ComponentEvent node missing 'event' or 'delegate' field"));
		return nullptr;
	}

	// Find the SCS node to get the component class and variable name
	USCS_Node* SCSNode = nullptr;
	if (Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->GetVariableName().ToString() == ComponentName)
			{
				SCSNode = Node;
				break;
			}
		}
	}

	if (!SCSNode)
	{
		UE_LOG(LogArborBP, Error,
			TEXT("Component '%s' not found in SCS for event '%s'"),
			*ComponentName, *EventName);
		return nullptr;
	}

	UClass* ComponentClass = SCSNode->ComponentClass;
	if (!ComponentClass)
	{
		UE_LOG(LogArborBP, Error,
			TEXT("Component '%s' has no class"), *ComponentName);
		return nullptr;
	}

	// Find the multicast delegate property on the component class
	FMulticastDelegateProperty* DelegateProp = nullptr;
	for (TFieldIterator<FMulticastDelegateProperty> It(ComponentClass); It; ++It)
	{
		if (It->GetName() == EventName)
		{
			DelegateProp = *It;
			break;
		}
	}

	if (!DelegateProp)
	{
		UE_LOG(LogArborBP, Error,
			TEXT("Delegate '%s' not found on component class '%s'"),
			*EventName, *ComponentClass->GetName());
		return nullptr;
	}

	if (!DelegateProp->SignatureFunction)
	{
		UE_LOG(LogArborBP, Error,
			TEXT("Delegate '%s' has no signature function"), *EventName);
		return nullptr;
	}

	UK2Node_ComponentBoundEvent* EventNode =
		NewObject<UK2Node_ComponentBoundEvent>(Graph);
	EventNode->ComponentPropertyName = SCSNode->GetVariableName();
	EventNode->DelegatePropertyName = DelegateProp->GetFName();
	EventNode->DelegateOwnerClass = ComponentClass;
	EventNode->EventReference.SetExternalDelegateMember(DelegateProp->SignatureFunction->GetFName());

	Graph->AddNode(EventNode, false, false);
	EventNode->AllocateDefaultPins();
	EventNode->NodePosX = PosX;
	EventNode->NodePosY = PosY;

	UE_LOG(LogArborBP, Verbose, TEXT("Created component event: %s on %s"),
		*EventName, *ComponentName);
	return EventNode;
}

// ============================================================================
// Event Graph — Call Function Node
// ============================================================================

UEdGraphNode* UBlueprintBuilder::CreateCallFunctionNode(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	const TSharedPtr<FJsonObject>& NodeJson,
	int32 PosX, int32 PosY)
{
	FString FunctionName;
	if (!NodeJson->TryGetStringField(TEXT("function"), FunctionName))
	{
		UE_LOG(LogArborBP, Warning, TEXT("CallFunction node missing 'function' field"));
		return nullptr;
	}

	// Parse target early so we can use it for function resolution
	FString TargetName;
	NodeJson->TryGetStringField(TEXT("target"), TargetName);

	FString OwnerClassName;
	NodeJson->TryGetStringField(TEXT("owner_class"), OwnerClassName);

	UFunction* Function = ResolveFunctionByName(FunctionName, Blueprint, TargetName, OwnerClassName);
	if (!Function)
	{
		UE_LOG(LogArborBP, Error, TEXT("Could not resolve function: %s"), *FunctionName);
		return nullptr;
	}

	UK2Node_CallFunction* FuncNode = NewObject<UK2Node_CallFunction>(Graph);
	FuncNode->SetFromFunction(Function);
	Graph->AddNode(FuncNode, false, false);
	FuncNode->AllocateDefaultPins();
	FuncNode->NodePosX = PosX;
	FuncNode->NodePosY = PosY;

	// Wire target component if specified
	if (!TargetName.IsEmpty())
	{
		UEdGraphPin* SelfPin = FuncNode->FindPin(UEdGraphSchema_K2::PN_Self);
		if (SelfPin)
		{
			UEdGraphPin* CompOutPin = CreateComponentRefNode(
				Blueprint, Graph, TargetName, PosX - 200, PosY + 80);
			if (CompOutPin)
			{
				CompOutPin->MakeLinkTo(SelfPin);
			}
		}
	}

	// Apply default values from "defaults" object
	const TSharedPtr<FJsonObject>* DefaultsJson;
	if (NodeJson->TryGetObjectField(TEXT("defaults"), DefaultsJson))
	{
		for (const auto& Pair : (*DefaultsJson)->Values)
		{
			UEdGraphPin* ParamPin = FuncNode->FindPin(FName(*Pair.Key));
			if (ParamPin)
			{
				SetPinDefaultOrCreateLiteral(Blueprint, Graph, ParamPin, Pair.Value,
					FuncNode->NodePosX, FuncNode->NodePosY);
			}
			else
			{
				UE_LOG(LogArborBP, Warning,
					TEXT("Pin '%s' not found on function '%s'"),
					*Pair.Key, *FunctionName);
			}
		}
	}

	UE_LOG(LogArborBP, Verbose, TEXT("Created CallFunction node: %s"), *FunctionName);
	return FuncNode;
}

// ============================================================================
// Event Graph — Variable Get Node
// ============================================================================

UEdGraphNode* UBlueprintBuilder::CreateVariableGetNode(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	const TSharedPtr<FJsonObject>& NodeJson,
	int32 PosX, int32 PosY)
{
	FString VarName;
	if (!NodeJson->TryGetStringField(TEXT("variable"), VarName))
	{
		UE_LOG(LogArborBP, Warning, TEXT("VariableGet node missing 'variable' field"));
		return nullptr;
	}

	UK2Node_VariableGet* VarGetNode = NewObject<UK2Node_VariableGet>(Graph);
	VarGetNode->VariableReference.SetSelfMember(FName(*VarName));
	Graph->AddNode(VarGetNode, false, false);
	VarGetNode->AllocateDefaultPins();
	VarGetNode->NodePosX = PosX;
	VarGetNode->NodePosY = PosY;

	UE_LOG(LogArborBP, Verbose, TEXT("Created VariableGet node: %s"), *VarName);
	return VarGetNode;
}

// ============================================================================
// Event Graph — Variable Set Node
// ============================================================================

UEdGraphNode* UBlueprintBuilder::CreateVariableSetNode(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	const TSharedPtr<FJsonObject>& NodeJson,
	int32 PosX, int32 PosY)
{
	FString VarName;
	if (!NodeJson->TryGetStringField(TEXT("variable"), VarName))
	{
		UE_LOG(LogArborBP, Warning, TEXT("VariableSet node missing 'variable' field"));
		return nullptr;
	}

	UK2Node_VariableSet* VarSetNode = NewObject<UK2Node_VariableSet>(Graph);
	VarSetNode->VariableReference.SetSelfMember(FName(*VarName));
	Graph->AddNode(VarSetNode, false, false);
	VarSetNode->AllocateDefaultPins();
	VarSetNode->NodePosX = PosX;
	VarSetNode->NodePosY = PosY;

	// Apply default value if provided
	const TSharedPtr<FJsonObject>* DefaultsJson;
	if (NodeJson->TryGetObjectField(TEXT("defaults"), DefaultsJson))
	{
		for (const auto& Pair : (*DefaultsJson)->Values)
		{
			UEdGraphPin* Pin = VarSetNode->FindPin(FName(*Pair.Key));
			if (Pin)
			{
				SetPinDefaultOrCreateLiteral(Blueprint, Graph, Pin, Pair.Value,
					VarSetNode->NodePosX, VarSetNode->NodePosY);
			}
		}
	}

	UE_LOG(LogArborBP, Verbose, TEXT("Created VariableSet node: %s"), *VarName);
	return VarSetNode;
}

// ============================================================================
// Event Graph — Branch Node
// ============================================================================

UEdGraphNode* UBlueprintBuilder::CreateBranchNode(
	UEdGraph* Graph,
	int32 PosX, int32 PosY)
{
	UK2Node_IfThenElse* BranchNode = NewObject<UK2Node_IfThenElse>(Graph);
	Graph->AddNode(BranchNode, false, false);
	BranchNode->AllocateDefaultPins();
	BranchNode->NodePosX = PosX;
	BranchNode->NodePosY = PosY;

	UE_LOG(LogArborBP, Verbose, TEXT("Created Branch node"));
	return BranchNode;
}

// ============================================================================
// Event Graph — Timeline Node
// ============================================================================

UEdGraphNode* UBlueprintBuilder::CreateTimelineNode(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	const TSharedPtr<FJsonObject>& NodeJson,
	int32 PosX, int32 PosY)
{
	FString TimelineName;
	if (!NodeJson->TryGetStringField(TEXT("timeline"), TimelineName))
	{
		UE_LOG(LogArborBP, Warning, TEXT("Timeline node missing 'timeline' field"));
		return nullptr;
	}

	// Verify the timeline template exists on the blueprint
	bool bFound = false;
	for (UTimelineTemplate* TL : Blueprint->Timelines)
	{
		if (TL && TL->GetVariableName() == FName(*TimelineName))
		{
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		UE_LOG(LogArborBP, Warning,
			TEXT("Timeline template '%s' not found on blueprint — node may not compile"),
			*TimelineName);
	}

	UK2Node_Timeline* TimelineNode = NewObject<UK2Node_Timeline>(Graph);
	TimelineNode->TimelineName = FName(*TimelineName);
	Graph->AddNode(TimelineNode, false, false);
	TimelineNode->AllocateDefaultPins();
	TimelineNode->NodePosX = PosX;
	TimelineNode->NodePosY = PosY;

	UE_LOG(LogArborBP, Verbose, TEXT("Created Timeline node: %s"), *TimelineName);
	return TimelineNode;
}

// ============================================================================
// Event Graph — Wire Connections
// ============================================================================

void UBlueprintBuilder::WireConnections(
	UEdGraph* Graph,
	const TArray<TSharedPtr<FJsonValue>>& ConnectionsJson,
	const TMap<FString, UEdGraphNode*>& NodeMap)
{
	for (const TSharedPtr<FJsonValue>& ConnValue : ConnectionsJson)
	{
		const TSharedPtr<FJsonObject>& ConnJson = ConnValue->AsObject();
		if (!ConnJson.IsValid()) continue;

		FString FromId, FromPinName, ToId, ToPinName;
		if (!ConnJson->TryGetStringField(TEXT("from"), FromId) ||
			!ConnJson->TryGetStringField(TEXT("from_pin"), FromPinName) ||
			!ConnJson->TryGetStringField(TEXT("to"), ToId) ||
			!ConnJson->TryGetStringField(TEXT("to_pin"), ToPinName))
		{
			UE_LOG(LogArborBP, Warning,
				TEXT("Connection missing required fields (from, from_pin, to, to_pin), skipping"));
			continue;
		}

		UEdGraphNode* const* FromNodePtr = NodeMap.Find(FromId);
		UEdGraphNode* const* ToNodePtr = NodeMap.Find(ToId);

		if (!FromNodePtr || !*FromNodePtr)
		{
			UE_LOG(LogArborBP, Warning, TEXT("Connection: source node '%s' not found"), *FromId);
			continue;
		}
		if (!ToNodePtr || !*ToNodePtr)
		{
			UE_LOG(LogArborBP, Warning, TEXT("Connection: target node '%s' not found"), *ToId);
			continue;
		}

		UEdGraphPin* FromPin = (*FromNodePtr)->FindPin(FName(*FromPinName), EGPD_Output);
		UEdGraphPin* ToPin = (*ToNodePtr)->FindPin(FName(*ToPinName), EGPD_Input);

		// Fallback: search without direction constraint
		if (!FromPin)
		{
			FromPin = (*FromNodePtr)->FindPin(FName(*FromPinName));
		}
		if (!ToPin)
		{
			ToPin = (*ToNodePtr)->FindPin(FName(*ToPinName));
		}

		if (!FromPin)
		{
			UE_LOG(LogArborBP, Warning,
				TEXT("Connection: pin '%s' not found on node '%s'"), *FromPinName, *FromId);
			continue;
		}
		if (!ToPin)
		{
			UE_LOG(LogArborBP, Warning,
				TEXT("Connection: pin '%s' not found on node '%s'"), *ToPinName, *ToId);
			continue;
		}

		const UEdGraphSchema* Schema = Graph->GetSchema();
		if (Schema && !Schema->TryCreateConnection(FromPin, ToPin))
		{
			// Fallback to raw link if schema rejects (e.g. exec pins)
			FromPin->MakeLinkTo(ToPin);
		}
		UE_LOG(LogArborBP, Verbose, TEXT("Wired: %s.%s → %s.%s"),
			*FromId, *FromPinName, *ToId, *ToPinName);
	}
}

// ============================================================================
// Event Graph — Class Resolution Helper
// ============================================================================

static UClass* ResolveClassByName(const FString& ClassName)
{
	// Content path (e.g. /Game/BP/BP_Player) — load as Blueprint
	if (ClassName.StartsWith(TEXT("/")))
	{
		UBlueprint* BP = Cast<UBlueprint>(
			StaticLoadObject(UBlueprint::StaticClass(), nullptr, *ClassName));
		if (!BP && !ClassName.Contains(TEXT(".")))
		{
			FString AssetName = FPaths::GetBaseFilename(ClassName);
			BP = Cast<UBlueprint>(
				StaticLoadObject(UBlueprint::StaticClass(), nullptr,
					*(ClassName + TEXT(".") + AssetName)));
		}
		if (BP && BP->GeneratedClass)
		{
			return BP->GeneratedClass;
		}

		// Try _C suffix for compiled class
		FString AssetName = FPaths::GetBaseFilename(ClassName);
		UClass* Cls = StaticLoadClass(UObject::StaticClass(), nullptr,
			*(ClassName + TEXT(".") + AssetName + TEXT("_C")));
		if (Cls) return Cls;
	}

	// Iterate all loaded classes — matches by name with optional A/U prefix
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Candidate = *It;
		const FString& CandidateName = Candidate->GetName();
		if (CandidateName == ClassName ||
			CandidateName == (TEXT("A") + ClassName) ||
			CandidateName == (TEXT("U") + ClassName))
		{
			return Candidate;
		}
	}

	return nullptr;
}

// ============================================================================
// Event Graph — Function Resolution
// ============================================================================

UFunction* UBlueprintBuilder::ResolveFunctionByName(
	const FString& FunctionName,
	UBlueprint* Blueprint,
	const FString& TargetComponentName,
	const FString& OwnerClassName)
{
	FName FuncFName(*FunctionName);

	// If owner_class is specified, resolve it and search there first
	if (!OwnerClassName.IsEmpty())
	{
		UClass* OwnerClass = ResolveClassByName(OwnerClassName);
		if (OwnerClass)
		{
			UFunction* Func = OwnerClass->FindFunctionByName(FuncFName);
			if (Func)
			{
				UE_LOG(LogArborBP, Log, TEXT("Resolved '%s' from owner_class '%s'"),
					*FunctionName, *OwnerClass->GetName());
				return Func;
			}
			UE_LOG(LogArborBP, Warning, TEXT("Function '%s' not found on owner_class '%s'"),
				*FunctionName, *OwnerClass->GetName());
		}
		else
		{
			UE_LOG(LogArborBP, Warning, TEXT("Could not resolve owner_class '%s'"),
				*OwnerClassName);
		}
	}

	// Search across common classes in priority order
	static const UClass* SearchClasses[] = {
		USceneComponent::StaticClass(),
		UPrimitiveComponent::StaticClass(),
		UStaticMeshComponent::StaticClass(),
		USkeletalMeshComponent::StaticClass(),
		ULightComponent::StaticClass(),
		AActor::StaticClass(),
		UKismetSystemLibrary::StaticClass(),
		UKismetMathLibrary::StaticClass(),
		UGameplayStatics::StaticClass(),
	};

	for (const UClass* SearchClass : SearchClasses)
	{
		UFunction* Func = SearchClass->FindFunctionByName(FuncFName);
		if (Func)
		{
			return Func;
		}
	}

	// Search the Blueprint's parent class hierarchy (e.g. AAIController for
	// RunBehaviorTree, ACharacter for LaunchCharacter, etc.).  Skip AActor
	// since it's already in the static list above.
	if (Blueprint && Blueprint->ParentClass)
	{
		for (UClass* ParentClass = Blueprint->ParentClass;
			ParentClass && ParentClass != AActor::StaticClass();
			ParentClass = ParentClass->GetSuperClass())
		{
			UFunction* Func = ParentClass->FindFunctionByName(FuncFName);
			if (Func)
			{
				UE_LOG(LogArborBP, Log,
					TEXT("Resolved '%s' from parent class hierarchy (%s)"),
					*FunctionName, *ParentClass->GetName());
				return Func;
			}
		}
	}

	// Fallback: resolve from the target component's actual class in SCS
	if (Blueprint && !TargetComponentName.IsEmpty() && Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->GetVariableName().ToString() == TargetComponentName)
			{
				UClass* CompClass = Node->ComponentClass;
				if (CompClass)
				{
					UFunction* Func = CompClass->FindFunctionByName(FuncFName);
					if (Func)
					{
						UE_LOG(LogArborBP, Verbose,
							TEXT("Resolved '%s' from SCS component '%s' (class %s)"),
							*FunctionName, *TargetComponentName, *CompClass->GetName());
						return Func;
					}
				}
				break;
			}
		}
	}

	// Global fallback: search ALL loaded classes for this function name
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UFunction* Func = It->FindFunctionByName(FuncFName, EIncludeSuperFlag::ExcludeSuper);
		if (Func)
		{
			UE_LOG(LogArborBP, Log,
				TEXT("Resolved '%s' via global class search (%s)"),
				*FunctionName, *It->GetName());
			return Func;
		}
	}

	UE_LOG(LogArborBP, Warning,
		TEXT("Function '%s' not found in any loaded class"), *FunctionName);
	return nullptr;
}

// ============================================================================
// Event Graph — Component Reference Node
// ============================================================================

UEdGraphPin* UBlueprintBuilder::CreateComponentRefNode(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	const FString& ComponentName,
	int32 PosX, int32 PosY)
{
	// Find the SCS variable name for this component
	FName VarName(*ComponentName);
	if (Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->GetVariableName().ToString() == ComponentName)
			{
				VarName = Node->GetVariableName();
				break;
			}
		}
	}

	UK2Node_VariableGet* VarGetNode = NewObject<UK2Node_VariableGet>(Graph);
	VarGetNode->VariableReference.SetSelfMember(VarName);
	Graph->AddNode(VarGetNode, false, false);
	VarGetNode->CreateNewGuid();
	VarGetNode->AllocateDefaultPins();
	VarGetNode->NodePosX = PosX;
	VarGetNode->NodePosY = PosY;

	// Return the value output pin
	UEdGraphPin* ValuePin = VarGetNode->GetValuePin();
	return ValuePin;
}

// ============================================================================
// Event Graph — Cast Node (CastTo type)
// ============================================================================

UEdGraphNode* UBlueprintBuilder::CreateCastNode(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	const TSharedPtr<FJsonObject>& NodeJson,
	int32 PosX, int32 PosY)
{
	FString ClassName;
	if (!NodeJson->TryGetStringField(TEXT("class"), ClassName))
	{
		UE_LOG(LogArborBP, Error, TEXT("CastTo node missing 'class' field"));
		return nullptr;
	}

	UClass* TargetClass = ResolveClassByName(ClassName);
	if (!TargetClass)
	{
		UE_LOG(LogArborBP, Error, TEXT("CastTo: could not resolve class '%s'"), *ClassName);
		return nullptr;
	}

	UK2Node_DynamicCast* CastNode = NewObject<UK2Node_DynamicCast>(Graph);
	CastNode->TargetType = TargetClass;
	Graph->AddNode(CastNode, false, false);
	CastNode->AllocateDefaultPins();
	CastNode->NodePosX = PosX;
	CastNode->NodePosY = PosY;

	UE_LOG(LogArborBP, Log, TEXT("Created CastTo node targeting '%s'"), *TargetClass->GetName());
	return CastNode;
}

// ============================================================================
// Event Graph — Generic Node (any UK2Node subclass by class name)
// ============================================================================

UEdGraphNode* UBlueprintBuilder::CreateGenericNode(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	const TSharedPtr<FJsonObject>& NodeJson,
	const FString& NodeType,
	int32 PosX, int32 PosY)
{
	// Resolve the node class by name. TryFindTypeSlow finds already-loaded native classes in
	// ANY module (BlueprintGraph, UMGEditor, ...) - StaticLoadClass misses compiled-in classes,
	// which is why editor K2Nodes like UK2Node_CreateWidget previously failed to resolve.
	FString ClassName = NodeType;
	UClass* NodeClass = UClass::TryFindTypeSlow<UClass>(ClassName);
	if (!NodeClass && !ClassName.StartsWith(TEXT("K2Node_")) && !ClassName.StartsWith(TEXT("UK2Node_")))
	{
		// Reflected class names drop the U prefix, so K2 nodes register as "K2Node_<X>".
		NodeClass = UClass::TryFindTypeSlow<UClass>(TEXT("K2Node_") + ClassName);
		if (!NodeClass)
		{
			NodeClass = UClass::TryFindTypeSlow<UClass>(TEXT("UK2Node_") + ClassName);
		}
	}
	if (!NodeClass)
	{
		NodeClass = FindObject<UClass>(static_cast<UObject*>(nullptr), *ClassName);
	}

	if (!NodeClass)
	{
		// Try with /Script/BlueprintGraph prefix (where most K2 nodes live)
		NodeClass = StaticLoadClass(
			UEdGraphNode::StaticClass(), nullptr,
			*FString::Printf(TEXT("/Script/BlueprintGraph.%s"), *ClassName));
	}

	if (!NodeClass)
	{
		// Try /Script/UnrealEd
		NodeClass = StaticLoadClass(
			UEdGraphNode::StaticClass(), nullptr,
			*FString::Printf(TEXT("/Script/UnrealEd.%s"), *ClassName));
	}

	if (!NodeClass)
	{
		// Try /Script/Engine
		NodeClass = StaticLoadClass(
			UEdGraphNode::StaticClass(), nullptr,
			*FString::Printf(TEXT("/Script/Engine.%s"), *ClassName));
	}

	if (!NodeClass)
	{
		// Try adding UK2Node_ prefix if not already present
		if (!ClassName.StartsWith(TEXT("UK2Node_")) && !ClassName.StartsWith(TEXT("K2Node_")))
		{
			FString WithPrefix = TEXT("UK2Node_") + ClassName;
			NodeClass = FindObject<UClass>(static_cast<UObject*>(nullptr), *WithPrefix);
			if (!NodeClass)
			{
				NodeClass = StaticLoadClass(
					UEdGraphNode::StaticClass(), nullptr,
					*FString::Printf(TEXT("/Script/BlueprintGraph.%s"), *WithPrefix));
			}
		}
	}

	if (!NodeClass || !NodeClass->IsChildOf(UEdGraphNode::StaticClass()))
	{
		UE_LOG(LogArborBP, Warning,
			TEXT("Generic node: could not find UEdGraphNode subclass '%s'"), *NodeType);
		return nullptr;
	}

	// Create the node
	UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, NodeClass);

	// Apply properties BEFORE AllocateDefaultPins — some nodes generate pins
	// based on their properties (e.g. Cast nodes need TargetType set first).
	const TSharedPtr<FJsonObject>* PropertiesJson;
	if (NodeJson->TryGetObjectField(TEXT("properties"), PropertiesJson))
	{
		for (const auto& Pair : (*PropertiesJson)->Values)
		{
			SetPropertyFromJson(Node, FString(*Pair.Key), Pair.Value);
		}
	}

	Graph->AddNode(Node, false, false);
	Node->AllocateDefaultPins();
	Node->NodePosX = PosX;
	Node->NodePosY = PosY;

	// Apply pin default values AFTER AllocateDefaultPins
	const TSharedPtr<FJsonObject>* DefaultsJson;
	if (NodeJson->TryGetObjectField(TEXT("defaults"), DefaultsJson))
	{
		for (const auto& Pair : (*DefaultsJson)->Values)
		{
			UEdGraphPin* Pin = Node->FindPin(FName(*Pair.Key));
			if (Pin)
			{
				SetPinDefaultOrCreateLiteral(Blueprint, Graph, Pin, Pair.Value,
					Node->NodePosX, Node->NodePosY);
			}
			else
			{
				UE_LOG(LogArborBP, Warning,
					TEXT("Generic node '%s': pin '%s' not found"),
					*NodeType, *Pair.Key);
			}
		}
	}

	// Construct-from-class nodes (e.g. UK2Node_CreateWidget) expose extra pins once their
	// Class pin is set - nudge them to regenerate now that defaults are applied.
	if (UEdGraphPin* ClassPin = Node->FindPin(TEXT("Class")))
	{
		if (ClassPin->DefaultObject != nullptr)
		{
			Node->PinDefaultValueChanged(ClassPin);
		}
	}

	UE_LOG(LogArborBP, Log,
		TEXT("Created generic node: %s (class: %s)"),
		*NodeType, *NodeClass->GetName());
	return Node;
}

// ============================================================================
// Event Graph — Pin Default Values
// ============================================================================

UObject* UBlueprintBuilder::ResolveObjectForPin(UEdGraphPin* Pin, const FString& AssetPath)
{
	if (!Pin || AssetPath.IsEmpty()) return nullptr;

	FName PinCategory = Pin->PinType.PinCategory;

	// Object / Interface pins — load the asset directly
	if (PinCategory == UEdGraphSchema_K2::PC_Object ||
		PinCategory == UEdGraphSchema_K2::PC_Interface)
	{
		UObject* LoadedObj = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
		if (!LoadedObj && !AssetPath.Contains(TEXT(".")))
		{
			FString AssetName = FPaths::GetBaseFilename(AssetPath);
			LoadedObj = StaticLoadObject(UObject::StaticClass(), nullptr,
				*(AssetPath + TEXT(".") + AssetName));
		}
		return LoadedObj;
	}

	// Class / SoftClass pins — resolve to UClass*
	if (PinCategory == UEdGraphSchema_K2::PC_Class ||
		PinCategory == UEdGraphSchema_K2::PC_SoftClass)
	{
		UClass* LoadedClass = StaticLoadClass(UObject::StaticClass(), nullptr, *AssetPath);

		// Try _C suffix (e.g. /Game/AI/ABP_Ghost → ABP_Ghost_C)
		if (!LoadedClass && !AssetPath.EndsWith(TEXT("_C")))
		{
			FString AssetName = FPaths::GetBaseFilename(AssetPath);
			LoadedClass = StaticLoadClass(UObject::StaticClass(), nullptr,
				*(AssetPath + TEXT(".") + AssetName + TEXT("_C")));
		}

		// Try loading a Blueprint asset and getting its GeneratedClass
		if (!LoadedClass)
		{
			UBlueprint* BP = Cast<UBlueprint>(
				StaticLoadObject(UBlueprint::StaticClass(), nullptr, *AssetPath));
			if (!BP && !AssetPath.Contains(TEXT(".")))
			{
				FString AssetName = FPaths::GetBaseFilename(AssetPath);
				BP = Cast<UBlueprint>(
					StaticLoadObject(UBlueprint::StaticClass(), nullptr,
						*(AssetPath + TEXT(".") + AssetName)));
			}
			if (BP && BP->GeneratedClass)
			{
				LoadedClass = BP->GeneratedClass;
			}
		}

		return LoadedClass;
	}

	return nullptr;
}

void UBlueprintBuilder::SetPinDefaultFromJson(
	UEdGraphPin* Pin,
	const TSharedPtr<FJsonValue>& JsonValue)
{
	if (!Pin || !JsonValue.IsValid()) return;

	switch (JsonValue->Type)
	{
	case EJson::Boolean:
		Pin->DefaultValue = JsonValue->AsBool() ? TEXT("true") : TEXT("false");
		break;

	case EJson::Number:
	{
		double Num = JsonValue->AsNumber();
		// Use integer format if it's a whole number and the pin expects int
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
		{
			Pin->DefaultValue = FString::FromInt(static_cast<int32>(Num));
		}
		else
		{
			Pin->DefaultValue = FString::SanitizeFloat(Num);
		}
		break;
	}

	case EJson::String:
	{
		FString StringVal = JsonValue->AsString();
		FName PinCategory = Pin->PinType.PinCategory;

		// Object/Interface/Class/SoftClass pins: resolve asset path to UObject*
		if (PinCategory == UEdGraphSchema_K2::PC_Object ||
			PinCategory == UEdGraphSchema_K2::PC_Interface ||
			PinCategory == UEdGraphSchema_K2::PC_Class ||
			PinCategory == UEdGraphSchema_K2::PC_SoftClass)
		{
			UObject* Resolved = ResolveObjectForPin(Pin, StringVal);
			if (Resolved)
			{
				Pin->DefaultObject = Resolved;
				UE_LOG(LogArborBP, Log,
					TEXT("Resolved object pin '%s' default to '%s'"),
					*Pin->GetName(), *Resolved->GetPathName());
			}
			else
			{
				UE_LOG(LogArborBP, Warning,
					TEXT("Could not load object '%s' for pin '%s'"),
					*StringVal, *Pin->GetName());
			}
		}
		else
		{
			// SoftObject, String, Name, and all other string-valued pins
			Pin->DefaultValue = StringVal;
		}
		break;
	}

	case EJson::Object:
	{
		const TSharedPtr<FJsonObject>& Obj = JsonValue->AsObject();
		if (!Obj.IsValid()) break;

		// FVector: {X, Y, Z}
		if (Obj->HasField(TEXT("X")) && Obj->HasField(TEXT("Y")) && Obj->HasField(TEXT("Z")))
		{
			double X = Obj->GetNumberField(TEXT("X"));
			double Y = Obj->GetNumberField(TEXT("Y"));
			double Z = Obj->GetNumberField(TEXT("Z"));
			Pin->DefaultValue = FString::Printf(TEXT("%f,%f,%f"), X, Y, Z);
		}
		// FRotator: {Pitch, Yaw, Roll}
		else if (Obj->HasField(TEXT("Pitch")) && Obj->HasField(TEXT("Yaw")))
		{
			double Pitch = Obj->GetNumberField(TEXT("Pitch"));
			double Yaw = Obj->GetNumberField(TEXT("Yaw"));
			double Roll = Obj->HasField(TEXT("Roll")) ? Obj->GetNumberField(TEXT("Roll")) : 0.0;
			Pin->DefaultValue = FString::Printf(TEXT("%f,%f,%f"), Pitch, Yaw, Roll);
		}
		break;
	}

	default:
		UE_LOG(LogArborBP, Warning,
			TEXT("Unsupported JSON value type for pin '%s'"), *Pin->GetName());
		break;
	}
}

// ============================================================================
// Granular Blueprint Editing — Helpers
// ============================================================================

UBlueprint* UBlueprintBuilder::LoadBlueprintForEditing(const FString& AssetPath)
{
	// AssetPath can be "/Game/BP/BP_Wolf" or "/Game/BP/BP_Wolf.BP_Wolf"
	FString ObjectPath = AssetPath;
	if (!ObjectPath.Contains(TEXT(".")))
	{
		FString Name = FPaths::GetCleanFilename(AssetPath);
		ObjectPath = AssetPath + TEXT(".") + Name;
	}

	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *ObjectPath);
	if (!BP)
	{
		UE_LOG(LogArborBP, Warning, TEXT("LoadBlueprintForEditing: Could not load '%s'"), *ObjectPath);
	}
	return BP;
}

UEdGraph* UBlueprintBuilder::GetEventGraph(UBlueprint* Blueprint, bool bCreateIfMissing)
{
	if (!Blueprint) return nullptr;

	if (Blueprint->UbergraphPages.Num() > 0)
	{
		return Blueprint->UbergraphPages[0];
	}

	if (bCreateIfMissing)
	{
		UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(
			Blueprint,
			UEdGraphSchema_K2::GN_EventGraph,
			UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass());
		Blueprint->UbergraphPages.Add(Graph);
		return Graph;
	}

	return nullptr;
}

UEdGraphNode* UBlueprintBuilder::FindNodeByGuid(UEdGraph* Graph, const FString& GuidString)
{
	if (!Graph) return nullptr;

	FGuid TargetGuid;
	if (!FGuid::Parse(GuidString, TargetGuid))
	{
		UE_LOG(LogArborBP, Warning, TEXT("FindNodeByGuid: Invalid GUID format '%s'"), *GuidString);
		return nullptr;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->NodeGuid == TargetGuid)
		{
			return Node;
		}
	}
	return nullptr;
}

UEdGraphNode* UBlueprintBuilder::FindNodeInAllGraphs(
	UBlueprint* Blueprint, const FString& GuidString, UEdGraph** OutGraph)
{
	if (!Blueprint) return nullptr;

	FGuid TargetGuid;
	if (!FGuid::Parse(GuidString, TargetGuid))
	{
		UE_LOG(LogArborBP, Warning, TEXT("FindNodeInAllGraphs: Invalid GUID format '%s'"), *GuidString);
		return nullptr;
	}

	// Search UbergraphPages (event graphs)
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == TargetGuid)
			{
				if (OutGraph) *OutGraph = Graph;
				return Node;
			}
		}
	}

	// Search FunctionGraphs (includes AnimGraph for AnimBlueprints)
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == TargetGuid)
			{
				if (OutGraph) *OutGraph = Graph;
				return Node;
			}
		}
	}

	return nullptr;
}

bool UBlueprintBuilder::IsHiddenComponentRefNode(UEdGraphNode* Node)
{
	UK2Node_VariableGet* VarGet = Cast<UK2Node_VariableGet>(Node);
	if (!VarGet) return false;

	// Check if any output pin is linked to a Self pin
	for (UEdGraphPin* Pin : VarGet->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output)
		{
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin && LinkedPin->GetFName() == UEdGraphSchema_K2::PN_Self)
				{
					return true;
				}
			}
		}
	}
	return false;
}

bool UBlueprintBuilder::IsHiddenLiteralNode(UEdGraphNode* Node)
{
	UK2Node_CallFunction* FuncNode = Cast<UK2Node_CallFunction>(Node);
	if (!FuncNode) return false;

	UFunction* Func = FuncNode->GetTargetFunction();
	if (!Func) return false;

	FName FuncName = Func->GetFName();
	if (FuncName != TEXT("MakeLiteralName") &&
		FuncName != TEXT("MakeLiteralString") &&
		FuncName != TEXT("MakeLiteralText"))
	{
		return false;
	}

	// Only hide if ReturnValue has exactly 1 link to a by-ref input pin
	UEdGraphPin* ReturnPin = FuncNode->FindPin(TEXT("ReturnValue"), EGPD_Output);
	if (!ReturnPin || ReturnPin->LinkedTo.Num() != 1) return false;

	UEdGraphPin* LinkedPin = ReturnPin->LinkedTo[0];
	return LinkedPin && LinkedPin->Direction == EGPD_Input && LinkedPin->PinType.bIsReference;
}

bool UBlueprintBuilder::CreateLiteralNodeForByRefPin(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	UEdGraphPin* TargetPin,
	const FString& Value,
	int32 PosX, int32 PosY)
{
	if (!Blueprint || !Graph || !TargetPin) return false;
	if (!TargetPin->PinType.bIsReference) return false;
	if (TargetPin->Direction != EGPD_Input) return false;

	// Map pin category to literal function name
	FString LiteralFuncName;
	FName PinCategory = TargetPin->PinType.PinCategory;
	if (PinCategory == UEdGraphSchema_K2::PC_Name)
	{
		LiteralFuncName = TEXT("MakeLiteralName");
	}
	else if (PinCategory == UEdGraphSchema_K2::PC_String)
	{
		LiteralFuncName = TEXT("MakeLiteralString");
	}
	else if (PinCategory == UEdGraphSchema_K2::PC_Text)
	{
		LiteralFuncName = TEXT("MakeLiteralText");
	}
	else
	{
		return false;
	}

	// Clean up any existing literal helper already wired to this pin
	for (int32 i = TargetPin->LinkedTo.Num() - 1; i >= 0; --i)
	{
		UEdGraphPin* LinkedPin = TargetPin->LinkedTo[i];
		if (LinkedPin && IsHiddenLiteralNode(LinkedPin->GetOwningNode()))
		{
			UEdGraphNode* OldLiteral = LinkedPin->GetOwningNode();
			OldLiteral->BreakAllNodeLinks();
			Graph->RemoveNode(OldLiteral);
		}
	}

	// Resolve the literal function
	UFunction* Function = ResolveFunctionByName(LiteralFuncName);
	if (!Function)
	{
		UE_LOG(LogArborBP, Warning,
			TEXT("CreateLiteralNodeForByRefPin: Could not resolve function '%s'"),
			*LiteralFuncName);
		return false;
	}

	// Create the literal node
	UK2Node_CallFunction* LiteralNode = NewObject<UK2Node_CallFunction>(Graph);
	LiteralNode->SetFromFunction(Function);
	Graph->AddNode(LiteralNode, false, false);
	LiteralNode->CreateNewGuid();
	LiteralNode->AllocateDefaultPins();
	LiteralNode->NodePosX = PosX - 250;
	LiteralNode->NodePosY = PosY;

	// Set the Value input pin
	UEdGraphPin* ValuePin = LiteralNode->FindPin(TEXT("Value"), EGPD_Input);
	if (ValuePin)
	{
		ValuePin->DefaultValue = Value;
	}
	else
	{
		UE_LOG(LogArborBP, Warning,
			TEXT("CreateLiteralNodeForByRefPin: 'Value' pin not found on %s"),
			*LiteralFuncName);
	}

	// Wire ReturnValue → TargetPin
	UEdGraphPin* ReturnPin = LiteralNode->FindPin(TEXT("ReturnValue"), EGPD_Output);
	if (ReturnPin)
	{
		const UEdGraphSchema* Schema = Graph->GetSchema();
		if (Schema && !Schema->TryCreateConnection(ReturnPin, TargetPin))
		{
			ReturnPin->MakeLinkTo(TargetPin);
		}
	}
	else
	{
		UE_LOG(LogArborBP, Warning,
			TEXT("CreateLiteralNodeForByRefPin: 'ReturnValue' pin not found on %s"),
			*LiteralFuncName);
		return false;
	}

	UE_LOG(LogArborBP, Log,
		TEXT("Auto-created %s node for by-ref pin '%s' with value '%s'"),
		*LiteralFuncName, *TargetPin->GetName(), *Value);
	return true;
}

void UBlueprintBuilder::SetPinDefaultOrCreateLiteral(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	UEdGraphPin* Pin,
	const TSharedPtr<FJsonValue>& JsonValue,
	int32 NodePosX, int32 NodePosY)
{
	if (!Pin || !JsonValue.IsValid()) return;

	// Check if this is a by-ref FName/FString/FText pin with a string value
	if (Pin->PinType.bIsReference && Pin->Direction == EGPD_Input)
	{
		FName PinCategory = Pin->PinType.PinCategory;
		if (PinCategory == UEdGraphSchema_K2::PC_Name ||
			PinCategory == UEdGraphSchema_K2::PC_String ||
			PinCategory == UEdGraphSchema_K2::PC_Text)
		{
			FString StringValue;
			if (JsonValue->Type == EJson::String)
			{
				StringValue = JsonValue->AsString();
			}
			else
			{
				// Convert non-string JSON to string representation
				StringValue = JsonValue->AsString();
			}

			if (CreateLiteralNodeForByRefPin(Blueprint, Graph, Pin, StringValue,
				NodePosX, NodePosY))
			{
				return;
			}
		}
	}

	// Fall through to normal pin default
	SetPinDefaultFromJson(Pin, JsonValue);
}

void UBlueprintBuilder::CompileAndSave(UBlueprint* Blueprint)
{
	if (!Blueprint) return;
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAsset(Blueprint);
}

void UBlueprintBuilder::CollectCompileDiagnostics(
	UBlueprint* Blueprint,
	TArray<FString>& OutErrors,
	TArray<FString>& OutWarnings)
{
	if (!Blueprint) return;

	// Collect from both UbergraphPages (event graphs) and FunctionGraphs (includes AnimGraph)
	TArray<UEdGraph*> AllGraphs;
	for (UEdGraph* G : Blueprint->UbergraphPages) { if (G) AllGraphs.Add(G); }
	for (UEdGraph* G : Blueprint->FunctionGraphs) { if (G) AllGraphs.Add(G); }

	for (UEdGraph* Graph : AllGraphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->bHasCompilerMessage)
			{
				FString Msg = Node->ErrorMsg;
				Msg.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
				Msg.ReplaceInline(TEXT("\""), TEXT("\\\""));
				Msg.ReplaceInline(TEXT("\n"), TEXT("\\n"));
				Msg.ReplaceInline(TEXT("\r"), TEXT(""));

				if (Node->ErrorType <= EMessageSeverity::Error)
				{
					OutErrors.Add(Msg);
				}
				else
				{
					OutWarnings.Add(Msg);
				}
			}
		}
	}
}

FString UBlueprintBuilder::FormatCompileDiagnosticsJson(
	const TArray<FString>& Errors,
	const TArray<FString>& Warnings)
{
	FString Json = TEXT(",\"compile_errors\":[");
	for (int32 i = 0; i < Errors.Num(); i++)
	{
		if (i > 0) Json += TEXT(",");
		Json += FString::Printf(TEXT("\"%s\""), *Errors[i]);
	}
	Json += TEXT("],\"compile_warnings\":[");
	for (int32 i = 0; i < Warnings.Num(); i++)
	{
		if (i > 0) Json += TEXT(",");
		Json += FString::Printf(TEXT("\"%s\""), *Warnings[i]);
	}
	Json += TEXT("]");
	return Json;
}

// ============================================================================
// Granular Blueprint Editing — SerializePinsToJsonArray
// ============================================================================

TArray<TSharedPtr<FJsonValue>> UBlueprintBuilder::SerializePinsToJsonArray(UEdGraphNode* Node)
{
	TArray<TSharedPtr<FJsonValue>> PinsArray;
	if (!Node) return PinsArray;

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->bHidden) continue;

		auto PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("name"), Pin->GetName());
		PinObj->SetStringField(TEXT("direction"),
			Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
		PinObj->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());

		if (!Pin->DefaultValue.IsEmpty() && Pin->LinkedTo.Num() == 0)
		{
			PinObj->SetStringField(TEXT("default"), Pin->DefaultValue);
		}
		if (Pin->LinkedTo.Num() > 0)
		{
			PinObj->SetBoolField(TEXT("connected"), true);
		}

		PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
	}
	return PinsArray;
}

// ============================================================================
// Granular Blueprint Editing — SerializeNodeToJson
// ============================================================================

TSharedPtr<FJsonObject> UBlueprintBuilder::SerializeNodeToJson(UEdGraphNode* Node)
{
	auto Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("guid"), Node->NodeGuid.ToString());

	// Position
	TArray<TSharedPtr<FJsonValue>> PosArray;
	PosArray.Add(MakeShared<FJsonValueNumber>(Node->NodePosX));
	PosArray.Add(MakeShared<FJsonValueNumber>(Node->NodePosY));
	Obj->SetArrayField(TEXT("position"), PosArray);

	// Type detection
	if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
	{
		Obj->SetStringField(TEXT("type"), TEXT("Event"));
		Obj->SetStringField(TEXT("event"), EventNode->EventReference.GetMemberName().ToString());
	}
	else if (UK2Node_ComponentBoundEvent* CompEvent = Cast<UK2Node_ComponentBoundEvent>(Node))
	{
		Obj->SetStringField(TEXT("type"), TEXT("ComponentEvent"));
		Obj->SetStringField(TEXT("component"), CompEvent->ComponentPropertyName.ToString());
		Obj->SetStringField(TEXT("event"), CompEvent->DelegatePropertyName.ToString());
	}
	else if (UK2Node_CallFunction* FuncNode = Cast<UK2Node_CallFunction>(Node))
	{
		Obj->SetStringField(TEXT("type"), TEXT("CallFunction"));
		Obj->SetStringField(TEXT("function"), FuncNode->FunctionReference.GetMemberName().ToString());

		// Check for target component (Self pin linked to a component ref)
		UEdGraphPin* SelfPin = FuncNode->FindPin(UEdGraphSchema_K2::PN_Self);
		if (SelfPin && SelfPin->LinkedTo.Num() > 0)
		{
			UEdGraphPin* LinkedPin = SelfPin->LinkedTo[0];
			if (LinkedPin)
			{
				UK2Node_VariableGet* RefNode = Cast<UK2Node_VariableGet>(LinkedPin->GetOwningNode());
				if (RefNode)
				{
					Obj->SetStringField(TEXT("target"),
						RefNode->VariableReference.GetMemberName().ToString());
				}
			}
		}
	}
	else if (UK2Node_VariableGet* VarGet = Cast<UK2Node_VariableGet>(Node))
	{
		Obj->SetStringField(TEXT("type"), TEXT("VariableGet"));
		Obj->SetStringField(TEXT("variable"), VarGet->VariableReference.GetMemberName().ToString());
	}
	else if (UK2Node_VariableSet* VarSet = Cast<UK2Node_VariableSet>(Node))
	{
		Obj->SetStringField(TEXT("type"), TEXT("VariableSet"));
		Obj->SetStringField(TEXT("variable"), VarSet->VariableReference.GetMemberName().ToString());
	}
	else if (Cast<UK2Node_IfThenElse>(Node))
	{
		Obj->SetStringField(TEXT("type"), TEXT("Branch"));
	}
	else if (UK2Node_Timeline* TLNode = Cast<UK2Node_Timeline>(Node))
	{
		Obj->SetStringField(TEXT("type"), TEXT("Timeline"));
		Obj->SetStringField(TEXT("timeline"), TLNode->TimelineName.ToString());
	}
	else
	{
		// Generic node — use class name
		Obj->SetStringField(TEXT("type"), Node->GetClass()->GetName());
	}

	// Pin defaults (non-empty input data pins without links)
	auto DefaultsObj = MakeShared<FJsonObject>();
	bool bHasDefaults = false;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input &&
			!Pin->DefaultValue.IsEmpty() &&
			Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec &&
			Pin->LinkedTo.Num() == 0)
		{
			DefaultsObj->SetStringField(Pin->GetName(), Pin->DefaultValue);
			bHasDefaults = true;
		}
	}
	if (bHasDefaults)
	{
		Obj->SetObjectField(TEXT("defaults"), DefaultsObj);
	}

	// Pins listing
	Obj->SetArrayField(TEXT("pins"), SerializePinsToJsonArray(Node));

	return Obj;
}

// ============================================================================
// Granular Blueprint Editing — QueryBlueprint
// ============================================================================

FString UBlueprintBuilder::QueryBlueprint(const FString& AssetPath)
{
	UBlueprint* Blueprint = LoadBlueprintForEditing(AssetPath);
	if (!Blueprint)
	{
		return TEXT("{\"success\":false,\"error\":\"Blueprint not found\"}");
	}

	auto Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("name"), Blueprint->GetName());
	Root->SetStringField(TEXT("parent_class"),
		Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("None"));

	// Components — SCS (BP-added) components
	TArray<TSharedPtr<FJsonValue>> ComponentsArray;
	TSet<FString> SeenComponentNames;
	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	if (SCS)
	{
		for (USCS_Node* SCSNode : SCS->GetAllNodes())
		{
			if (!SCSNode || !SCSNode->ComponentTemplate) continue;

			FString VarName = SCSNode->GetVariableName().ToString();
			auto CompObj = MakeShared<FJsonObject>();
			CompObj->SetStringField(TEXT("name"), VarName);
			CompObj->SetStringField(TEXT("class"), SCSNode->ComponentTemplate->GetClass()->GetName());
			ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
			SeenComponentNames.Add(VarName);
		}
	}

	// Components — inherited C++ components (from parent class CDO)
	if (Blueprint->GeneratedClass)
	{
		UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
		if (CDO)
		{
			TArray<UObject*> DefaultSubObjects;
			CDO->GetDefaultSubobjects(DefaultSubObjects);
			for (UObject* SubObj : DefaultSubObjects)
			{
				if (!SubObj || !SubObj->IsA(UActorComponent::StaticClass())) continue;
				FString SubName = SubObj->GetName();
				if (SeenComponentNames.Contains(SubName)) continue;

				auto CompObj = MakeShared<FJsonObject>();
				CompObj->SetStringField(TEXT("name"), SubName);
				CompObj->SetStringField(TEXT("class"), SubObj->GetClass()->GetName());
				CompObj->SetBoolField(TEXT("inherited"), true);
				ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
			}
		}
	}
	Root->SetArrayField(TEXT("components"), ComponentsArray);

	// Variables
	TArray<TSharedPtr<FJsonValue>> VariablesArray;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		auto VarObj = MakeShared<FJsonObject>();
		VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
		if (!Var.DefaultValue.IsEmpty())
		{
			VarObj->SetStringField(TEXT("default"), Var.DefaultValue);
		}
		VariablesArray.Add(MakeShared<FJsonValueObject>(VarObj));
	}
	Root->SetArrayField(TEXT("variables"), VariablesArray);

	// Event graph
	UEdGraph* Graph = GetEventGraph(Blueprint);
	if (Graph)
	{
		auto EventGraphObj = MakeShared<FJsonObject>();

		// Build GUID lookup for connection serialization
		TMap<UEdGraphNode*, FString> NodeGuids;

		// Nodes
		TArray<TSharedPtr<FJsonValue>> NodesArray;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node || IsHiddenComponentRefNode(Node) || IsHiddenLiteralNode(Node)) continue;

			TSharedPtr<FJsonObject> NodeJson = SerializeNodeToJson(Node);
			if (NodeJson.IsValid())
			{
				NodeGuids.Add(Node, Node->NodeGuid.ToString());
				NodesArray.Add(MakeShared<FJsonValueObject>(NodeJson));
			}
		}
		EventGraphObj->SetArrayField(TEXT("nodes"), NodesArray);

		// Connections
		TArray<TSharedPtr<FJsonValue>> ConnectionsArray;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node || IsHiddenComponentRefNode(Node) || IsHiddenLiteralNode(Node)) continue;

			const FString* FromGuid = NodeGuids.Find(Node);
			if (!FromGuid) continue;

			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output) continue;

				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin) continue;
					UEdGraphNode* TargetNode = LinkedPin->GetOwningNode();
					if (!TargetNode || IsHiddenComponentRefNode(TargetNode) || IsHiddenLiteralNode(TargetNode)) continue;

					const FString* ToGuid = NodeGuids.Find(TargetNode);
					if (!ToGuid) continue;

					auto ConnObj = MakeShared<FJsonObject>();
					ConnObj->SetStringField(TEXT("from"), *FromGuid);
					ConnObj->SetStringField(TEXT("from_pin"), Pin->GetName());
					ConnObj->SetStringField(TEXT("to"), *ToGuid);
					ConnObj->SetStringField(TEXT("to_pin"), LinkedPin->GetName());
					ConnectionsArray.Add(MakeShared<FJsonValueObject>(ConnObj));
				}
			}
		}
		EventGraphObj->SetArrayField(TEXT("connections"), ConnectionsArray);

		Root->SetObjectField(TEXT("event_graph"), EventGraphObj);
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Root, Writer);
	return OutputString;
}

// ============================================================================
// Granular Blueprint Editing — AddEventGraphNode
// ============================================================================

FString UBlueprintBuilder::AddEventGraphNode(const FString& AssetPath, const FString& NodeJsonString)
{
	UBlueprint* Blueprint = LoadBlueprintForEditing(AssetPath);
	if (!Blueprint)
	{
		return TEXT("{\"success\":false,\"error\":\"Blueprint not found\"}");
	}

	UEdGraph* Graph = GetEventGraph(Blueprint, true);
	if (!Graph)
	{
		return TEXT("{\"success\":false,\"error\":\"Could not get event graph\"}");
	}

	// Parse node JSON
	TSharedPtr<FJsonObject> NodeJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(NodeJsonString);
	if (!FJsonSerializer::Deserialize(Reader, NodeJson) || !NodeJson.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	// Position: use explicit or auto-compute
	int32 PosX = 0;
	int32 PosY = 0;

	const TArray<TSharedPtr<FJsonValue>>* PosArray;
	if (NodeJson->TryGetArrayField(TEXT("position"), PosArray) && PosArray->Num() >= 2)
	{
		PosX = FMath::RoundToInt((*PosArray)[0]->AsNumber());
		PosY = FMath::RoundToInt((*PosArray)[1]->AsNumber());
	}
	else
	{
		// Auto-position: find rightmost node and place to the right
		for (UEdGraphNode* Existing : Graph->Nodes)
		{
			if (Existing)
			{
				PosX = FMath::Max(PosX, Existing->NodePosX);
			}
		}
		PosX += 400;
	}

	UEdGraphNode* CreatedNode = CreateNodeFromJson(Blueprint, Graph, NodeJson, PosX, PosY);
	if (!CreatedNode)
	{
		return TEXT("{\"success\":false,\"error\":\"Failed to create node\"}");
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	CompileAndSave(Blueprint);

	TArray<FString> CompErrors, CompWarnings;
	CollectCompileDiagnostics(Blueprint, CompErrors, CompWarnings);

	FString Result = FString::Printf(
		TEXT("{\"success\":true,\"node_guid\":\"%s\"%s}"),
		*CreatedNode->NodeGuid.ToString(),
		*FormatCompileDiagnosticsJson(CompErrors, CompWarnings));
	return Result;
}

// ============================================================================
// Granular Blueprint Editing — GetNodePins (introspection without modifying assets)
// ============================================================================

FString UBlueprintBuilder::GetNodePins(const FString& NodeJsonString, const FString& ContextAssetPath)
{
	// Parse node spec JSON
	TSharedPtr<FJsonObject> NodeJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(NodeJsonString);
	if (!FJsonSerializer::Deserialize(Reader, NodeJson) || !NodeJson.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	// Determine which Blueprint to use for context
	UBlueprint* Blueprint = nullptr;
	bool bCreatedTransientBP = false;

	if (!ContextAssetPath.IsEmpty())
	{
		Blueprint = LoadBlueprintForEditing(ContextAssetPath);
		if (!Blueprint)
		{
			return TEXT("{\"success\":false,\"error\":\"Context Blueprint not found\"}");
		}
	}
	else
	{
		// Create a transient Blueprint for node instantiation (unique name to avoid assert on repeated calls)
		static int32 TempCounter = 0;
		FName TempName = *FString::Printf(TEXT("TEMP_PinIntrospection_%d"), TempCounter++);
		Blueprint = FKismetEditorUtilities::CreateBlueprint(
			AActor::StaticClass(),
			GetTransientPackage(),
			TempName,
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass());
		if (!Blueprint)
		{
			return TEXT("{\"success\":false,\"error\":\"Failed to create transient Blueprint\"}");
		}
		bCreatedTransientBP = true;
	}

	// Create a transient graph for the temporary node. Parent it to the Blueprint (not the
	// transient package) so nodes can resolve GetBlueprint() during AllocateDefaultPins -
	// member-function nodes on UMG/other classes dereference it and crash if it's null.
	UEdGraph* TempGraph = NewObject<UEdGraph>(Blueprint, NAME_None, RF_Transient);
	TempGraph->Schema = UEdGraphSchema_K2::StaticClass();

	// Create the node using existing infrastructure
	int32 PosX = 0;
	int32 PosY = 0;
	UEdGraphNode* CreatedNode = CreateNodeFromJson(Blueprint, TempGraph, NodeJson, PosX, PosY);

	if (!CreatedNode)
	{
		return TEXT("{\"success\":false,\"error\":\"Failed to create node — check node type and parameters\"}");
	}

	// Serialize pins
	TArray<TSharedPtr<FJsonValue>> PinsArray = SerializePinsToJsonArray(CreatedNode);

	// Determine node type string for the response
	FString NodeType = NodeJson->GetStringField(TEXT("type"));

	// Build result JSON
	auto Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("node_type"), NodeType);
	Root->SetArrayField(TEXT("pins"), PinsArray);

	FString OutputString;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
	FJsonSerializer::Serialize(Root, Writer);

	UE_LOG(LogArborBP, Log, TEXT("GetNodePins: introspected '%s' — %d pins"), *NodeType, PinsArray.Num());

	return OutputString;
}

// ============================================================================
// Granular Blueprint Editing — RemoveEventGraphNode
// ============================================================================

FString UBlueprintBuilder::RemoveEventGraphNode(const FString& AssetPath, const FString& NodeGuidString)
{
	UBlueprint* Blueprint = LoadBlueprintForEditing(AssetPath);
	if (!Blueprint)
	{
		return TEXT("{\"success\":false,\"error\":\"Blueprint not found\"}");
	}

	UEdGraph* Graph = nullptr;
	UEdGraphNode* TargetNode = FindNodeInAllGraphs(Blueprint, NodeGuidString, &Graph);
	if (!TargetNode)
	{
		return TEXT("{\"success\":false,\"error\":\"Node not found\"}");
	}

	// Collect hidden component ref nodes that only link to this node
	TArray<UEdGraphNode*> NodesToRemove;
	NodesToRemove.Add(TargetNode);

	for (UEdGraphPin* Pin : TargetNode->Pins)
	{
		if (!Pin) continue;
		for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			if (!LinkedPin) continue;
			UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
			if (LinkedNode && (IsHiddenComponentRefNode(LinkedNode) || IsHiddenLiteralNode(LinkedNode)))
			{
				// Check if this ref node's outputs ONLY go to the target node
				bool bOnlyLinkedToTarget = true;
				for (UEdGraphPin* RefPin : LinkedNode->Pins)
				{
					if (RefPin && RefPin->Direction == EGPD_Output)
					{
						for (UEdGraphPin* RefLinked : RefPin->LinkedTo)
						{
							if (RefLinked && RefLinked->GetOwningNode() != TargetNode)
							{
								bOnlyLinkedToTarget = false;
								break;
							}
						}
					}
					if (!bOnlyLinkedToTarget) break;
				}
				if (bOnlyLinkedToTarget)
				{
					NodesToRemove.AddUnique(LinkedNode);
				}
			}
		}
	}

	for (UEdGraphNode* Node : NodesToRemove)
	{
		Node->BreakAllNodeLinks();
		Graph->RemoveNode(Node);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	CompileAndSave(Blueprint);

	TArray<FString> CompErrors, CompWarnings;
	CollectCompileDiagnostics(Blueprint, CompErrors, CompWarnings);

	FString Result = FString::Printf(
		TEXT("{\"success\":true,\"removed_count\":%d%s}"),
		NodesToRemove.Num(),
		*FormatCompileDiagnosticsJson(CompErrors, CompWarnings));
	return Result;
}

// ============================================================================
// Granular Blueprint Editing — SetPinDefault
// ============================================================================

FString UBlueprintBuilder::SetPinDefault(
	const FString& AssetPath, const FString& NodeGuidString,
	const FString& PinName, const FString& DefaultValue)
{
	UBlueprint* Blueprint = LoadBlueprintForEditing(AssetPath);
	if (!Blueprint)
	{
		return TEXT("{\"success\":false,\"error\":\"Blueprint not found\"}");
	}

	UEdGraph* Graph = nullptr;
	UEdGraphNode* Node = FindNodeInAllGraphs(Blueprint, NodeGuidString, &Graph);
	if (!Node)
	{
		return TEXT("{\"success\":false,\"error\":\"Node not found\"}");
	}

	UEdGraphPin* Pin = Node->FindPin(FName(*PinName), EGPD_Input);
	if (!Pin)
	{
		Pin = Node->FindPin(FName(*PinName));
	}
	if (!Pin)
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"Pin '%s' not found\"}"), *PinName);
	}

	if (CreateLiteralNodeForByRefPin(Blueprint, Graph, Pin, DefaultValue,
		Node->NodePosX, Node->NodePosY))
	{
		// By-ref pin handled via auto-created literal node
	}
	else
	{
		FName PinCategory = Pin->PinType.PinCategory;

		// Object/Interface/Class/SoftClass pins: resolve asset path to UObject*
		if (PinCategory == UEdGraphSchema_K2::PC_Object ||
			PinCategory == UEdGraphSchema_K2::PC_Interface ||
			PinCategory == UEdGraphSchema_K2::PC_Class ||
			PinCategory == UEdGraphSchema_K2::PC_SoftClass)
		{
			UObject* Resolved = ResolveObjectForPin(Pin, DefaultValue);
			if (Resolved)
			{
				Pin->DefaultObject = Resolved;
			}
			else
			{
				return FString::Printf(
					TEXT("{\"success\":false,\"error\":\"Could not load object '%s' for pin '%s'\"}"),
					*DefaultValue, *PinName);
			}
		}
		else
		{
			Pin->DefaultValue = DefaultValue;
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	CompileAndSave(Blueprint);

	TArray<FString> CompErrors, CompWarnings;
	CollectCompileDiagnostics(Blueprint, CompErrors, CompWarnings);
	return FString::Printf(TEXT("{\"success\":true%s}"),
		*FormatCompileDiagnosticsJson(CompErrors, CompWarnings));
}

// ============================================================================
// Granular Blueprint Editing — ConnectPins
// ============================================================================

FString UBlueprintBuilder::ConnectPins(
	const FString& AssetPath,
	const FString& FromNodeGuid, const FString& FromPinName,
	const FString& ToNodeGuid, const FString& ToPinName)
{
	UBlueprint* Blueprint = LoadBlueprintForEditing(AssetPath);
	if (!Blueprint)
	{
		return TEXT("{\"success\":false,\"error\":\"Blueprint not found\"}");
	}

	UEdGraph* FromGraph = nullptr;
	UEdGraph* ToGraph = nullptr;
	UEdGraphNode* FromNode = FindNodeInAllGraphs(Blueprint, FromNodeGuid, &FromGraph);
	UEdGraphNode* ToNode = FindNodeInAllGraphs(Blueprint, ToNodeGuid, &ToGraph);
	if (!FromNode)
	{
		return TEXT("{\"success\":false,\"error\":\"Source node not found\"}");
	}
	if (!ToNode)
	{
		return TEXT("{\"success\":false,\"error\":\"Target node not found\"}");
	}
	if (FromGraph != ToGraph)
	{
		return TEXT("{\"success\":false,\"error\":\"Cannot connect pins across different graphs\"}");
	}
	UEdGraph* Graph = FromGraph;

	// Find pins with direction-aware fallback
	UEdGraphPin* FromPin = FromNode->FindPin(FName(*FromPinName), EGPD_Output);
	if (!FromPin) FromPin = FromNode->FindPin(FName(*FromPinName));

	UEdGraphPin* ToPin = ToNode->FindPin(FName(*ToPinName), EGPD_Input);
	if (!ToPin) ToPin = ToNode->FindPin(FName(*ToPinName));

	if (!FromPin)
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"Pin '%s' not found on source node\"}"), *FromPinName);
	}
	if (!ToPin)
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"Pin '%s' not found on target node\"}"), *ToPinName);
	}

	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (Schema && !Schema->TryCreateConnection(FromPin, ToPin))
	{
		// Fallback to raw link if schema rejects (e.g. exec pins)
		FromPin->MakeLinkTo(ToPin);
	}
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	CompileAndSave(Blueprint);

	TArray<FString> CompErrors, CompWarnings;
	CollectCompileDiagnostics(Blueprint, CompErrors, CompWarnings);
	return FString::Printf(TEXT("{\"success\":true%s}"),
		*FormatCompileDiagnosticsJson(CompErrors, CompWarnings));
}

// ============================================================================
// Granular Blueprint Editing — DisconnectPin
// ============================================================================

FString UBlueprintBuilder::DisconnectPin(
	const FString& AssetPath,
	const FString& NodeGuidString, const FString& PinName)
{
	UBlueprint* Blueprint = LoadBlueprintForEditing(AssetPath);
	if (!Blueprint)
	{
		return TEXT("{\"success\":false,\"error\":\"Blueprint not found\"}");
	}

	UEdGraphNode* Node = FindNodeInAllGraphs(Blueprint, NodeGuidString);
	if (!Node)
	{
		return TEXT("{\"success\":false,\"error\":\"Node not found\"}");
	}

	UEdGraphPin* Pin = Node->FindPin(FName(*PinName));
	if (!Pin)
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"Pin '%s' not found\"}"), *PinName);
	}

	Pin->BreakAllPinLinks();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	CompileAndSave(Blueprint);

	TArray<FString> CompErrors, CompWarnings;
	CollectCompileDiagnostics(Blueprint, CompErrors, CompWarnings);
	return FString::Printf(TEXT("{\"success\":true%s}"),
		*FormatCompileDiagnosticsJson(CompErrors, CompWarnings));
}

// ============================================================================
// Granular Blueprint Editing — CompileAndSaveBlueprint
// ============================================================================

FString UBlueprintBuilder::CompileAndSaveBlueprint(const FString& AssetPath)
{
	UBlueprint* Blueprint = LoadBlueprintForEditing(AssetPath);
	if (!Blueprint)
	{
		return TEXT("{\"success\":false,\"error\":\"Blueprint not found\"}");
	}

	CompileAndSave(Blueprint);

	TArray<FString> Errors, Warnings;
	CollectCompileDiagnostics(Blueprint, Errors, Warnings);

	bool bSuccess = (Blueprint->Status != BS_Error);
	FString Result = FString::Printf(TEXT("{\"success\":%s"),
		bSuccess ? TEXT("true") : TEXT("false"));
	Result += FormatCompileDiagnosticsJson(Errors, Warnings);
	Result += TEXT("}");
	return Result;
}

// ============================================================================
// Granular Component Editing — AddSCSComponent
// ============================================================================

FString UBlueprintBuilder::AddSCSComponent(const FString& AssetPath, const FString& ComponentJsonString)
{
	UBlueprint* Blueprint = LoadBlueprintForEditing(AssetPath);
	if (!Blueprint)
	{
		return TEXT("{\"success\":false,\"error\":\"Blueprint not found\"}");
	}

	// Parse JSON
	TSharedPtr<FJsonObject> CompJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ComponentJsonString);
	if (!FJsonSerializer::Deserialize(Reader, CompJson) || !CompJson.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	// Validate required fields
	FString CompName;
	if (!CompJson->TryGetStringField(TEXT("name"), CompName))
	{
		return TEXT("{\"success\":false,\"error\":\"Missing 'name' field\"}");
	}

	FString CompType;
	if (!CompJson->TryGetStringField(TEXT("type"), CompType))
	{
		return TEXT("{\"success\":false,\"error\":\"Missing 'type' field\"}");
	}

	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	if (!SCS)
	{
		return TEXT("{\"success\":false,\"error\":\"Blueprint has no SimpleConstructionScript\"}");
	}

	// Check for duplicate name
	for (USCS_Node* Existing : SCS->GetAllNodes())
	{
		if (Existing && Existing->GetVariableName().ToString() == CompName)
		{
			return FString::Printf(
				TEXT("{\"success\":false,\"error\":\"Component '%s' already exists\"}"), *CompName);
		}
	}

	// Reuse the existing private AddComponent which handles inherited detection,
	// class resolution, SCS node creation, and property application
	TArray<TPair<FString, TSharedPtr<FJsonObject>>> InheritedOverrides;
	USCS_Node* NewNode = AddComponent(Blueprint, CompJson, InheritedOverrides);

	if (InheritedOverrides.Num() > 0)
	{
		// Inherited component — compile first, then apply properties on CDO
		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		if (Blueprint->GeneratedClass)
		{
			UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
			if (CDO)
			{
				for (const auto& Override : InheritedOverrides)
				{
					TArray<UObject*> DefaultSubObjects;
					CDO->GetDefaultSubobjects(DefaultSubObjects);
					for (UObject* SubObj : DefaultSubObjects)
					{
						if (SubObj && SubObj->GetName().Contains(Override.Key))
						{
							ApplyComponentProperties(SubObj, Override.Value);

							// Propagate to placed instances (same as SetComponentProperty)
							if (SubObj->HasAnyFlags(RF_ArchetypeObject))
							{
								TArray<UObject*> ArchetypeInstances;
								SubObj->GetArchetypeInstances(ArchetypeInstances);
								for (UObject* Instance : ArchetypeInstances)
								{
									UActorComponent* InstanceComp = Cast<UActorComponent>(Instance);
									if (InstanceComp)
									{
										ApplyComponentProperties(InstanceComp, Override.Value);
									}
								}
							}

							break;
						}
					}
				}
				CDO->MarkPackageDirty();
			}
		}

		SaveAsset(Blueprint);

		TArray<FString> CompErrors, CompWarnings;
		CollectCompileDiagnostics(Blueprint, CompErrors, CompWarnings);
		return FString::Printf(
			TEXT("{\"success\":true,\"component_name\":\"%s\",\"inherited\":true%s}"),
			*CompName, *FormatCompileDiagnosticsJson(CompErrors, CompWarnings));
	}

	if (!NewNode)
	{
		return TEXT("{\"success\":false,\"error\":\"Failed to create component\"}");
	}

	FString ResultName = NewNode->GetVariableName().ToString();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	CompileAndSave(Blueprint);

	TArray<FString> CompErrors, CompWarnings;
	CollectCompileDiagnostics(Blueprint, CompErrors, CompWarnings);
	return FString::Printf(
		TEXT("{\"success\":true,\"component_name\":\"%s\"%s}"),
		*ResultName, *FormatCompileDiagnosticsJson(CompErrors, CompWarnings));
}

// ============================================================================
// Granular Component Editing — RemoveSCSComponent
// ============================================================================

FString UBlueprintBuilder::RemoveSCSComponent(const FString& AssetPath, const FString& ComponentName)
{
	UBlueprint* Blueprint = LoadBlueprintForEditing(AssetPath);
	if (!Blueprint)
	{
		return TEXT("{\"success\":false,\"error\":\"Blueprint not found\"}");
	}

	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	if (!SCS)
	{
		return TEXT("{\"success\":false,\"error\":\"Blueprint has no SimpleConstructionScript\"}");
	}

	// Find SCS node by variable name
	USCS_Node* TargetNode = nullptr;
	for (USCS_Node* Node : SCS->GetAllNodes())
	{
		if (Node && Node->GetVariableName().ToString() == ComponentName)
		{
			TargetNode = Node;
			break;
		}
	}

	if (!TargetNode)
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"Component '%s' not found\"}"), *ComponentName);
	}

	// Clean up event graph references
	int32 RemovedGraphNodes = 0;
	UEdGraph* Graph = GetEventGraph(Blueprint);
	if (Graph)
	{
		FName VarName = TargetNode->GetVariableName();
		TArray<UEdGraphNode*> NodesToRemove;

		for (UEdGraphNode* GNode : Graph->Nodes)
		{
			if (!GNode) continue;

			// ComponentBoundEvent nodes referencing this component
			if (UK2Node_ComponentBoundEvent* BoundEvent = Cast<UK2Node_ComponentBoundEvent>(GNode))
			{
				if (BoundEvent->ComponentPropertyName == VarName)
				{
					NodesToRemove.Add(GNode);
					continue;
				}
			}

			// Hidden VariableGet nodes referencing this component
			if (UK2Node_VariableGet* VarGet = Cast<UK2Node_VariableGet>(GNode))
			{
				if (VarGet->VariableReference.GetMemberName() == VarName)
				{
					NodesToRemove.Add(GNode);
					continue;
				}
			}
		}

		for (UEdGraphNode* Node : NodesToRemove)
		{
			Node->BreakAllNodeLinks();
			Graph->RemoveNode(Node);
		}
		RemovedGraphNodes = NodesToRemove.Num();
	}

	SCS->RemoveNode(TargetNode);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	CompileAndSave(Blueprint);

	TArray<FString> CompErrors, CompWarnings;
	CollectCompileDiagnostics(Blueprint, CompErrors, CompWarnings);
	return FString::Printf(
		TEXT("{\"success\":true,\"removed_graph_nodes\":%d%s}"),
		RemovedGraphNodes, *FormatCompileDiagnosticsJson(CompErrors, CompWarnings));
}

// ============================================================================
// Granular Component Editing — SetComponentProperty
// ============================================================================

FString UBlueprintBuilder::SetComponentProperty(
	const FString& AssetPath, const FString& ComponentName, const FString& PropertyJsonString)
{
	UBlueprint* Blueprint = LoadBlueprintForEditing(AssetPath);
	if (!Blueprint)
	{
		return TEXT("{\"success\":false,\"error\":\"Blueprint not found\"}");
	}

	// Parse properties JSON
	TSharedPtr<FJsonObject> PropsJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PropertyJsonString);
	if (!FJsonSerializer::Deserialize(Reader, PropsJson) || !PropsJson.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	// Try SCS (BP-added) components first
	UObject* TargetComponent = nullptr;
	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	if (SCS)
	{
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (Node && Node->GetVariableName().ToString() == ComponentName)
			{
				TargetComponent = Node->ComponentTemplate;
				break;
			}
		}
	}

	// Fallback: inherited C++ components on the CDO
	if (!TargetComponent && Blueprint->GeneratedClass)
	{
		UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
		if (CDO)
		{
			TArray<UObject*> DefaultSubObjects;
			CDO->GetDefaultSubobjects(DefaultSubObjects);

			// 1) Exact name match
			for (UObject* SubObj : DefaultSubObjects)
			{
				if (SubObj && SubObj->GetName() == ComponentName)
				{
					TargetComponent = SubObj;
					break;
				}
			}

			// 2) Substring match (e.g. "Mesh" matches "CharacterMesh0")
			if (!TargetComponent)
			{
				for (UObject* SubObj : DefaultSubObjects)
				{
					if (SubObj && SubObj->GetName().Contains(ComponentName))
					{
						TargetComponent = SubObj;
						break;
					}
				}
			}

			// 3) Property-based lookup: find a UObject property on the CDO class
			//    whose name matches ComponentName and whose value is a component.
			//    Handles cases where the property name differs from the subobject
			//    name (e.g. "CapsuleComponent" property -> "CollisionCylinder" subobject,
			//    "CharacterMovement" property -> "CharMoveComp" subobject).
			if (!TargetComponent)
			{
				FName PropName(*ComponentName);
				FProperty* Prop = CDO->GetClass()->FindPropertyByName(PropName);
				if (!Prop)
				{
					// Try with "Component" suffix stripped/added, or snake_case
					// Just iterate all object properties for a case-insensitive match
					for (TFieldIterator<FObjectPropertyBase> It(CDO->GetClass()); It; ++It)
					{
						if (It->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
						{
							Prop = *It;
							break;
						}
					}
				}
				if (Prop)
				{
					FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop);
					if (ObjProp)
					{
						UObject* Value = ObjProp->GetObjectPropertyValue_InContainer(CDO);
						if (Value && Value->IsA(UActorComponent::StaticClass()))
						{
							TargetComponent = Value;
						}
					}
				}
			}
		}
	}

	if (!TargetComponent)
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"Component '%s' not found\"}"), *ComponentName);
	}

	ApplyComponentProperties(TargetComponent, PropsJson);

	// Propagate inherited (CDO) component changes to already-placed instances.
	// SCS component templates are handled by blueprint reinstancing, but inherited
	// C++ default-subobject components need explicit propagation because the
	// compile/reinstancing path does not automatically push archetype property
	// changes to existing instances in the level.
	if (TargetComponent->HasAnyFlags(RF_ArchetypeObject))
	{
		TArray<UObject*> ArchetypeInstances;
		TargetComponent->GetArchetypeInstances(ArchetypeInstances);
		for (UObject* Instance : ArchetypeInstances)
		{
			UActorComponent* InstanceComp = Cast<UActorComponent>(Instance);
			if (!InstanceComp)
			{
				continue;
			}
			// ApplyComponentProperties handles per-property PreEditChange /
			// PostEditChangeProperty internally, which triggers component
			// side-effects (render state recreation, transform updates, etc.).
			ApplyComponentProperties(InstanceComp, PropsJson);
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	CompileAndSave(Blueprint);

	TArray<FString> CompErrors, CompWarnings;
	CollectCompileDiagnostics(Blueprint, CompErrors, CompWarnings);
	return FString::Printf(TEXT("{\"success\":true%s}"),
		*FormatCompileDiagnosticsJson(CompErrors, CompWarnings));
}
