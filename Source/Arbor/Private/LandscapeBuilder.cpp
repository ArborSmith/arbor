#include "LandscapeBuilder.h"
#include "ArborNoiseLibrary.h"
#include "ArborNoiseInternal.h"

#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeComponent.h"
#include "LandscapeEdit.h"
#include "LandscapeDataAccess.h"
#include "LandscapeEditorUtils.h"
#include "LandscapeLayerInfoObject.h"
#include "Runtime/Launch/Resources/Version.h"

// Edit-layer API timeline (verified against engine headers):
//   5.4:    ALandscape::LandscapeLayers (TArray<FLandscapeLayer>) -- direct array access.
//   5.5:    ALandscape::LandscapeLayers_DEPRECATED -- same struct, renamed UPROPERTY,
//           ULandscapeEditLayerBase exists but ALandscape::GetEditLayer(int) does NOT.
//   5.6+:   ALandscape::GetEditLayer(int) -> ULandscapeEditLayerBase*, plus
//           ULandscapeEditLayerBase::GetGuid(). LandscapeLayers_DEPRECATED still
//           present but accessors are preferred.
//
//   ULandscapeLayerInfoObject::SetLayerName / GetLayerName landed in 5.7;
//   before that the LayerName UPROPERTY is read/written directly.
//
// We hide both splits behind helpers below.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
	#define ARBOR_HAS_GET_EDIT_LAYER 1
	#include "LandscapeEditLayer.h"
#else
	#define ARBOR_HAS_GET_EDIT_LAYER 0
#endif

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
	#define ARBOR_HAS_LAYER_NAME_ACCESSORS 1
#else
	#define ARBOR_HAS_LAYER_NAME_ACCESSORS 0
#endif

// ALandscape::Import(...) final argument signature:
//   5.4:   const TArray<FLandscapeLayer>* (nullable pointer)
//   5.5+:  const TArray<FLandscapeLayer>& (reference; pass empty array for none)
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
	#define ARBOR_LANDSCAPE_IMPORT_BY_REF 1
#else
	#define ARBOR_LANDSCAPE_IMPORT_BY_REF 0
#endif

namespace
{
	// Returns the GUID of the first (base) edit layer on a landscape, or an empty
	// FGuid if the landscape has no edit layers. Abstracts over the 5.4 / 5.5 /
	// 5.6+ API splits so builders compile cleanly against any of them.
	FGuid GetFirstEditLayerGuid(ALandscape* LandscapeActor)
	{
		if (!LandscapeActor)
		{
			return FGuid();
		}
#if ARBOR_HAS_GET_EDIT_LAYER
		if (ULandscapeEditLayerBase* BaseLayer = LandscapeActor->GetEditLayer(0))
		{
			return BaseLayer->GetGuid();
		}
#elif ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 5
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (LandscapeActor->LandscapeLayers_DEPRECATED.Num() > 0)
		{
			return LandscapeActor->LandscapeLayers_DEPRECATED[0].Guid;
		}
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
#else
		if (LandscapeActor->LandscapeLayers.Num() > 0)
		{
			return LandscapeActor->LandscapeLayers[0].Guid;
		}
#endif
		return FGuid();
	}

	void SetLayerInfoName(ULandscapeLayerInfoObject* LayerInfo, FName Name)
	{
		if (!LayerInfo) return;
#if ARBOR_HAS_LAYER_NAME_ACCESSORS
		LayerInfo->SetLayerName(Name, /*bInModify=*/false);
#else
		LayerInfo->LayerName = Name;
#endif
	}

	FName GetLayerInfoName(ULandscapeLayerInfoObject* LayerInfo)
	{
		if (!LayerInfo) return NAME_None;
#if ARBOR_HAS_LAYER_NAME_ACCESSORS
		return LayerInfo->GetLayerName();
#else
		return LayerInfo->LayerName;
#endif
	}
}

#include "Editor.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "EditorAssetLibrary.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/SplineComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionLandscapeLayerBlend.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionConstant.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogArborLandscape, Log, All);

// ---------------------------------------------------------------------------
// Internal: convert TArray<int32> ↔ TArray<uint16> (Blueprint can't use uint16)
// ---------------------------------------------------------------------------

static TArray<uint16> Int32ToUint16(const TArray<int32>& In)
{
	TArray<uint16> Out;
	Out.SetNumUninitialized(In.Num());
	for (int32 i = 0; i < In.Num(); ++i)
	{
		Out[i] = static_cast<uint16>(FMath::Clamp(In[i], 0, 65535));
	}
	return Out;
}

static TArray<int32> Uint16ToInt32(const TArray<uint16>& In)
{
	TArray<int32> Out;
	Out.SetNumUninitialized(In.Num());
	for (int32 i = 0; i < In.Num(); ++i)
	{
		Out[i] = static_cast<int32>(In[i]);
	}
	return Out;
}

// ---------------------------------------------------------------------------
// Internal: convert TArray<int32> ↔ TArray<uint8> (Blueprint can't use uint8 arrays directly in some contexts)
// ---------------------------------------------------------------------------

static TArray<uint8> Int32ToUint8(const TArray<int32>& In)
{
	TArray<uint8> Out;
	Out.SetNumUninitialized(In.Num());
	for (int32 i = 0; i < In.Num(); ++i)
	{
		Out[i] = static_cast<uint8>(FMath::Clamp(In[i], 0, 255));
	}
	return Out;
}

static TArray<int32> Uint8ToInt32(const TArray<uint8>& In)
{
	TArray<int32> Out;
	Out.SetNumUninitialized(In.Num());
	for (int32 i = 0; i < In.Num(); ++i)
	{
		Out[i] = static_cast<int32>(In[i]);
	}
	return Out;
}

// ---------------------------------------------------------------------------
// Internal: auto-find first landscape if pointer is null
// ---------------------------------------------------------------------------

static ALandscapeProxy* AutoFindLandscape(ALandscapeProxy* Provided)
{
	if (Provided) return Provided;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return nullptr;

	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

// ============================================================================
// CalcHeightmapSize
// ============================================================================

int32 ULandscapeBuilder::CalcHeightmapSize(
	int32 SectionSize, int32 SectionsPerComponent,
	int32 ComponentCountX, int32 ComponentCountY)
{
	const int32 QuadsPerComponent = SectionSize * SectionsPerComponent;
	const int32 SizeX = ComponentCountX * QuadsPerComponent + 1;
	const int32 SizeY = ComponentCountY * QuadsPerComponent + 1;
	return SizeX * SizeY;
}

// ============================================================================
// CreateLandscape
// ============================================================================

ALandscape* ULandscapeBuilder::CreateLandscape(
	FVector Location, FVector Scale,
	int32 SectionSize, int32 SectionsPerComponent,
	int32 ComponentCountX, int32 ComponentCountY,
	const TArray<int32>& HeightData)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		UE_LOG(LogArborLandscape, Error, TEXT("No editor world available"));
		return nullptr;
	}

	// Validate section size
	static const TArray<int32> ValidSections = { 7, 15, 31, 63, 127, 255 };
	if (!ValidSections.Contains(SectionSize))
	{
		UE_LOG(LogArborLandscape, Error,
			TEXT("Invalid SectionSize %d. Must be one of: 7, 15, 31, 63, 127, 255"),
			SectionSize);
		return nullptr;
	}

	if (SectionsPerComponent != 1 && SectionsPerComponent != 2)
	{
		UE_LOG(LogArborLandscape, Error,
			TEXT("SectionsPerComponent must be 1 or 2, got %d"), SectionsPerComponent);
		return nullptr;
	}

	if (ComponentCountX < 1 || ComponentCountY < 1)
	{
		UE_LOG(LogArborLandscape, Error,
			TEXT("ComponentCount must be >= 1 (got %dx%d)"), ComponentCountX, ComponentCountY);
		return nullptr;
	}

	const int32 QuadsPerComponent = SectionSize * SectionsPerComponent;
	const int32 SizeX = ComponentCountX * QuadsPerComponent + 1;
	const int32 SizeY = ComponentCountY * QuadsPerComponent + 1;
	const int32 ExpectedCount = SizeX * SizeY;

	// Build height data — use provided or flat
	TArray<uint16> Heights;
	if (HeightData.Num() == 0)
	{
		// Flat terrain at midpoint
		Heights.SetNumUninitialized(ExpectedCount);
		for (int32 i = 0; i < ExpectedCount; ++i)
		{
			Heights[i] = 32768;
		}
	}
	else if (HeightData.Num() == ExpectedCount)
	{
		Heights = Int32ToUint16(HeightData);
	}
	else
	{
		UE_LOG(LogArborLandscape, Error,
			TEXT("HeightData size mismatch: got %d, expected %d (%dx%d)"),
			HeightData.Num(), ExpectedCount, SizeX, SizeY);
		return nullptr;
	}

	// Spawn ALandscape
	FTransform LandscapeTransform;
	LandscapeTransform.SetLocation(Location);
	LandscapeTransform.SetScale3D(Scale);

	ALandscape* Landscape = World->SpawnActor<ALandscape>();
	if (!Landscape)
	{
		UE_LOG(LogArborLandscape, Error, TEXT("Failed to spawn ALandscape actor"));
		return nullptr;
	}

	Landscape->SetActorTransform(LandscapeTransform);
	Landscape->LandscapeMaterial = nullptr;

	// Prepare heightmap data per layers
	TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
	HeightDataPerLayers.Add(FGuid(), MoveTemp(Heights));

	// Empty material layer data — must have matching key for each HeightDataPerLayers entry
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayers;
	MaterialLayerDataPerLayers.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

	// Import -- final edit-layers argument: 5.4 expects `const TArray<FLandscapeLayer>*`
	// (pointer, nullptr allowed to mean "no layers"); 5.5+ takes the array directly.
#if ARBOR_LANDSCAPE_IMPORT_BY_REF
	TArray<FLandscapeLayer> EmptyLayers;
	Landscape->Import(
		FGuid::NewGuid(),
		0, 0,
		SizeX - 1, SizeY - 1,
		SectionsPerComponent,
		SectionSize,
		HeightDataPerLayers,
		nullptr,  // HeightmapFilename
		MaterialLayerDataPerLayers,
		ELandscapeImportAlphamapType::Additive,
		EmptyLayers
	);
#else
	Landscape->Import(
		FGuid::NewGuid(),
		0, 0,
		SizeX - 1, SizeY - 1,
		SectionsPerComponent,
		SectionSize,
		HeightDataPerLayers,
		nullptr,  // HeightmapFilename
		MaterialLayerDataPerLayers,
		ELandscapeImportAlphamapType::Additive,
		nullptr  // no edit layers on 5.4
	);
#endif

	// Finalize
	Landscape->RegisterAllComponents();
	Landscape->PostEditChange();

	UE_LOG(LogArborLandscape, Log,
		TEXT("Created landscape: %dx%d verts, %dx%d components, section=%d"),
		SizeX, SizeY, ComponentCountX, ComponentCountY, SectionSize);

	return Landscape;
}

// ============================================================================
// SetHeightmapData
// ============================================================================

bool ULandscapeBuilder::SetHeightmapData(
	ALandscapeProxy* Landscape, const TArray<int32>& HeightData)
{
	if (!Landscape)
	{
		UE_LOG(LogArborLandscape, Error, TEXT("SetHeightmapData: Landscape is null"));
		return false;
	}

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info)
	{
		UE_LOG(LogArborLandscape, Error, TEXT("SetHeightmapData: no LandscapeInfo"));
		return false;
	}

	// Determine landscape extent
	int32 MinX = MAX_int32, MinY = MAX_int32;
	int32 MaxX = MIN_int32, MaxY = MIN_int32;
	if (!Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
	{
		UE_LOG(LogArborLandscape, Error, TEXT("SetHeightmapData: failed to get extent"));
		return false;
	}

	const int32 SizeX = MaxX - MinX + 1;
	const int32 SizeY = MaxY - MinY + 1;
	const int32 ExpectedCount = SizeX * SizeY;

	if (HeightData.Num() != ExpectedCount)
	{
		UE_LOG(LogArborLandscape, Error,
			TEXT("SetHeightmapData: size mismatch: got %d, expected %d (%dx%d)"),
			HeightData.Num(), ExpectedCount, SizeX, SizeY);
		return false;
	}

	// Convert int32 → uint16
	TArray<uint16> Heights = Int32ToUint16(HeightData);

	// -----------------------------------------------------------------------
	// UE5.7: Edit layers are mandatory.  We must write heightmap data into a
	// specific edit layer via FScopedSetLandscapeEditingLayer, then request a
	// layers content update so the engine merges all layers and updates the
	// final rendered heightmap.
	// -----------------------------------------------------------------------

	ALandscape* LandscapeActor = Cast<ALandscape>(Landscape);
	if (!LandscapeActor)
	{
		LandscapeActor = Info->LandscapeActor.Get();
	}
	if (!LandscapeActor)
	{
		UE_LOG(LogArborLandscape, Error,
			TEXT("SetHeightmapData: could not resolve ALandscape actor"));
		return false;
	}

	// Get the first (base) edit layer's GUID (version-agnostic helper)
	FGuid EditLayerGuid = GetFirstEditLayerGuid(LandscapeActor);
	if (!EditLayerGuid.IsValid())
	{
		UE_LOG(LogArborLandscape, Warning,
			TEXT("SetHeightmapData: no edit layers found, writing with empty GUID"));
	}

	// Set the active edit layer and write heightmap data.
	// When the scope exits, the completion callback triggers the layer merge.
	{
		FScopedSetLandscapeEditingLayer Scope(
			LandscapeActor, EditLayerGuid,
			[LandscapeActor]
			{
				LandscapeActor->RequestLayersContentUpdateForceAll(
					ELandscapeLayerUpdateMode::Update_All);
			});

		FLandscapeEditDataInterface EditInterface(Info);
		EditInterface.SetHeightData(
			MinX, MinY, MaxX, MaxY,
			Heights.GetData(), 0, true);
		EditInterface.Flush();
	} // ~FScopedSetLandscapeEditingLayer calls RequestLayersContentUpdateForceAll

	// Force synchronous merge of edit layers so the resolved heightmap is
	// immediately readable by GetHeightmapData.
	LandscapeActor->ForceUpdateLayersContent();

	UE_LOG(LogArborLandscape, Log,
		TEXT("SetHeightmapData: applied %d values to %dx%d landscape"),
		HeightData.Num(), SizeX, SizeY);

	return true;
}

// ============================================================================
// GetHeightmapData
// ============================================================================

TArray<int32> ULandscapeBuilder::GetHeightmapData(ALandscapeProxy* Landscape)
{
	TArray<int32> Result;

	if (!Landscape)
	{
		UE_LOG(LogArborLandscape, Error, TEXT("GetHeightmapData: Landscape is null"));
		return Result;
	}

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info)
	{
		UE_LOG(LogArborLandscape, Error, TEXT("GetHeightmapData: no LandscapeInfo"));
		return Result;
	}

	int32 MinX = MAX_int32, MinY = MAX_int32;
	int32 MaxX = MIN_int32, MaxY = MIN_int32;
	if (!Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
	{
		UE_LOG(LogArborLandscape, Error, TEXT("GetHeightmapData: failed to get extent"));
		return Result;
	}

	const int32 SizeX = MaxX - MinX + 1;
	const int32 SizeY = MaxY - MinY + 1;

	TArray<uint16> RawData;
	RawData.SetNumUninitialized(SizeX * SizeY);

	FLandscapeEditDataInterface EditInterface(Info);
	EditInterface.GetHeightDataFast(
		MinX, MinY, MaxX, MaxY,
		RawData.GetData(), 0);

	// Convert uint16 → int32 for Blueprint/Python
	Result = Uint16ToInt32(RawData);

	UE_LOG(LogArborLandscape, Log,
		TEXT("GetHeightmapData: read %d values from %dx%d landscape"),
		Result.Num(), SizeX, SizeY);

	return Result;
}

// ============================================================================
// Helper: find a layer info object by name on a landscape
// ============================================================================

static ULandscapeLayerInfoObject* FindLayerInfoByName(
	ULandscapeInfo* Info, const FString& LayerName)
{
	if (!Info) return nullptr;

	FName TargetName(*LayerName);
	for (const FLandscapeInfoLayerSettings& Layer : Info->Layers)
	{
		if (Layer.LayerInfoObj && Layer.GetLayerName() == TargetName)
		{
			return Layer.LayerInfoObj;
		}
	}
	return nullptr;
}

// ============================================================================
// CreateLayerInfoAsset
// ============================================================================

FString ULandscapeBuilder::CreateLayerInfoAsset(
	const FString& LayerName, const FString& SavePath)
{
	if (LayerName.IsEmpty())
	{
		UE_LOG(LogArborLandscape, Error, TEXT("CreateLayerInfoAsset: LayerName is empty"));
		return FString();
	}

	// Build package path: /Game/Landscape/LI_Grass
	FString AssetName = FString::Printf(TEXT("LI_%s"), *LayerName);
	FString PackagePath = SavePath / AssetName;

	// Check if asset already exists
	ULandscapeLayerInfoObject* Existing = LoadObject<ULandscapeLayerInfoObject>(
		nullptr, *PackagePath);
	if (Existing)
	{
		UE_LOG(LogArborLandscape, Log,
			TEXT("CreateLayerInfoAsset: '%s' already exists at %s"),
			*LayerName, *PackagePath);
		return PackagePath;
	}

	// Create package and object
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		UE_LOG(LogArborLandscape, Error,
			TEXT("CreateLayerInfoAsset: failed to create package '%s'"), *PackagePath);
		return FString();
	}

	ULandscapeLayerInfoObject* LayerInfo = NewObject<ULandscapeLayerInfoObject>(
		Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (!LayerInfo)
	{
		UE_LOG(LogArborLandscape, Error,
			TEXT("CreateLayerInfoAsset: failed to create object '%s'"), *AssetName);
		return FString();
	}

	SetLayerInfoName(LayerInfo, FName(*LayerName));

	// Notify asset registry and save
	FAssetRegistryModule::AssetCreated(LayerInfo);
	LayerInfo->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, LayerInfo,
		*FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension()),
		SaveArgs);

	UE_LOG(LogArborLandscape, Log,
		TEXT("CreateLayerInfoAsset: created '%s' at %s"), *LayerName, *PackagePath);

	return PackagePath;
}

// ============================================================================
// AddLayerToLandscape
// ============================================================================

bool ULandscapeBuilder::AddLayerToLandscape(
	ALandscapeProxy* Landscape, const FString& LayerInfoPath)
{
	if (!Landscape)
	{
		UE_LOG(LogArborLandscape, Error, TEXT("AddLayerToLandscape: Landscape is null"));
		return false;
	}

	// Load the layer info asset
	ULandscapeLayerInfoObject* LayerInfo = LoadObject<ULandscapeLayerInfoObject>(
		nullptr, *LayerInfoPath);
	if (!LayerInfo)
	{
		UE_LOG(LogArborLandscape, Error,
			TEXT("AddLayerToLandscape: could not load layer info '%s'"), *LayerInfoPath);
		return false;
	}

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info)
	{
		UE_LOG(LogArborLandscape, Error,
			TEXT("AddLayerToLandscape: no LandscapeInfo"));
		return false;
	}

	// Check for duplicate
	FName LayerName = GetLayerInfoName(LayerInfo);
	for (const FLandscapeInfoLayerSettings& Existing : Info->Layers)
	{
		if (Existing.LayerInfoObj && Existing.GetLayerName() == LayerName)
		{
			UE_LOG(LogArborLandscape, Log,
				TEXT("AddLayerToLandscape: layer '%s' already registered"),
				*LayerName.ToString());
			return true;
		}
	}

	// Add new layer settings (skip PostEditChange — it re-syncs with the
	// material and may strip the layer before SetLayerWeights can write data).
	FLandscapeInfoLayerSettings NewSettings(LayerInfo, Landscape);
	Info->Layers.Add(NewSettings);

	UE_LOG(LogArborLandscape, Log,
		TEXT("AddLayerToLandscape: registered layer '%s'"),
		*LayerName.ToString());

	return true;
}

// ============================================================================
// GetLandscapeLayers
// ============================================================================

TArray<FString> ULandscapeBuilder::GetLandscapeLayers(ALandscapeProxy* Landscape)
{
	TArray<FString> Result;

	if (!Landscape)
	{
		UE_LOG(LogArborLandscape, Error, TEXT("GetLandscapeLayers: Landscape is null"));
		return Result;
	}

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info)
	{
		UE_LOG(LogArborLandscape, Error, TEXT("GetLandscapeLayers: no LandscapeInfo"));
		return Result;
	}

	for (const FLandscapeInfoLayerSettings& Layer : Info->Layers)
	{
		if (Layer.LayerInfoObj)
		{
			Result.Add(Layer.GetLayerName().ToString());
		}
	}

	UE_LOG(LogArborLandscape, Log,
		TEXT("GetLandscapeLayers: found %d layers"), Result.Num());

	return Result;
}

// ============================================================================
// SetLayerWeights
// ============================================================================

bool ULandscapeBuilder::SetLayerWeights(
	ALandscapeProxy* Landscape, const FString& LayerName,
	const TArray<int32>& WeightData)
{
	if (!Landscape)
	{
		UE_LOG(LogArborLandscape, Error, TEXT("SetLayerWeights: Landscape is null"));
		return false;
	}

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info)
	{
		UE_LOG(LogArborLandscape, Error, TEXT("SetLayerWeights: no LandscapeInfo"));
		return false;
	}

	// Find the layer info — auto-register if not yet present
	ULandscapeLayerInfoObject* LayerInfo = FindLayerInfoByName(Info, LayerName);
	if (!LayerInfo)
	{
		// Try to find and register a matching LayerInfoObject asset
		FString SearchPath = FString::Printf(TEXT("/Game/Landscape/LI_%s"), *LayerName);
		LayerInfo = LoadObject<ULandscapeLayerInfoObject>(nullptr, *SearchPath);
		if (LayerInfo)
		{
			FLandscapeInfoLayerSettings NewSettings(LayerInfo, Landscape);
			Info->Layers.Add(NewSettings);
			UE_LOG(LogArborLandscape, Log,
				TEXT("SetLayerWeights: auto-registered layer '%s' from '%s'"),
				*LayerName, *SearchPath);
		}
		else
		{
			UE_LOG(LogArborLandscape, Error,
				TEXT("SetLayerWeights: layer '%s' not found on landscape and no "
					 "asset at '%s'. Register it first with AddLayerToLandscape."),
				*LayerName, *SearchPath);
			return false;
		}
	}

	// Determine landscape extent
	int32 MinX = MAX_int32, MinY = MAX_int32;
	int32 MaxX = MIN_int32, MaxY = MIN_int32;
	if (!Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
	{
		UE_LOG(LogArborLandscape, Error, TEXT("SetLayerWeights: failed to get extent"));
		return false;
	}

	const int32 SizeX = MaxX - MinX + 1;
	const int32 SizeY = MaxY - MinY + 1;
	const int32 ExpectedCount = SizeX * SizeY;

	if (WeightData.Num() != ExpectedCount)
	{
		UE_LOG(LogArborLandscape, Error,
			TEXT("SetLayerWeights: size mismatch: got %d, expected %d (%dx%d)"),
			WeightData.Num(), ExpectedCount, SizeX, SizeY);
		return false;
	}

	// Convert int32 → uint8
	TArray<uint8> Weights = Int32ToUint8(WeightData);

	// -----------------------------------------------------------------------
	// UE5.7: Edit layers are mandatory.  We must write weight data within
	// a FScopedSetLandscapeEditingLayer scope (same as SetHeightmapData),
	// then request a layers content update so the engine merges all layers.
	// -----------------------------------------------------------------------

	ALandscape* LandscapeActor = Cast<ALandscape>(Landscape);
	if (!LandscapeActor)
	{
		LandscapeActor = Info->LandscapeActor.Get();
	}
	if (!LandscapeActor)
	{
		UE_LOG(LogArborLandscape, Error,
			TEXT("SetLayerWeights: could not resolve ALandscape actor"));
		return false;
	}

	// Get the first (base) edit layer's GUID (version-agnostic helper)
	FGuid EditLayerGuid = GetFirstEditLayerGuid(LandscapeActor);
	if (!EditLayerGuid.IsValid())
	{
		UE_LOG(LogArborLandscape, Warning,
			TEXT("SetLayerWeights: no edit layers found, writing with empty GUID"));
	}

	// Set the active edit layer and write weight data.
	{
		FScopedSetLandscapeEditingLayer Scope(
			LandscapeActor, EditLayerGuid,
			[LandscapeActor]
			{
				LandscapeActor->RequestLayersContentUpdateForceAll(
					ELandscapeLayerUpdateMode::Update_All);
			});

		FLandscapeEditDataInterface EditInterface(Info);
		EditInterface.SetAlphaData(
			LayerInfo,
			MinX, MinY, MaxX, MaxY,
			Weights.GetData(), 0,
			ELandscapeLayerPaintingRestriction::None);
		EditInterface.Flush();
	} // ~FScopedSetLandscapeEditingLayer triggers RequestLayersContentUpdateForceAll

	// Force synchronous merge of edit layers so the resolved weightmap is
	// immediately readable by GetLayerWeights / GetWeightDataFast.
	// Without this, the merge is deferred to the next tick and any immediate
	// read returns stale (empty) data.
	LandscapeActor->ForceUpdateLayersContent();

	// Mark components dirty so the updated weights persist on save.
	for (ULandscapeComponent* Comp : LandscapeActor->LandscapeComponents)
	{
		if (Comp)
		{
			Comp->MarkPackageDirty();
		}
	}
	LandscapeActor->MarkPackageDirty();

	UE_LOG(LogArborLandscape, Log,
		TEXT("SetLayerWeights: applied %d values for layer '%s' on %dx%d landscape"),
		WeightData.Num(), *LayerName, SizeX, SizeY);

	return true;
}

// ============================================================================
// GetLayerWeights
// ============================================================================

TArray<int32> ULandscapeBuilder::GetLayerWeights(
	ALandscapeProxy* Landscape, const FString& LayerName)
{
	TArray<int32> Result;

	if (!Landscape)
	{
		UE_LOG(LogArborLandscape, Error, TEXT("GetLayerWeights: Landscape is null"));
		return Result;
	}

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info)
	{
		UE_LOG(LogArborLandscape, Error, TEXT("GetLayerWeights: no LandscapeInfo"));
		return Result;
	}

	// Find the layer info
	ULandscapeLayerInfoObject* LayerInfo = FindLayerInfoByName(Info, LayerName);
	if (!LayerInfo)
	{
		UE_LOG(LogArborLandscape, Error,
			TEXT("GetLayerWeights: layer '%s' not found on landscape"), *LayerName);
		return Result;
	}

	// Determine landscape extent
	int32 MinX = MAX_int32, MinY = MAX_int32;
	int32 MaxX = MIN_int32, MaxY = MIN_int32;
	if (!Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
	{
		UE_LOG(LogArborLandscape, Error, TEXT("GetLayerWeights: failed to get extent"));
		return Result;
	}

	const int32 SizeX = MaxX - MinX + 1;
	const int32 SizeY = MaxY - MinY + 1;

	// Ensure edit layers are fully merged before reading resolved data.
	// SetLayerWeights now calls ForceUpdateLayersContent(), but if called
	// from a different code path the merge may still be pending.
	ALandscape* LandscapeActor = Cast<ALandscape>(Landscape);
	if (!LandscapeActor)
	{
		LandscapeActor = Info->LandscapeActor.Get();
	}
	if (LandscapeActor)
	{
		LandscapeActor->ForceUpdateLayersContent();
	}

	TArray<uint8> RawData;
	RawData.SetNumZeroed(SizeX * SizeY);

	FLandscapeEditDataInterface EditInterface(Info);
	EditInterface.GetWeightDataFast(
		LayerInfo,
		MinX, MinY, MaxX, MaxY,
		RawData.GetData(), 0);

	// Convert uint8 → int32 for Blueprint/Python
	Result = Uint8ToInt32(RawData);

	UE_LOG(LogArborLandscape, Log,
		TEXT("GetLayerWeights: read %d values for layer '%s' from %dx%d landscape"),
		Result.Num(), *LayerName, SizeX, SizeY);

	return Result;
}

// ============================================================================
// CreateBasicLandscapeMaterial
// ============================================================================

static FLinearColor GetDefaultLayerColor(const FString& Name)
{
	FString Lower = Name.ToLower();
	if (Lower.Contains(TEXT("grass")))  return FLinearColor(0.2f,  0.5f,  0.1f);
	if (Lower.Contains(TEXT("dirt")))   return FLinearColor(0.45f, 0.32f, 0.18f);
	if (Lower.Contains(TEXT("rock")))   return FLinearColor(0.4f,  0.4f,  0.4f);
	if (Lower.Contains(TEXT("sand")))   return FLinearColor(0.76f, 0.7f,  0.5f);
	if (Lower.Contains(TEXT("snow")))   return FLinearColor(0.9f,  0.9f,  0.95f);
	if (Lower.Contains(TEXT("mud")))    return FLinearColor(0.3f,  0.22f, 0.12f);
	if (Lower.Contains(TEXT("gravel"))) return FLinearColor(0.5f,  0.48f, 0.42f);
	if (Lower.Contains(TEXT("clay")))   return FLinearColor(0.6f,  0.35f, 0.2f);
	return FLinearColor(0.5f, 0.5f, 0.5f);
}

FString ULandscapeBuilder::CreateBasicLandscapeMaterial(
	const TArray<FString>& LayerNames,
	const FString& SavePath,
	const FString& MaterialName)
{
	if (LayerNames.Num() == 0)
	{
		UE_LOG(LogArborLandscape, Error,
			TEXT("CreateBasicLandscapeMaterial: no layer names provided"));
		return FString();
	}

	FString PackagePath = SavePath / MaterialName;

	// Return existing material if already created
	UMaterial* ExistingMat = LoadObject<UMaterial>(nullptr, *PackagePath);
	if (ExistingMat)
	{
		UE_LOG(LogArborLandscape, Log,
			TEXT("CreateBasicLandscapeMaterial: '%s' already exists"), *PackagePath);
		return PackagePath;
	}

	// Create package and material
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		UE_LOG(LogArborLandscape, Error,
			TEXT("CreateBasicLandscapeMaterial: failed to create package '%s'"),
			*PackagePath);
		return FString();
	}

	UMaterial* Material = NewObject<UMaterial>(
		Package, *MaterialName, RF_Public | RF_Standalone | RF_Transactional);
	if (!Material)
	{
		UE_LOG(LogArborLandscape, Error,
			TEXT("CreateBasicLandscapeMaterial: failed to create material object"));
		return FString();
	}

	// Create LandscapeLayerBlend expression
	UMaterialExpressionLandscapeLayerBlend* BlendNode =
		Cast<UMaterialExpressionLandscapeLayerBlend>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material,
				UMaterialExpressionLandscapeLayerBlend::StaticClass(),
				-400, 0));

	if (!BlendNode)
	{
		UE_LOG(LogArborLandscape, Error,
			TEXT("CreateBasicLandscapeMaterial: failed to create LandscapeLayerBlend"));
		return FString();
	}

	// Populate layer entries
	BlendNode->Layers.Empty();
	for (int32 i = 0; i < LayerNames.Num(); ++i)
	{
		FLayerBlendInput LayerInput;
		LayerInput.LayerName = FName(*LayerNames[i]);
		LayerInput.BlendType = LB_WeightBlend;
		LayerInput.PreviewWeight = (i == 0) ? 1.0f : 0.0f;
		BlendNode->Layers.Add(LayerInput);
	}

	// Create a VectorParameter per layer and wire to blend inputs
	for (int32 i = 0; i < LayerNames.Num(); ++i)
	{
		UMaterialExpressionVectorParameter* ColorParam =
			Cast<UMaterialExpressionVectorParameter>(
				UMaterialEditingLibrary::CreateMaterialExpression(
					Material,
					UMaterialExpressionVectorParameter::StaticClass(),
					-800, i * 200));

		if (!ColorParam)
		{
			UE_LOG(LogArborLandscape, Warning,
				TEXT("CreateBasicLandscapeMaterial: failed to create color param for '%s'"),
				*LayerNames[i]);
			continue;
		}

		ColorParam->ParameterName = FName(*FString::Printf(TEXT("%s_Color"), *LayerNames[i]));
		ColorParam->DefaultValue = GetDefaultLayerColor(LayerNames[i]);

		// Wire directly via the Layers array (more reliable than pin-name lookup)
		BlendNode->Layers[i].LayerInput.Expression = ColorParam;
	}

	// Wire blend output → BaseColor
	UMaterialEditingLibrary::ConnectMaterialProperty(
		BlendNode, TEXT(""),
		EMaterialProperty::MP_BaseColor);

	// Roughness constant → 0.8
	UMaterialExpressionConstant* RoughnessNode =
		Cast<UMaterialExpressionConstant>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material,
				UMaterialExpressionConstant::StaticClass(),
				-400, 300));
	if (RoughnessNode)
	{
		RoughnessNode->R = 0.8f;
		UMaterialEditingLibrary::ConnectMaterialProperty(
			RoughnessNode, TEXT(""),
			EMaterialProperty::MP_Roughness);
	}

	// Recompile the material
	UMaterialEditingLibrary::RecompileMaterial(Material);

	// Save
	FAssetRegistryModule::AssetCreated(Material);
	Material->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, Material,
		*FPackageName::LongPackageNameToFilename(PackagePath,
			FPackageName::GetAssetPackageExtension()),
		SaveArgs);

	UE_LOG(LogArborLandscape, Log,
		TEXT("CreateBasicLandscapeMaterial: created '%s' with %d layers"),
		*PackagePath, LayerNames.Num());

	return PackagePath;
}

// ---------------------------------------------------------------------------
// Water
// ---------------------------------------------------------------------------

bool ULandscapeBuilder::RefreshWaterBody(AActor* Actor)
{
	if (!Actor)
	{
		UE_LOG(LogArborLandscape, Error, TEXT("RefreshWaterBody: null actor"));
		return false;
	}

	// PostEditMove(true) is the code path the editor triggers when the user
	// finishes dragging an actor.  For Water Body actors this rebuilds all
	// SplineMeshComponents from the current spline state.
	Actor->PostEditMove(true);

	UE_LOG(LogArborLandscape, Log,
		TEXT("RefreshWaterBody: refreshed '%s'"),
		*Actor->GetActorLabel());

	return true;
}

// ---------------------------------------------------------------------------
// Water body helpers (no hard Water module dependency)
// ---------------------------------------------------------------------------

static UClass* FindWaterClass(const TCHAR* ClassName)
{
	return FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Water.%s"), ClassName));
}

static USplineComponent* FindSplineOnActor(AActor* Actor)
{
	if (!Actor) return nullptr;

	// Try known property names first
	for (const TCHAR* PropName : { TEXT("WaterSpline"), TEXT("SplineComp"), TEXT("Spline") })
	{
		FProperty* Prop = Actor->GetClass()->FindPropertyByName(PropName);
		if (Prop)
		{
			if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
			{
				UObject* Val = ObjProp->GetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(Actor));
				if (USplineComponent* Spline = Cast<USplineComponent>(Val))
				{
					return Spline;
				}
			}
		}
	}

	// Fallback: find any SplineComponent
	return Actor->FindComponentByClass<USplineComponent>();
}

static void SetWaterBodyProperty(AActor* Actor, const FName& PropName, float Value)
{
	// Try on actor first, then on components
	FProperty* Prop = Actor->GetClass()->FindPropertyByName(PropName);
	if (Prop)
	{
		if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
		{
			FP->SetPropertyValue_InContainer(Actor, Value);
			Actor->PostEditChange();
			return;
		}
		if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
		{
			DP->SetPropertyValue_InContainer(Actor, static_cast<double>(Value));
			Actor->PostEditChange();
			return;
		}
	}

	// Search components
	for (UActorComponent* Comp : Actor->GetComponents())
	{
		if (!Comp) continue;
		FProperty* CompProp = Comp->GetClass()->FindPropertyByName(PropName);
		if (!CompProp) continue;
		if (FFloatProperty* FP = CastField<FFloatProperty>(CompProp))
		{
			FP->SetPropertyValue_InContainer(Comp, Value);
			Comp->PostEditChange();
			return;
		}
		if (FDoubleProperty* DP = CastField<FDoubleProperty>(CompProp))
		{
			DP->SetPropertyValue_InContainer(Comp, static_cast<double>(Value));
			Comp->PostEditChange();
			return;
		}
	}
}

static void SetWaterBodyBoolProperty(AActor* Actor, const FName& PropName, bool Value)
{
	FProperty* Prop = Actor->GetClass()->FindPropertyByName(PropName);
	if (Prop)
	{
		if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
		{
			BP->SetPropertyValue_InContainer(Actor, Value);
			Actor->PostEditChange();
			return;
		}
	}
	for (UActorComponent* Comp : Actor->GetComponents())
	{
		if (!Comp) continue;
		FProperty* CompProp = Comp->GetClass()->FindPropertyByName(PropName);
		if (!CompProp) continue;
		if (FBoolProperty* BP = CastField<FBoolProperty>(CompProp))
		{
			BP->SetPropertyValue_InContainer(Comp, Value);
			Comp->PostEditChange();
			return;
		}
	}
}

static float TraceTerrainZ(UWorld* World, float X, float Y, const TArray<AActor*>& Ignore)
{
	const FVector Start(X, Y, 50000.0f);
	const FVector End(X, Y, -50000.0f);

	FHitResult Hit;
	FCollisionQueryParams QueryParams;
	QueryParams.bTraceComplex = true;
	for (AActor* A : Ignore)
	{
		QueryParams.AddIgnoredActor(A);
	}

	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, QueryParams))
	{
		return Hit.ImpactPoint.Z;
	}
	return -999999.0f;
}

static TArray<FVector> SnapRiverPointsToTerrain(
	UWorld* World, const TArray<FVector>& Points,
	float SampleOffset, bool bEnforceDownhill,
	const TArray<AActor*>& IgnoreActors)
{
	TArray<FVector> Result;
	const int32 N = Points.Num();
	if (N == 0) return Result;

	for (int32 I = 0; I < N; ++I)
	{
		const FVector& Pt = Points[I];
		float DX, DY;

		if (I == 0)
		{
			DX = Points[1].X - Pt.X;
			DY = Points[1].Y - Pt.Y;
		}
		else if (I == N - 1)
		{
			DX = Pt.X - Points[I - 1].X;
			DY = Pt.Y - Points[I - 1].Y;
		}
		else
		{
			DX = Points[I + 1].X - Points[I - 1].X;
			DY = Points[I + 1].Y - Points[I - 1].Y;
		}

		const float Len = FMath::Sqrt(DX * DX + DY * DY);
		if (Len < 1e-6f)
		{
			Result.Add(Pt);
			continue;
		}

		// Perpendicular direction
		const float PerpX = -DY / Len * SampleOffset;
		const float PerpY = DX / Len * SampleOffset;

		const float ZLeft = TraceTerrainZ(World, Pt.X + PerpX, Pt.Y + PerpY, IgnoreActors);
		const float ZRight = TraceTerrainZ(World, Pt.X - PerpX, Pt.Y - PerpY, IgnoreActors);

		float NewZ = Pt.Z;
		int32 SampleCount = 0;
		float SampleSum = 0.0f;
		if (ZLeft > -999998.0f) { SampleSum += ZLeft; ++SampleCount; }
		if (ZRight > -999998.0f) { SampleSum += ZRight; ++SampleCount; }

		if (SampleCount > 0)
		{
			const float TerrainZ = SampleSum / SampleCount;
			const float TFrac = static_cast<float>(I) / FMath::Max(N - 1, 1);
			const float LinearZ = Points[0].Z * (1.0f - TFrac) + Points.Last().Z * TFrac;
			NewZ = TerrainZ * 0.6f + LinearZ * 0.4f;
		}

		Result.Add(FVector(Pt.X, Pt.Y, NewZ));
	}

	// Enforce monotonically decreasing Z
	if (bEnforceDownhill && Result.Num() > 1)
	{
		const float MinDrop = 1.0f;
		for (int32 I = 1; I < Result.Num(); ++I)
		{
			const float PrevZ = Result[I - 1].Z;
			if (Result[I].Z > PrevZ - MinDrop)
			{
				Result[I].Z = PrevZ - MinDrop;
			}
		}
	}

	return Result;
}

// ============================================================================
// AddWaterBodyRiver
// ============================================================================

FString ULandscapeBuilder::AddWaterBodyRiver(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> Params;
	auto Reader = TJsonReaderFactory<>::Create(ParamsJson);
	if (!FJsonSerializer::Deserialize(Reader, Params) || !Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return TEXT("{\"success\":false,\"error\":\"No editor world\"}");
	}

	UClass* RiverClass = FindWaterClass(TEXT("WaterBodyRiver"));
	if (!RiverClass)
	{
		return TEXT("{\"success\":false,\"error\":\"WaterBodyRiver not found. Enable the Water Plugin.\"}");
	}

	// Parse spline points
	const TArray<TSharedPtr<FJsonValue>>* PointsArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("spline_points"), PointsArr) || !PointsArr || PointsArr->Num() < 2)
	{
		return TEXT("{\"success\":false,\"error\":\"spline_points must have at least 2 points\"}");
	}

	TArray<FVector> SplinePoints;
	for (const auto& Val : *PointsArr)
	{
		const TArray<TSharedPtr<FJsonValue>>& Arr = Val->AsArray();
		if (Arr.Num() >= 3)
		{
			SplinePoints.Add(FVector(Arr[0]->AsNumber(), Arr[1]->AsNumber(), Arr[2]->AsNumber()));
		}
	}

	if (SplinePoints.Num() < 2)
	{
		return TEXT("{\"success\":false,\"error\":\"Need at least 2 valid [x,y,z] points\"}");
	}

	FString Label = TEXT("River");
	Params->TryGetStringField(TEXT("label"), Label);

	double Width = 500.0;
	Params->TryGetNumberField(TEXT("width"), Width);

	bool bSnapToTerrain = false;
	Params->TryGetBoolField(TEXT("snap_to_terrain"), bSnapToTerrain);

	bool bEnforceDownhill = true;
	Params->TryGetBoolField(TEXT("enforce_downhill"), bEnforceDownhill);

	// Snap to terrain if requested
	if (bSnapToTerrain)
	{
		// Collect water actors to ignore during traces
		TArray<AActor*> IgnoreActors;
		for (const TCHAR* WaterClassName : {
			TEXT("WaterBodyRiver"), TEXT("WaterBodyLake"),
			TEXT("WaterBodyOcean"), TEXT("WaterBodyCustom"),
			TEXT("WaterBrushManager"), TEXT("WaterZoneActor") })
		{
			UClass* WC = FindWaterClass(WaterClassName);
			if (!WC) continue;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (It->IsA(WC))
				{
					IgnoreActors.Add(*It);
				}
			}
		}

		SplinePoints = SnapRiverPointsToTerrain(
			World, SplinePoints, 3000.0f, bEnforceDownhill, IgnoreActors);
	}

	// Spawn at first point
	const FVector SpawnLocation = SplinePoints[0];
	AActor* Actor = GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->SpawnActorFromClass(
		RiverClass, SpawnLocation, FRotator::ZeroRotator);
	if (!Actor)
	{
		return TEXT("{\"success\":false,\"error\":\"Failed to spawn WaterBodyRiver\"}");
	}

	Actor->SetActorLabel(Label);

	// Set always_generate_water_mesh_tiles = true
	SetWaterBodyBoolProperty(Actor, TEXT("bAlwaysGenerateWaterMeshTiles"), true);

	// Set spline points (world → local)
	USplineComponent* Spline = FindSplineOnActor(Actor);
	if (Spline && SplinePoints.Num() > 1)
	{
		const FVector ActorLoc = Actor->GetActorLocation();
		TArray<FVector> LocalPoints;
		for (const FVector& WP : SplinePoints)
		{
			LocalPoints.Add(WP - ActorLoc);
		}
		Spline->SetSplinePoints(LocalPoints, ESplineCoordinateSpace::Local);
	}

	// Set river width
	SetWaterBodyProperty(Actor, TEXT("RiverWidth"), static_cast<float>(Width));

	// Refresh to rebuild meshes
	RefreshWaterBody(Actor);

	UE_LOG(LogArborLandscape, Log,
		TEXT("AddWaterBodyRiver: spawned '%s' with %d points"),
		*Label, SplinePoints.Num());

	return FString::Printf(
		TEXT("{\"success\":true,\"actor_name\":\"%s\",\"actor_path\":\"%s\",\"num_points\":%d}"),
		*Label, *Actor->GetPathName(), SplinePoints.Num());
}

// ============================================================================
// AddWaterBodyLake
// ============================================================================

FString ULandscapeBuilder::AddWaterBodyLake(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> Params;
	auto Reader = TJsonReaderFactory<>::Create(ParamsJson);
	if (!FJsonSerializer::Deserialize(Reader, Params) || !Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return TEXT("{\"success\":false,\"error\":\"No editor world\"}");
	}

	UClass* LakeClass = FindWaterClass(TEXT("WaterBodyLake"));
	if (!LakeClass)
	{
		return TEXT("{\"success\":false,\"error\":\"WaterBodyLake not found. Enable the Water Plugin.\"}");
	}

	// Parse location
	FVector Location(0, 0, 0);
	const TArray<TSharedPtr<FJsonValue>>* LocArr = nullptr;
	if (Params->TryGetArrayField(TEXT("location"), LocArr) && LocArr && LocArr->Num() >= 3)
	{
		Location.X = (*LocArr)[0]->AsNumber();
		Location.Y = (*LocArr)[1]->AsNumber();
		Location.Z = (*LocArr)[2]->AsNumber();
	}

	double Radius = 1000.0;
	Params->TryGetNumberField(TEXT("radius"), Radius);

	FString Label = TEXT("Lake");
	Params->TryGetStringField(TEXT("label"), Label);

	// Spawn
	AActor* Actor = GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->SpawnActorFromClass(
		LakeClass, Location, FRotator::ZeroRotator);
	if (!Actor)
	{
		return TEXT("{\"success\":false,\"error\":\"Failed to spawn WaterBodyLake\"}");
	}

	Actor->SetActorLabel(Label);

	// Set 8-point circular spline in local space
	USplineComponent* Spline = FindSplineOnActor(Actor);
	if (Spline)
	{
		TArray<FVector> Points;
		constexpr int32 NumPts = 8;
		for (int32 I = 0; I < NumPts; ++I)
		{
			const float Angle = (2.0f * PI * I) / NumPts;
			Points.Add(FVector(
				FMath::Cos(Angle) * Radius,
				FMath::Sin(Angle) * Radius,
				0.0f));
		}
		Spline->SetSplinePoints(Points, ESplineCoordinateSpace::Local);
		Spline->SetClosedLoop(true, true);
	}

	// Refresh to rebuild meshes
	RefreshWaterBody(Actor);

	UE_LOG(LogArborLandscape, Log,
		TEXT("AddWaterBodyLake: spawned '%s' at (%.0f, %.0f, %.0f), radius=%.0f"),
		*Label, Location.X, Location.Y, Location.Z, Radius);

	return FString::Printf(
		TEXT("{\"success\":true,\"actor_name\":\"%s\",\"actor_path\":\"%s\"}"),
		*Label, *Actor->GetPathName());
}

// ============================================================================
// ComputeSlopeMap
// ============================================================================

TArray<float> ULandscapeBuilder::ComputeSlopeMap(
	const TArray<int32>& HeightData, int32 Width, int32 Height)
{
	TArray<float> SlopeMap;
	const int32 Total = Width * Height;
	if (HeightData.Num() != Total || Total <= 0)
	{
		return SlopeMap;
	}
	SlopeMap.SetNumUninitialized(Total);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Idx = Y * Width + X;
			float DZDX, DZDY;

			// Finite differences: forward/backward at edges, central in middle
			if (X == 0)
				DZDX = static_cast<float>(HeightData[Idx + 1] - HeightData[Idx]);
			else if (X == Width - 1)
				DZDX = static_cast<float>(HeightData[Idx] - HeightData[Idx - 1]);
			else
				DZDX = static_cast<float>(HeightData[Idx + 1] - HeightData[Idx - 1]) * 0.5f;

			if (Y == 0)
				DZDY = static_cast<float>(HeightData[Idx + Width] - HeightData[Idx]);
			else if (Y == Height - 1)
				DZDY = static_cast<float>(HeightData[Idx] - HeightData[Idx - Width]);
			else
				DZDY = static_cast<float>(HeightData[Idx + Width] - HeightData[Idx - Width]) * 0.5f;

			const float GradMag = FMath::Sqrt(DZDX * DZDX + DZDY * DZDY);
			SlopeMap[Idx] = FMath::Atan(GradMag / 128.0f) * (180.0f / PI);
		}
	}

	return SlopeMap;
}

// ============================================================================
// CarveRiverValley
// ============================================================================

bool ULandscapeBuilder::CarveRiverValley(
	ALandscapeProxy* Landscape,
	const FString& RiverPointsJson,
	float WidthFrac, float Depth)
{
	Landscape = AutoFindLandscape(Landscape);
	if (!Landscape)
	{
		UE_LOG(LogArborLandscape, Error, TEXT("CarveRiverValley: no landscape found"));
		return false;
	}

	// Parse river points JSON: [[x, y], ...]
	TArray<TSharedPtr<FJsonValue>> PointsArray;
	auto Reader = TJsonReaderFactory<>::Create(RiverPointsJson);
	if (!FJsonSerializer::Deserialize(Reader, PointsArray))
	{
		UE_LOG(LogArborLandscape, Error, TEXT("CarveRiverValley: failed to parse river points JSON"));
		return false;
	}

	struct FPoint2D { float X, Y; };
	TArray<FPoint2D> Points;
	for (const auto& Val : PointsArray)
	{
		const TArray<TSharedPtr<FJsonValue>>* Pair;
		if (Val->TryGetArray(Pair) && Pair->Num() >= 2)
		{
			Points.Add({
				static_cast<float>((*Pair)[0]->AsNumber()),
				static_cast<float>((*Pair)[1]->AsNumber())
			});
		}
	}

	if (Points.Num() < 2) return false;

	// Read heightmap
	TArray<int32> HeightData = GetHeightmapData(Landscape);
	if (HeightData.Num() == 0) return false;

	// Determine heightmap dimensions
	ULandscapeInfo* LandInfo = Landscape->GetLandscapeInfo();
	if (!LandInfo) return false;

	int32 MinX, MinY, MaxX, MaxY;
	if (!LandInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY)) return false;
	const int32 W = MaxX - MinX + 1;
	const int32 H = MaxY - MinY + 1;

	if (HeightData.Num() != W * H) return false;

	// Densify path — interpolate between points
	TArray<FPoint2D> DensePath;
	const int32 SegSteps = 20;
	for (int32 I = 0; I < Points.Num() - 1; ++I)
	{
		for (int32 S = 0; S < SegSteps; ++S)
		{
			const float T = static_cast<float>(S) / static_cast<float>(SegSteps);
			DensePath.Add({
				FMath::Lerp(Points[I].X, Points[I + 1].X, T),
				FMath::Lerp(Points[I].Y, Points[I + 1].Y, T)
			});
		}
	}
	DensePath.Add(Points.Last());

	// Carve
	const float RadiusPixels = WidthFrac * static_cast<float>(W);
	const float DepthU16 = Depth * 32768.0f;
	const int32 RadiusCeil = FMath::CeilToInt32(RadiusPixels);

	for (const FPoint2D& P : DensePath)
	{
		const int32 CX = FMath::RoundToInt32(P.X * static_cast<float>(W - 1));
		const int32 CY = FMath::RoundToInt32(P.Y * static_cast<float>(H - 1));

		for (int32 DY = -RadiusCeil; DY <= RadiusCeil; ++DY)
		{
			for (int32 DX = -RadiusCeil; DX <= RadiusCeil; ++DX)
			{
				const int32 PX = CX + DX;
				const int32 PY = CY + DY;
				if (PX < 0 || PX >= W || PY < 0 || PY >= H) continue;

				const float Dist = FMath::Sqrt(static_cast<float>(DX * DX + DY * DY));
				if (Dist > RadiusPixels) continue;

				const float T = Dist / RadiusPixels;
				// Cosine falloff
				const float Falloff = 0.5f * (1.0f + FMath::Cos(PI * T));

				const int32 Idx = PY * W + PX;
				int32 Val = HeightData[Idx] - FMath::RoundToInt32(DepthU16 * Falloff);
				HeightData[Idx] = FMath::Clamp(Val, 0, 65535);
			}
		}
	}

	// Write back
	return SetHeightmapData(Landscape, HeightData);
}

// ============================================================================
// FindFlatArea
// ============================================================================

FString ULandscapeBuilder::FindFlatArea(
	ALandscapeProxy* Landscape, float MinRadius,
	const FString& RegionJson)
{
	Landscape = AutoFindLandscape(Landscape);
	if (!Landscape)
	{
		return TEXT("{\"error\":\"no landscape found\"}");
	}

	TArray<int32> HeightData = GetHeightmapData(Landscape);
	if (HeightData.Num() == 0)
	{
		return TEXT("{\"error\":\"empty heightmap\"}");
	}

	ULandscapeInfo* LandInfo = Landscape->GetLandscapeInfo();
	if (!LandInfo) return TEXT("{\"error\":\"no landscape info\"}");

	int32 MinLX, MinLY, MaxLX, MaxLY;
	if (!LandInfo->GetLandscapeExtent(MinLX, MinLY, MaxLX, MaxLY))
	{
		return TEXT("{\"error\":\"landscape extent failed\"}");
	}
	const int32 W = MaxLX - MinLX + 1;
	const int32 H = MaxLY - MinLY + 1;

	if (HeightData.Num() != W * H)
	{
		return TEXT("{\"error\":\"heightmap size mismatch\"}");
	}

	// Compute slope map
	TArray<float> SlopeMap = ComputeSlopeMap(HeightData, W, H);

	// Compute world-to-grid mapping
	const FVector Loc = Landscape->GetActorLocation();
	const FVector Scl = Landscape->GetActorScale3D();
	const float WorldW = static_cast<float>(W - 1) * Scl.X;
	const float WorldH = static_cast<float>(H - 1) * Scl.Y;

	// Radius in grid cells
	const int32 RadiusCells = FMath::Max(1, FMath::RoundToInt32(MinRadius / Scl.X));

	// Build summed area table of slopes
	TArray<double> SAT;
	SAT.SetNumZeroed(W * H);
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const int32 Idx = Y * W + X;
			double Val = static_cast<double>(SlopeMap[Idx]);
			if (X > 0) Val += SAT[Idx - 1];
			if (Y > 0) Val += SAT[Idx - W];
			if (X > 0 && Y > 0) Val -= SAT[Idx - W - 1];
			SAT[Idx] = Val;
		}
	}

	// Parse region filter
	int32 SearchMinX = 0, SearchMinY = 0, SearchMaxX = W - 1, SearchMaxY = H - 1;
	if (!RegionJson.IsEmpty())
	{
		TSharedPtr<FJsonObject> RegionObj;
		auto RegReader = TJsonReaderFactory<>::Create(RegionJson);
		if (FJsonSerializer::Deserialize(RegReader, RegionObj) && RegionObj.IsValid())
		{
			const float RMinX = RegionObj->GetNumberField(TEXT("min_x"));
			const float RMinY = RegionObj->GetNumberField(TEXT("min_y"));
			const float RMaxX = RegionObj->GetNumberField(TEXT("max_x"));
			const float RMaxY = RegionObj->GetNumberField(TEXT("max_y"));
			SearchMinX = FMath::Clamp(FMath::RoundToInt32((RMinX - Loc.X) / Scl.X), 0, W - 1);
			SearchMinY = FMath::Clamp(FMath::RoundToInt32((RMinY - Loc.Y) / Scl.Y), 0, H - 1);
			SearchMaxX = FMath::Clamp(FMath::RoundToInt32((RMaxX - Loc.X) / Scl.X), 0, W - 1);
			SearchMaxY = FMath::Clamp(FMath::RoundToInt32((RMaxY - Loc.Y) / Scl.Y), 0, H - 1);
		}
	}

	// Search for flattest box of size (2*RadiusCells+1)
	const int32 BoxSize = 2 * RadiusCells + 1;
	double BestSlope = TNumericLimits<double>::Max();
	int32 BestX = (SearchMinX + SearchMaxX) / 2;
	int32 BestY = (SearchMinY + SearchMaxY) / 2;

	for (int32 Y = SearchMinY + RadiusCells; Y <= SearchMaxY - RadiusCells; ++Y)
	{
		for (int32 X = SearchMinX + RadiusCells; X <= SearchMaxX - RadiusCells; ++X)
		{
			const int32 X1 = X - RadiusCells;
			const int32 Y1 = Y - RadiusCells;
			const int32 X2 = FMath::Min(X + RadiusCells, W - 1);
			const int32 Y2 = FMath::Min(Y + RadiusCells, H - 1);

			double Sum = SAT[Y2 * W + X2];
			if (X1 > 0) Sum -= SAT[Y2 * W + (X1 - 1)];
			if (Y1 > 0) Sum -= SAT[(Y1 - 1) * W + X2];
			if (X1 > 0 && Y1 > 0) Sum += SAT[(Y1 - 1) * W + (X1 - 1)];

			if (Sum < BestSlope)
			{
				BestSlope = Sum;
				BestX = X;
				BestY = Y;
			}
		}
	}

	// Convert grid coords to world coords
	const float WorldX = Loc.X + static_cast<float>(BestX) * Scl.X;
	const float WorldY = Loc.Y + static_cast<float>(BestY) * Scl.Y;
	const float HVal = static_cast<float>(HeightData[BestY * W + BestX]);
	const float WorldZ = Loc.Z + (HVal - 32768.0f) * Scl.Z / 512.0f;

	return FString::Printf(TEXT("{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f}"), WorldX, WorldY, WorldZ);
}

// ============================================================================
// FindExtremePoint
// ============================================================================

FString ULandscapeBuilder::FindExtremePoint(
	ALandscapeProxy* Landscape, const FString& Mode,
	const FString& RegionJson)
{
	Landscape = AutoFindLandscape(Landscape);
	if (!Landscape)
	{
		return TEXT("{\"error\":\"no landscape found\"}");
	}

	TArray<int32> HeightData = GetHeightmapData(Landscape);
	if (HeightData.Num() == 0)
	{
		return TEXT("{\"error\":\"empty heightmap\"}");
	}

	ULandscapeInfo* LandInfo = Landscape->GetLandscapeInfo();
	if (!LandInfo) return TEXT("{\"error\":\"no landscape info\"}");

	int32 MinLX, MinLY, MaxLX, MaxLY;
	if (!LandInfo->GetLandscapeExtent(MinLX, MinLY, MaxLX, MaxLY))
	{
		return TEXT("{\"error\":\"landscape extent failed\"}");
	}
	const int32 W = MaxLX - MinLX + 1;
	const int32 H = MaxLY - MinLY + 1;

	if (HeightData.Num() != W * H)
	{
		return TEXT("{\"error\":\"heightmap size mismatch\"}");
	}

	const FVector Loc = Landscape->GetActorLocation();
	const FVector Scl = Landscape->GetActorScale3D();

	// Parse region filter
	int32 SearchMinX = 0, SearchMinY = 0, SearchMaxX = W - 1, SearchMaxY = H - 1;
	if (!RegionJson.IsEmpty())
	{
		TSharedPtr<FJsonObject> RegionObj;
		auto RegReader = TJsonReaderFactory<>::Create(RegionJson);
		if (FJsonSerializer::Deserialize(RegReader, RegionObj) && RegionObj.IsValid())
		{
			SearchMinX = FMath::Clamp(FMath::RoundToInt32((RegionObj->GetNumberField(TEXT("min_x")) - Loc.X) / Scl.X), 0, W - 1);
			SearchMinY = FMath::Clamp(FMath::RoundToInt32((RegionObj->GetNumberField(TEXT("min_y")) - Loc.Y) / Scl.Y), 0, H - 1);
			SearchMaxX = FMath::Clamp(FMath::RoundToInt32((RegionObj->GetNumberField(TEXT("max_x")) - Loc.X) / Scl.X), 0, W - 1);
			SearchMaxY = FMath::Clamp(FMath::RoundToInt32((RegionObj->GetNumberField(TEXT("max_y")) - Loc.Y) / Scl.Y), 0, H - 1);
		}
	}

	const bool bFindMax = !Mode.Equals(TEXT("min"), ESearchCase::IgnoreCase);
	int32 BestVal = bFindMax ? -1 : 99999;
	int32 BestX = SearchMinX, BestY = SearchMinY;

	for (int32 Y = SearchMinY; Y <= SearchMaxY; ++Y)
	{
		for (int32 X = SearchMinX; X <= SearchMaxX; ++X)
		{
			const int32 Val = HeightData[Y * W + X];
			if ((bFindMax && Val > BestVal) || (!bFindMax && Val < BestVal))
			{
				BestVal = Val;
				BestX = X;
				BestY = Y;
			}
		}
	}

	const float WorldX = Loc.X + static_cast<float>(BestX) * Scl.X;
	const float WorldY = Loc.Y + static_cast<float>(BestY) * Scl.Y;
	const float WorldZ = Loc.Z + (static_cast<float>(BestVal) - 32768.0f) * Scl.Z / 512.0f;

	return FString::Printf(TEXT("{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f}"), WorldX, WorldY, WorldZ);
}

// ============================================================================
// AutoPaintLayers
// ============================================================================

FString ULandscapeBuilder::AutoPaintLayers(
	ALandscapeProxy* Landscape,
	const FString& RulesJson,
	int32 Seed, const FString& SavePath)
{
	Landscape = AutoFindLandscape(Landscape);
	if (!Landscape)
	{
		return TEXT("{\"success\":false,\"error\":\"no landscape found\"}");
	}

	// Parse rules
	TArray<TSharedPtr<FJsonValue>> RulesArray;
	auto Reader = TJsonReaderFactory<>::Create(RulesJson);
	if (!FJsonSerializer::Deserialize(Reader, RulesArray) || RulesArray.Num() == 0)
	{
		return TEXT("{\"success\":false,\"error\":\"failed to parse rules JSON\"}");
	}

	// Read heightmap
	TArray<int32> HeightData = GetHeightmapData(Landscape);
	if (HeightData.Num() == 0)
	{
		return TEXT("{\"success\":false,\"error\":\"empty heightmap\"}");
	}

	ULandscapeInfo* LandInfo = Landscape->GetLandscapeInfo();
	if (!LandInfo) return TEXT("{\"success\":false,\"error\":\"no landscape info\"}");

	int32 MinLX, MinLY, MaxLX, MaxLY;
	if (!LandInfo->GetLandscapeExtent(MinLX, MinLY, MaxLX, MaxLY))
	{
		return TEXT("{\"success\":false,\"error\":\"landscape extent failed\"}");
	}
	const int32 W = MaxLX - MinLX + 1;
	const int32 H = MaxLY - MinLY + 1;
	const int32 Total = W * H;

	if (HeightData.Num() != Total)
	{
		return TEXT("{\"success\":false,\"error\":\"heightmap size mismatch\"}");
	}

	// Normalize heightmap to [0,1]
	int32 MinH = 65535, MaxH = 0;
	for (int32 Val : HeightData)
	{
		MinH = FMath::Min(MinH, Val);
		MaxH = FMath::Max(MaxH, Val);
	}
	const float HRange = static_cast<float>(FMath::Max(MaxH - MinH, 1));
	TArray<float> NormHeight;
	NormHeight.SetNumUninitialized(Total);
	for (int32 I = 0; I < Total; ++I)
	{
		NormHeight[I] = static_cast<float>(HeightData[I] - MinH) / HRange;
	}

	// Compute slope map
	TArray<float> SlopeMap = ComputeSlopeMap(HeightData, W, H);

	// Parse rules and compute weights
	struct FLayerRule
	{
		FString Name;
		float MinHeight = 0.0f;
		float MaxHeight = 1.0f;
		float MinSlope = 0.0f;
		float MaxSlope = 90.0f;
		float Falloff = 0.1f;
	};

	TArray<FLayerRule> Rules;
	for (const auto& Val : RulesArray)
	{
		const TSharedPtr<FJsonObject>& Obj = Val->AsObject();
		if (!Obj.IsValid()) continue;

		FLayerRule Rule;
		Rule.Name = Obj->GetStringField(TEXT("name"));
		if (Obj->HasField(TEXT("min_height"))) Rule.MinHeight = Obj->GetNumberField(TEXT("min_height"));
		if (Obj->HasField(TEXT("max_height"))) Rule.MaxHeight = Obj->GetNumberField(TEXT("max_height"));
		if (Obj->HasField(TEXT("min_slope"))) Rule.MinSlope = Obj->GetNumberField(TEXT("min_slope"));
		if (Obj->HasField(TEXT("max_slope"))) Rule.MaxSlope = Obj->GetNumberField(TEXT("max_slope"));
		if (Obj->HasField(TEXT("falloff"))) Rule.Falloff = Obj->GetNumberField(TEXT("falloff"));
		Rules.Add(Rule);
	}

	if (Rules.Num() == 0)
	{
		return TEXT("{\"success\":false,\"error\":\"no valid rules\"}");
	}

	// Compute per-layer weight maps (float)
	TArray<TArray<float>> LayerWeights;
	LayerWeights.SetNum(Rules.Num());
	for (int32 L = 0; L < Rules.Num(); ++L)
	{
		LayerWeights[L].SetNumZeroed(Total);
	}

	for (int32 I = 0; I < Total; ++I)
	{
		const int32 GridX = I % W;
		const int32 GridY = I / W;
		const float NH = NormHeight[I];
		const float Slope = SlopeMap[I];

		for (int32 L = 0; L < Rules.Num(); ++L)
		{
			const FLayerRule& R = Rules[L];
			const float FalloffH = R.Falloff;
			const float FalloffS = R.Falloff * 90.0f;

			// Height factor with falloff ramp
			float HFactor = 1.0f;
			if (NH < R.MinHeight)
				HFactor = FMath::Max(0.0f, 1.0f - (R.MinHeight - NH) / FMath::Max(FalloffH, 0.001f));
			else if (NH > R.MaxHeight)
				HFactor = FMath::Max(0.0f, 1.0f - (NH - R.MaxHeight) / FMath::Max(FalloffH, 0.001f));

			// Slope factor with falloff ramp
			float SFactor = 1.0f;
			if (Slope < R.MinSlope)
				SFactor = FMath::Max(0.0f, 1.0f - (R.MinSlope - Slope) / FMath::Max(FalloffS, 0.001f));
			else if (Slope > R.MaxSlope)
				SFactor = FMath::Max(0.0f, 1.0f - (Slope - R.MaxSlope) / FMath::Max(FalloffS, 0.001f));

			float Weight = HFactor * SFactor;

			// Noise perturbation
			const float NX = static_cast<float>(GridX) * 0.02f;
			const float NY = static_cast<float>(GridY) * 0.02f;
			const float Noise = ArborNoise::Fbm2D(NX, NY, 3, 2.0f, 0.5f, Seed + L * 47);
			Weight *= 0.7f + 0.6f * Noise;  // [0.7, 1.3] range
			Weight = FMath::Max(0.0f, Weight);

			LayerWeights[L][I] = Weight;
		}
	}

	// Normalize per-vertex so all layers sum to 255
	TArray<TArray<int32>> FinalWeights;
	FinalWeights.SetNum(Rules.Num());
	for (int32 L = 0; L < Rules.Num(); ++L)
	{
		FinalWeights[L].SetNumZeroed(Total);
	}

	for (int32 I = 0; I < Total; ++I)
	{
		float Sum = 0.0f;
		for (int32 L = 0; L < Rules.Num(); ++L)
		{
			Sum += LayerWeights[L][I];
		}
		if (Sum > 0.0f)
		{
			for (int32 L = 0; L < Rules.Num(); ++L)
			{
				FinalWeights[L][I] = FMath::RoundToInt32(LayerWeights[L][I] / Sum * 255.0f);
			}
		}
		else if (Rules.Num() > 0)
		{
			FinalWeights[0][I] = 255;
		}
	}

	// Create layer infos and write weights
	TArray<FString> PaintedLayers;
	for (int32 L = 0; L < Rules.Num(); ++L)
	{
		const FString& LayerName = Rules[L].Name;

		// Create layer info asset
		FString InfoPath = CreateLayerInfoAsset(LayerName, SavePath);
		if (InfoPath.IsEmpty())
		{
			UE_LOG(LogArborLandscape, Warning, TEXT("AutoPaintLayers: failed to create layer info for '%s'"), *LayerName);
			continue;
		}

		// Register with landscape
		AddLayerToLandscape(Landscape, InfoPath);

		// Write weights
		if (SetLayerWeights(Landscape, LayerName, FinalWeights[L]))
		{
			PaintedLayers.Add(LayerName);
		}
	}

	// Auto-create material if none assigned
	if (!Landscape->LandscapeMaterial)
	{
		TArray<FString> LayerNames;
		for (const FLayerRule& R : Rules)
		{
			LayerNames.Add(R.Name);
		}
		FString MatPath = CreateBasicLandscapeMaterial(LayerNames, SavePath);
		if (!MatPath.IsEmpty())
		{
			UMaterial* Mat = Cast<UMaterial>(
				UEditorAssetLibrary::LoadAsset(MatPath));
			if (Mat)
			{
				Landscape->LandscapeMaterial = Mat;
				Landscape->PostEditChange();
			}
		}
	}

	// Build result JSON
	FString LayersStr = TEXT("[");
	for (int32 I = 0; I < PaintedLayers.Num(); ++I)
	{
		if (I > 0) LayersStr += TEXT(",");
		LayersStr += FString::Printf(TEXT("\"%s\""), *PaintedLayers[I]);
	}
	LayersStr += TEXT("]");

	return FString::Printf(
		TEXT("{\"success\":true,\"layers\":%s,\"vertex_count\":%d}"),
		*LayersStr, Total);
}

// ============================================================================
// PaintLayerCircle
// ============================================================================

bool ULandscapeBuilder::PaintLayerCircle(
	ALandscapeProxy* Landscape, const FString& LayerName,
	float CenterX, float CenterY, float RadiusFrac, float Strength)
{
	Landscape = AutoFindLandscape(Landscape);
	if (!Landscape)
	{
		UE_LOG(LogArborLandscape, Error, TEXT("PaintLayerCircle: no landscape found"));
		return false;
	}

	ULandscapeInfo* LandInfo = Landscape->GetLandscapeInfo();
	if (!LandInfo) return false;

	int32 MinLX, MinLY, MaxLX, MaxLY;
	if (!LandInfo->GetLandscapeExtent(MinLX, MinLY, MaxLX, MaxLY)) return false;
	const int32 W = MaxLX - MinLX + 1;
	const int32 H = MaxLY - MinLY + 1;
	const int32 Total = W * H;

	// Read existing weights (or start with zeros)
	TArray<int32> Existing = GetLayerWeights(Landscape, LayerName);
	if (Existing.Num() != Total)
	{
		Existing.SetNumZeroed(Total);
	}

	const float RadiusPixels = RadiusFrac * static_cast<float>(FMath::Max(W, H));
	const int32 CXPx = FMath::RoundToInt32(CenterX * static_cast<float>(W - 1));
	const int32 CYPx = FMath::RoundToInt32(CenterY * static_cast<float>(H - 1));
	const int32 R = FMath::CeilToInt32(RadiusPixels);

	for (int32 DY = -R; DY <= R; ++DY)
	{
		for (int32 DX = -R; DX <= R; ++DX)
		{
			const int32 PX = CXPx + DX;
			const int32 PY = CYPx + DY;
			if (PX < 0 || PX >= W || PY < 0 || PY >= H) continue;

			const float Dist = FMath::Sqrt(static_cast<float>(DX * DX + DY * DY));
			if (Dist > RadiusPixels) continue;

			const float T = Dist / RadiusPixels;
			const float Falloff = 0.5f * (1.0f + FMath::Cos(PI * T));

			const int32 Idx = PY * W + PX;
			int32 Val = Existing[Idx] + FMath::RoundToInt32(Strength * Falloff * 255.0f);
			Existing[Idx] = FMath::Clamp(Val, 0, 255);
		}
	}

	return SetLayerWeights(Landscape, LayerName, Existing);
}

// ============================================================================
// CreateTerrainPipeline
// ============================================================================

FString ULandscapeBuilder::CreateTerrainPipeline(const FString& ParamsJson)
{
	// Parse params
	TSharedPtr<FJsonObject> Params;
	auto Reader = TJsonReaderFactory<>::Create(ParamsJson);
	if (!FJsonSerializer::Deserialize(Reader, Params) || !Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"failed to parse params JSON\"}");
	}

	// Extract parameters with defaults
	const TArray<TSharedPtr<FJsonValue>>* LocArr = nullptr;
	Params->TryGetArrayField(TEXT("location"), LocArr);
	FVector Location(0, 0, 0);
	if (LocArr && LocArr->Num() >= 3)
	{
		Location.X = (*LocArr)[0]->AsNumber();
		Location.Y = (*LocArr)[1]->AsNumber();
		Location.Z = (*LocArr)[2]->AsNumber();
	}

	const TArray<TSharedPtr<FJsonValue>>* SclArr = nullptr;
	Params->TryGetArrayField(TEXT("scale"), SclArr);
	FVector Scale(100, 100, 100);
	if (SclArr && SclArr->Num() >= 3)
	{
		Scale.X = (*SclArr)[0]->AsNumber();
		Scale.Y = (*SclArr)[1]->AsNumber();
		Scale.Z = (*SclArr)[2]->AsNumber();
	}

	const int32 ComponentCount = Params->GetIntegerField(TEXT("component_count"));
	const float Frequency = Params->GetNumberField(TEXT("frequency"));
	const float Amplitude = Params->GetNumberField(TEXT("amplitude"));
	const int32 Octaves = Params->GetIntegerField(TEXT("octaves"));
	FString NoiseType = TEXT("fbm");
	Params->TryGetStringField(TEXT("noise_type"), NoiseType);

	int32 Seed = FMath::RandRange(0, 2147483647);
	if (Params->HasField(TEXT("seed")) && Params->TryGetField(TEXT("seed")).IsValid() && Params->TryGetField(TEXT("seed"))->Type != EJson::Null)
	{
		Seed = Params->GetIntegerField(TEXT("seed"));
	}

	const bool bRiver = Params->HasField(TEXT("river")) && Params->GetBoolField(TEXT("river"));
	const float RiverWidth = Params->HasField(TEXT("river_width"))
		? Params->GetNumberField(TEXT("river_width")) : 500.0f;

	FString MaterialPath;
	Params->TryGetStringField(TEXT("material_path"), MaterialPath);

	// Destroy existing landscape
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World)
	{
		for (TActorIterator<ALandscape> It(World); It; ++It)
		{
			It->Destroy();
		}
	}

	// 1. Generate heightmap
	const int32 SectionSize = 63;
	const int32 SectionsPerComp = 1;
	const int32 QuadsPerComp = SectionSize * SectionsPerComp;
	const int32 SizeX = ComponentCount * QuadsPerComp + 1;
	const int32 SizeY = SizeX;

	TArray<int32> HeightData = UArborNoiseLibrary::GenerateHeightmap(
		SizeX, SizeY, Frequency, Amplitude, Octaves,
		2.0f, 0.5f, Seed, 0.5f, NoiseType);

	if (HeightData.Num() != SizeX * SizeY)
	{
		return TEXT("{\"success\":false,\"error\":\"heightmap generation failed\"}");
	}

	// 2. Create landscape
	ALandscape* Landscape = CreateLandscape(
		Location, Scale, SectionSize, SectionsPerComp,
		ComponentCount, ComponentCount, HeightData);

	if (!Landscape)
	{
		return TEXT("{\"success\":false,\"error\":\"landscape creation failed\"}");
	}

	// Apply material if provided
	if (!MaterialPath.IsEmpty())
	{
		UMaterial* Mat = Cast<UMaterial>(
			UEditorAssetLibrary::LoadAsset(MaterialPath));
		if (Mat)
		{
			Landscape->LandscapeMaterial = Mat;
			Landscape->PostEditChange();
		}
	}

	FString RiverInfo;

	// 3. Optional river
	if (bRiver)
	{
		// Generate river path
		FString RiverPathJson = UArborNoiseLibrary::GenerateRiverPath(
			SizeX, SizeY, HeightData, 10, TEXT("north"), Seed + 42, 0.3f);

		// Carve valley
		const float WidthFrac = RiverWidth / (static_cast<float>(SizeX - 1) * Scale.X);
		CarveRiverValley(Landscape, RiverPathJson, WidthFrac * 2.0f, 0.015f);

		RiverInfo = TEXT("\"river\":\"river_carved\"");
	}

	// 4. Optional auto-paint
	FString LayersInfo;
	const bool bAutoPaint = Params->HasField(TEXT("auto_paint")) && Params->GetBoolField(TEXT("auto_paint"));
	if (bAutoPaint)
	{
		const TArray<TSharedPtr<FJsonValue>>* LayerRules = nullptr;
		FString RulesStr;
		if (Params->TryGetArrayField(TEXT("layers"), LayerRules))
		{
			auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&RulesStr);
			FJsonSerializer::Serialize(*LayerRules, Writer);
		}
		else
		{
			RulesStr = TEXT("[{\"name\":\"Grass\",\"max_slope\":25,\"max_height\":0.65,\"falloff\":0.1},{\"name\":\"Dirt\",\"max_slope\":40,\"falloff\":0.15},{\"name\":\"Rock\",\"min_slope\":20,\"falloff\":0.1}]");
		}

		FString PaintResult = AutoPaintLayers(Landscape, RulesStr, Seed, TEXT("/Game/Landscape"));
		// Extract painted layer names from result
		TSharedPtr<FJsonObject> PaintObj;
		auto PReader = TJsonReaderFactory<>::Create(PaintResult);
		if (FJsonSerializer::Deserialize(PReader, PaintObj) && PaintObj.IsValid())
		{
			if (PaintObj->GetBoolField(TEXT("success")))
			{
				const TArray<TSharedPtr<FJsonValue>>* Layers;
				if (PaintObj->TryGetArrayField(TEXT("layers"), Layers))
				{
					FString LayersList = TEXT("[");
					for (int32 I = 0; I < Layers->Num(); ++I)
					{
						if (I > 0) LayersList += TEXT(",");
						LayersList += FString::Printf(TEXT("\"%s\""), *(*Layers)[I]->AsString());
					}
					LayersList += TEXT("]");
					LayersInfo = FString::Printf(TEXT("\"layers_painted\":%s"), *LayersList);
				}
			}
		}
	}

	// Build result
	FString Result = FString::Printf(
		TEXT("{\"success\":true,\"landscape\":\"%s\",\"heightmap_size\":[%d,%d],\"seed\":%d"),
		*Landscape->GetActorLabel(), SizeX, SizeY, Seed);

	if (!RiverInfo.IsEmpty())
	{
		Result += TEXT(",") + RiverInfo;
	}
	if (!LayersInfo.IsEmpty())
	{
		Result += TEXT(",") + LayersInfo;
	}
	Result += TEXT("}");

	UE_LOG(LogArborLandscape, Log, TEXT("CreateTerrainPipeline: %s"), *Result);
	return Result;
}
