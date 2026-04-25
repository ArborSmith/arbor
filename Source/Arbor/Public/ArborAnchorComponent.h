#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ArborAnchorTypes.h"
#include "ArborAnchorComponent.generated.h"

/**
 * Editor-only component that visualizes anchor points from the anchor registry.
 * Attach to any StaticMeshActor — it auto-detects the mesh and loads anchors.
 * Debug drawing is controlled by the Arbor plugin setting "Show anchor debug visualization".
 */
UCLASS(ClassGroup=(Arbor), meta=(BlueprintSpawnableComponent, DisplayName="Arbor Anchor Debug"))
class ARBOR_API UArborAnchorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UArborAnchorComponent();

	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** Force reload anchor data from registry. */
	void ReloadAnchors();

	/** The loaded anchors. */
	const TArray<FArborAnchor>& GetAnchors() const { return Anchors; }

	/** The asset path this component resolved. */
	const FString& GetAssetPath() const { return AssetPath; }

private:
	/** Try to find the static mesh on the owning actor and resolve its asset path. */
	FString ResolveAssetPath() const;

	/** Load anchors from the registry data asset (or legacy sidecar as fallback). */
	bool LoadAnchorsFromRegistry(const FString& MeshPath);

	UPROPERTY()
	FString AssetPath;

	TArray<FArborAnchor> Anchors;
	bool bAnchorsLoaded = false;
};
