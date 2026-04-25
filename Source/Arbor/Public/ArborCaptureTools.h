#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborCaptureTools.generated.h"

UCLASS()
class ARBOR_API UArborCaptureTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Get the active level viewport camera location and rotation.
	 * Handles UE5 version differences internally.
	 *
	 * @return JSON: {success, location:{x,y,z}, rotation:{pitch,yaw,roll}}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Capture")
	static FString GetViewportCamera();

	/**
	 * Move the active level viewport camera.
	 *
	 * @param ParamsJson  JSON: {location:[x,y,z], rotation:[pitch,yaw,roll]}
	 * @return JSON: {success}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Capture")
	static FString SetViewportCamera(const FString& ParamsJson);

	/**
	 * Capture the viewport to a JPEG file using SceneCapture2D.
	 * Synchronous — file is on disk when the function returns.
	 *
	 * @param ParamsJson  JSON: {location:[x,y,z], rotation:[pitch,yaw,roll],
	 *                           output_path?, filename?, width?, height?}
	 * @return JSON: {success, file_path}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Capture")
	static FString CaptureFromPosition(const FString& ParamsJson);

	/**
	 * Capture the current viewport camera view.
	 *
	 * @param ParamsJson  JSON: {output_path?, filename?}
	 * @return JSON: {success, file_path}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Capture")
	static FString CaptureViewport(const FString& ParamsJson);

private:
	/** Get viewport camera info with version-safe fallbacks. */
	static bool GetViewportCameraInfo(FVector& OutLocation, FRotator& OutRotation);

	/** Set viewport camera info with version-safe fallbacks. */
	static bool SetViewportCameraInfo(const FVector& Location, const FRotator& Rotation);

	/** Core capture implementation using SceneCapture2D. */
	static FString CaptureToFile(const FVector& Location, const FRotator& Rotation,
		const FString& OutputDir, const FString& Filename,
		int32 Width, int32 Height);

	/** Ensure screenshot output directory exists. */
	static FString EnsureScreenshotDir(const FString& OutputPath);
};
