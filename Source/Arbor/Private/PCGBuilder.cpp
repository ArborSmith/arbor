#include "PCGBuilder.h"

// JSON
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// PCG core
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "PCGComponent.h"
#include "PCGPin.h"
#include "PCGEdge.h"
#include "PCGInputOutputSettings.h"

// Asset infrastructure
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// World / Actor
#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogArborPCG, Log, All);

// ============================================================================
// Main Entry Points
// ============================================================================

UPCGGraph* UPCGBuilder::BuildPCGGraphFromJSON(
	const FString& JsonFilePath, const FString& AssetPath)
{
	TSharedPtr<FJsonObject> JsonRoot;
	if (!LoadAndParseJSON(JsonFilePath, JsonRoot))
	{
		return nullptr;
	}
	return BuildFromParsedJSON(JsonRoot, AssetPath);
}

UPCGGraph* UPCGBuilder::BuildPCGGraphFromJSONString(
	const FString& JsonString, const FString& AssetPath)
{
	TSharedPtr<FJsonObject> JsonRoot;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonRoot) || !JsonRoot.IsValid())
	{
		UE_LOG(LogArborPCG, Error, TEXT("Failed to parse JSON string"));
		return nullptr;
	}
	return BuildFromParsedJSON(JsonRoot, AssetPath);
}

// ============================================================================
// Build from Parsed JSON
// ============================================================================

UPCGGraph* UPCGBuilder::BuildFromParsedJSON(
	const TSharedPtr<FJsonObject>& JsonRoot, const FString& AssetPath)
{
	// 1. Extract graph name
	FString GraphName;
	if (!JsonRoot->TryGetStringField(TEXT("name"), GraphName))
	{
		UE_LOG(LogArborPCG, Error, TEXT("JSON missing 'name' field"));
		return nullptr;
	}

	// 2. Create/load PCG graph asset
	UPCGGraph* Graph = CreatePCGGraphAsset(GraphName, AssetPath);
	if (!Graph)
	{
		UE_LOG(LogArborPCG, Error, TEXT("Failed to create PCG graph asset"));
		return nullptr;
	}

	// 3. Parse nodes array
	const TArray<TSharedPtr<FJsonValue>>* NodesArray;
	if (!JsonRoot->TryGetArrayField(TEXT("nodes"), NodesArray))
	{
		UE_LOG(LogArborPCG, Error, TEXT("JSON missing 'nodes' array"));
		return nullptr;
	}

	// Map user IDs to PCG nodes for wiring
	TMap<FString, UPCGNode*> NodeIdMap;

	// Map "input" and "output" to the graph's built-in Input/Output nodes
	if (Graph->GetInputNode())
	{
		NodeIdMap.Add(TEXT("input"), Graph->GetInputNode());
	}
	if (Graph->GetOutputNode())
	{
		NodeIdMap.Add(TEXT("output"), Graph->GetOutputNode());
	}

	// 4. Create all nodes
	for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArray)
	{
		const TSharedPtr<FJsonObject>& NodeJson = NodeValue->AsObject();
		if (!NodeJson.IsValid())
		{
			continue;
		}

		FString NodeId;
		if (!NodeJson->TryGetStringField(TEXT("id"), NodeId))
		{
			UE_LOG(LogArborPCG, Warning, TEXT("Node missing 'id' field, skipping"));
			continue;
		}

		FString NodeType;
		if (!NodeJson->TryGetStringField(TEXT("type"), NodeType))
		{
			UE_LOG(LogArborPCG, Warning, TEXT("Node '%s' missing 'type' field, skipping"), *NodeId);
			continue;
		}

		// Resolve the settings class
		UClass* SettingsClass = ResolveSettingsClass(NodeType);
		if (!SettingsClass)
		{
			UE_LOG(LogArborPCG, Error, TEXT("Unknown PCG node type: '%s'"), *NodeType);
			continue;
		}

		// Create the settings instance
		UPCGSettings* Settings = NewObject<UPCGSettings>(Graph, SettingsClass);
		if (!Settings)
		{
			UE_LOG(LogArborPCG, Error, TEXT("Failed to create settings for type: '%s'"), *NodeType);
			continue;
		}

		// Apply parameters before adding to graph
		const TSharedPtr<FJsonObject>* ParamsJson;
		if (NodeJson->TryGetObjectField(TEXT("params"), ParamsJson))
		{
			ApplyNodeParams(Settings, *ParamsJson);
		}

		// Add node to graph
		UPCGNode* NewNode = Graph->AddNode(Settings);
		if (!NewNode)
		{
			UE_LOG(LogArborPCG, Error, TEXT("Failed to add node '%s' to graph"), *NodeId);
			continue;
		}

		// Set node title for identification
		NewNode->NodeTitle = FName(*NodeId);

		NodeIdMap.Add(NodeId, NewNode);
		UE_LOG(LogArborPCG, Log, TEXT("  Added node '%s' (type: %s)"), *NodeId, *NodeType);
	}

	// 5. Parse and apply connections
	const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray;
	if (JsonRoot->TryGetArrayField(TEXT("connections"), ConnectionsArray))
	{
		for (const TSharedPtr<FJsonValue>& ConnValue : *ConnectionsArray)
		{
			const TSharedPtr<FJsonObject>& ConnJson = ConnValue->AsObject();
			if (!ConnJson.IsValid())
			{
				continue;
			}

			FString FromId, FromPin, ToId, ToPin;
			if (!ConnJson->TryGetStringField(TEXT("from"), FromId) ||
				!ConnJson->TryGetStringField(TEXT("from_pin"), FromPin) ||
				!ConnJson->TryGetStringField(TEXT("to"), ToId) ||
				!ConnJson->TryGetStringField(TEXT("to_pin"), ToPin))
			{
				UE_LOG(LogArborPCG, Warning, TEXT("Connection missing required fields (from, from_pin, to, to_pin), skipping"));
				continue;
			}

			UPCGNode* FromNode = NodeIdMap.FindRef(FromId);
			UPCGNode* ToNode = NodeIdMap.FindRef(ToId);

			if (!FromNode)
			{
				UE_LOG(LogArborPCG, Warning, TEXT("Connection source node '%s' not found"), *FromId);
				continue;
			}
			if (!ToNode)
			{
				UE_LOG(LogArborPCG, Warning, TEXT("Connection target node '%s' not found"), *ToId);
				continue;
			}

			// Find pins by label
			UPCGPin* OutPin = FromNode->GetOutputPin(FName(*FromPin));
			UPCGPin* InPin = ToNode->GetInputPin(FName(*ToPin));

			if (!OutPin)
			{
				// Try default output pin if label doesn't match
				const TArray<UPCGPin*> OutputPins = FromNode->GetOutputPins();
				if (OutputPins.Num() > 0)
				{
					// List available pin names for debugging
					TArray<FString> PinNames;
					for (UPCGPin* Pin : OutputPins)
					{
						PinNames.Add(Pin->Properties.Label.ToString());
					}
					UE_LOG(LogArborPCG, Warning, TEXT("Output pin '%s' not found on node '%s'. Available: %s"),
						*FromPin, *FromId, *FString::Join(PinNames, TEXT(", ")));
				}
				continue;
			}
			if (!InPin)
			{
				const TArray<UPCGPin*> InputPins = ToNode->GetInputPins();
				if (InputPins.Num() > 0)
				{
					TArray<FString> PinNames;
					for (UPCGPin* Pin : InputPins)
					{
						PinNames.Add(Pin->Properties.Label.ToString());
					}
					UE_LOG(LogArborPCG, Warning, TEXT("Input pin '%s' not found on node '%s'. Available: %s"),
						*ToPin, *ToId, *FString::Join(PinNames, TEXT(", ")));
				}
				continue;
			}

			// Make the connection
			bool bConnected = OutPin->AddEdgeTo(InPin);
			if (bConnected)
			{
				UE_LOG(LogArborPCG, Log, TEXT("  Connected %s:%s -> %s:%s"), *FromId, *FromPin, *ToId, *ToPin);
			}
			else
			{
				UE_LOG(LogArborPCG, Warning, TEXT("  Failed to connect %s:%s -> %s:%s"), *FromId, *FromPin, *ToId, *ToPin);
			}
		}
	}

	// 6. Save asset
	SaveAsset(Graph);

	UE_LOG(LogArborPCG, Log, TEXT("Successfully built PCG graph '%s' at %s"), *GraphName, *AssetPath);
	return Graph;
}

// ============================================================================
// JSON Parsing
// ============================================================================

bool UPCGBuilder::LoadAndParseJSON(
	const FString& JsonFilePath, TSharedPtr<FJsonObject>& OutJsonObject)
{
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
	{
		UE_LOG(LogArborPCG, Error, TEXT("Failed to load JSON file: %s"), *JsonFilePath);
		return false;
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, OutJsonObject) || !OutJsonObject.IsValid())
	{
		UE_LOG(LogArborPCG, Error, TEXT("Failed to parse JSON file: %s"), *JsonFilePath);
		return false;
	}

	return true;
}

// ============================================================================
// Asset Creation
// ============================================================================

UPCGGraph* UPCGBuilder::CreatePCGGraphAsset(const FString& Name, const FString& AssetPath)
{
	const FString PackagePath = AssetPath / Name;
	const FString AssetObjectPath = PackagePath + TEXT(".") + Name;

	// Check if asset already exists — update in place to preserve references
	if (UEditorAssetLibrary::DoesAssetExist(PackagePath))
	{
		UPCGGraph* ExistingGraph = LoadObject<UPCGGraph>(nullptr, *AssetObjectPath);
		if (ExistingGraph)
		{
			// Remove all user-added nodes (keep input/output)
			TArray<UPCGNode*> NodesToRemove;
			for (UPCGNode* Node : ExistingGraph->GetNodes())
			{
				if (Node != ExistingGraph->GetInputNode() && Node != ExistingGraph->GetOutputNode())
				{
					NodesToRemove.Add(Node);
				}
			}
			for (UPCGNode* Node : NodesToRemove)
			{
				ExistingGraph->RemoveNode(Node);
			}

			ExistingGraph->GetPackage()->MarkPackageDirty();
			UE_LOG(LogArborPCG, Log, TEXT("Updating existing PCG graph: %s"), *PackagePath);
			return ExistingGraph;
		}
	}

	// Asset doesn't exist — create new
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		UE_LOG(LogArborPCG, Error, TEXT("Failed to create package: %s"), *PackagePath);
		return nullptr;
	}

	UPCGGraph* Graph = NewObject<UPCGGraph>(
		Package, UPCGGraph::StaticClass(), *Name,
		RF_Public | RF_Standalone | RF_Transactional);

	UE_LOG(LogArborPCG, Log, TEXT("Creating new PCG graph: %s"), *PackagePath);
	return Graph;
}

// ============================================================================
// Class Resolution
// ============================================================================

UClass* UPCGBuilder::ResolveSettingsClass(const FString& NodeType)
{
	// Static map of friendly names to settings class names
	static const TMap<FString, FString> SettingsMap = {
		// Samplers / Generators
		{TEXT("SurfaceSampler"),      TEXT("PCGSurfaceSamplerSettings")},
		{TEXT("VolumeSampler"),       TEXT("PCGVolumeSamplerSettings")},
		{TEXT("GetActorData"),        TEXT("PCGGetActorDataSettings")},
		{TEXT("GetLandscapeData"),    TEXT("PCGGetLandscapeDataSettings")},
		{TEXT("GetSplineData"),       TEXT("PCGGetSplineDataSettings")},

		// Point operations
		{TEXT("TransformPoints"),     TEXT("PCGTransformPointsSettings")},
		{TEXT("CopyPoints"),          TEXT("PCGCopyPointsSettings")},
		{TEXT("CreatePoints"),        TEXT("PCGCreatePointsGridSettings")},
		{TEXT("CreatePointsGrid"),    TEXT("PCGCreatePointsGridSettings")},
		{TEXT("BoundsModifier"),      TEXT("PCGBoundsModifierSettings")},
		{TEXT("SelfPruning"),         TEXT("PCGSelfPruningSettings")},
		{TEXT("Projection"),          TEXT("PCGProjectionSettings")},

		// Density
		{TEXT("DensityFilter"),       TEXT("PCGDensityFilterSettings")},
		{TEXT("DensityRemap"),        TEXT("PCGDensityRemapSettings")},
		{TEXT("DensityNoise"),        TEXT("PCGDensityNoiseSettings")},

		// Filters
		{TEXT("PointFilter"),         TEXT("PCGPointFilterSettings")},

		// Spawners
		{TEXT("StaticMeshSpawner"),   TEXT("PCGStaticMeshSpawnerSettings")},
		{TEXT("SpawnActor"),          TEXT("PCGSpawnActorSettings")},

		// Spatial
		{TEXT("SpatialNoise"),        TEXT("PCGSpatialNoiseSettings")},

		// Subgraph
		{TEXT("Subgraph"),            TEXT("PCGSubgraphSettings")},
	};

	// 1. Look up friendly name
	if (const FString* ClassName = SettingsMap.Find(NodeType))
	{
		FString FullPath = FString::Printf(TEXT("/Script/PCG.%s"), **ClassName);
		UClass* Found = StaticLoadClass(UPCGSettings::StaticClass(), nullptr, *FullPath);
		if (Found)
		{
			return Found;
		}
		UE_LOG(LogArborPCG, Warning, TEXT("Mapped class '%s' not found in /Script/PCG, trying fallbacks"), **ClassName);
	}

	// 2. Try direct class name (e.g. "PCGSurfaceSamplerSettings")
	{
		FString FullPath = FString::Printf(TEXT("/Script/PCG.%s"), *NodeType);
		UClass* Found = StaticLoadClass(UPCGSettings::StaticClass(), nullptr, *FullPath);
		if (Found)
		{
			return Found;
		}
	}

	// 3. Try with "PCG" prefix + "Settings" suffix
	{
		FString WithPrefixSuffix = FString::Printf(TEXT("/Script/PCG.PCG%sSettings"), *NodeType);
		UClass* Found = StaticLoadClass(UPCGSettings::StaticClass(), nullptr, *WithPrefixSuffix);
		if (Found)
		{
			return Found;
		}
	}

	// 4. Try with just the type name as-is (full path)
	{
		UClass* Found = StaticLoadClass(UPCGSettings::StaticClass(), nullptr, *NodeType);
		if (Found)
		{
			return Found;
		}
	}

	UE_LOG(LogArborPCG, Error, TEXT("Could not resolve PCG settings class for type: '%s'"), *NodeType);
	return nullptr;
}

// ============================================================================
// Parameter Application
// ============================================================================

void UPCGBuilder::ApplyNodeParams(
	UPCGSettings* Settings, const TSharedPtr<FJsonObject>& ParamsJson)
{
	if (!ParamsJson.IsValid() || !Settings)
	{
		return;
	}

	for (const auto& Pair : ParamsJson->Values)
	{
		const FString& Key = Pair.Key;
		const TSharedPtr<FJsonValue>& Value = Pair.Value;

		// Numeric params
		if (Value->Type == EJson::Number)
		{
			SetNumericParam(Settings, Key, Value->AsNumber());
			continue;
		}

		// Bool params
		if (Value->Type == EJson::Boolean)
		{
			FProperty* Prop = Settings->GetClass()->FindPropertyByName(FName(*Key));
			if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
			{
				void* PropAddr = BoolProp->ContainerPtrToValuePtr<void>(Settings);
				BoolProp->SetPropertyValue(PropAddr, Value->AsBool());
			}
			else
			{
				UE_LOG(LogArborPCG, Warning, TEXT("Property '%s' not found or not bool type"), *Key);
			}
			continue;
		}

		// String params (enum or plain string or object path)
		if (Value->Type == EJson::String)
		{
			FProperty* Prop = Settings->GetClass()->FindPropertyByName(FName(*Key));
			if (!Prop)
			{
				UE_LOG(LogArborPCG, Warning, TEXT("Property '%s' not found on %s"), *Key, *Settings->GetClass()->GetName());
				continue;
			}

			void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Settings);

			// Enum property
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
					else
					{
						UE_LOG(LogArborPCG, Warning, TEXT("Invalid enum value '%s' for property '%s'"),
							*Value->AsString(), *Key);
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
			// Soft object reference (e.g. mesh paths)
			else if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Prop))
			{
				FSoftObjectPtr SoftPtr(FSoftObjectPath(Value->AsString()));
				SoftObjProp->SetPropertyValue(PropAddr, SoftPtr);
			}
			// Object reference (e.g. UStaticMesh)
			else if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
			{
				UObject* Obj = StaticLoadObject(ObjProp->PropertyClass, nullptr, *Value->AsString());
				if (Obj)
				{
					ObjProp->SetObjectPropertyValue(PropAddr, Obj);
				}
				else
				{
					UE_LOG(LogArborPCG, Warning, TEXT("Failed to load object '%s' for property '%s'"),
						*Value->AsString(), *Key);
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
			continue;
		}

		// Object params (FVector, etc.)
		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject>& ObjValue = Value->AsObject();
			FProperty* Prop = Settings->GetClass()->FindPropertyByName(FName(*Key));
			if (!Prop)
			{
				UE_LOG(LogArborPCG, Warning, TEXT("Property '%s' not found on %s"), *Key, *Settings->GetClass()->GetName());
				continue;
			}

			void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Settings);

			if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
			{
				// FVector
				if (StructProp->Struct == TBaseStructure<FVector>::Get())
				{
					FVector* Vec = static_cast<FVector*>(PropAddr);
					Vec->X = ObjValue->GetNumberField(TEXT("X"));
					Vec->Y = ObjValue->GetNumberField(TEXT("Y"));
					Vec->Z = ObjValue->GetNumberField(TEXT("Z"));
				}
				// FRotator
				else if (StructProp->Struct == TBaseStructure<FRotator>::Get())
				{
					FRotator* Rot = static_cast<FRotator*>(PropAddr);
					Rot->Pitch = ObjValue->GetNumberField(TEXT("Pitch"));
					Rot->Yaw = ObjValue->GetNumberField(TEXT("Yaw"));
					Rot->Roll = ObjValue->GetNumberField(TEXT("Roll"));
				}
				// FVector2D
				else if (StructProp->Struct == TBaseStructure<FVector2D>::Get())
				{
					FVector2D* Vec2D = static_cast<FVector2D*>(PropAddr);
					Vec2D->X = ObjValue->GetNumberField(TEXT("X"));
					Vec2D->Y = ObjValue->GetNumberField(TEXT("Y"));
				}
			}
			continue;
		}

		// Array params (e.g. mesh entries)
		if (Value->Type == EJson::Array)
		{
			// For now log a warning — complex array handling (mesh entries) will be added
			// as we discover the exact struct layout at compile time
			FProperty* Prop = Settings->GetClass()->FindPropertyByName(FName(*Key));
			if (!Prop)
			{
				UE_LOG(LogArborPCG, Warning, TEXT("Array property '%s' not found on %s"), *Key, *Settings->GetClass()->GetName());
			}
			else
			{
				UE_LOG(LogArborPCG, Warning, TEXT("Array property '%s' on %s — complex array params not yet implemented. Set this via granular API after creation."),
					*Key, *Settings->GetClass()->GetName());
			}
			continue;
		}
	}
}

void UPCGBuilder::SetNumericParam(
	UObject* Object, const FString& PropertyName, double Value)
{
	FProperty* Prop = Object->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		UE_LOG(LogArborPCG, Warning, TEXT("Property '%s' not found on %s"),
			*PropertyName, *Object->GetClass()->GetName());
		return;
	}

	void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Object);

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
	else if (FInt64Property* Int64Prop = CastField<FInt64Property>(Prop))
	{
		Int64Prop->SetPropertyValue(PropAddr, static_cast<int64>(Value));
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		// FAIDataProviderFloatValue (used by some PCG node types)
		if (StructProp->Struct->GetName() == TEXT("AIDataProviderFloatValue"))
		{
			// Fallback: set the DefaultValue member directly
			FProperty* DefaultValProp = StructProp->Struct->FindPropertyByName(TEXT("DefaultValue"));
			if (DefaultValProp)
			{
				void* DVAddr = DefaultValProp->ContainerPtrToValuePtr<void>(PropAddr);
				if (FFloatProperty* DVFloat = CastField<FFloatProperty>(DefaultValProp))
				{
					DVFloat->SetPropertyValue(DVAddr, static_cast<float>(Value));
				}
			}
		}
	}
	else
	{
		UE_LOG(LogArborPCG, Warning, TEXT("Property '%s' on %s is not a numeric type"),
			*PropertyName, *Object->GetClass()->GetName());
	}
}

// ============================================================================
// Asset Saving
// ============================================================================

bool UPCGBuilder::SaveAsset(UObject* Asset)
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
		UE_LOG(LogArborPCG, Log, TEXT("Saved asset: %s"), *PackageName);
	}
	else
	{
		UE_LOG(LogArborPCG, Error, TEXT("Failed to save asset: %s"), *PackageName);
	}

	return bSuccess;
}

// ============================================================================
// Loading Helper
// ============================================================================

UPCGGraph* UPCGBuilder::LoadPCGGraphForEditing(const FString& AssetPath)
{
	FString AssetName = FPaths::GetBaseFilename(AssetPath);
	FString ObjectPath = AssetPath + TEXT(".") + AssetName;

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *ObjectPath);
	if (!Graph)
	{
		UE_LOG(LogArborPCG, Error, TEXT("Failed to load PCG graph: %s"), *AssetPath);
	}
	return Graph;
}

// ============================================================================
// Query — Serialization Helpers
// ============================================================================

FString UPCGBuilder::GetNodeTypeName(UPCGSettings* Settings)
{
	if (!Settings)
	{
		return TEXT("Unknown");
	}
	FString ClassName = Settings->GetClass()->GetName();
	// Strip "PCG" prefix and "Settings" suffix for cleaner display
	ClassName.RemoveFromStart(TEXT("PCG"));
	ClassName.RemoveFromEnd(TEXT("Settings"));
	return ClassName;
}

TSharedPtr<FJsonObject> UPCGBuilder::SerializeSettingsParams(UPCGSettings* Settings)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	if (!Settings)
	{
		return Params;
	}

	UClass* Class = Settings->GetClass();
	UClass* BaseClass = UPCGSettings::StaticClass();

	for (TFieldIterator<FProperty> It(Class); It; ++It)
	{
		FProperty* Prop = *It;
		// Skip base PCGSettings properties (internal)
		if (Prop->GetOwnerClass() == BaseClass)
		{
			continue;
		}

		// Skip properties that shouldn't be serialized
		if (!Prop->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}

		const FString PropName = Prop->GetName();
		void* PropAddr = Prop->ContainerPtrToValuePtr<void>(Settings);

		// Float/Double
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
				Params->SetStringField(PropName, ByteProp->Enum->GetNameStringByValue(ByteValue));
			}
			else
			{
				Params->SetNumberField(PropName, ByteProp->GetPropertyValue(PropAddr));
			}
			continue;
		}

		// String / Name
		if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
		{
			Params->SetStringField(PropName, StrProp->GetPropertyValue(PropAddr));
			continue;
		}
		if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
		{
			Params->SetStringField(PropName, NameProp->GetPropertyValue(PropAddr).ToString());
			continue;
		}

		// Struct (FVector, FRotator, etc.)
		if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			if (StructProp->Struct == TBaseStructure<FVector>::Get())
			{
				const FVector* Vec = static_cast<const FVector*>(PropAddr);
				TSharedPtr<FJsonObject> VecObj = MakeShared<FJsonObject>();
				VecObj->SetNumberField(TEXT("X"), Vec->X);
				VecObj->SetNumberField(TEXT("Y"), Vec->Y);
				VecObj->SetNumberField(TEXT("Z"), Vec->Z);
				Params->SetObjectField(PropName, VecObj);
				continue;
			}
			if (StructProp->Struct == TBaseStructure<FRotator>::Get())
			{
				const FRotator* Rot = static_cast<const FRotator*>(PropAddr);
				TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
				RotObj->SetNumberField(TEXT("Pitch"), Rot->Pitch);
				RotObj->SetNumberField(TEXT("Yaw"), Rot->Yaw);
				RotObj->SetNumberField(TEXT("Roll"), Rot->Roll);
				Params->SetObjectField(PropName, RotObj);
				continue;
			}
		}

		// Soft object reference
		if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Prop))
		{
			FSoftObjectPath SoftPath = SoftObjProp->GetPropertyValue(PropAddr).ToSoftObjectPath();
			if (!SoftPath.IsNull())
			{
				Params->SetStringField(PropName, SoftPath.ToString());
			}
			continue;
		}

		// Object reference
		if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
		{
			UObject* Obj = ObjProp->GetObjectPropertyValue(PropAddr);
			if (Obj)
			{
				Params->SetStringField(PropName, Obj->GetPathName());
			}
			continue;
		}
	}

	return Params;
}

TSharedPtr<FJsonObject> UPCGBuilder::SerializeNodeToJson(UPCGNode* Node)
{
	TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
	if (!Node)
	{
		return NodeObj;
	}

	// Node ID (UID as string)
	NodeObj->SetStringField(TEXT("node_id"), FString::FromInt(Node->GetUniqueID()));

	// Node title (user-assigned name)
	NodeObj->SetStringField(TEXT("title"), Node->NodeTitle.ToString());

	// Settings type
	UPCGSettings* Settings = Node->GetSettings();
	if (Settings)
	{
		NodeObj->SetStringField(TEXT("type"), GetNodeTypeName(Settings));
		NodeObj->SetStringField(TEXT("class"), Settings->GetClass()->GetName());
		NodeObj->SetObjectField(TEXT("params"), SerializeSettingsParams(Settings));
	}

	// Position
	TSharedPtr<FJsonObject> PosObj = MakeShared<FJsonObject>();
	PosObj->SetNumberField(TEXT("X"), Node->PositionX);
	PosObj->SetNumberField(TEXT("Y"), Node->PositionY);
	NodeObj->SetObjectField(TEXT("position"), PosObj);

	// Input pins
	TArray<TSharedPtr<FJsonValue>> InputPinsArray;
	for (UPCGPin* Pin : Node->GetInputPins())
	{
		TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("label"), Pin->Properties.Label.ToString());
		PinObj->SetStringField(TEXT("direction"), TEXT("input"));

		// List connected pins
		TArray<TSharedPtr<FJsonValue>> ConnectedArray;
		for (UPCGEdge* Edge : Pin->Edges)
		{
			if (!Edge) continue;
			UPCGPin* Other = Edge->GetOtherPin(Pin);
			if (Other && Other->Node)
			{
				TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
				ConnObj->SetStringField(TEXT("node_id"), FString::FromInt(Other->Node->GetUniqueID()));
				ConnObj->SetStringField(TEXT("pin"), Other->Properties.Label.ToString());
				ConnectedArray.Add(MakeShared<FJsonValueObject>(ConnObj));
			}
		}
		PinObj->SetArrayField(TEXT("connected_to"), ConnectedArray);

		InputPinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
	}
	NodeObj->SetArrayField(TEXT("input_pins"), InputPinsArray);

	// Output pins
	TArray<TSharedPtr<FJsonValue>> OutputPinsArray;
	for (UPCGPin* Pin : Node->GetOutputPins())
	{
		TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("label"), Pin->Properties.Label.ToString());
		PinObj->SetStringField(TEXT("direction"), TEXT("output"));

		TArray<TSharedPtr<FJsonValue>> ConnectedArray;
		for (UPCGEdge* Edge : Pin->Edges)
		{
			if (!Edge) continue;
			UPCGPin* Other = Edge->GetOtherPin(Pin);
			if (Other && Other->Node)
			{
				TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
				ConnObj->SetStringField(TEXT("node_id"), FString::FromInt(Other->Node->GetUniqueID()));
				ConnObj->SetStringField(TEXT("pin"), Other->Properties.Label.ToString());
				ConnectedArray.Add(MakeShared<FJsonValueObject>(ConnObj));
			}
		}
		PinObj->SetArrayField(TEXT("connected_to"), ConnectedArray);

		OutputPinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
	}
	NodeObj->SetArrayField(TEXT("output_pins"), OutputPinsArray);

	return NodeObj;
}

// ============================================================================
// Query — Main Function
// ============================================================================

FString UPCGBuilder::QueryPCGGraph(const FString& AssetPath)
{
	UPCGGraph* Graph = LoadPCGGraphForEditing(AssetPath);
	if (!Graph)
	{
		return TEXT("{\"error\": \"Failed to load PCG graph\"}");
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), Graph->GetName());
	Result->SetStringField(TEXT("asset_path"), AssetPath);

	// Serialize all nodes
	TArray<TSharedPtr<FJsonValue>> NodesArray;

	// Input node
	if (Graph->GetInputNode())
	{
		TSharedPtr<FJsonObject> InputObj = SerializeNodeToJson(Graph->GetInputNode());
		InputObj->SetStringField(TEXT("role"), TEXT("input"));
		NodesArray.Add(MakeShared<FJsonValueObject>(InputObj));
	}

	// Output node
	if (Graph->GetOutputNode())
	{
		TSharedPtr<FJsonObject> OutputObj = SerializeNodeToJson(Graph->GetOutputNode());
		OutputObj->SetStringField(TEXT("role"), TEXT("output"));
		NodesArray.Add(MakeShared<FJsonValueObject>(OutputObj));
	}

	// Regular nodes
	for (UPCGNode* Node : Graph->GetNodes())
	{
		if (Node == Graph->GetInputNode() || Node == Graph->GetOutputNode())
		{
			continue;
		}
		TSharedPtr<FJsonObject> NodeObj = SerializeNodeToJson(Node);
		NodeObj->SetStringField(TEXT("role"), TEXT("node"));
		NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	Result->SetArrayField(TEXT("nodes"), NodesArray);

	// Serialize to string
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

	return OutputString;
}

// ============================================================================
// Find Node by ID
// ============================================================================

UPCGNode* UPCGBuilder::FindNodeById(UPCGGraph* Graph, const FString& NodeIdString)
{
	if (!Graph)
	{
		return nullptr;
	}

	uint32 TargetUID = FCString::Atoi(*NodeIdString);

	// Check input/output nodes first
	if (Graph->GetInputNode() && Graph->GetInputNode()->GetUniqueID() == TargetUID)
	{
		return Graph->GetInputNode();
	}
	if (Graph->GetOutputNode() && Graph->GetOutputNode()->GetUniqueID() == TargetUID)
	{
		return Graph->GetOutputNode();
	}

	// Also check by title (for user-friendly lookup using the "id" they assigned)
	FName TitleName(*NodeIdString);
	for (UPCGNode* Node : Graph->GetNodes())
	{
		if (Node->GetUniqueID() == TargetUID)
		{
			return Node;
		}
		if (Node->NodeTitle == TitleName)
		{
			return Node;
		}
	}

	return nullptr;
}

// ============================================================================
// Granular Editing — Add Node
// ============================================================================

FString UPCGBuilder::AddPCGNode(const FString& AssetPath, const FString& NodeJsonString)
{
	UPCGGraph* Graph = LoadPCGGraphForEditing(AssetPath);
	if (!Graph)
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to load PCG graph\"}");
	}

	TSharedPtr<FJsonObject> NodeJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(NodeJsonString);
	if (!FJsonSerializer::Deserialize(Reader, NodeJson) || !NodeJson.IsValid())
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to parse node JSON\"}");
	}

	FString NodeType;
	if (!NodeJson->TryGetStringField(TEXT("type"), NodeType))
	{
		return TEXT("{\"success\": false, \"error\": \"Missing 'type' field\"}");
	}

	UClass* SettingsClass = ResolveSettingsClass(NodeType);
	if (!SettingsClass)
	{
		return FString::Printf(TEXT("{\"success\": false, \"error\": \"Unknown node type: '%s'\"}"), *NodeType);
	}

	UPCGSettings* Settings = NewObject<UPCGSettings>(Graph, SettingsClass);
	if (!Settings)
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to create settings\"}");
	}

	const TSharedPtr<FJsonObject>* ParamsJson;
	if (NodeJson->TryGetObjectField(TEXT("params"), ParamsJson))
	{
		ApplyNodeParams(Settings, *ParamsJson);
	}

	UPCGNode* NewNode = Graph->AddNode(Settings);
	if (!NewNode)
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to add node to graph\"}");
	}

	// Set title if provided
	FString NodeId;
	if (NodeJson->TryGetStringField(TEXT("id"), NodeId))
	{
		NewNode->NodeTitle = FName(*NodeId);
	}

	SaveAsset(Graph);

	return FString::Printf(TEXT("{\"success\": true, \"node_id\": \"%d\", \"title\": \"%s\"}"),
		NewNode->GetUniqueID(), *NewNode->NodeTitle.ToString());
}

// ============================================================================
// Granular Editing — Remove Node
// ============================================================================

FString UPCGBuilder::RemovePCGNode(const FString& AssetPath, const FString& NodeIdString)
{
	UPCGGraph* Graph = LoadPCGGraphForEditing(AssetPath);
	if (!Graph)
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to load PCG graph\"}");
	}

	UPCGNode* Node = FindNodeById(Graph, NodeIdString);
	if (!Node)
	{
		return FString::Printf(TEXT("{\"success\": false, \"error\": \"Node '%s' not found\"}"), *NodeIdString);
	}

	// Don't remove input/output nodes
	if (Node == Graph->GetInputNode() || Node == Graph->GetOutputNode())
	{
		return TEXT("{\"success\": false, \"error\": \"Cannot remove input/output nodes\"}");
	}

	Graph->RemoveNode(Node);
	SaveAsset(Graph);

	return TEXT("{\"success\": true}");
}

// ============================================================================
// Granular Editing — Set Node Params
// ============================================================================

FString UPCGBuilder::SetPCGNodeParams(const FString& AssetPath, const FString& NodeIdString,
                                       const FString& ParamsJsonString)
{
	UPCGGraph* Graph = LoadPCGGraphForEditing(AssetPath);
	if (!Graph)
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to load PCG graph\"}");
	}

	UPCGNode* Node = FindNodeById(Graph, NodeIdString);
	if (!Node)
	{
		return FString::Printf(TEXT("{\"success\": false, \"error\": \"Node '%s' not found\"}"), *NodeIdString);
	}

	UPCGSettings* Settings = Node->GetSettings();
	if (!Settings)
	{
		return TEXT("{\"success\": false, \"error\": \"Node has no settings\"}");
	}

	TSharedPtr<FJsonObject> ParamsJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ParamsJsonString);
	if (!FJsonSerializer::Deserialize(Reader, ParamsJson) || !ParamsJson.IsValid())
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to parse params JSON\"}");
	}

	ApplyNodeParams(Settings, ParamsJson);
	SaveAsset(Graph);

	return TEXT("{\"success\": true}");
}

// ============================================================================
// Pin Wiring — Connect
// ============================================================================

FString UPCGBuilder::ConnectPCGPins(const FString& AssetPath,
	const FString& FromNodeId, const FString& FromPinLabel,
	const FString& ToNodeId, const FString& ToPinLabel)
{
	UPCGGraph* Graph = LoadPCGGraphForEditing(AssetPath);
	if (!Graph)
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to load PCG graph\"}");
	}

	UPCGNode* FromNode = FindNodeById(Graph, FromNodeId);
	if (!FromNode)
	{
		return FString::Printf(TEXT("{\"success\": false, \"error\": \"Source node '%s' not found\"}"), *FromNodeId);
	}

	UPCGNode* ToNode = FindNodeById(Graph, ToNodeId);
	if (!ToNode)
	{
		return FString::Printf(TEXT("{\"success\": false, \"error\": \"Target node '%s' not found\"}"), *ToNodeId);
	}

	UPCGPin* OutPin = FromNode->GetOutputPin(FName(*FromPinLabel));
	if (!OutPin)
	{
		TArray<FString> PinNames;
		for (UPCGPin* Pin : FromNode->GetOutputPins())
		{
			PinNames.Add(Pin->Properties.Label.ToString());
		}
		return FString::Printf(TEXT("{\"success\": false, \"error\": \"Output pin '%s' not found. Available: %s\"}"),
			*FromPinLabel, *FString::Join(PinNames, TEXT(", ")));
	}

	UPCGPin* InPin = ToNode->GetInputPin(FName(*ToPinLabel));
	if (!InPin)
	{
		TArray<FString> PinNames;
		for (UPCGPin* Pin : ToNode->GetInputPins())
		{
			PinNames.Add(Pin->Properties.Label.ToString());
		}
		return FString::Printf(TEXT("{\"success\": false, \"error\": \"Input pin '%s' not found. Available: %s\"}"),
			*ToPinLabel, *FString::Join(PinNames, TEXT(", ")));
	}

	bool bConnected = OutPin->AddEdgeTo(InPin);
	if (!bConnected)
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to create connection\"}");
	}

	SaveAsset(Graph);
	return TEXT("{\"success\": true}");
}

// ============================================================================
// Pin Wiring — Disconnect
// ============================================================================

FString UPCGBuilder::DisconnectPCGPin(const FString& AssetPath,
	const FString& NodeIdString, const FString& PinLabel)
{
	UPCGGraph* Graph = LoadPCGGraphForEditing(AssetPath);
	if (!Graph)
	{
		return TEXT("{\"success\": false, \"error\": \"Failed to load PCG graph\"}");
	}

	UPCGNode* Node = FindNodeById(Graph, NodeIdString);
	if (!Node)
	{
		return FString::Printf(TEXT("{\"success\": false, \"error\": \"Node '%s' not found\"}"), *NodeIdString);
	}

	FName PinName(*PinLabel);

	// Try output pin first, then input
	UPCGPin* Pin = Node->GetOutputPin(PinName);
	if (!Pin)
	{
		Pin = Node->GetInputPin(PinName);
	}
	if (!Pin)
	{
		return FString::Printf(TEXT("{\"success\": false, \"error\": \"Pin '%s' not found on node '%s'\"}"),
			*PinLabel, *NodeIdString);
	}

	// Remove all edges on this pin
	Pin->BreakAllEdges();

	SaveAsset(Graph);
	return TEXT("{\"success\": true}");
}

// ============================================================================
// Execution
// ============================================================================

AActor* UPCGBuilder::FindActorByLabel(const FString& Label)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && Actor->GetActorLabel() == Label)
		{
			return Actor;
		}
	}

	return nullptr;
}

FString UPCGBuilder::AddPCGComponentToActor(const FString& ActorLabel, const FString& GraphAssetPath)
{
	AActor* Actor = FindActorByLabel(ActorLabel);
	if (!Actor)
	{
		return FString::Printf(TEXT("{\"success\": false, \"error\": \"Actor '%s' not found in level\"}"), *ActorLabel);
	}

	// Load PCG graph
	UPCGGraph* Graph = LoadPCGGraphForEditing(GraphAssetPath);
	if (!Graph)
	{
		return FString::Printf(TEXT("{\"success\": false, \"error\": \"PCG graph '%s' not found\"}"), *GraphAssetPath);
	}

	// Find existing PCGComponent or create new one
	UPCGComponent* PCGComp = Actor->FindComponentByClass<UPCGComponent>();
	if (!PCGComp)
	{
		PCGComp = NewObject<UPCGComponent>(Actor, UPCGComponent::StaticClass(),
			NAME_None, RF_Transactional);
		if (!PCGComp)
		{
			return TEXT("{\"success\": false, \"error\": \"Failed to create PCGComponent\"}");
		}
		Actor->AddInstanceComponent(PCGComp);
		PCGComp->RegisterComponent();
	}

	// Set the graph
	PCGComp->SetGraph(Graph);

	Actor->MarkPackageDirty();

	return FString::Printf(TEXT("{\"success\": true, \"component\": \"%s\"}"),
		*PCGComp->GetName());
}

FString UPCGBuilder::ExecutePCGOnActor(const FString& GraphAssetPath, const FString& ActorLabel)
{
	// First add/set the component
	FString AddResult = AddPCGComponentToActor(ActorLabel, GraphAssetPath);

	// Parse to check success
	TSharedPtr<FJsonObject> ResultJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(AddResult);
	if (!FJsonSerializer::Deserialize(Reader, ResultJson) || !ResultJson.IsValid())
	{
		return AddResult; // Return the error as-is
	}

	bool bSuccess = false;
	ResultJson->TryGetBoolField(TEXT("success"), bSuccess);
	if (!bSuccess)
	{
		return AddResult;
	}

	// Now generate
	AActor* Actor = FindActorByLabel(ActorLabel);
	if (!Actor)
	{
		return TEXT("{\"success\": false, \"error\": \"Actor lost after component setup\"}");
	}

	UPCGComponent* PCGComp = Actor->FindComponentByClass<UPCGComponent>();
	if (!PCGComp)
	{
		return TEXT("{\"success\": false, \"error\": \"PCGComponent not found after setup\"}");
	}

	// Trigger generation
	PCGComp->Generate();

	return FString::Printf(TEXT("{\"success\": true, \"actor\": \"%s\", \"component\": \"%s\"}"),
		*ActorLabel, *PCGComp->GetName());
}
