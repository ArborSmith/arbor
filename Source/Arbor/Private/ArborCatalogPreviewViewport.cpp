#include "ArborCatalogPreviewViewport.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "ArborCatalogPreviewViewport"


// ============================================================================
// FArborCatalogPreviewViewportClient
// ============================================================================

FArborCatalogPreviewViewportClient::FArborCatalogPreviewViewportClient(
	const TSharedRef<SEditorViewport>& InViewport,
	FAdvancedPreviewScene& InPreviewScene)
	: FEditorViewportClient(nullptr, &InPreviewScene, StaticCastSharedRef<SEditorViewport>(InViewport))
{
	// Sane defaults for a preview viewport: perspective, lit, single view.
	SetViewportType(ELevelViewportType::LVT_Perspective);
	SetViewMode(EViewModeIndex::VMI_Lit);
	SetRealtime(true);
	// Place camera at a reasonable distance from origin looking at the sphere.
	SetViewLocation(FVector(-200.f, 0.f, 80.f));
	SetViewRotation(FRotator(-15.f, 0.f, 0.f));

	// Disable widget gizmos and gridlines that would clutter the preview.
	EngineShowFlags.SetGrid(false);
	EngineShowFlags.SetGame(true);
	bUsingOrbitCamera = false;
	bDisableInput = false;

	// Create the sphere component using engine's basic shapes. Lift it by half
	// its diameter (100cm basic shape, radius 50) so it rests on the preview
	// scene's floor at Z=0 rather than clipping through it.
	UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh)
	{
		SphereComponent = NewObject<UStaticMeshComponent>(GetTransientPackage());
		SphereComponent->SetStaticMesh(SphereMesh);
		InPreviewScene.AddComponent(SphereComponent, FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, 50.f)));
	}
}


void FArborCatalogPreviewViewportClient::SetMaterial(UMaterialInterface* InMaterial)
{
	if (!SphereComponent) return;
	const int32 NumMats = SphereComponent->GetNumMaterials();
	for (int32 i = 0; i < NumMats; ++i)
	{
		SphereComponent->SetMaterial(i, InMaterial);
	}
	// Reset rotation so each new material starts at the same orientation.
	SphereYaw = 0.f;
	SpherePitch = 0.f;
	SphereComponent->SetWorldRotation(FRotator(0, 0, 0));
}


bool FArborCatalogPreviewViewportClient::InputKey(const FInputKeyEventArgs& Args)
{
	// Track LMB drag state for mouse-rotation
	if (Args.Key == EKeys::LeftMouseButton)
	{
		bDragging = (Args.Event == IE_Pressed);
		return true;
	}
	return FEditorViewportClient::InputKey(Args);
}


bool FArborCatalogPreviewViewportClient::InputAxis(const FInputKeyEventArgs& Args)
{
	if (bDragging && SphereComponent)
	{
		const float Sensitivity = 0.5f;
		if (Args.Key == EKeys::MouseX)
		{
			SphereYaw += Args.AmountDepressed * Sensitivity;
		}
		else if (Args.Key == EKeys::MouseY)
		{
			SpherePitch = FMath::Clamp(SpherePitch + Args.AmountDepressed * Sensitivity, -85.f, 85.f);
		}
		SphereComponent->SetWorldRotation(FRotator(SpherePitch, SphereYaw, 0.f));
		return true;
	}
	return FEditorViewportClient::InputAxis(Args);
}


// ============================================================================
// SArborCatalogPreviewViewport
// ============================================================================

void SArborCatalogPreviewViewport::Construct(const FArguments& InArgs)
{
	PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
	SEditorViewport::Construct(SEditorViewport::FArguments());
}


SArborCatalogPreviewViewport::~SArborCatalogPreviewViewport()
{
	ViewportClient.Reset();
	PreviewScene.Reset();
}


TSharedRef<FEditorViewportClient> SArborCatalogPreviewViewport::MakeEditorViewportClient()
{
	ViewportClient = MakeShared<FArborCatalogPreviewViewportClient>(SharedThis(this), *PreviewScene);
	return ViewportClient.ToSharedRef();
}


void SArborCatalogPreviewViewport::SetMaterial(UMaterialInterface* InMaterial)
{
	if (ViewportClient.IsValid())
	{
		ViewportClient->SetMaterial(InMaterial);
	}
}


#undef LOCTEXT_NAMESPACE
