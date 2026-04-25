#include "ArborAnchorComponent.h"
#include "ArborAnchorTypes.h"
#include "ArborAnchorAnalyzer.h"
#include "ArborSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "DrawDebugHelpers.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"

// ---------------------------------------------------------------------------
// Anchor color LUT (matches ArborAnchorAnalyzer)
// ---------------------------------------------------------------------------

static FColor GetAnchorColor(const FString& Type)
{
	if (Type == TEXT("floor_edge"))      return FColor(0,   100, 255);  // Blue
	if (Type == TEXT("wall_edge"))       return FColor(255, 128, 0);    // Orange
	if (Type == TEXT("wall_connector"))  return FColor(255, 200, 0);    // Yellow
	if (Type == TEXT("wall_snap"))       return FColor(255, 0,   255);  // Magenta
	if (Type == TEXT("snap_point"))      return FColor(255, 0,   0);    // Red
	if (Type == TEXT("surface"))         return FColor(0,   230, 50);   // Green
	if (Type == TEXT("door"))            return FColor(200, 0,   200);  // Purple
	if (Type == TEXT("path_connect"))    return FColor(0,   200, 200);  // Cyan
	if (Type == TEXT("road_edge"))       return FColor(128, 128, 128);  // Gray
	if (Type == TEXT("door_opening"))    return FColor(255, 100, 50);   // Dark Orange
	if (Type == TEXT("window_opening"))  return FColor(100, 200, 255);  // Light Blue
	return FColor(180, 180, 180);
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

UArborAnchorComponent::UArborAnchorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	bTickInEditor = true;
	bAutoActivate = true;

	// This is an editor-only debug component
	bIsEditorOnly = true;
}

// ---------------------------------------------------------------------------
// OnRegister — load anchors when component is added
// ---------------------------------------------------------------------------

void UArborAnchorComponent::OnRegister()
{
	Super::OnRegister();
	ReloadAnchors();
}

// ---------------------------------------------------------------------------
// ReloadAnchors
// ---------------------------------------------------------------------------

void UArborAnchorComponent::ReloadAnchors()
{
	Anchors.Empty();
	bAnchorsLoaded = false;
	AssetPath.Empty();

	FString Path = ResolveAssetPath();
	if (Path.IsEmpty())
	{
		return;
	}

	AssetPath = Path;
	bAnchorsLoaded = LoadAnchorsFromRegistry(AssetPath);

	UE_LOG(LogTemp, Log, TEXT("[ArborAnchorComponent] %s: loaded %d anchors for %s"),
		*GetOwner()->GetActorLabel(), Anchors.Num(), *AssetPath);
}

// ---------------------------------------------------------------------------
// TickComponent — draw debug anchors each frame when setting is on
// ---------------------------------------------------------------------------

void UArborAnchorComponent::TickComponent(float DeltaTime, enum ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bAnchorsLoaded || Anchors.Num() == 0)
	{
		return;
	}

	const UArborSettings* Settings = GetDefault<UArborSettings>();
	if (!Settings || !Settings->bShowAnchorDebug)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const FTransform ActorTransform = Owner->GetActorTransform();
	const float Radius = 6.0f;
	const float ArrowLength = 30.0f;
	const float Thickness = 2.0f;
	const float MaxDrawDistance = 5000.0f;

	// Get camera location for distance culling
	FVector CameraLocation = FVector::ZeroVector;
	bool bHasCamera = false;
	if (GEditor && GEditor->GetActiveViewport())
	{
		FEditorViewportClient* ViewportClient = static_cast<FEditorViewportClient*>(
			GEditor->GetActiveViewport()->GetClient());
		if (ViewportClient)
		{
			CameraLocation = ViewportClient->GetViewLocation();
			bHasCamera = true;
		}
	}

	for (const FArborAnchor& Anchor : Anchors)
	{
		// Transform anchor position and direction from local to world space
		FVector WorldPos = ActorTransform.TransformPosition(Anchor.Position);

		// Distance culling
		if (bHasCamera && FVector::Dist(WorldPos, CameraLocation) > MaxDrawDistance)
		{
			continue;
		}

		FVector WorldDir = ActorTransform.TransformVectorNoScale(Anchor.Direction).GetSafeNormal();
		FColor Color = GetAnchorColor(Anchor.Type);

		// Sphere at anchor position (single-frame draws, refreshed each tick)
		DrawDebugSphere(World, WorldPos, Radius, 12, Color, false, 0.0f, SDPG_Foreground, Thickness);

		// Direction arrow
		FVector ArrowEnd = WorldPos + WorldDir * ArrowLength;
		DrawDebugDirectionalArrow(World, WorldPos, ArrowEnd, 15.0f, Color, false, 0.0f, SDPG_Foreground, Thickness);

		// Label
		DrawDebugString(World, WorldPos + FVector(0, 0, Radius + 5), Anchor.Id, nullptr, Color, 0.0f, false, 1.0f);
	}
}

// ---------------------------------------------------------------------------
// ResolveAssetPath — find the static mesh on the owning actor
// ---------------------------------------------------------------------------

FString UArborAnchorComponent::ResolveAssetPath() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FString();
	}

	// Try StaticMeshActor first
	AStaticMeshActor* SMActor = Cast<AStaticMeshActor>(Owner);
	if (SMActor)
	{
		UStaticMeshComponent* SMC = SMActor->GetStaticMeshComponent();
		if (SMC && SMC->GetStaticMesh())
		{
			return SMC->GetStaticMesh()->GetPathName();
		}
	}

	// Fallback: find any StaticMeshComponent
	UStaticMeshComponent* SMC = Owner->FindComponentByClass<UStaticMeshComponent>();
	if (SMC && SMC->GetStaticMesh())
	{
		return SMC->GetStaticMesh()->GetPathName();
	}

	return FString();
}

// ---------------------------------------------------------------------------
// LoadAnchorsFromRegistry
// ---------------------------------------------------------------------------

bool UArborAnchorComponent::LoadAnchorsFromRegistry(const FString& MeshPath)
{
	// Primary: load from anchor registry data asset
	UArborAnchorRegistry* Registry = UArborAnchorRegistry::FindRegistry(MeshPath);
	if (Registry)
	{
		const FArborMeshAnchors* Data = Registry->FindAnchors(MeshPath);
		if (Data)
		{
			Anchors = Data->Anchors;
			return Anchors.Num() > 0;
		}
	}

	// Fallback: legacy sidecar JSON
	FString SidecarPath = UArborAnchorAnalyzer::GetSidecarPath(MeshPath);
	FString JsonStr;
	if (!FFileHelper::LoadFileToString(JsonStr, *SidecarPath))
	{
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	auto Reader = TJsonReaderFactory<>::Create(JsonStr);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* AnchorsArr;
	if (!Root->TryGetArrayField(TEXT("anchors"), AnchorsArr))
	{
		return false;
	}

	for (const auto& AnchorVal : *AnchorsArr)
	{
		auto AnchorObj = AnchorVal->AsObject();
		if (!AnchorObj.IsValid()) continue;

		FArborAnchor Data;
		Data.Id = AnchorObj->GetStringField(TEXT("id"));
		AnchorObj->TryGetStringField(TEXT("type"), Data.Type);

		const TSharedPtr<FJsonObject>* PosObj;
		if (AnchorObj->TryGetObjectField(TEXT("position"), PosObj))
		{
			Data.Position.X = (*PosObj)->GetNumberField(TEXT("x"));
			Data.Position.Y = (*PosObj)->GetNumberField(TEXT("y"));
			Data.Position.Z = (*PosObj)->GetNumberField(TEXT("z"));
		}

		const TSharedPtr<FJsonObject>* DirObj;
		if (AnchorObj->TryGetObjectField(TEXT("direction"), DirObj))
		{
			Data.Direction.X = (*DirObj)->GetNumberField(TEXT("x"));
			Data.Direction.Y = (*DirObj)->GetNumberField(TEXT("y"));
			Data.Direction.Z = (*DirObj)->GetNumberField(TEXT("z"));
		}

		Anchors.Add(Data);
	}

	return Anchors.Num() > 0;
}
