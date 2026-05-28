// Slate widget that hosts a live, rotatable 3D preview of a UMaterialInterface
// applied to a sphere. The pattern mirrors UE's SMaterialEditor3DPreviewViewport
// but trimmed to the essentials: one sphere, one material, fixed lighting via
// FAdvancedPreviewScene, mouse-drag rotation.

#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "EditorViewportClient.h"
#include "AdvancedPreviewScene.h"

class UMaterialInterface;
class UStaticMeshComponent;


/** ViewportClient: owns the FAdvancedPreviewScene + the sphere component, and
 *  handles mouse-drag rotation. */
class FArborCatalogPreviewViewportClient : public FEditorViewportClient
{
public:
	FArborCatalogPreviewViewportClient(const TSharedRef<class SEditorViewport>& InViewport,
	                                    FAdvancedPreviewScene& InPreviewScene);

	/** Apply a material to the sphere. nullptr clears it. */
	void SetMaterial(UMaterialInterface* InMaterial);

	/** FEditorViewportClient overrides */
	virtual bool InputAxis(const FInputKeyEventArgs& Args) override;
	virtual bool InputKey(const FInputKeyEventArgs& Args) override;

private:
	/** Static mesh component holding the preview sphere. Owned by the scene. */
	UStaticMeshComponent* SphereComponent = nullptr;

	/** Cumulative drag rotation applied to the sphere. */
	float SphereYaw = 0.f;
	float SpherePitch = 0.f;

	/** True while the user is mid-drag (LMB held). */
	bool bDragging = false;
};


/** Slate widget hosting the viewport. */
class SArborCatalogPreviewViewport : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SArborCatalogPreviewViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SArborCatalogPreviewViewport();

	/** Switch the displayed material. Call after Construct. */
	void SetMaterial(UMaterialInterface* InMaterial);

protected:
	/** SEditorViewport overrides */
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	TSharedPtr<FAdvancedPreviewScene> PreviewScene;
	TSharedPtr<FArborCatalogPreviewViewportClient> ViewportClient;
};
