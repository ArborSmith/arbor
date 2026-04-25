#include "AnimBlueprintBuilder.h"

// JSON
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

// Animation
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimInstance.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"

// AnimGraph nodes (editor-time)
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"

// K2 nodes (for variable get in AnimGraph)
#include "K2Node_VariableGet.h"
#include "EdGraphSchema_K2.h"

// Blueprint compilation
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraph/EdGraph.h"

// Asset management
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogArborAnimBP, Log, All);

// ============================================================================
// Private helpers
// ============================================================================

UAnimBlueprint* UAnimBlueprintBuilder::LoadAnimBlueprintForEditing(const FString& AssetPath)
{
	// Try loading directly (handles both /Game/Folder/Asset and /Game/Folder/Asset.Asset)
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AssetPath);
	if (AnimBP)
	{
		return AnimBP;
	}

	// Try with .AssetName suffix
	FString BaseName = FPaths::GetBaseFilename(AssetPath);
	FString FullPath = AssetPath + TEXT(".") + BaseName;
	AnimBP = LoadObject<UAnimBlueprint>(nullptr, *FullPath);
	return AnimBP;
}

UEdGraph* UAnimBlueprintBuilder::GetAnimGraph(UAnimBlueprint* AnimBP)
{
	if (!AnimBP)
	{
		return nullptr;
	}

	for (UEdGraph* Graph : AnimBP->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == FName(TEXT("AnimGraph")))
		{
			return Graph;
		}
	}

	return nullptr;
}

UAnimGraphNode_Base* UAnimBlueprintBuilder::FindRootNode(UEdGraph* AnimGraph)
{
	if (!AnimGraph)
	{
		return nullptr;
	}

	for (UEdGraphNode* Node : AnimGraph->Nodes)
	{
		if (UAnimGraphNode_Root* RootNode = Cast<UAnimGraphNode_Root>(Node))
		{
			return RootNode;
		}
	}

	return nullptr;
}

UEdGraphNode* UAnimBlueprintBuilder::FindNodeByGuid(UEdGraph* Graph, const FString& GuidString)
{
	if (!Graph)
	{
		return nullptr;
	}

	FGuid TargetGuid;
	FGuid::Parse(GuidString, TargetGuid);

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->NodeGuid == TargetGuid)
		{
			return Node;
		}
	}

	return nullptr;
}

TSharedPtr<FJsonObject> UAnimBlueprintBuilder::SerializeAnimGraphNodeToJson(UEdGraphNode* Node)
{
	if (!Node)
	{
		return nullptr;
	}

	auto NodeObj = MakeShared<FJsonObject>();
	NodeObj->SetStringField(TEXT("guid"), Node->NodeGuid.ToString());
	NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
	NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

	// Position
	auto PosArr = MakeShared<FJsonObject>();
	PosArr->SetNumberField(TEXT("x"), Node->NodePosX);
	PosArr->SetNumberField(TEXT("y"), Node->NodePosY);
	NodeObj->SetObjectField(TEXT("position"), PosArr);

	// AnimGraph-specific: asset reference
	if (UAnimGraphNode_Base* AnimNode = Cast<UAnimGraphNode_Base>(Node))
	{
		// Try to get the animation asset from BlendSpacePlayer
		if (UAnimGraphNode_BlendSpacePlayer* BSNode = Cast<UAnimGraphNode_BlendSpacePlayer>(Node))
		{
			UBlendSpace* BS = Cast<UBlendSpace>(BSNode->GetAnimationAsset());
			if (BS)
			{
				NodeObj->SetStringField(TEXT("blendspace"), BS->GetPathName());

				// Add axis parameter metadata so callers know what each pin represents
				TArray<TSharedPtr<FJsonValue>> AxesArray;
				int32 NumAxes = BS->IsA<UBlendSpace1D>() ? 1 : 2;
				for (int32 i = 0; i < NumAxes; i++)
				{
					const FBlendParameter& Param = BS->GetBlendParameter(i);
					auto AxisObj = MakeShared<FJsonObject>();
					AxisObj->SetStringField(TEXT("axis"), i == 0 ? TEXT("X") : TEXT("Y"));
					AxisObj->SetStringField(TEXT("display_name"), Param.DisplayName);
					AxisObj->SetNumberField(TEXT("min"), Param.Min);
					AxisObj->SetNumberField(TEXT("max"), Param.Max);
					AxesArray.Add(MakeShared<FJsonValueObject>(AxisObj));
				}
				NodeObj->SetArrayField(TEXT("blendspace_axes"), AxesArray);
			}
		}
	}

	// Pins
	TArray<TSharedPtr<FJsonValue>> PinsArray;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin)
		{
			continue;
		}

		auto PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("name"), Pin->GetName());
		PinObj->SetStringField(TEXT("direction"),
			Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
		PinObj->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());

		if (!Pin->DefaultValue.IsEmpty())
		{
			PinObj->SetStringField(TEXT("default"), Pin->DefaultValue);
		}

		// Show linked pin count
		if (Pin->LinkedTo.Num() > 0)
		{
			PinObj->SetNumberField(TEXT("linked_count"), Pin->LinkedTo.Num());
		}

		PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
	}
	NodeObj->SetArrayField(TEXT("pins"), PinsArray);

	return NodeObj;
}

bool UAnimBlueprintBuilder::SaveAsset(UObject* Asset)
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
		UE_LOG(LogArborAnimBP, Log, TEXT("Saved asset: %s"), *PackageName);
	}
	else
	{
		UE_LOG(LogArborAnimBP, Error, TEXT("Failed to save asset: %s"), *PackageName);
	}

	return bSuccess;
}

// ============================================================================
// SetupLocomotionGraph
// ============================================================================

FString UAnimBlueprintBuilder::SetupLocomotionGraph(
	const FString& AssetPath, const FString& ParamsJsonString)
{
	// Parse params JSON
	TSharedPtr<FJsonObject> Params;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ParamsJsonString);
	if (!FJsonSerializer::Deserialize(Reader, Params) || !Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON parameters\"}");
	}

	FString BlendSpacePath;
	if (!Params->TryGetStringField(TEXT("blendspace_path"), BlendSpacePath))
	{
		return TEXT("{\"success\":false,\"error\":\"Missing 'blendspace_path' parameter\"}");
	}

	// Parse variable bindings — supports array or single legacy params
	struct FVarBinding { FString Name; FString Axis; };
	TArray<FVarBinding> Bindings;

	const TArray<TSharedPtr<FJsonValue>>* BindingsArray;
	if (Params->TryGetArrayField(TEXT("variable_bindings"), BindingsArray))
	{
		for (const auto& Val : *BindingsArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Val->TryGetObject(Obj))
			{
				FVarBinding B;
				(*Obj)->TryGetStringField(TEXT("variable_name"), B.Name);
				if (!(*Obj)->TryGetStringField(TEXT("variable_axis"), B.Axis))
				{
					B.Axis = TEXT("X");
				}
				if (!B.Name.IsEmpty())
				{
					Bindings.Add(B);
				}
			}
		}
	}

	// Backward compat: single variable_name / variable_axis
	if (Bindings.Num() == 0)
	{
		FVarBinding B;
		B.Name = TEXT("Speed");
		Params->TryGetStringField(TEXT("variable_name"), B.Name);
		B.Axis = TEXT("X");
		Params->TryGetStringField(TEXT("variable_axis"), B.Axis);
		Bindings.Add(B);
	}

	// Load BlendSpace (supports both BlendSpace and BlendSpace1D)
	UBlendSpace* BlendSpace = LoadObject<UBlendSpace>(nullptr, *BlendSpacePath);
	if (!BlendSpace)
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"BlendSpace not found: %s\"}"), *BlendSpacePath);
	}

	// Load existing AnimBlueprint
	UAnimBlueprint* AnimBP = LoadAnimBlueprintForEditing(AssetPath);
	if (!AnimBP)
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"AnimBlueprint not found: %s. Create it first with build_bp (parent_class: AnimInstance).\"}"),
			*AssetPath);
	}

	// Get AnimGraph
	UEdGraph* AnimGraph = GetAnimGraph(AnimBP);
	if (!AnimGraph)
	{
		return TEXT("{\"success\":false,\"error\":\"No AnimGraph found in this AnimBlueprint\"}");
	}

	// Find Root node (OutputPose — always created by schema)
	UAnimGraphNode_Root* RootNode = nullptr;
	for (UEdGraphNode* Node : AnimGraph->Nodes)
	{
		RootNode = Cast<UAnimGraphNode_Root>(Node);
		if (RootNode)
		{
			break;
		}
	}
	if (!RootNode)
	{
		return TEXT("{\"success\":false,\"error\":\"No Root (Output Pose) node found in AnimGraph\"}");
	}

	// Clear existing nodes (except Root) for a clean graph
	TArray<UEdGraphNode*> NodesToRemove;
	for (UEdGraphNode* Node : AnimGraph->Nodes)
	{
		if (Node && !Cast<UAnimGraphNode_Root>(Node))
		{
			NodesToRemove.Add(Node);
		}
	}
	for (UEdGraphNode* Node : NodesToRemove)
	{
		AnimGraph->RemoveNode(Node);
	}

	// Break existing connections on Root
	for (UEdGraphPin* Pin : RootNode->Pins)
	{
		Pin->BreakAllPinLinks();
	}

	// -----------------------------------------------------------------------
	// Create BlendSpacePlayer node via FGraphNodeCreator
	// -----------------------------------------------------------------------
	FGraphNodeCreator<UAnimGraphNode_BlendSpacePlayer> NodeCreator(*AnimGraph);
	UAnimGraphNode_BlendSpacePlayer* BSPlayerNode = NodeCreator.CreateNode(false);

	// Set the BlendSpace asset via the editor node API
	BSPlayerNode->SetAnimationAsset(BlendSpace);

	NodeCreator.Finalize(); // CreateNewGuid + PostPlacedNewNode + AllocateDefaultPins

	// Position to the left of Root
	BSPlayerNode->NodePosX = RootNode->NodePosX - 500;
	BSPlayerNode->NodePosY = RootNode->NodePosY;

	UE_LOG(LogArborAnimBP, Log, TEXT("Created BlendSpacePlayer node: %s (GUID: %s)"),
		*BlendSpace->GetName(), *BSPlayerNode->NodeGuid.ToString());

	// -----------------------------------------------------------------------
	// Wire: BlendSpacePlayer pose output -> Root pose input
	// -----------------------------------------------------------------------
	UEdGraphPin* OutputPosePin = nullptr;
	for (UEdGraphPin* Pin : BSPlayerNode->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output &&
			Pin->PinType.PinCategory == UAnimationGraphSchema::PC_Struct)
		{
			// Pose pins are struct pins with FPoseLink/FComponentSpacePoseLink
			OutputPosePin = Pin;
			break;
		}
	}

	UEdGraphPin* InputPosePin = nullptr;
	for (UEdGraphPin* Pin : RootNode->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input &&
			Pin->PinType.PinCategory == UAnimationGraphSchema::PC_Struct)
		{
			InputPosePin = Pin;
			break;
		}
	}

	bool bPoseWired = false;
	if (OutputPosePin && InputPosePin)
	{
		const UEdGraphSchema* Schema = AnimGraph->GetSchema();
		bPoseWired = Schema->TryCreateConnection(OutputPosePin, InputPosePin);

		if (bPoseWired)
		{
			UE_LOG(LogArborAnimBP, Log, TEXT("Wired BlendSpacePlayer pose -> Root"));
		}
		else
		{
			UE_LOG(LogArborAnimBP, Warning, TEXT("TryCreateConnection failed for pose pins"));
		}
	}
	else
	{
		// Fallback: try by pin name
		UEdGraphPin* FallbackOutput = BSPlayerNode->FindPin(TEXT("Pose"));
		if (!FallbackOutput)
		{
			FallbackOutput = BSPlayerNode->FindPin(TEXT("Output Pose"));
		}

		UEdGraphPin* FallbackInput = RootNode->FindPin(TEXT("Result"));
		if (!FallbackInput)
		{
			FallbackInput = RootNode->FindPin(TEXT("Pose"));
		}

		if (FallbackOutput && FallbackInput)
		{
			const UEdGraphSchema* Schema = AnimGraph->GetSchema();
			bPoseWired = Schema->TryCreateConnection(FallbackOutput, FallbackInput);
		}

		if (!bPoseWired)
		{
			UE_LOG(LogArborAnimBP, Warning, TEXT("Could not find or connect pose pins. "
				"Output pins: %d, Input pins: %d"), BSPlayerNode->Pins.Num(), RootNode->Pins.Num());

			// Log all pins for debugging
			for (UEdGraphPin* Pin : BSPlayerNode->Pins)
			{
				UE_LOG(LogArborAnimBP, Log, TEXT("  BSPlayer pin: %s dir=%s cat=%s"),
					*Pin->GetName(),
					Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"),
					*Pin->PinType.PinCategory.ToString());
			}
			for (UEdGraphPin* Pin : RootNode->Pins)
			{
				UE_LOG(LogArborAnimBP, Log, TEXT("  Root pin: %s dir=%s cat=%s"),
					*Pin->GetName(),
					Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"),
					*Pin->PinType.PinCategory.ToString());
			}
		}
	}

	// -----------------------------------------------------------------------
	// Ensure all variables exist in the AnimBP
	// -----------------------------------------------------------------------
	for (const FVarBinding& Binding : Bindings)
	{
		bool bVariableExists = false;
		for (const FBPVariableDescription& Var : AnimBP->NewVariables)
		{
			if (Var.VarName == FName(*Binding.Name))
			{
				bVariableExists = true;
				break;
			}
		}

		if (!bVariableExists)
		{
			FBPVariableDescription NewVar;
			NewVar.VarName = FName(*Binding.Name);
			NewVar.FriendlyName = Binding.Name;
			NewVar.Category = FText::FromString(TEXT("Default"));
			NewVar.PropertyFlags = CPF_Edit | CPF_BlueprintVisible | CPF_DisableEditOnInstance;
			NewVar.VarType.PinCategory = UEdGraphSchema_K2::PC_Real;
			NewVar.VarType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
			NewVar.DefaultValue = TEXT("0.0");
			AnimBP->NewVariables.Add(NewVar);

			UE_LOG(LogArborAnimBP, Log, TEXT("Added variable '%s' (Float) to AnimBP"), *Binding.Name);
		}
	}

	// Compile so the generated class knows about the new variables —
	// AllocateDefaultPins on VariableGet needs the property to exist.
	FKismetEditorUtilities::CompileBlueprint(AnimBP);

	// -----------------------------------------------------------------------
	// Create VariableGet nodes and wire each to its BlendSpace axis pin
	// -----------------------------------------------------------------------
	bool bAllVariablesWired = true;
	TArray<TSharedPtr<FJsonValue>> VariablesWiredArray;

	for (int32 BindIdx = 0; BindIdx < Bindings.Num(); BindIdx++)
	{
		const FVarBinding& Binding = Bindings[BindIdx];

		UK2Node_VariableGet* VarGetNode = NewObject<UK2Node_VariableGet>(AnimGraph);
		VarGetNode->VariableReference.SetSelfMember(FName(*Binding.Name));
		AnimGraph->AddNode(VarGetNode, false, false);
		VarGetNode->CreateNewGuid();
		VarGetNode->PostPlacedNewNode();
		VarGetNode->AllocateDefaultPins();

		// Stagger Y position for each binding
		VarGetNode->NodePosX = BSPlayerNode->NodePosX - 250;
		VarGetNode->NodePosY = BSPlayerNode->NodePosY + 100 + (BindIdx * 80);

		// Wire VariableGet output -> BlendSpacePlayer axis pin
		UEdGraphPin* VarOutputPin = VarGetNode->GetValuePin();
		FString TargetPinName = Binding.Axis.ToUpper() == TEXT("Y") ? TEXT("Y") : TEXT("X");
		int32 TargetAxisIndex = Binding.Axis.ToUpper() == TEXT("Y") ? 1 : 0;

		// Strategy 1: find by literal name "X" / "Y"
		UEdGraphPin* AxisPin = BSPlayerNode->FindPin(TargetPinName);

		// Strategy 2: find by BlendSpace parameter display name
		if (!AxisPin && BlendSpace)
		{
			const FBlendParameter& Param = BlendSpace->GetBlendParameter(TargetAxisIndex);
			if (!Param.DisplayName.IsEmpty())
			{
				AxisPin = BSPlayerNode->FindPin(Param.DisplayName);
			}
		}

		// Strategy 3: find by float input pin index
		if (!AxisPin)
		{
			int32 FloatIndex = 0;
			for (UEdGraphPin* Pin : BSPlayerNode->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Input &&
					Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
				{
					if (FloatIndex == TargetAxisIndex)
					{
						AxisPin = Pin;
						break;
					}
					FloatIndex++;
				}
			}
		}

		bool bWired = false;
		if (VarOutputPin && AxisPin)
		{
			const UEdGraphSchema* Schema = AnimGraph->GetSchema();
			bWired = Schema->TryCreateConnection(VarOutputPin, AxisPin);

			if (bWired)
			{
				UE_LOG(LogArborAnimBP, Log, TEXT("Wired %s variable -> BlendSpacePlayer pin '%s'"),
					*Binding.Name, *AxisPin->GetName());
			}
		}

		if (!bWired)
		{
			bAllVariablesWired = false;
			UE_LOG(LogArborAnimBP, Warning,
				TEXT("Could not wire variable '%s' to BlendSpacePlayer axis %s. "
					"VarOutputPin=%s, AxisPin=%s"),
				*Binding.Name, *TargetPinName,
				VarOutputPin ? TEXT("valid") : TEXT("null"),
				AxisPin ? TEXT("valid") : TEXT("null"));

			// Log available BSPlayer pins for debugging
			for (UEdGraphPin* Pin : BSPlayerNode->Pins)
			{
				if (Pin->Direction == EGPD_Input)
				{
					UE_LOG(LogArborAnimBP, Log, TEXT("  BSPlayer input pin: %s (cat=%s sub=%s)"),
						*Pin->GetName(), *Pin->PinType.PinCategory.ToString(),
						*Pin->PinType.PinSubCategory.ToString());
				}
			}
		}

		// Track per-binding result
		auto BindingResult = MakeShared<FJsonObject>();
		BindingResult->SetStringField(TEXT("name"), Binding.Name);
		BindingResult->SetStringField(TEXT("axis"), Binding.Axis);
		BindingResult->SetBoolField(TEXT("wired"), bWired);
		VariablesWiredArray.Add(MakeShared<FJsonValueObject>(BindingResult));
	}

	// -----------------------------------------------------------------------
	// Compile and save
	// -----------------------------------------------------------------------
	FKismetEditorUtilities::CompileBlueprint(AnimBP);
	SaveAsset(AnimBP);

	UE_LOG(LogArborAnimBP, Log, TEXT("SetupLocomotionGraph complete: %s"), *AssetPath);

	// Build result JSON
	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("blendspace_node_guid"), BSPlayerNode->NodeGuid.ToString());
	Result->SetBoolField(TEXT("pose_wired"), bPoseWired);
	Result->SetBoolField(TEXT("variable_wired"), bAllVariablesWired);
	Result->SetArrayField(TEXT("variables_wired"), VariablesWiredArray);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Result, Writer);
	return OutputString;
}

// ============================================================================
// QueryAnimGraph
// ============================================================================

FString UAnimBlueprintBuilder::QueryAnimGraph(const FString& AssetPath)
{
	UAnimBlueprint* AnimBP = LoadAnimBlueprintForEditing(AssetPath);
	if (!AnimBP)
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"AnimBlueprint not found: %s\"}"), *AssetPath);
	}

	auto Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("name"), AnimBP->GetName());
	Root->SetStringField(TEXT("parent_class"), AnimBP->ParentClass ?
		AnimBP->ParentClass->GetName() : TEXT("None"));

	// Skeleton
	USkeleton* Skeleton = AnimBP->TargetSkeleton.Get();
	Root->SetStringField(TEXT("skeleton"), Skeleton ? Skeleton->GetPathName() : TEXT("None"));

	// Variables
	TArray<TSharedPtr<FJsonValue>> VarsArray;
	for (const FBPVariableDescription& Var : AnimBP->NewVariables)
	{
		auto VarObj = MakeShared<FJsonObject>();
		VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
		if (!Var.DefaultValue.IsEmpty())
		{
			VarObj->SetStringField(TEXT("default"), Var.DefaultValue);
		}
		VarsArray.Add(MakeShared<FJsonValueObject>(VarObj));
	}
	Root->SetArrayField(TEXT("variables"), VarsArray);

	// AnimGraph
	UEdGraph* AnimGraph = GetAnimGraph(AnimBP);
	if (AnimGraph)
	{
		auto GraphObj = MakeShared<FJsonObject>();

		// Nodes
		TArray<TSharedPtr<FJsonValue>> NodesArray;
		for (UEdGraphNode* Node : AnimGraph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			auto NodeJson = SerializeAnimGraphNodeToJson(Node);
			if (NodeJson.IsValid())
			{
				NodesArray.Add(MakeShared<FJsonValueObject>(NodeJson));
			}
		}
		GraphObj->SetArrayField(TEXT("nodes"), NodesArray);

		// Connections (output -> input)
		TArray<TSharedPtr<FJsonValue>> ConnectionsArray;
		for (UEdGraphNode* Node : AnimGraph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output)
				{
					continue;
				}

				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin)
					{
						continue;
					}

					UEdGraphNode* TargetNode = LinkedPin->GetOwningNode();
					if (!TargetNode)
					{
						continue;
					}

					auto ConnObj = MakeShared<FJsonObject>();
					ConnObj->SetStringField(TEXT("from"), Node->NodeGuid.ToString());
					ConnObj->SetStringField(TEXT("from_pin"), Pin->GetName());
					ConnObj->SetStringField(TEXT("to"), TargetNode->NodeGuid.ToString());
					ConnObj->SetStringField(TEXT("to_pin"), LinkedPin->GetName());
					ConnectionsArray.Add(MakeShared<FJsonValueObject>(ConnObj));
				}
			}
		}
		GraphObj->SetArrayField(TEXT("connections"), ConnectionsArray);

		Root->SetObjectField(TEXT("anim_graph"), GraphObj);
	}

	// Serialize
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Root, Writer);
	return OutputString;
}
