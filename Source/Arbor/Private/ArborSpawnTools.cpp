#include "ArborSpawnTools.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "EditorAssetLibrary.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/DirectionalLight.h"
#include "Engine/RectLight.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// ============================================================================
// Helpers
// ============================================================================

static UEditorActorSubsystem* GetEditorActorSubsystem()
{
	return GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
}

static FString SerializeJson(TSharedPtr<FJsonObject> Root)
{
	FString Output;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}

static TSharedPtr<FJsonObject> ParseJson(const FString& Json)
{
	TSharedPtr<FJsonObject> Obj;
	auto Reader = TJsonReaderFactory<>::Create(Json);
	FJsonSerializer::Deserialize(Reader, Obj);
	return Obj;
}

static double GetOpt(const TSharedPtr<FJsonObject>& Obj, const FString& Key, double Default)
{
	double Val;
	if (Obj->TryGetNumberField(Key, Val)) return Val;
	return Default;
}

static FString GetOptStr(const TSharedPtr<FJsonObject>& Obj, const FString& Key, const FString& Default = TEXT(""))
{
	FString Val;
	if (Obj->TryGetStringField(Key, Val)) return Val;
	return Default;
}

// ============================================================================
// SpawnLight
// ============================================================================

FString UArborSpawnTools::SpawnLight(const FString& ParamsJson)
{
	auto Params = ParseJson(ParamsJson);
	if (!Params.IsValid())
	{
		return TEXT("{\"error\":\"Invalid JSON\"}");
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return TEXT("{\"error\":\"No editor world\"}");
	}

	const FString LightType = Params->GetStringField(TEXT("light_type"));
	const double X = GetOpt(Params, TEXT("x"), 0);
	const double Y = GetOpt(Params, TEXT("y"), 0);
	const double Z = GetOpt(Params, TEXT("z"), 0);
	const double Intensity = GetOpt(Params, TEXT("intensity"), 5000);
	const double Attenuation = GetOpt(Params, TEXT("attenuation_radius"), 1000);
	const FString Label = GetOptStr(Params, TEXT("label"));

	// Parse color
	double R = 1.0, G = 0.9, B = 0.8;
	const TSharedPtr<FJsonObject>* ColorObj;
	if (Params->TryGetObjectField(TEXT("color"), ColorObj))
	{
		R = GetOpt(*ColorObj, TEXT("r"), 1.0);
		G = GetOpt(*ColorObj, TEXT("g"), 0.9);
		B = GetOpt(*ColorObj, TEXT("b"), 0.8);
	}

	// Resolve light class
	UClass* LightClass = nullptr;
	if (LightType == TEXT("point")) LightClass = APointLight::StaticClass();
	else if (LightType == TEXT("spot")) LightClass = ASpotLight::StaticClass();
	else if (LightType == TEXT("directional")) LightClass = ADirectionalLight::StaticClass();
	else if (LightType == TEXT("rect")) LightClass = ARectLight::StaticClass();
	else
	{
		return FString::Printf(TEXT("{\"error\":\"Unknown light type: %s\"}"), *LightType);
	}

	const FVector Location(X, Y, Z);
	AActor* Actor = GetEditorActorSubsystem()->SpawnActorFromClass(LightClass, Location, FRotator::ZeroRotator);
	if (!Actor)
	{
		return FString::Printf(TEXT("{\"error\":\"Failed to spawn %s light\"}"), *LightType);
	}

	// Configure light component
	ULightComponent* LightComp = Actor->FindComponentByClass<ULightComponent>();
	if (LightComp)
	{
		LightComp->SetIntensity(Intensity);
		LightComp->SetLightColor(FLinearColor(R, G, B, 1.0f));

		if (LightType == TEXT("point") || LightType == TEXT("spot"))
		{
			if (UPointLightComponent* PLC = Cast<UPointLightComponent>(LightComp))
			{
				PLC->SetAttenuationRadius(Attenuation);
			}
		}
	}

	if (!Label.IsEmpty())
	{
		Actor->SetActorLabel(Label);
	}

	TSharedPtr<FJsonObject> PosObj = MakeShared<FJsonObject>();
	PosObj->SetNumberField(TEXT("x"), X);
	PosObj->SetNumberField(TEXT("y"), Y);
	PosObj->SetNumberField(TEXT("z"), Z);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("actor_path"), Actor->GetPathName());
	Root->SetStringField(TEXT("light_type"), LightType);
	Root->SetObjectField(TEXT("position"), PosObj);
	return SerializeJson(Root);
}

// ============================================================================
// SpawnPrimitive
// ============================================================================

FString UArborSpawnTools::SpawnPrimitive(const FString& ParamsJson)
{
	auto Params = ParseJson(ParamsJson);
	if (!Params.IsValid())
	{
		return TEXT("{\"error\":\"Invalid JSON\"}");
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return TEXT("{\"error\":\"No editor world\"}");
	}

	const FString Shape = Params->GetStringField(TEXT("shape"));
	const double X = GetOpt(Params, TEXT("x"), 0);
	const double Y = GetOpt(Params, TEXT("y"), 0);
	const double Z = GetOpt(Params, TEXT("z"), 0);
	const double SX = GetOpt(Params, TEXT("scale_x"), 1);
	const double SY = GetOpt(Params, TEXT("scale_y"), 1);
	const double SZ = GetOpt(Params, TEXT("scale_z"), 1);
	const double Pitch = GetOpt(Params, TEXT("pitch"), 0);
	const double Yaw = GetOpt(Params, TEXT("yaw"), 0);
	const double Roll = GetOpt(Params, TEXT("roll"), 0);
	const FString Label = GetOptStr(Params, TEXT("label"));

	// Map shape to engine basic shape path
	FString MeshPath;
	if (Shape == TEXT("cube")) MeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	else if (Shape == TEXT("sphere")) MeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	else if (Shape == TEXT("cylinder")) MeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	else if (Shape == TEXT("cone")) MeshPath = TEXT("/Engine/BasicShapes/Cone.Cone");
	else if (Shape == TEXT("plane")) MeshPath = TEXT("/Engine/BasicShapes/Plane.Plane");
	else
	{
		return FString::Printf(TEXT("{\"error\":\"Unknown shape: %s\"}"), *Shape);
	}

	UStaticMesh* Mesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *MeshPath));
	if (!Mesh)
	{
		return FString::Printf(TEXT("{\"error\":\"Could not load mesh: %s\"}"), *MeshPath);
	}

	const FVector Location(X, Y, Z);
	const FRotator Rotation(Pitch, Yaw, Roll);
	AActor* Actor = GetEditorActorSubsystem()->SpawnActorFromObject(Mesh, Location, Rotation);
	if (!Actor)
	{
		return FString::Printf(TEXT("{\"error\":\"Failed to spawn %s\"}"), *Shape);
	}

	Actor->SetActorScale3D(FVector(SX, SY, SZ));

	if (!Label.IsEmpty())
	{
		Actor->SetActorLabel(Label);
	}

	FString ActorName = Label.IsEmpty() ? Actor->GetName() : Actor->GetActorLabel();

	TSharedPtr<FJsonObject> PosObj = MakeShared<FJsonObject>();
	PosObj->SetNumberField(TEXT("x"), X);
	PosObj->SetNumberField(TEXT("y"), Y);
	PosObj->SetNumberField(TEXT("z"), Z);

	TSharedPtr<FJsonObject> ScObj = MakeShared<FJsonObject>();
	ScObj->SetNumberField(TEXT("x"), SX);
	ScObj->SetNumberField(TEXT("y"), SY);
	ScObj->SetNumberField(TEXT("z"), SZ);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("actor_path"), Actor->GetPathName());
	Root->SetStringField(TEXT("actor_name"), ActorName);
	Root->SetStringField(TEXT("shape"), Shape);
	Root->SetObjectField(TEXT("position"), PosObj);
	Root->SetObjectField(TEXT("scale"), ScObj);
	return SerializeJson(Root);
}

// ============================================================================
// SpawnNavMesh
// ============================================================================

FString UArborSpawnTools::SpawnNavMesh(const FString& ParamsJson)
{
	auto Params = ParseJson(ParamsJson);
	if (!Params.IsValid())
	{
		return TEXT("{\"error\":\"Invalid JSON\"}");
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return TEXT("{\"error\":\"No editor world\"}");
	}

	const double X = GetOpt(Params, TEXT("x"), 0);
	const double Y = GetOpt(Params, TEXT("y"), 0);
	const double Z = GetOpt(Params, TEXT("z"), 0);
	const double EX = GetOpt(Params, TEXT("extent_x"), 2000);
	const double EY = GetOpt(Params, TEXT("extent_y"), 2000);
	const double EZ = GetOpt(Params, TEXT("extent_z"), 2000);

	const FVector Location(X, Y, Z);
	AActor* Actor = GetEditorActorSubsystem()->SpawnActorFromClass(
		ANavMeshBoundsVolume::StaticClass(), Location, FRotator::ZeroRotator);
	if (!Actor)
	{
		return TEXT("{\"error\":\"Failed to spawn NavMeshBoundsVolume\"}");
	}

	// Default brush half-extent is 100. Scale to match desired extents.
	const double DefaultHalf = 100.0;
	Actor->SetActorScale3D(FVector(EX / DefaultHalf, EY / DefaultHalf, EZ / DefaultHalf));

	TSharedPtr<FJsonObject> PosObj = MakeShared<FJsonObject>();
	PosObj->SetNumberField(TEXT("x"), X);
	PosObj->SetNumberField(TEXT("y"), Y);
	PosObj->SetNumberField(TEXT("z"), Z);

	TSharedPtr<FJsonObject> ExtObj = MakeShared<FJsonObject>();
	ExtObj->SetNumberField(TEXT("x"), EX);
	ExtObj->SetNumberField(TEXT("y"), EY);
	ExtObj->SetNumberField(TEXT("z"), EZ);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("actor_path"), Actor->GetPathName());
	Root->SetObjectField(TEXT("position"), PosObj);
	Root->SetObjectField(TEXT("extent"), ExtObj);
	return SerializeJson(Root);
}

// ============================================================================
// PlaceActor
// ============================================================================

FString UArborSpawnTools::PlaceActor(const FString& ParamsJson)
{
	auto Params = ParseJson(ParamsJson);
	if (!Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return TEXT("{\"success\":false,\"error\":\"No editor world\"}");
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const double X = GetOpt(Params, TEXT("x"), 0);
	const double Y = GetOpt(Params, TEXT("y"), 0);
	const double Z = GetOpt(Params, TEXT("z"), 0);
	const double Pitch = GetOpt(Params, TEXT("pitch"), 0);
	const double Yaw = GetOpt(Params, TEXT("yaw"), 0);
	const double Roll = GetOpt(Params, TEXT("roll"), 0);
	const double SX = GetOpt(Params, TEXT("scale_x"), 1);
	const double SY = GetOpt(Params, TEXT("scale_y"), 1);
	const double SZ = GetOpt(Params, TEXT("scale_z"), 1);

	// Load asset
	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Could not load asset: %s\"}"), *AssetPath);
	}

	// If it's a Blueprint, get its GeneratedClass for spawning
	UBlueprint* BP = Cast<UBlueprint>(Asset);
	AActor* Actor = nullptr;
	const FVector Location(X, Y, Z);
	const FRotator Rotation(Pitch, Yaw, Roll);

	if (BP && BP->GeneratedClass && BP->GeneratedClass->IsChildOf(AActor::StaticClass()))
	{
		Actor = GetEditorActorSubsystem()->SpawnActorFromClass(
			TSubclassOf<AActor>(BP->GeneratedClass), Location, Rotation);
	}
	else
	{
		Actor = GetEditorActorSubsystem()->SpawnActorFromObject(Asset, Location, Rotation);
	}

	if (!Actor)
	{
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Failed to spawn actor from: %s\"}"), *AssetPath);
	}

	// Apply scale if non-default
	if (SX != 1.0 || SY != 1.0 || SZ != 1.0)
	{
		Actor->SetActorScale3D(FVector(SX, SY, SZ));
	}

	TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
	LocObj->SetNumberField(TEXT("x"), X);
	LocObj->SetNumberField(TEXT("y"), Y);
	LocObj->SetNumberField(TEXT("z"), Z);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("actor_name"), Actor->GetActorLabel().IsEmpty() ? Actor->GetName() : Actor->GetActorLabel());
	Root->SetStringField(TEXT("actor_path"), Actor->GetPathName());
	Root->SetObjectField(TEXT("location"), LocObj);
	return SerializeJson(Root);
}

// ============================================================================
// ScatterMeshes
// ============================================================================

FString UArborSpawnTools::ScatterMeshes(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> Params;
	auto Reader = TJsonReaderFactory<>::Create(ParamsJson);
	if (!FJsonSerializer::Deserialize(Reader, Params) || !Params.IsValid())
	{
		return TEXT("{\"success\":false,\"placed\":0,\"error\":\"Invalid JSON\"}");
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return TEXT("{\"success\":false,\"placed\":0,\"error\":\"No editor world\"}");
	}

	UEditorActorSubsystem* Sub = GetEditorActorSubsystem();
	if (!Sub)
	{
		return TEXT("{\"success\":false,\"placed\":0,\"error\":\"No EditorActorSubsystem\"}");
	}

	const FString MeshPath = Params->GetStringField(TEXT("mesh_path"));
	UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
	if (!Mesh)
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"placed\":0,\"error\":\"Cannot load mesh: %s\"}"), *MeshPath);
	}

	const int32 Count = (int32)Params->GetNumberField(TEXT("count"));
	const bool bSnapToGround = !Params->HasField(TEXT("snap_to_ground"))
		|| Params->GetBoolField(TEXT("snap_to_ground"));
	const bool bRandomYaw = !Params->HasField(TEXT("random_yaw"))
		|| Params->GetBoolField(TEXT("random_yaw"));
	const float ScaleMin = Params->HasField(TEXT("scale_min"))
		? Params->GetNumberField(TEXT("scale_min")) : 0.8f;
	const float ScaleMax = Params->HasField(TEXT("scale_max"))
		? Params->GetNumberField(TEXT("scale_max")) : 1.2f;
	const int32 Seed = Params->HasField(TEXT("seed"))
		? (int32)Params->GetNumberField(TEXT("seed")) : FMath::Rand();
	const FString LabelPrefix = Params->HasField(TEXT("label_prefix"))
		? Params->GetStringField(TEXT("label_prefix")) : TEXT("Scattered");

	// Parse bounds
	const TArray<TSharedPtr<FJsonValue>>* BMinArr;
	const TArray<TSharedPtr<FJsonValue>>* BMaxArr;
	if (!Params->TryGetArrayField(TEXT("bounds_min"), BMinArr) ||
		!Params->TryGetArrayField(TEXT("bounds_max"), BMaxArr) ||
		BMinArr->Num() < 3 || BMaxArr->Num() < 3)
	{
		return TEXT("{\"success\":false,\"placed\":0,\"error\":\"Missing bounds_min/bounds_max\"}");
	}

	const float XMin = (*BMinArr)[0]->AsNumber();
	const float YMin = (*BMinArr)[1]->AsNumber();
	const float ZMin = (*BMinArr)[2]->AsNumber();
	const float XMax = (*BMaxArr)[0]->AsNumber();
	const float YMax = (*BMaxArr)[1]->AsNumber();
	const float ZMax = (*BMaxArr)[2]->AsNumber();

	FRandomStream RNG(Seed);
	TArray<TSharedPtr<FJsonValue>> ActorsArray;
	int32 Placed = 0;

	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(ArborScatterTrace), true);

	for (int32 i = 0; i < Count; i++)
	{
		float X = RNG.FRandRange(XMin, XMax);
		float Y = RNG.FRandRange(YMin, YMax);
		float Z = RNG.FRandRange(ZMin, ZMax);

		if (bSnapToGround)
		{
			FVector Start(X, Y, ZMax + 10000.0f);
			FVector End(X, Y, ZMin - 50000.0f);
			FHitResult HitResult;
			if (World->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility, TraceParams))
			{
				Z = HitResult.ImpactPoint.Z;
			}
		}

		float Yaw = bRandomYaw ? RNG.FRandRange(0.0f, 360.0f) : 0.0f;
		float Scale = RNG.FRandRange(ScaleMin, ScaleMax);

		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(),
			FVector(X, Y, Z),
			FRotator(0.0f, Yaw, 0.0f));

		if (!Actor) continue;

		Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
		Actor->SetActorScale3D(FVector(Scale, Scale, Scale));
		Actor->SetActorLabel(FString::Printf(TEXT("%s_%d"), *LabelPrefix, i));
		Placed++;

		TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
		ActorObj->SetStringField(TEXT("name"), Actor->GetActorLabel());
		TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
		LocObj->SetNumberField(TEXT("x"), X);
		LocObj->SetNumberField(TEXT("y"), Y);
		LocObj->SetNumberField(TEXT("z"), Z);
		ActorObj->SetObjectField(TEXT("location"), LocObj);
		ActorsArray.Add(MakeShared<FJsonValueObject>(ActorObj));
	}

	UE_LOG(LogTemp, Log, TEXT("[ArborSpawnTools] ScatterMeshes: placed %d/%d '%s'"),
		Placed, Count, *MeshPath);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetNumberField(TEXT("placed"), Placed);
	Root->SetArrayField(TEXT("actors"), ActorsArray);
	return SerializeJson(Root);
}
