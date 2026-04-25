#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborNoiseLibrary.generated.h"

UCLASS()
class ARBOR_API UArborNoiseLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Generate a heightmap as int32 values (0-65535, uint16 range) ready for
	 * ULandscapeBuilder::CreateLandscape / SetHeightmapData.
	 *
	 * Combines noise generation + float→uint16 conversion in one call.
	 *
	 * @param Width          Heightmap width in pixels.
	 * @param Height         Heightmap height in pixels.
	 * @param Frequency      Base noise frequency (2=gentle, 4=moderate, 8=many hills).
	 * @param Amplitude      Height variation 0-1 (0=flat, 0.5=moderate, 1=full range).
	 * @param Octaves        Number of fBm layers (1-8).
	 * @param Lacunarity     Frequency multiplier per octave (typically 2.0).
	 * @param Persistence    Amplitude decay per octave (typically 0.5).
	 * @param Seed           Random seed for reproducibility.
	 * @param BaseHeight     Center height in uint16 (32768 = UE5 sea level).
	 * @param NoiseType      "fbm" for rolling hills, "ridge" for mountains.
	 * @return int32 array of length Width*Height, values 0-65535.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Noise")
	static TArray<int32> GenerateHeightmap(
		int32 Width, int32 Height,
		float Frequency, float Amplitude, int32 Octaves,
		float Lacunarity, float Persistence, int32 Seed,
		float BaseHeight, const FString& NoiseType);

	/**
	 * Generate a meandering river path across a heightmap using gradient
	 * descent with noise perturbation.
	 *
	 * @param Width          Heightmap width (matching GenerateHeightmap).
	 * @param Height         Heightmap height.
	 * @param HeightData     int32 heightmap data (0-65535) from GenerateHeightmap.
	 * @param NumPoints      Number of spline control points to output.
	 * @param StartEdge      "north", "south", "east", or "west".
	 * @param Seed           Random seed.
	 * @param Meander        Meander strength (0=straight, 1=very winding).
	 * @return JSON string: [[x_frac, y_frac], ...] in [0,1] range.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Noise")
	static FString GenerateRiverPath(
		int32 Width, int32 Height,
		const TArray<int32>& HeightData,
		int32 NumPoints, const FString& StartEdge,
		int32 Seed, float Meander);
};
