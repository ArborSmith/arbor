#include "ArborEnvironmentSpawner.h"
#include "ArborAnchorComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

static TSharedPtr<FJsonObject> ParseJson(const FString& Json)
{
	TSharedPtr<FJsonObject> Obj;
	auto Reader = TJsonReaderFactory<>::Create(Json);
	FJsonSerializer::Deserialize(Reader, Obj);
	return Obj;
}

static FString SerializeJson(TSharedPtr<FJsonObject> Root)
{
	FString Output;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}

static UEditorActorSubsystem* GetEditorActorSubsystem()
{
	return GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
}

// ---------------------------------------------------------------------------
// SpawnEnvironment
// ---------------------------------------------------------------------------

FString UArborEnvironmentSpawner::SpawnEnvironment(const FString& ParamsJson)
{
	auto Params = ParseJson(ParamsJson);
	if (!Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	const FString EnvId = Params->GetStringField(TEXT("environment_id"));
	if (EnvId.IsEmpty())
	{
		return TEXT("{\"success\":false,\"error\":\"Missing 'environment_id'\"}");
	}

	const TSharedPtr<FJsonObject>* NodesObj;
	if (!Params->TryGetObjectField(TEXT("nodes"), NodesObj))
	{
		return TEXT("{\"success\":false,\"error\":\"Missing 'nodes'\"}");
	}

	UEditorActorSubsystem* ActorSub = GetEditorActorSubsystem();
	if (!ActorSub)
	{
		return TEXT("{\"success\":false,\"error\":\"No editor actor subsystem\"}");
	}

	TArray<TSharedPtr<FJsonValue>> SpawnedArr;
	TArray<TSharedPtr<FJsonValue>> FailedArr;

	for (const auto& Pair : (*NodesObj)->Values)
	{
		const FString& NodeId = Pair.Key;
		auto NodeObj = Pair.Value->AsObject();
		if (!NodeObj.IsValid())
		{
			TSharedPtr<FJsonObject> F = MakeShared<FJsonObject>();
			F->SetStringField(TEXT("node_id"), NodeId);
			F->SetStringField(TEXT("error"), TEXT("Invalid node JSON"));
			FailedArr.Add(MakeShared<FJsonValueObject>(F));
			continue;
		}

		const FString AssetPath = NodeObj->GetStringField(TEXT("asset_path"));

		// Parse location
		FVector Location = FVector::ZeroVector;
		const TSharedPtr<FJsonObject>* LocObj;
		if (NodeObj->TryGetObjectField(TEXT("location"), LocObj))
		{
			Location.X = (*LocObj)->GetNumberField(TEXT("x"));
			Location.Y = (*LocObj)->GetNumberField(TEXT("y"));
			Location.Z = (*LocObj)->GetNumberField(TEXT("z"));
		}

		// Parse rotation
		FRotator Rotation = FRotator::ZeroRotator;
		const TSharedPtr<FJsonObject>* RotObj;
		if (NodeObj->TryGetObjectField(TEXT("rotation"), RotObj))
		{
			double Pitch = 0, Yaw = 0, Roll = 0;
			(*RotObj)->TryGetNumberField(TEXT("pitch"), Pitch);
			(*RotObj)->TryGetNumberField(TEXT("yaw"), Yaw);
			(*RotObj)->TryGetNumberField(TEXT("roll"), Roll);
			Rotation = FRotator(Pitch, Yaw, Roll);
		}

		// Parse scale
		FVector Scale = FVector::OneVector;
		const TSharedPtr<FJsonObject>* ScaleObj;
		if (NodeObj->TryGetObjectField(TEXT("scale"), ScaleObj))
		{
			Scale.X = (*ScaleObj)->GetNumberField(TEXT("x"));
			Scale.Y = (*ScaleObj)->GetNumberField(TEXT("y"));
			Scale.Z = (*ScaleObj)->GetNumberField(TEXT("z"));
		}

		// Load mesh
		UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(AssetPath));
		if (!Mesh)
		{
			TSharedPtr<FJsonObject> F = MakeShared<FJsonObject>();
			F->SetStringField(TEXT("node_id"), NodeId);
			F->SetStringField(TEXT("error"), FString::Printf(TEXT("Cannot load mesh: %s"), *AssetPath));
			FailedArr.Add(MakeShared<FJsonValueObject>(F));
			continue;
		}

		// Spawn
		AActor* Actor = ActorSub->SpawnActorFromObject(Mesh, Location, Rotation);
		if (!Actor)
		{
			TSharedPtr<FJsonObject> F = MakeShared<FJsonObject>();
			F->SetStringField(TEXT("node_id"), NodeId);
			F->SetStringField(TEXT("error"), TEXT("SpawnActorFromObject failed"));
			FailedArr.Add(MakeShared<FJsonValueObject>(F));
			continue;
		}

		Actor->SetActorScale3D(Scale);

		// Label for tracking
		FString Label = FString::Printf(TEXT("Env_%s_%s"), *EnvId, *NodeId);
		FString CustomLabel;
		if (NodeObj->TryGetStringField(TEXT("label"), CustomLabel) && !CustomLabel.IsEmpty())
		{
			Label = CustomLabel;
		}
		Actor->SetActorLabel(Label);

		// Attach anchor debug component (auto-loads sidecar if present)
		UArborAnchorComponent* AnchorComp = NewObject<UArborAnchorComponent>(
			Actor, UArborAnchorComponent::StaticClass(), TEXT("ArborAnchors"));
		AnchorComp->RegisterComponent();
		Actor->AddInstanceComponent(AnchorComp);

		UE_LOG(LogTemp, Log, TEXT("[ArborEnvironmentSpawner] Spawned '%s' at (%.0f, %.0f, %.0f)"),
			*Label, Location.X, Location.Y, Location.Z);

		TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("node_id"), NodeId);
		S->SetStringField(TEXT("actor_name"), Actor->GetActorLabel());
		S->SetStringField(TEXT("actor_path"), Actor->GetPathName());
		SpawnedArr.Add(MakeShared<FJsonValueObject>(S));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), FailedArr.Num() == 0);
	Result->SetArrayField(TEXT("spawned"), SpawnedArr);
	Result->SetArrayField(TEXT("failed"), FailedArr);
	return SerializeJson(Result);
}

// ---------------------------------------------------------------------------
// DespawnEnvironment
// ---------------------------------------------------------------------------

FString UArborEnvironmentSpawner::DespawnEnvironment(const FString& EnvironmentId)
{
	if (EnvironmentId.IsEmpty())
	{
		return TEXT("{\"success\":false,\"error\":\"Missing environment_id\"}");
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return TEXT("{\"success\":false,\"error\":\"No editor world\"}");
	}

	const FString Prefix = FString::Printf(TEXT("Env_%s_"), *EnvironmentId);

	TArray<AActor*> ToDestroy;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && Actor->GetActorLabel().StartsWith(Prefix))
		{
			ToDestroy.Add(Actor);
		}
	}

	UEditorActorSubsystem* ActorSub = GetEditorActorSubsystem();
	for (AActor* Actor : ToDestroy)
	{
		UE_LOG(LogTemp, Log, TEXT("[ArborEnvironmentSpawner] Destroying '%s'"),
			*Actor->GetActorLabel());
		if (ActorSub)
		{
			ActorSub->DestroyActor(Actor);
		}
		else
		{
			Actor->Destroy();
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("destroyed_count"), ToDestroy.Num());
	return SerializeJson(Result);
}
