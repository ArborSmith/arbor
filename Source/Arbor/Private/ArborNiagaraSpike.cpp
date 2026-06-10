#include "ArborNiagaraSpike.h"

#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraTypes.h"
#include "NiagaraCommon.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSpriteRendererProperties.h"
#include "Materials/MaterialInterface.h"

#include "NiagaraSystemFactoryNew.h"
#include "NiagaraEmitterFactoryNew.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EditorAssetLibrary.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// ============================================================================
// JSON helpers (mirrors ArborMaterialGraphTools.cpp style)
// ============================================================================

namespace
{
	FString SerializeJson(const TSharedRef<FJsonObject>& Root)
	{
		FString Output;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Root, Writer);
		return Output;
	}

	TSharedPtr<FJsonObject> ParseJson(const FString& Json)
	{
		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		FJsonSerializer::Deserialize(Reader, Obj);
		return Obj;
	}

	/** Accumulates per-step pass/fail results so a late failure still reports
	 *  everything that worked before it. */
	struct FSpikeLog
	{
		TArray<TSharedPtr<FJsonValue>> Steps;
		bool bAllOk = true;

		void Add(const FString& Step, bool bOk, const FString& Detail)
		{
			const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("step"), Step);
			Obj->SetBoolField(TEXT("success"), bOk);
			Obj->SetStringField(TEXT("detail"), Detail);
			Steps.Add(MakeShared<FJsonValueObject>(Obj));
			bAllOk &= bOk;
			UE_LOG(LogTemp, Log, TEXT("[ArborNiagaraSpike] %s: %s (%s)"),
				*Step, bOk ? TEXT("OK") : TEXT("FAIL"), *Detail);
		}
	};

	// ========================================================================
	// Spike primitives (prototypes for UArborNiagaraTools)
	// ========================================================================

	UNiagaraSystem* CreateSystemAsset(const FString& Path, FSpikeLog& Log)
	{
		if (UEditorAssetLibrary::DoesAssetExist(Path))
		{
			UEditorAssetLibrary::DeleteAsset(Path);
		}

		UPackage* Package = CreatePackage(*Path);
		if (!Package)
		{
			Log.Add(TEXT("create_system"), false, FString::Printf(TEXT("CreatePackage failed for %s"), *Path));
			return nullptr;
		}

		const FString AssetName = FPackageName::GetLongPackageAssetName(Path);
		UNiagaraSystem* System = NewObject<UNiagaraSystem>(
			Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (!System)
		{
			Log.Add(TEXT("create_system"), false, TEXT("NewObject<UNiagaraSystem> failed"));
			return nullptr;
		}

		UNiagaraSystemFactoryNew::InitializeSystem(System, true);
		FAssetRegistryModule::AssetCreated(System);
		System->MarkPackageDirty();
		Log.Add(TEXT("create_system"), true, Path);
		return System;
	}

	FNiagaraEmitterHandle* AddEmitter(UNiagaraSystem* System, const FString& Name,
		bool bWithDefaults, FSpikeLog& Log)
	{
		UNiagaraEmitter* Temp = NewObject<UNiagaraEmitter>(GetTransientPackage(), NAME_None, RF_Transient);
		if (!Temp)
		{
			Log.Add(TEXT("add_emitter"), false, TEXT("NewObject<UNiagaraEmitter> failed"));
			return nullptr;
		}
		UNiagaraEmitterFactoryNew::InitializeEmitter(Temp, bWithDefaults);

		const FGuid Version = Temp->GetExposedVersion().VersionGuid;
		const FGuid HandleId = FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Temp, Version, /*bCreateCopy=*/true);

		for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			if (Handle.GetId() == HandleId)
			{
				Handle.SetName(FName(*Name), *System);
				Log.Add(TEXT("add_emitter"), true, FString::Printf(
					TEXT("name=%s defaults=%d handle=%s"), *Name, bWithDefaults ? 1 : 0, *HandleId.ToString()));
				return &Handle;
			}
		}
		Log.Add(TEXT("add_emitter"), false, TEXT("AddEmitterToSystem returned an id with no matching handle"));
		return nullptr;
	}

	UNiagaraGraph* GetEmitterGraph(const FNiagaraEmitterHandle& Handle)
	{
		const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		if (!Data)
		{
			return nullptr;
		}
		const UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Data->GraphSource);
		return Source ? Source->NodeGraph : nullptr;
	}

	/** Load a module script by long path; fall back to an asset registry search
	 *  by asset name under /Niagara so wrong folder guesses self-heal. */
	UNiagaraScript* ResolveModuleScript(const FString& PathOrName, FSpikeLog& Log)
	{
		UNiagaraScript* Script = LoadObject<UNiagaraScript>(nullptr, *PathOrName);
		if (Script)
		{
			return Script;
		}

		const FString ShortName = FPackageName::GetLongPackageAssetName(PathOrName);
		const IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
			TEXT("AssetRegistry")).Get();

		TArray<FAssetData> Assets;
		FARFilter Filter;
		Filter.ClassPaths.Add(UNiagaraScript::StaticClass()->GetClassPathName());
		Filter.PackagePaths.Add(TEXT("/Niagara"));
		Filter.bRecursivePaths = true;
		Registry.GetAssets(Filter, Assets);

		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetName.ToString().Equals(ShortName, ESearchCase::IgnoreCase))
			{
				Script = Cast<UNiagaraScript>(Asset.GetAsset());
				if (Script)
				{
					Log.Add(TEXT("resolve_module"), true, FString::Printf(
						TEXT("'%s' not at given path; resolved to %s"),
						*PathOrName, *Asset.GetSoftObjectPath().ToString()));
					return Script;
				}
			}
		}
		Log.Add(TEXT("resolve_module"), false, FString::Printf(TEXT("could not resolve '%s'"), *PathOrName));
		return nullptr;
	}

	UNiagaraNodeFunctionCall* AddModule(UNiagaraGraph* Graph, ENiagaraScriptUsage Usage,
		const FString& ScriptPath, const FString& ArborId, FSpikeLog& Log)
	{
		UNiagaraScript* ModuleScript = ResolveModuleScript(ScriptPath, Log);
		if (!ModuleScript)
		{
			return nullptr;
		}

		UNiagaraNodeOutput* OutputNode = Graph->FindEquivalentOutputNode(Usage);
		if (!OutputNode)
		{
			Log.Add(TEXT("add_module"), false, FString::Printf(
				TEXT("no output node for usage %d in graph"), (int32)Usage));
			return nullptr;
		}

		UNiagaraNodeFunctionCall* FunctionNode =
			FNiagaraStackGraphUtilities::AddScriptModuleToStack(ModuleScript, *OutputNode);
		if (!FunctionNode)
		{
			Log.Add(TEXT("add_module"), false, FString::Printf(
				TEXT("AddScriptModuleToStack returned null for %s"), *ScriptPath));
			return nullptr;
		}

		// Sentinel for idempotent rebuilds (same scheme as material expressions,
		// but materials use Desc; Niagara nodes expose NodeComment).
		FunctionNode->NodeComment = FString::Printf(TEXT("__arbor_id:%s"), *ArborId);
		FunctionNode->bCommentBubbleVisible = false;

		Log.Add(TEXT("add_module"), true, FString::Printf(
			TEXT("%s as '%s' (usage %d)"), *ScriptPath, *FunctionNode->GetFunctionName(), (int32)Usage));
		return FunctionNode;
	}

	/** Write a local (rapid iteration) value for a module input. Returns the
	 *  generated constant name so the spike can report the alias format. */
	template <typename T>
	bool SetLocalInput(UNiagaraScript* OwningScript, const FString& EmitterUniqueName,
		ENiagaraScriptUsage Usage, UNiagaraNodeFunctionCall* FunctionNode,
		const FString& InputName, const FNiagaraTypeDefinition& Type, const T& Value,
		FString& OutConstantName)
	{
		if (!OwningScript || !FunctionNode)
		{
			return false;
		}

		const FString AliasedName = FunctionNode->GetFunctionName() + TEXT(".") + InputName;
		FNiagaraVariable Var(Type, FName(*AliasedName));
		const FNiagaraVariable RapidVar = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(
			Var, *EmitterUniqueName, Usage);
		OutConstantName = RapidVar.GetName().ToString();

		return OwningScript->RapidIterationParameters.SetParameterValue(Value, RapidVar, /*bAdd=*/true);
	}

	/** Local reimplementation of module removal (RemoveModuleFromStack is not
	 *  exported): splice the ParameterMap chain around the node, then remove it.
	 *  Only safe for modules without override nodes; M1 extends this. */
	bool RemoveModuleBySplice(UNiagaraGraph* Graph, UNiagaraNodeFunctionCall* Node, FString& OutError)
	{
		UEdGraphPin* LinkedInputPin = nullptr;
		UEdGraphPin* LinkedOutputPin = nullptr;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == EGPD_Input && Pin->LinkedTo.Num() > 0 && !LinkedInputPin)
			{
				LinkedInputPin = Pin;
			}
			if (Pin->Direction == EGPD_Output && Pin->LinkedTo.Num() > 0 && !LinkedOutputPin)
			{
				LinkedOutputPin = Pin;
			}
		}
		if (!LinkedInputPin || !LinkedOutputPin)
		{
			OutError = TEXT("module node is not wired into a ParameterMap chain");
			return false;
		}

		UEdGraphPin* Previous = LinkedInputPin->LinkedTo[0];
		const TArray<UEdGraphPin*> Following = LinkedOutputPin->LinkedTo;

		Node->BreakAllNodeLinks();
		for (UEdGraphPin* Next : Following)
		{
			Previous->MakeLinkTo(Next);
		}
		Graph->RemoveNode(Node);
		Graph->NotifyGraphChanged();
		return true;
	}

	/** Per-script compile status as JSON. */
	TSharedRef<FJsonObject> ScriptCompileStatus(const FString& Label, UNiagaraScript* Script)
	{
		const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("script"), Label);
		if (!Script)
		{
			Obj->SetStringField(TEXT("status"), TEXT("missing"));
			return Obj;
		}

		const ENiagaraScriptCompileStatus Status = Script->GetLastCompileStatus();
		Obj->SetStringField(TEXT("status"),
			StaticEnum<ENiagaraScriptCompileStatus>()->GetNameStringByValue((int64)Status));

		TArray<TSharedPtr<FJsonValue>> Errors;
		const FNiagaraVMExecutableData& VMData = Script->GetVMExecutableData();
		if (!VMData.ErrorMsg.IsEmpty())
		{
			Errors.Add(MakeShared<FJsonValueString>(VMData.ErrorMsg));
		}
		for (const FNiagaraCompileEvent& Event : VMData.LastCompileEvents)
		{
			if (Event.Severity == FNiagaraCompileEventSeverity::Error ||
				Event.Severity == FNiagaraCompileEventSeverity::Warning)
			{
				Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s: %s"),
					Event.Severity == FNiagaraCompileEventSeverity::Error ? TEXT("error") : TEXT("warning"),
					*Event.Message)));
			}
		}
		Obj->SetArrayField(TEXT("errors"), Errors);
		return Obj;
	}

	/** Compile a system and report every script's status. */
	TArray<TSharedPtr<FJsonValue>> CompileAndReport(UNiagaraSystem* System, FSpikeLog& Log)
	{
		// Creates missing rapid iteration parameters from module defaults and
		// propagates emitter script values into the aggregated system scripts.
		// Without this, values written to emitter-owned scripts never reach the
		// compiled system simulation.
		System->PrepareRapidIterationParametersForCompilation();
		System->RequestCompile(/*bForce=*/true);
		System->WaitForCompilationComplete(/*bIncludingGPUShaders=*/false, /*bShowProgress=*/false);
		Log.Add(TEXT("compile"), true, TEXT("RequestCompile + WaitForCompilationComplete returned"));

		TArray<TSharedPtr<FJsonValue>> Result;
		Result.Add(MakeShared<FJsonValueObject>(
			ScriptCompileStatus(TEXT("system_spawn"), System->GetSystemSpawnScript())));
		Result.Add(MakeShared<FJsonValueObject>(
			ScriptCompileStatus(TEXT("system_update"), System->GetSystemUpdateScript())));

		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
			if (!Data)
			{
				continue;
			}
			const FString Prefix = Handle.GetName().ToString();
			Result.Add(MakeShared<FJsonValueObject>(ScriptCompileStatus(
				Prefix + TEXT(".particle_spawn"), Data->SpawnScriptProps.Script)));
			Result.Add(MakeShared<FJsonValueObject>(ScriptCompileStatus(
				Prefix + TEXT(".particle_update"), Data->UpdateScriptProps.Script)));
		}
		return Result;
	}

	// ========================================================================
	// Part A: default-template emitter, every input mechanism exercised
	// ========================================================================

	TSharedRef<FJsonObject> RunPartA(const FString& Path, FSpikeLog& Log,
		TArray<TSharedPtr<FJsonValue>>& OutRapidIterationParams)
	{
		const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("path"), Path);

		UNiagaraSystem* System = CreateSystemAsset(Path, Log);
		if (!System)
		{
			return Out;
		}

		FNiagaraEmitterHandle* Handle = AddEmitter(System, TEXT("spike"), /*bWithDefaults=*/true, Log);
		if (!Handle)
		{
			return Out;
		}
		UNiagaraGraph* Graph = GetEmitterGraph(*Handle);
		if (!Graph)
		{
			Log.Add(TEXT("emitter_graph"), false, TEXT("GraphSource is not a UNiagaraScriptSource or has no NodeGraph"));
			return Out;
		}
		Log.Add(TEXT("emitter_graph"), true, TEXT("resolved UNiagaraGraph from emitter GraphSource"));

		// --- Add a module to Particle Update ---
		UNiagaraNodeFunctionCall* GravityNode = AddModule(Graph, ENiagaraScriptUsage::ParticleUpdateScript,
			TEXT("/Niagara/Modules/Update/Forces/GravityForce.GravityForce"), TEXT("grav"), Log);

		// --- Local input via rapid iteration parameter ---
		const FVersionedNiagaraEmitter VersionedEmitter = Handle->GetInstance();
		FVersionedNiagaraEmitterData* Data = Handle->GetEmitterData();
		if (GravityNode && Data && VersionedEmitter.Emitter)
		{
			FString ConstantName;
			const bool bSet = SetLocalInput(Data->UpdateScriptProps.Script,
				VersionedEmitter.Emitter->GetUniqueEmitterName(),
				ENiagaraScriptUsage::ParticleUpdateScript, GravityNode,
				TEXT("Gravity"), FNiagaraTypeDefinition::GetVec3Def(),
				FVector3f(0.0f, 0.0f, -2000.0f), ConstantName);
			Log.Add(TEXT("set_local_input"), bSet, FString::Printf(
				TEXT("rapid iteration constant '%s'"), *ConstantName));
		}

		// --- Module removal by local splice ---
		UNiagaraNodeFunctionCall* DragNode = AddModule(Graph, ENiagaraScriptUsage::ParticleUpdateScript,
			TEXT("/Niagara/Modules/Update/Forces/Drag.Drag"), TEXT("drag_tmp"), Log);
		if (DragNode)
		{
			FString Error;
			const bool bRemoved = RemoveModuleBySplice(Graph, DragNode, Error);
			Log.Add(TEXT("remove_module_splice"), bRemoved,
				bRemoved ? TEXT("Drag module spliced out of the chain") : Error);
		}

		// --- Renderer add/remove ---
		if (Data && VersionedEmitter.Emitter)
		{
			const int32 CountBefore = Data->GetRenderers().Num();
			UNiagaraSpriteRendererProperties* ExtraRenderer = NewObject<UNiagaraSpriteRendererProperties>(
				VersionedEmitter.Emitter, NAME_None, RF_Transactional);
			VersionedEmitter.Emitter->AddRenderer(ExtraRenderer, VersionedEmitter.Version);
			const int32 CountAfterAdd = Data->GetRenderers().Num();
			VersionedEmitter.Emitter->RemoveRenderer(ExtraRenderer, VersionedEmitter.Version);
			const int32 CountAfterRemove = Data->GetRenderers().Num();
			Log.Add(TEXT("renderer_add_remove"),
				CountAfterAdd == CountBefore + 1 && CountAfterRemove == CountBefore,
				FString::Printf(TEXT("renderers %d -> %d -> %d"), CountBefore, CountAfterAdd, CountAfterRemove));
		}

		// --- Compile + report ---
		Out->SetArrayField(TEXT("compile"), CompileAndReport(System, Log));

		// Dump rapid iteration parameter names so we learn the alias format.
		if (Data && Data->UpdateScriptProps.Script)
		{
			TArray<FNiagaraVariable> Parameters;
			Data->UpdateScriptProps.Script->RapidIterationParameters.GetParameters(Parameters);
			for (const FNiagaraVariable& Parameter : Parameters)
			{
				OutRapidIterationParams.Add(MakeShared<FJsonValueString>(Parameter.GetName().ToString()));
			}
		}

		const bool bSaved = UEditorAssetLibrary::SaveLoadedAsset(System, /*bOnlyIfIsDirty=*/false);
		Log.Add(TEXT("save_a"), bSaved, Path);
		return Out;
	}

	// ========================================================================
	// Part B: minimal emitter assembled from scratch
	// ========================================================================

	TSharedRef<FJsonObject> RunPartB(const FString& Path, FSpikeLog& Log)
	{
		const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("path"), Path);

		UNiagaraSystem* System = CreateSystemAsset(Path, Log);
		if (!System)
		{
			return Out;
		}

		FNiagaraEmitterHandle* Handle = AddEmitter(System, TEXT("minimal"), /*bWithDefaults=*/false, Log);
		if (!Handle)
		{
			return Out;
		}
		UNiagaraGraph* Graph = GetEmitterGraph(*Handle);
		if (!Graph)
		{
			Log.Add(TEXT("emitter_graph_b"), false, TEXT("no NodeGraph on minimal emitter"));
			return Out;
		}

		// Emitter Update: emitter state + spawn rate
		AddModule(Graph, ENiagaraScriptUsage::EmitterUpdateScript,
			TEXT("/Niagara/Modules/Emitter/EmitterState.EmitterState"), TEXT("state_e"), Log);
		UNiagaraNodeFunctionCall* SpawnRateNode = AddModule(Graph, ENiagaraScriptUsage::EmitterUpdateScript,
			TEXT("/Niagara/Modules/Emitter/SpawnRate.SpawnRate"), TEXT("rate"), Log);

		// Particle Spawn: initialize particle
		UNiagaraNodeFunctionCall* InitNode = AddModule(Graph, ENiagaraScriptUsage::ParticleSpawnScript,
			TEXT("/Niagara/Modules/Spawn/Initialization/InitializeParticle.InitializeParticle"), TEXT("init"), Log);

		// Particle Update: state + forces + solver
		AddModule(Graph, ENiagaraScriptUsage::ParticleUpdateScript,
			TEXT("/Niagara/Modules/Update/Lifetime/ParticleState.ParticleState"), TEXT("state_p"), Log);
		AddModule(Graph, ENiagaraScriptUsage::ParticleUpdateScript,
			TEXT("/Niagara/Modules/Update/Forces/GravityForce.GravityForce"), TEXT("grav"), Log);
		AddModule(Graph, ENiagaraScriptUsage::ParticleUpdateScript,
			TEXT("/Niagara/Modules/Update/Forces/Drag.Drag"), TEXT("drag"), Log);
		AddModule(Graph, ENiagaraScriptUsage::ParticleUpdateScript,
			TEXT("/Niagara/Modules/Solvers/SolveForcesAndVelocity.SolveForcesAndVelocity"), TEXT("solve"), Log);

		// Linked input via override pin: InitializeParticle Color -> User.SpikeColor
		if (InitNode)
		{
			const FNiagaraVariable UserColor(FNiagaraTypeDefinition::GetColorDef(), TEXT("User.SpikeColor"));
			System->GetExposedParameters().AddParameter(UserColor, /*bInitialize=*/true, /*bTriggerRebind=*/true, nullptr);

			const FNiagaraParameterHandle AliasedHandle =
				FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
					FName(TEXT("Module.Color")), FName(*InitNode->GetFunctionName()));
			UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
				*InitNode, AliasedHandle, FNiagaraTypeDefinition::GetColorDef(), FGuid(), FGuid());

			TSet<FNiagaraVariableBase> KnownParameters;
			KnownParameters.Add(UserColor);
			FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(
				OverridePin, UserColor, KnownParameters, ENiagaraDefaultMode::Value);
			Log.Add(TEXT("set_linked_input"), OverridePin.LinkedTo.Num() > 0, FString::Printf(
				TEXT("InitializeParticle Color -> User.SpikeColor (override pin links: %d)"),
				OverridePin.LinkedTo.Num()));
		}

		// Spawn rate value (emitter stage rapid iteration constants live on the
		// SYSTEM scripts with the emitter name in the alias; verify that here).
		const FVersionedNiagaraEmitter VersionedEmitter = Handle->GetInstance();
		FVersionedNiagaraEmitterData* EmitterData = Handle->GetEmitterData();
		if (SpawnRateNode && VersionedEmitter.Emitter && EmitterData)
		{
			FString ConstantName;
			const bool bSet = SetLocalInput(EmitterData->EmitterUpdateScriptProps.Script,
				VersionedEmitter.Emitter->GetUniqueEmitterName(),
				ENiagaraScriptUsage::EmitterUpdateScript, SpawnRateNode,
				TEXT("SpawnRate"), FNiagaraTypeDefinition::GetFloatDef(), 50.0f, ConstantName);
			Log.Add(TEXT("set_emitter_stage_input"), bSet, FString::Printf(
				TEXT("rapid iteration constant '%s' on emitter update script"), *ConstantName));
		}

		// Sprite renderer
		FVersionedNiagaraEmitterData* Data = Handle->GetEmitterData();
		if (Data && VersionedEmitter.Emitter)
		{
			UNiagaraSpriteRendererProperties* Sprite = NewObject<UNiagaraSpriteRendererProperties>(
				VersionedEmitter.Emitter, NAME_None, RF_Transactional);
			Sprite->Material = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Niagara/DefaultAssets/DefaultSpriteMaterial.DefaultSpriteMaterial"));
			VersionedEmitter.Emitter->AddRenderer(Sprite, VersionedEmitter.Version);
			Log.Add(TEXT("add_renderer_b"), Data->GetRenderers().Num() == 1, FString::Printf(
				TEXT("renderer count %d, material %s"), Data->GetRenderers().Num(),
				Sprite->Material ? *Sprite->Material->GetName() : TEXT("null")));
		}

		Out->SetArrayField(TEXT("compile"), CompileAndReport(System, Log));

		// Where did the SpawnRate constant land after preparation + compile?
		TArray<TSharedPtr<FJsonValue>> RapidParams;
		auto DumpStore = [&RapidParams](const FString& Label, UNiagaraScript* Script)
		{
			if (!Script)
			{
				return;
			}
			TArray<FNiagaraVariable> Parameters;
			Script->RapidIterationParameters.GetParameters(Parameters);
			for (const FNiagaraVariable& Parameter : Parameters)
			{
				RapidParams.Add(MakeShared<FJsonValueString>(
					Label + TEXT(": ") + Parameter.GetName().ToString()));
			}
		};
		DumpStore(TEXT("system_update"), System->GetSystemUpdateScript());
		if (EmitterData)
		{
			DumpStore(TEXT("emitter_update"), EmitterData->EmitterUpdateScriptProps.Script);
		}
		Out->SetArrayField(TEXT("rapid_iteration_params"), RapidParams);

		const bool bSaved = UEditorAssetLibrary::SaveLoadedAsset(System, /*bOnlyIfIsDirty=*/false);
		Log.Add(TEXT("save_b"), bSaved, Path);
		return Out;
	}
}

// ============================================================================
// Entry point
// ============================================================================

FString UArborNiagaraSpike::RunNiagaraSpike(const FString& ParamsJson)
{
	const TSharedPtr<FJsonObject> Params = ParseJson(ParamsJson);
	FString PathA = TEXT("/Game/VFX/Spike/NS_ArborSpikeA");
	FString PathB = TEXT("/Game/VFX/Spike/NS_ArborSpikeB");
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("path_a"), PathA);
		Params->TryGetStringField(TEXT("path_b"), PathB);
	}

	FSpikeLog Log;
	TArray<TSharedPtr<FJsonValue>> RapidIterationParams;

	const TSharedRef<FJsonObject> ResultA = RunPartA(PathA, Log, RapidIterationParams);
	const TSharedRef<FJsonObject> ResultB = RunPartB(PathB, Log);

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), Log.bAllOk);
	Root->SetArrayField(TEXT("steps"), Log.Steps);
	Root->SetObjectField(TEXT("a"), ResultA);
	Root->SetObjectField(TEXT("b"), ResultB);
	Root->SetArrayField(TEXT("rapid_iteration_params"), RapidIterationParams);
	return SerializeJson(Root);
}
