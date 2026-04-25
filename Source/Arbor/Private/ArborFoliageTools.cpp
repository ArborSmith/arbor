#include "ArborFoliageTools.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "InstancedFoliageActor.h"
#include "FoliageType.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "CollisionQueryParams.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeStreamingProxy.h"

// ============================================================================
// Helpers
// ============================================================================

static FString SerializeJson(TSharedPtr<FJsonObject> Root)
{
	FString Output;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}

static FVector ParseJsonVector(const TArray<TSharedPtr<FJsonValue>>& Arr)
{
	if (Arr.Num() >= 3)
	{
		return FVector(Arr[0]->AsNumber(), Arr[1]->AsNumber(), Arr[2]->AsNumber());
	}
	return FVector::ZeroVector;
}

static bool TraceGroundZAt(UWorld* World, float X, float Y, float StartZ,
	float TraceDistance, float& OutZ)
{
	if (!World) return false;

	const FVector Start(X, Y, StartZ);
	const FVector End(X, Y, StartZ - TraceDistance);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ArborFoliageTrace), true);

	FHitResult HitResult;
	if (World->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility, Params))
	{
		OutZ = HitResult.ImpactPoint.Z;
		return true;
	}
	return false;
}

static ALandscapeProxy* FindLandscapeInWorld(UWorld* World)
{
	if (!World) return nullptr;

	for (TActorIterator<ALandscape> It(World); It; ++It)
	{
		return *It;
	}
	for (TActorIterator<ALandscapeStreamingProxy> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

static int32 CountFoliageInstances(UWorld* World)
{
	int32 Total = 0;
	for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
	{
		TArray<UActorComponent*> Comps;
		It->GetComponents(UInstancedStaticMeshComponent::StaticClass(), Comps);
		for (UActorComponent* Comp : Comps)
		{
			UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Comp);
			if (ISM)
			{
				Total += ISM->GetInstanceCount();
			}
		}
	}
	return Total;
}

// ============================================================================
// CreateFoliageType
// ============================================================================

FString UArborFoliageTools::CreateFoliageType(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> Params;
	auto Reader = TJsonReaderFactory<>::Create(ParamsJson);
	if (!FJsonSerializer::Deserialize(Reader, Params) || !Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	const FString MeshPath = Params->GetStringField(TEXT("mesh_path"));
	UStaticMesh* Mesh = Cast<UStaticMesh>(
		UEditorAssetLibrary::LoadAsset(MeshPath));
	if (!Mesh)
	{
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Cannot load mesh: %s\"}"), *MeshPath);
	}

	const FString ContentPath = Params->HasField(TEXT("content_path"))
		? Params->GetStringField(TEXT("content_path")) : TEXT("/Game/Foliage");
	FString AssetName = Params->HasField(TEXT("name"))
		? Params->GetStringField(TEXT("name"))
		: FString::Printf(TEXT("FT_%s"), *Mesh->GetName());

	// Create asset via AssetTools
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(
		"AssetTools").Get();

	UFoliageType_InstancedStaticMesh* FoliageType =
		Cast<UFoliageType_InstancedStaticMesh>(
			AssetTools.CreateAsset(AssetName, ContentPath,
				UFoliageType_InstancedStaticMesh::StaticClass(), nullptr));

	if (!FoliageType)
	{
		return TEXT("{\"success\":false,\"error\":\"Failed to create foliage type asset\"}");
	}

	// Configure properties
	FoliageType->Mesh = Mesh;

	const float Density = Params->HasField(TEXT("density"))
		? Params->GetNumberField(TEXT("density")) : 100.0f;
	FoliageType->Density = Density;

	const float ScaleMin = Params->HasField(TEXT("scale_min"))
		? Params->GetNumberField(TEXT("scale_min")) : 0.8f;
	const float ScaleMax = Params->HasField(TEXT("scale_max"))
		? Params->GetNumberField(TEXT("scale_max")) : 1.2f;
	FoliageType->ScaleX = FFloatInterval(ScaleMin, ScaleMax);
	FoliageType->ScaleY = FFloatInterval(ScaleMin, ScaleMax);
	FoliageType->ScaleZ = FFloatInterval(ScaleMin, ScaleMax);

	const bool bAlignToNormal = !Params->HasField(TEXT("align_to_normal"))
		|| Params->GetBoolField(TEXT("align_to_normal"));
	FoliageType->AlignToNormal = bAlignToNormal;

	const bool bRandomYaw = !Params->HasField(TEXT("random_yaw"))
		|| Params->GetBoolField(TEXT("random_yaw"));
	FoliageType->RandomYaw = bRandomYaw;

	const float GroundSlopeAngle = Params->HasField(TEXT("ground_slope_angle"))
		? Params->GetNumberField(TEXT("ground_slope_angle")) : 45.0f;
	FoliageType->GroundSlopeAngle = FFloatInterval(0.0f, GroundSlopeAngle);

	const int32 CullDistMax = Params->HasField(TEXT("cull_distance_max"))
		? (int32)Params->GetNumberField(TEXT("cull_distance_max")) : 10000;
	const int32 CullDistStart = FMath::RoundToInt32(CullDistMax * 0.75f);
	FoliageType->CullDistance = FInt32Interval(CullDistStart, CullDistMax);

	UEditorAssetLibrary::SaveLoadedAsset(FoliageType);

	const FString FullPath = FString::Printf(TEXT("%s/%s"), *ContentPath, *AssetName);
	UE_LOG(LogTemp, Log, TEXT("[ArborFoliageTools] Created foliage type '%s' for mesh '%s'"),
		*FullPath, *MeshPath);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), FullPath);
	return SerializeJson(Result);
}

// ============================================================================
// PaintFoliageInstances
// ============================================================================

FString UArborFoliageTools::PaintFoliageInstances(const FString& ParamsJson)
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

	const FString FoliageTypePath = Params->GetStringField(TEXT("foliage_type_path"));
	UFoliageType_InstancedStaticMesh* FoliageType =
		Cast<UFoliageType_InstancedStaticMesh>(
			UEditorAssetLibrary::LoadAsset(FoliageTypePath));
	if (!FoliageType)
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"placed\":0,\"error\":\"Cannot load foliage type: %s\"}"),
			*FoliageTypePath);
	}

	UStaticMesh* Mesh = FoliageType->Mesh;
	const int32 Count = Params->HasField(TEXT("count"))
		? (int32)Params->GetNumberField(TEXT("count")) : 100;
	const bool bSnapToGround = !Params->HasField(TEXT("snap_to_ground"))
		|| Params->GetBoolField(TEXT("snap_to_ground"));
	const bool bRandomYaw = FoliageType->RandomYaw;
	const float ScaleMin = FoliageType->ScaleX.Min;
	const float ScaleMax = FoliageType->ScaleX.Max;
	const int32 Seed = Params->HasField(TEXT("seed"))
		? (int32)Params->GetNumberField(TEXT("seed")) : FMath::Rand();

	FRandomStream RNG(Seed);

	// Determine scatter bounds
	float XMin, XMax, YMin, YMax, ZTop;
	bool bCircular = false;
	FVector Center = FVector::ZeroVector;
	float Radius = 1000.0f;

	const TArray<TSharedPtr<FJsonValue>>* CenterArr;
	if (Params->TryGetArrayField(TEXT("center"), CenterArr))
	{
		Center = ParseJsonVector(*CenterArr);
		Radius = Params->HasField(TEXT("radius"))
			? Params->GetNumberField(TEXT("radius")) : 1000.0f;
		bCircular = true;
		XMin = Center.X - Radius;
		XMax = Center.X + Radius;
		YMin = Center.Y - Radius;
		YMax = Center.Y + Radius;
		ZTop = Center.Z + 10000.0f;
	}
	else
	{
		const TArray<TSharedPtr<FJsonValue>>* BMinArr;
		const TArray<TSharedPtr<FJsonValue>>* BMaxArr;
		if (Params->TryGetArrayField(TEXT("bounds_min"), BMinArr) &&
			Params->TryGetArrayField(TEXT("bounds_max"), BMaxArr))
		{
			FVector BMin = ParseJsonVector(*BMinArr);
			FVector BMax = ParseJsonVector(*BMaxArr);
			XMin = BMin.X; XMax = BMax.X;
			YMin = BMin.Y; YMax = BMax.Y;
			ZTop = BMax.Z + 10000.0f;
		}
		else
		{
			// Use landscape bounds
			ALandscapeProxy* Landscape = FindLandscapeInWorld(World);
			if (!Landscape)
			{
				return TEXT("{\"success\":false,\"placed\":0,\"error\":\"No landscape found and no bounds specified\"}");
			}
			FVector Origin, Extent;
			Landscape->GetActorBounds(false, Origin, Extent);
			XMin = Origin.X - Extent.X;
			XMax = Origin.X + Extent.X;
			YMin = Origin.Y - Extent.Y;
			YMax = Origin.Y + Extent.Y;
			ZTop = Origin.Z + Extent.Z + 10000.0f;
		}
	}

	// Generate positions
	TArray<FTransform> Transforms;
	Transforms.Reserve(Count);

	int32 MaxAttempts = Count * 5;
	int32 Attempts = 0;

	while (Transforms.Num() < Count && Attempts < MaxAttempts)
	{
		Attempts++;

		float X = RNG.FRandRange(XMin, XMax);
		float Y = RNG.FRandRange(YMin, YMax);

		// Circular rejection
		if (bCircular)
		{
			float DX = X - Center.X;
			float DY = Y - Center.Y;
			if (DX * DX + DY * DY > Radius * Radius)
			{
				continue;
			}
		}

		float Z = 0.0f;
		if (bSnapToGround)
		{
			if (!TraceGroundZAt(World, X, Y, ZTop, ZTop + 50000.0f, Z))
			{
				continue;  // No ground hit — skip
			}
		}

		FTransform T;
		T.SetTranslation(FVector(X, Y, Z));

		float Yaw = bRandomYaw ? RNG.FRandRange(0.0f, 360.0f) : 0.0f;
		T.SetRotation(FRotator(0.0f, Yaw, 0.0f).Quaternion());

		float Scale = RNG.FRandRange(ScaleMin, ScaleMax);
		T.SetScale3D(FVector(Scale, Scale, Scale));

		Transforms.Add(T);
	}

	if (Transforms.Num() == 0)
	{
		return TEXT("{\"success\":true,\"placed\":0,\"method\":\"none\"}");
	}

	// Try InstancedFoliageActor first
	FString Method = TEXT("none");
	int32 Placed = 0;

	int32 CountBefore = CountFoliageInstances(World);

	// Get or create the foliage actor for this level
	AInstancedFoliageActor* IFA = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, true);
	if (IFA)
	{
		FFoliageInfo* FoliageInfo = IFA->FindOrAddMesh(FoliageType);
		if (FoliageInfo)
		{
			for (const FTransform& T : Transforms)
			{
				FFoliageInstance Instance;
				Instance.Location = T.GetTranslation();
				Instance.Rotation = T.GetRotation().Rotator();
				FVector3d Scale = T.GetScale3D();
				Instance.DrawScale3D = FVector3f(Scale.X, Scale.Y, Scale.Z);
				FoliageInfo->AddInstance(FoliageType, Instance, nullptr);
			}
			FoliageInfo->Refresh(true, false);
		}
	}

	int32 CountAfter = CountFoliageInstances(World);
	int32 Added = CountAfter - CountBefore;

	if (Added >= Transforms.Num() / 2)
	{
		Placed = Added;
		Method = TEXT("foliage");
		UE_LOG(LogTemp, Log, TEXT("[ArborFoliageTools] Placed %d instances via InstancedFoliageActor"),
			Placed);
	}
	else
	{
		// HISM fallback
		UE_LOG(LogTemp, Warning,
			TEXT("[ArborFoliageTools] InstancedFoliageActor added %d/%d, falling back to HISM"),
			Added, Transforms.Num());

		AActor* HISMActor = World->SpawnActor<AActor>(AActor::StaticClass());
		if (HISMActor)
		{
			FString MeshName = Mesh ? Mesh->GetName() : TEXT("unknown");
			HISMActor->SetActorLabel(FString::Printf(TEXT("Foliage_%s"), *MeshName));

			UHierarchicalInstancedStaticMeshComponent* HISM =
				NewObject<UHierarchicalInstancedStaticMeshComponent>(HISMActor);
			if (HISM)
			{
				HISM->SetStaticMesh(Mesh);
				HISM->SetMobility(EComponentMobility::Static);
				HISM->RegisterComponent();
				HISMActor->AddInstanceComponent(HISM);

				for (const FTransform& T : Transforms)
				{
					HISM->AddInstance(T);
				}

				Placed = Transforms.Num();
				Method = TEXT("hism");
				UE_LOG(LogTemp, Log,
					TEXT("[ArborFoliageTools] Placed %d instances via HISM fallback"), Placed);
			}
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("placed"), Placed);
	Result->SetStringField(TEXT("method"), Method);
	return SerializeJson(Result);
}

// ============================================================================
// RemoveFoliageInstances
// ============================================================================

FString UArborFoliageTools::RemoveFoliageInstances(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> Params;
	auto Reader = TJsonReaderFactory<>::Create(ParamsJson);
	if (!FJsonSerializer::Deserialize(Reader, Params) || !Params.IsValid())
	{
		return TEXT("{\"success\":false,\"removed\":0,\"error\":\"Invalid JSON\"}");
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return TEXT("{\"success\":false,\"removed\":0,\"error\":\"No editor world\"}");
	}

	const FString FoliageTypePath = Params->GetStringField(TEXT("foliage_type_path"));
	UFoliageType_InstancedStaticMesh* FoliageType =
		Cast<UFoliageType_InstancedStaticMesh>(
			UEditorAssetLibrary::LoadAsset(FoliageTypePath));

	int32 Removed = 0;

	// Remove via InstancedFoliageActor
	if (FoliageType)
	{
		for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
		{
			AInstancedFoliageActor* IFA = *It;
			// Remove all instances of this foliage type from this actor
			TArray<UActorComponent*> Comps;
			IFA->GetComponents(UInstancedStaticMeshComponent::StaticClass(), Comps);
			for (UActorComponent* Comp : Comps)
			{
				UInstancedStaticMeshComponent* ISM =
					Cast<UInstancedStaticMeshComponent>(Comp);
				if (ISM && ISM->GetStaticMesh() == FoliageType->Mesh)
				{
					int32 InstCount = ISM->GetInstanceCount();
					ISM->ClearInstances();
					Removed += InstCount;
				}
			}
		}
	}

	// Remove HISM fallback actors
	FString MeshName;
	if (FoliageType && FoliageType->Mesh)
	{
		MeshName = FoliageType->Mesh->GetName();
	}

	TArray<AActor*> ToDestroy;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		FString Label = It->GetActorLabel();
		if (Label.StartsWith(TEXT("Foliage_")))
		{
			bool bShouldRemove = false;
			if (!MeshName.IsEmpty() && Label.Contains(MeshName))
			{
				bShouldRemove = true;
			}
			if (bShouldRemove)
			{
				ToDestroy.Add(*It);
			}
		}
	}

	for (AActor* Actor : ToDestroy)
	{
		Actor->Destroy();
		Removed++;
	}

	UE_LOG(LogTemp, Log, TEXT("[ArborFoliageTools] Removed %d foliage entries"), Removed);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("removed"), Removed);
	return SerializeJson(Result);
}

// ============================================================================
// GetFoliageCount
// ============================================================================

FString UArborFoliageTools::GetFoliageCount()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	int32 Total = World ? CountFoliageInstances(World) : 0;
	return FString::Printf(TEXT("{\"count\":%d}"), Total);
}
