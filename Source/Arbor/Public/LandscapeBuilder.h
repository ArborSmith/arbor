#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LandscapeBuilder.generated.h"

class ALandscape;
class ALandscapeProxy;

UCLASS()
class ARBOR_API ULandscapeBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create a Landscape actor with the given dimensions and heightmap data.
	 *
	 * @param Location       World location for the landscape.
	 * @param Scale          World scale (X, Y, Z). Z controls height range.
	 * @param SectionSize    Quads per section (7, 15, 31, 63, 127, 255).
	 * @param SectionsPerComponent  1 or 2.
	 * @param ComponentCountX  Components along X axis.
	 * @param ComponentCountY  Components along Y axis.
	 * @param HeightData     Heightmap data as int32 (values 0-65535, stored as uint16).
	 *                       Length must match
	 *                       (ComponentCountX*SectionSize*SectionsPerComponent+1) *
	 *                       (ComponentCountY*SectionSize*SectionsPerComponent+1).
	 *                       Pass empty array for flat terrain (fills with 32768).
	 * @return The created Landscape actor, or nullptr on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Landscape")
	static ALandscape* CreateLandscape(
		FVector Location,
		FVector Scale,
		int32 SectionSize,
		int32 SectionsPerComponent,
		int32 ComponentCountX,
		int32 ComponentCountY,
		const TArray<int32>& HeightData);

	/**
	 * Set heightmap data on an existing landscape.
	 *
	 * @param Landscape  The landscape to modify.
	 * @param HeightData int32 array (values 0-65535) matching the landscape vertex count.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Landscape")
	static bool SetHeightmapData(ALandscapeProxy* Landscape, const TArray<int32>& HeightData);

	/**
	 * Read heightmap data from an existing landscape.
	 *
	 * @param Landscape  The landscape to read from.
	 * @return int32 array of heightmap values (0-65535), empty on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Landscape")
	static TArray<int32> GetHeightmapData(ALandscapeProxy* Landscape);

	/**
	 * Calculate required heightmap vertex count for given landscape dimensions.
	 *
	 * @return Total number of values needed in the HeightData array.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Landscape")
	static int32 CalcHeightmapSize(
		int32 SectionSize,
		int32 SectionsPerComponent,
		int32 ComponentCountX,
		int32 ComponentCountY);

	// ----- Layer Painting -----

	/**
	 * Create a ULandscapeLayerInfoObject asset and return its content path.
	 *
	 * @param LayerName  Name for the landscape layer (e.g. "Grass").
	 * @param SavePath   Content folder to save into (e.g. "/Game/Landscape").
	 * @return Content path of the created asset, or empty string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Landscape")
	static FString CreateLayerInfoAsset(const FString& LayerName, const FString& SavePath);

	/**
	 * Register a layer info asset with a landscape so it can receive weight data.
	 *
	 * @param Landscape      The landscape to modify.
	 * @param LayerInfoPath  Content path to a ULandscapeLayerInfoObject asset.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Landscape")
	static bool AddLayerToLandscape(ALandscapeProxy* Landscape, const FString& LayerInfoPath);

	/**
	 * List all registered layer names on a landscape.
	 *
	 * @param Landscape  The landscape to query.
	 * @return Array of layer name strings.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Landscape")
	static TArray<FString> GetLandscapeLayers(ALandscapeProxy* Landscape);

	/**
	 * Write per-vertex weight data for a named layer on a landscape.
	 *
	 * @param Landscape   The landscape to modify.
	 * @param LayerName   Name of the layer (must be registered via AddLayerToLandscape).
	 * @param WeightData  int32 array (values 0-255) matching the landscape vertex count.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Landscape")
	static bool SetLayerWeights(ALandscapeProxy* Landscape, const FString& LayerName,
		const TArray<int32>& WeightData);

	/**
	 * Read per-vertex weight data for a named layer from a landscape.
	 *
	 * @param Landscape  The landscape to read from.
	 * @param LayerName  Name of the layer.
	 * @return int32 array of weight values (0-255), empty on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Landscape")
	static TArray<int32> GetLayerWeights(ALandscapeProxy* Landscape, const FString& LayerName);

	// ----- Water -----

	/**
	 * Force a Water Body actor to rebuild its spline meshes.
	 *
	 * Calls PostEditMove(true) on the actor, which is the same code path
	 * triggered when the user stops dragging the actor in the editor and
	 * reliably regenerates SplineMeshComponents on WaterBodyRiver/Lake.
	 *
	 * @param WaterBodyActor  The Water Body actor to refresh.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Water")
	static bool RefreshWaterBody(AActor* WaterBodyActor);

	/**
	 * Spawn a WaterBodyRiver actor with a spline path.
	 * Requires the Water Plugin to be enabled.
	 *
	 * @param ParamsJson JSON: {spline_points:[[x,y,z],...], label, width,
	 *                          snap_to_terrain, enforce_downhill}
	 * @return JSON: {success, actor_name, actor_path, num_points}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Water")
	static FString AddWaterBodyRiver(const FString& ParamsJson);

	/**
	 * Spawn a WaterBodyLake actor with an 8-point circular spline.
	 * Requires the Water Plugin to be enabled.
	 *
	 * @param ParamsJson JSON: {location:[x,y,z], radius, label}
	 * @return JSON: {success, actor_name, actor_path}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Water")
	static FString AddWaterBodyLake(const FString& ParamsJson);

	// ----- Material -----

	/**
	 * Create a basic landscape material with a LandscapeLayerBlend node.
	 *
	 * Generates a UMaterial with a VectorParameter per layer (sensible default
	 * colors based on name: Grass=green, Dirt=brown, Rock=gray, etc.) wired
	 * through a LandscapeLayerBlend to BaseColor, plus constant roughness.
	 *
	 * If the material already exists at the target path, returns that path
	 * without recreating it.
	 *
	 * @param LayerNames    Array of layer names matching the LI_* assets.
	 * @param SavePath      Content folder (e.g. "/Game/Landscape").
	 * @param MaterialName  Asset name (default "M_Landscape_Auto").
	 * @return Content path of the material, or empty string on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Landscape")
	static FString CreateBasicLandscapeMaterial(
		const TArray<FString>& LayerNames,
		const FString& SavePath,
		const FString& MaterialName = TEXT("M_Landscape_Auto"));

	// ----- Heightmap Processing -----

	/** Compute per-vertex slope angles (degrees) from a heightmap.
	 * @param HeightData  int32 heightmap (0-65535).
	 * @param Width       Heightmap width.
	 * @param Height      Heightmap height.
	 * @return Float array of slope angles in degrees, length Width*Height.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Landscape")
	static TArray<float> ComputeSlopeMap(
		const TArray<int32>& HeightData, int32 Width, int32 Height);

	/** Carve a river valley into a landscape's heightmap using cosine falloff.
	 * @param Landscape       The landscape to modify.
	 * @param RiverPointsJson JSON: [[x_frac, y_frac], ...] in [0,1] range.
	 * @param WidthFrac       River width as fraction of landscape width.
	 * @param Depth           Carving depth (0-1, relative to full height range).
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Landscape")
	static bool CarveRiverValley(
		ALandscapeProxy* Landscape,
		const FString& RiverPointsJson,
		float WidthFrac, float Depth);

	/** Find the flattest area on a landscape of at least the given radius.
	 * @param Landscape  The landscape to search.
	 * @param MinRadius  Minimum flat area radius in world units.
	 * @param RegionJson Optional JSON: {min_x, min_y, max_x, max_y} bounding box.
	 * @return JSON: {x, y, z} world coordinates of the flattest area center.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Landscape")
	static FString FindFlatArea(
		ALandscapeProxy* Landscape, float MinRadius,
		const FString& RegionJson);

	/** Find the highest or lowest point on a landscape.
	 * @param Landscape  The landscape to search.
	 * @param Mode       "max" for highest, "min" for lowest.
	 * @param RegionJson Optional JSON bounding box filter.
	 * @return JSON: {x, y, z} world coordinates.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Landscape")
	static FString FindExtremePoint(
		ALandscapeProxy* Landscape, const FString& Mode,
		const FString& RegionJson);

	/** Auto-paint landscape layers based on height/slope rules with noise.
	 * @param Landscape The landscape to paint.
	 * @param RulesJson JSON: [{name, min_height, max_height, min_slope, max_slope, falloff}]
	 * @param Seed      Random seed for noise perturbation.
	 * @param SavePath  Content folder for layer info assets.
	 * @return JSON: {success, layers, vertex_count}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Landscape")
	static FString AutoPaintLayers(
		ALandscapeProxy* Landscape,
		const FString& RulesJson,
		int32 Seed, const FString& SavePath);

	/** Paint a circular brush for a single layer.
	 * @param Landscape The landscape to paint.
	 * @param LayerName Layer to paint.
	 * @param CenterX   Center X in fractional [0,1] landscape coords.
	 * @param CenterY   Center Y in fractional [0,1] landscape coords.
	 * @param RadiusFrac Radius as fraction of landscape size.
	 * @param Strength  Paint strength 0-1.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Landscape")
	static bool PaintLayerCircle(
		ALandscapeProxy* Landscape, const FString& LayerName,
		float CenterX, float CenterY, float RadiusFrac, float Strength);

	/** Full terrain creation pipeline: noise → landscape → optional river + paint.
	 * @param ParamsJson JSON with all terrain parameters.
	 * @return JSON: {success, landscape, heightmap_size, seed, river?, layers_painted?}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Landscape")
	static FString CreateTerrainPipeline(const FString& ParamsJson);
};
