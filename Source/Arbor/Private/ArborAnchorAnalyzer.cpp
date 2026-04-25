#include "ArborAnchorAnalyzer.h"
#include "ArborAnchorTypes.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "EditorAssetLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "StaticMeshResources.h"
#include "ArborAnchorComponent.h"
#include "EngineUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"

// ---------------------------------------------------------------------------
// JSON helpers (same pattern as other Arbor tools)
// ---------------------------------------------------------------------------

static TSharedPtr<FJsonObject> ParseJson(const FString& Json)
{
	TSharedPtr<FJsonObject> Obj;
	auto Reader = TJsonReaderFactory<>::Create(Json);
	FJsonSerializer::Deserialize(Reader, Obj);
	return Obj;
}

static FString SerializeJson(TSharedPtr<FJsonObject> Root)
{
	FString Output;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}

static FString SerializeJsonPretty(TSharedPtr<FJsonObject> Root)
{
	FString Output;
	auto Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}

static TSharedPtr<FJsonObject> VecToJson(const FVector& V)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("x"), V.X);
	Obj->SetNumberField(TEXT("y"), V.Y);
	Obj->SetNumberField(TEXT("z"), V.Z);
	return Obj;
}

// ---------------------------------------------------------------------------
// Sidecar path: /Game/Foo/Bar -> {ContentDir}/Foo/Bar.anchor.json
// ---------------------------------------------------------------------------

FString UArborAnchorAnalyzer::GetSidecarPath(const FString& AssetPath)
{
	FString RelPath = AssetPath;
	RelPath.RemoveFromStart(TEXT("/Game/"));
	// Strip any object suffix (e.g. ".SM_House" from "/Game/Foo/SM_House.SM_House")
	int32 DotIdx;
	if (RelPath.FindChar('.', DotIdx))
	{
		RelPath.LeftInline(DotIdx);
	}
	return FPaths::ProjectContentDir() / RelPath + TEXT(".anchor.json");
}

// ---------------------------------------------------------------------------
// Anchor helper
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> UArborAnchorAnalyzer::MakeAnchor(
	const FString& Id, const FString& Type,
	const FVector& Position, const FVector& Direction, double Width, double Height)
{
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
	A->SetStringField(TEXT("id"), Id);
	A->SetStringField(TEXT("type"), Type);
	A->SetObjectField(TEXT("position"), VecToJson(Position));
	A->SetObjectField(TEXT("direction"), VecToJson(Direction));
	if (Width > 0.0)
	{
		A->SetNumberField(TEXT("width"), Width);
	}
	if (Height > 0.0)
	{
		A->SetNumberField(TEXT("height"), Height);
	}
	return A;
}

// ---------------------------------------------------------------------------
// AnalyzeMesh
// ---------------------------------------------------------------------------

FString UArborAnchorAnalyzer::AnalyzeMesh(const FString& ParamsJson)
{
	auto Params = ParseJson(ParamsJson);
	if (!Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString AssetType;
	Params->TryGetStringField(TEXT("asset_type"), AssetType);

	bool bDetectOpenings = false;
	Params->TryGetBoolField(TEXT("detect_openings"), bDetectOpenings);
	if (!bDetectOpenings && (AssetType == TEXT("wall") || AssetType == TEXT("roof")))
	{
		bDetectOpenings = true; // Default on for walls and roofs
	}

	UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Mesh)
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"Cannot load mesh: %s\"}"), *AssetPath);
	}

	// Get bounds
	const FBox Bounds = Mesh->GetBoundingBox();
	const FVector Min = Bounds.Min;
	const FVector Max = Bounds.Max;
	const FVector Center = Bounds.GetCenter();
	const FVector Extent = Bounds.GetExtent();

	// Footprint (XY projection)
	TSharedPtr<FJsonObject> Footprint = MakeShared<FJsonObject>();
	Footprint->SetNumberField(TEXT("min_x"), Min.X);
	Footprint->SetNumberField(TEXT("min_y"), Min.Y);
	Footprint->SetNumberField(TEXT("max_x"), Max.X);
	Footprint->SetNumberField(TEXT("max_y"), Max.Y);
	Footprint->SetNumberField(TEXT("area_cm2"), (Max.X - Min.X) * (Max.Y - Min.Y));

	// 3D bounds
	TSharedPtr<FJsonObject> Bounds3D = MakeShared<FJsonObject>();
	Bounds3D->SetObjectField(TEXT("min"), VecToJson(Min));
	Bounds3D->SetObjectField(TEXT("max"), VecToJson(Max));
	Bounds3D->SetObjectField(TEXT("center"), VecToJson(Center));
	Bounds3D->SetObjectField(TEXT("extent"), VecToJson(Extent));

	// Generate cardinal-face anchors
	TArray<TSharedPtr<FJsonValue>> Anchors;

	// North (+X face)
	Anchors.Add(MakeShared<FJsonValueObject>(
		MakeAnchor(TEXT("face_north"), TEXT("wall_edge"),
			FVector(Max.X, Center.Y, Center.Z), FVector(1, 0, 0))));

	// South (-X face)
	Anchors.Add(MakeShared<FJsonValueObject>(
		MakeAnchor(TEXT("face_south"), TEXT("wall_edge"),
			FVector(Min.X, Center.Y, Center.Z), FVector(-1, 0, 0))));

	// East (+Y face)
	Anchors.Add(MakeShared<FJsonValueObject>(
		MakeAnchor(TEXT("face_east"), TEXT("wall_edge"),
			FVector(Center.X, Max.Y, Center.Z), FVector(0, 1, 0))));

	// West (-Y face)
	Anchors.Add(MakeShared<FJsonValueObject>(
		MakeAnchor(TEXT("face_west"), TEXT("wall_edge"),
			FVector(Center.X, Min.Y, Center.Z), FVector(0, -1, 0))));

	// Bottom center (snap base)
	Anchors.Add(MakeShared<FJsonValueObject>(
		MakeAnchor(TEXT("snap_base"), TEXT("snap_point"),
			FVector(Center.X, Center.Y, Min.Z), FVector(0, 0, -1))));

	// Type-specific anchors
	if (AssetType == TEXT("building"))
	{
		// Front door on south face, at ground level
		double DoorWidth = FMath::Min(120.0, (Max.Y - Min.Y) * 0.3);
		Anchors.Add(MakeShared<FJsonValueObject>(
			MakeAnchor(TEXT("front_door"), TEXT("door"),
				FVector(Min.X, Center.Y, Min.Z), FVector(-1, 0, 0), DoorWidth)));
	}
	else if (AssetType == TEXT("road_segment"))
	{
		// Start and end along longest XY axis
		bool bXLonger = (Max.X - Min.X) >= (Max.Y - Min.Y);
		if (bXLonger)
		{
			Anchors.Add(MakeShared<FJsonValueObject>(
				MakeAnchor(TEXT("road_start"), TEXT("path_connect"),
					FVector(Min.X, Center.Y, Min.Z), FVector(-1, 0, 0))));
			Anchors.Add(MakeShared<FJsonValueObject>(
				MakeAnchor(TEXT("road_end"), TEXT("path_connect"),
					FVector(Max.X, Center.Y, Min.Z), FVector(1, 0, 0))));
		}
		else
		{
			Anchors.Add(MakeShared<FJsonValueObject>(
				MakeAnchor(TEXT("road_start"), TEXT("path_connect"),
					FVector(Center.X, Min.Y, Min.Z), FVector(0, -1, 0))));
			Anchors.Add(MakeShared<FJsonValueObject>(
				MakeAnchor(TEXT("road_end"), TEXT("path_connect"),
					FVector(Center.X, Max.Y, Min.Z), FVector(0, 1, 0))));
		}
		// Side anchors for things placed along the road
		Anchors.Add(MakeShared<FJsonValueObject>(
			MakeAnchor(TEXT("side_left"), TEXT("road_edge"),
				FVector(Center.X, Min.Y, Min.Z), FVector(0, -1, 0))));
		Anchors.Add(MakeShared<FJsonValueObject>(
			MakeAnchor(TEXT("side_right"), TEXT("road_edge"),
				FVector(Center.X, Max.Y, Min.Z), FVector(0, 1, 0))));
	}
	else if (AssetType == TEXT("wall"))
	{
		// Wall-specific: connection points at left and right edges (at ground level)
		bool bXLonger = (Max.X - Min.X) >= (Max.Y - Min.Y);
		if (bXLonger)
		{
			Anchors.Add(MakeShared<FJsonValueObject>(
				MakeAnchor(TEXT("wall_left"), TEXT("wall_connector"),
					FVector(Min.X, Center.Y, Min.Z), FVector(-1, 0, 0))));
			Anchors.Add(MakeShared<FJsonValueObject>(
				MakeAnchor(TEXT("wall_right"), TEXT("wall_connector"),
					FVector(Max.X, Center.Y, Min.Z), FVector(1, 0, 0))));
		}
		else
		{
			Anchors.Add(MakeShared<FJsonValueObject>(
				MakeAnchor(TEXT("wall_left"), TEXT("wall_connector"),
					FVector(Center.X, Min.Y, Min.Z), FVector(0, -1, 0))));
			Anchors.Add(MakeShared<FJsonValueObject>(
				MakeAnchor(TEXT("wall_right"), TEXT("wall_connector"),
					FVector(Center.X, Max.Y, Min.Z), FVector(0, 1, 0))));
		}

	}
	else if (AssetType == TEXT("floor"))
	{
		// Floor: top surface anchor
		Anchors.Add(MakeShared<FJsonValueObject>(
			MakeAnchor(TEXT("surface_center"), TEXT("surface"),
				FVector(Center.X, Center.Y, Max.Z), FVector(0, 0, 1))));

		// Edge center anchors (outward normal) — useful for "place facing this edge"
		Anchors.Add(MakeShared<FJsonValueObject>(
			MakeAnchor(TEXT("edge_north"), TEXT("floor_edge"),
				FVector(Max.X, Center.Y, Max.Z), FVector(1, 0, 0))));
		Anchors.Add(MakeShared<FJsonValueObject>(
			MakeAnchor(TEXT("edge_south"), TEXT("floor_edge"),
				FVector(Min.X, Center.Y, Max.Z), FVector(-1, 0, 0))));
		Anchors.Add(MakeShared<FJsonValueObject>(
			MakeAnchor(TEXT("edge_east"), TEXT("floor_edge"),
				FVector(Center.X, Max.Y, Max.Z), FVector(0, 1, 0))));
		Anchors.Add(MakeShared<FJsonValueObject>(
			MakeAnchor(TEXT("edge_west"), TEXT("floor_edge"),
				FVector(Center.X, Min.Y, Max.Z), FVector(0, -1, 0))));

		// Corner snap points — direction points ALONG the edge (tangent).
		// Used to place walls: connect wall_left/wall_right to these.
		// The wall extends from this corner in the opposite direction (face-to-face matching).
		// North edge (at Max.X): NW corner → east, NE corner → west
		Anchors.Add(MakeShared<FJsonValueObject>(
			MakeAnchor(TEXT("north_edge_west"), TEXT("wall_snap"),
				FVector(Max.X, Min.Y, Max.Z), FVector(0, 1, 0))));
		Anchors.Add(MakeShared<FJsonValueObject>(
			MakeAnchor(TEXT("north_edge_east"), TEXT("wall_snap"),
				FVector(Max.X, Max.Y, Max.Z), FVector(0, -1, 0))));
		// South edge (at Min.X): SW corner → east, SE corner → west
		Anchors.Add(MakeShared<FJsonValueObject>(
			MakeAnchor(TEXT("south_edge_west"), TEXT("wall_snap"),
				FVector(Min.X, Min.Y, Max.Z), FVector(0, 1, 0))));
		Anchors.Add(MakeShared<FJsonValueObject>(
			MakeAnchor(TEXT("south_edge_east"), TEXT("wall_snap"),
				FVector(Min.X, Max.Y, Max.Z), FVector(0, -1, 0))));
		// East edge (at Max.Y): NE corner → south, SE corner → north
		Anchors.Add(MakeShared<FJsonValueObject>(
			MakeAnchor(TEXT("east_edge_north"), TEXT("wall_snap"),
				FVector(Max.X, Max.Y, Max.Z), FVector(-1, 0, 0))));
		Anchors.Add(MakeShared<FJsonValueObject>(
			MakeAnchor(TEXT("east_edge_south"), TEXT("wall_snap"),
				FVector(Min.X, Max.Y, Max.Z), FVector(1, 0, 0))));
		// West edge (at Min.Y): NW corner → south, SW corner → north
		Anchors.Add(MakeShared<FJsonValueObject>(
			MakeAnchor(TEXT("west_edge_north"), TEXT("wall_snap"),
				FVector(Max.X, Min.Y, Max.Z), FVector(-1, 0, 0))));
		Anchors.Add(MakeShared<FJsonValueObject>(
			MakeAnchor(TEXT("west_edge_south"), TEXT("wall_snap"),
				FVector(Min.X, Min.Y, Max.Z), FVector(1, 0, 0))));
	}

	// --- Socket-based anchors (Tier 2) ---
	int32 SocketCount = 0;
	if (Mesh->Sockets.Num() > 0)
	{
		for (const UStaticMeshSocket* Socket : Mesh->Sockets)
		{
			if (!Socket) continue;
			FString SocketName = Socket->SocketName.ToString();
			FVector Pos = Socket->RelativeLocation;
			FRotator Rot = Socket->RelativeRotation;
			FVector Dir = Rot.RotateVector(FVector::ForwardVector);

			// Infer anchor type from socket name prefix
			FString AnchorType = TEXT("socket");
			if (SocketName.StartsWith(TEXT("snap_")))        AnchorType = TEXT("snap_point");
			else if (SocketName.StartsWith(TEXT("door_")))   AnchorType = TEXT("door");
			else if (SocketName.StartsWith(TEXT("wall_")))   AnchorType = TEXT("wall_connector");
			else if (SocketName.StartsWith(TEXT("connect_"))) AnchorType = TEXT("path_connect");

			Anchors.Add(MakeShared<FJsonValueObject>(
				MakeAnchor(FString::Printf(TEXT("socket_%s"), *SocketName),
					AnchorType, Pos, Dir)));
			SocketCount++;
		}
	}

	// Detect door/window openings via raycasting (works for walls, roofs, etc.)
	if (bDetectOpenings)
	{
		DetectOpenings(Mesh, Bounds, Anchors);
	}

	// Build result
	TSharedPtr<FJsonObject> Metadata = MakeShared<FJsonObject>();
	Metadata->SetStringField(TEXT("asset_path"), AssetPath);
	Metadata->SetStringField(TEXT("asset_type"), AssetType.IsEmpty() ? TEXT("unknown") : AssetType);
	Metadata->SetObjectField(TEXT("footprint"), Footprint);
	Metadata->SetObjectField(TEXT("bounds_3d"), Bounds3D);
	Metadata->SetArrayField(TEXT("anchors"), Anchors);
	Metadata->SetNumberField(TEXT("default_ground_offset"), 0.0);
	Metadata->SetNumberField(TEXT("scale_reference"), 1.0);

	// Save to anchor registry data asset
	FString RegistryPath;
	{
		TArray<FArborAnchor> AnchorStructs;
		for (const auto& AnchorVal : Anchors)
		{
			auto AnchorObj = AnchorVal->AsObject();
			if (!AnchorObj.IsValid()) continue;

			FArborAnchor A;
			A.Id = AnchorObj->GetStringField(TEXT("id"));
			AnchorObj->TryGetStringField(TEXT("type"), A.Type);

			const TSharedPtr<FJsonObject>* PosObj;
			if (AnchorObj->TryGetObjectField(TEXT("position"), PosObj))
			{
				A.Position.X = (*PosObj)->GetNumberField(TEXT("x"));
				A.Position.Y = (*PosObj)->GetNumberField(TEXT("y"));
				A.Position.Z = (*PosObj)->GetNumberField(TEXT("z"));
			}
			const TSharedPtr<FJsonObject>* DirObj;
			if (AnchorObj->TryGetObjectField(TEXT("direction"), DirObj))
			{
				A.Direction.X = (*DirObj)->GetNumberField(TEXT("x"));
				A.Direction.Y = (*DirObj)->GetNumberField(TEXT("y"));
				A.Direction.Z = (*DirObj)->GetNumberField(TEXT("z"));
			}
			double W = 0;
			if (AnchorObj->TryGetNumberField(TEXT("width"), W))
			{
				A.Width = static_cast<float>(W);
			}
			double H = 0;
			if (AnchorObj->TryGetNumberField(TEXT("height"), H))
			{
				A.Height = static_cast<float>(H);
			}

			AnchorStructs.Add(A);
		}

		FArborMeshAnchors MeshData;
		MeshData.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(AssetPath));
		MeshData.AssetType = AssetType.IsEmpty() ? TEXT("unknown") : AssetType;
		MeshData.Anchors = MoveTemp(AnchorStructs);

		UArborAnchorRegistry* Registry = UArborAnchorRegistry::FindOrCreateRegistry(AssetPath);
		if (Registry)
		{
			Registry->SetAnchors(AssetPath, MeshData);
			UEditorAssetLibrary::SaveLoadedAsset(Registry);
			RegistryPath = UArborAnchorRegistry::GetRegistryPath(AssetPath);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[ArborAnchorAnalyzer] AnalyzeMesh: %s → %d anchors, registry %s"),
		*AssetPath, Anchors.Num(), *RegistryPath);

	// Return with success
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_type"), AssetType.IsEmpty() ? TEXT("unknown") : AssetType);
	Result->SetObjectField(TEXT("footprint"), Footprint);
	Result->SetObjectField(TEXT("bounds_3d"), Bounds3D);
	Result->SetArrayField(TEXT("anchors"), Anchors);
	Result->SetNumberField(TEXT("socket_count"), SocketCount);
	Result->SetStringField(TEXT("registry_path"), RegistryPath);
	return SerializeJson(Result);
}

// ---------------------------------------------------------------------------
// GetAnchorMetadata
// ---------------------------------------------------------------------------

FString UArborAnchorAnalyzer::GetAnchorMetadata(const FString& AssetPath)
{
	// Primary: read from registry data asset
	UArborAnchorRegistry* Registry = UArborAnchorRegistry::FindRegistry(AssetPath);
	if (Registry)
	{
		const FArborMeshAnchors* Data = Registry->FindAnchors(AssetPath);
		if (Data)
		{
			TArray<TSharedPtr<FJsonValue>> AnchorsArr;
			for (const FArborAnchor& A : Data->Anchors)
			{
				TSharedPtr<FJsonObject> AnchorObj = MakeShared<FJsonObject>();
				AnchorObj->SetStringField(TEXT("id"), A.Id);
				AnchorObj->SetStringField(TEXT("type"), A.Type);
				AnchorObj->SetObjectField(TEXT("position"), VecToJson(A.Position));
				AnchorObj->SetObjectField(TEXT("direction"), VecToJson(A.Direction));
				if (A.Width > 0.f)
				{
					AnchorObj->SetNumberField(TEXT("width"), A.Width);
				}
				if (A.Height > 0.f)
				{
					AnchorObj->SetNumberField(TEXT("height"), A.Height);
				}
				AnchorsArr.Add(MakeShared<FJsonValueObject>(AnchorObj));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), true);
			Result->SetStringField(TEXT("asset_path"), AssetPath);
			Result->SetStringField(TEXT("asset_type"), Data->AssetType);
			Result->SetArrayField(TEXT("anchors"), AnchorsArr);
			return SerializeJson(Result);
		}
	}

	// Fallback: legacy sidecar JSON
	const FString SidecarPath = GetSidecarPath(AssetPath);
	FString JsonStr;
	if (!FFileHelper::LoadFileToString(JsonStr, *SidecarPath))
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"No anchor metadata for %s\"}"),
			*AssetPath);
	}

	auto Metadata = ParseJson(JsonStr);
	if (!Metadata.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON in sidecar file\"}");
	}

	Metadata->SetBoolField(TEXT("success"), true);
	return SerializeJson(Metadata);
}

// ---------------------------------------------------------------------------
// SetAnchorMetadata
// ---------------------------------------------------------------------------

FString UArborAnchorAnalyzer::SetAnchorMetadata(const FString& ParamsJson)
{
	auto Params = ParseJson(ParamsJson);
	if (!Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const TSharedPtr<FJsonObject>* MetadataPtr;
	if (!Params->TryGetObjectField(TEXT("metadata"), MetadataPtr))
	{
		return TEXT("{\"success\":false,\"error\":\"Missing 'metadata' field\"}");
	}

	// Parse anchors from the metadata JSON into FArborAnchor structs
	const TArray<TSharedPtr<FJsonValue>>* AnchorsArr;
	if (!(*MetadataPtr)->TryGetArrayField(TEXT("anchors"), AnchorsArr))
	{
		return TEXT("{\"success\":false,\"error\":\"Missing 'anchors' in metadata\"}");
	}

	FArborMeshAnchors MeshData;
	(*MetadataPtr)->TryGetStringField(TEXT("asset_type"), MeshData.AssetType);
	MeshData.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(AssetPath));

	for (const auto& AnchorVal : *AnchorsArr)
	{
		auto AnchorObj = AnchorVal->AsObject();
		if (!AnchorObj.IsValid()) continue;

		FArborAnchor A;
		A.Id = AnchorObj->GetStringField(TEXT("id"));
		AnchorObj->TryGetStringField(TEXT("type"), A.Type);

		const TSharedPtr<FJsonObject>* PosObj;
		if (AnchorObj->TryGetObjectField(TEXT("position"), PosObj))
		{
			A.Position.X = (*PosObj)->GetNumberField(TEXT("x"));
			A.Position.Y = (*PosObj)->GetNumberField(TEXT("y"));
			A.Position.Z = (*PosObj)->GetNumberField(TEXT("z"));
		}
		const TSharedPtr<FJsonObject>* DirObj;
		if (AnchorObj->TryGetObjectField(TEXT("direction"), DirObj))
		{
			A.Direction.X = (*DirObj)->GetNumberField(TEXT("x"));
			A.Direction.Y = (*DirObj)->GetNumberField(TEXT("y"));
			A.Direction.Z = (*DirObj)->GetNumberField(TEXT("z"));
		}
		double W = 0;
		if (AnchorObj->TryGetNumberField(TEXT("width"), W))
		{
			A.Width = static_cast<float>(W);
		}
		double H = 0;
		if (AnchorObj->TryGetNumberField(TEXT("height"), H))
		{
			A.Height = static_cast<float>(H);
		}

		MeshData.Anchors.Add(A);
	}

	UArborAnchorRegistry* Registry = UArborAnchorRegistry::FindOrCreateRegistry(AssetPath);
	if (!Registry)
	{
		return TEXT("{\"success\":false,\"error\":\"Failed to create anchor registry\"}");
	}

	Registry->SetAnchors(AssetPath, MeshData);
	UEditorAssetLibrary::SaveLoadedAsset(Registry);

	FString RegistryPath = UArborAnchorRegistry::GetRegistryPath(AssetPath);
	UE_LOG(LogTemp, Log, TEXT("[ArborAnchorAnalyzer] SetAnchorMetadata: %s → %s"), *AssetPath, *RegistryPath);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("registry_path"), RegistryPath);
	return SerializeJson(Result);
}

// ---------------------------------------------------------------------------
// FindCompatibleAnchors
// ---------------------------------------------------------------------------

FString UArborAnchorAnalyzer::FindCompatibleAnchors(const FString& ParamsJson)
{
	auto Params = ParseJson(ParamsJson);
	if (!Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	const FString FromAsset = Params->GetStringField(TEXT("from_asset"));
	const FString ToAsset = Params->GetStringField(TEXT("to_asset"));

	// Optional type filter — only return pairs where from_type or to_type matches
	FString FilterType;
	Params->TryGetStringField(TEXT("filter_type"), FilterType);

	// Load both anchor sidecars
	auto FromMeta = ParseJson(GetAnchorMetadata(FromAsset));
	auto ToMeta = ParseJson(GetAnchorMetadata(ToAsset));

	if (!FromMeta.IsValid() || !FromMeta->GetBoolField(TEXT("success")))
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"No anchor metadata for %s\"}"), *FromAsset);
	}
	if (!ToMeta.IsValid() || !ToMeta->GetBoolField(TEXT("success")))
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"No anchor metadata for %s\"}"), *ToAsset);
	}

	// Load compatibility table
	const FString ConfigPath = FPaths::ProjectPluginsDir() / TEXT("Arbor/Content/Config/anchor_compatibility.json");
	FString ConfigStr;
	if (!FFileHelper::LoadFileToString(ConfigStr, *ConfigPath))
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"Cannot load compatibility table at %s\"}"), *ConfigPath);
	}

	TArray<TSharedPtr<FJsonValue>> Rules;
	auto ConfigReader = TJsonReaderFactory<>::Create(ConfigStr);
	if (!FJsonSerializer::Deserialize(ConfigReader, Rules))
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON in compatibility table\"}");
	}

	// Get anchor arrays
	const TArray<TSharedPtr<FJsonValue>>* FromAnchors;
	const TArray<TSharedPtr<FJsonValue>>* ToAnchors;
	if (!FromMeta->TryGetArrayField(TEXT("anchors"), FromAnchors) ||
		!ToMeta->TryGetArrayField(TEXT("anchors"), ToAnchors))
	{
		return TEXT("{\"success\":false,\"error\":\"Missing anchors array\"}");
	}

	// Match anchors using compatibility rules
	TArray<TSharedPtr<FJsonValue>> Pairs;

	for (const auto& FromVal : *FromAnchors)
	{
		auto FromObj = FromVal->AsObject();
		if (!FromObj.IsValid()) continue;

		FString FromType;
		FromObj->TryGetStringField(TEXT("type"), FromType);
		FString FromId = FromObj->GetStringField(TEXT("id"));

		for (const auto& ToVal : *ToAnchors)
		{
			auto ToObj = ToVal->AsObject();
			if (!ToObj.IsValid()) continue;

			FString ToType;
			ToObj->TryGetStringField(TEXT("type"), ToType);
			FString ToId = ToObj->GetStringField(TEXT("id"));

			// Check each rule for a match
			for (const auto& RuleVal : Rules)
			{
				auto Rule = RuleVal->AsObject();
				if (!Rule.IsValid()) continue;

				FString RuleFrom = Rule->GetStringField(TEXT("from_type"));
				FString RuleTo = Rule->GetStringField(TEXT("to_type"));

				bool bMatch = (FromType == RuleFrom && ToType == RuleTo);
				// Also check reverse: to_type matches from, from_type matches to
				if (!bMatch)
				{
					bMatch = (FromType == RuleTo && ToType == RuleFrom);
				}

				if (bMatch)
				{
					// Apply type filter if specified
					if (!FilterType.IsEmpty())
					{
						if (FromType != FilterType && ToType != FilterType)
						{
							break; // Rule matched but doesn't pass filter
						}
					}

					TSharedPtr<FJsonObject> Pair = MakeShared<FJsonObject>();
					Pair->SetStringField(TEXT("from_anchor"), FromId);
					Pair->SetStringField(TEXT("to_anchor"), ToId);
					Pair->SetStringField(TEXT("from_type"), FromType);
					Pair->SetStringField(TEXT("to_type"), ToType);
					Pair->SetStringField(TEXT("hint"), Rule->GetStringField(TEXT("hint")));
					Pair->SetStringField(TEXT("relationship"), Rule->GetStringField(TEXT("relationship")));
					Pairs.Add(MakeShared<FJsonValueObject>(Pair));
					break; // First matching rule wins for this pair
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[ArborAnchorAnalyzer] FindCompatibleAnchors: %s × %s → %d pairs"),
		*FromAsset, *ToAsset, Pairs.Num());

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetArrayField(TEXT("pairs"), Pairs);
	return SerializeJson(Result);
}

// ---------------------------------------------------------------------------
// Möller–Trumbore ray-triangle intersection (local mesh space)
// ---------------------------------------------------------------------------

static bool RayHitsTriangle(
	const FVector3f& Origin, const FVector3f& Dir, float MaxDist,
	const FVector3f& V0, const FVector3f& V1, const FVector3f& V2)
{
	const float EPSILON = 1e-6f;
	FVector3f Edge1 = V1 - V0;
	FVector3f Edge2 = V2 - V0;
	FVector3f H = FVector3f::CrossProduct(Dir, Edge2);
	float A = FVector3f::DotProduct(Edge1, H);
	if (FMath::Abs(A) < EPSILON) return false;
	float F = 1.0f / A;
	FVector3f S = Origin - V0;
	float U = F * FVector3f::DotProduct(S, H);
	if (U < 0.0f || U > 1.0f) return false;
	FVector3f Q = FVector3f::CrossProduct(S, Edge1);
	float V = F * FVector3f::DotProduct(Dir, Q);
	if (V < 0.0f || U + V > 1.0f) return false;
	float T = F * FVector3f::DotProduct(Edge2, Q);
	return T > EPSILON && T < MaxDist;
}

// ---------------------------------------------------------------------------
// DetectOpenings — ray-mesh intersection to find door/window holes
// Uses direct mesh geometry queries (no physics system dependency)
// ---------------------------------------------------------------------------

void UArborAnchorAnalyzer::DetectOpenings(UStaticMesh* Mesh, const FBox& Bounds,
	TArray<TSharedPtr<FJsonValue>>& OutAnchors)
{
	if (!Mesh) return;

	// Access mesh render data directly (always available in editor)
	const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
	if (!RenderData || RenderData->LODResources.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ArborAnchorAnalyzer] DetectOpenings: no render data"));
		return;
	}

	const FStaticMeshLODResources& LOD = RenderData->LODResources[0];
	const FPositionVertexBuffer& PosBuffer = LOD.VertexBuffers.PositionVertexBuffer;
	const int32 NumVertices = PosBuffer.GetNumVertices();
	if (NumVertices == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ArborAnchorAnalyzer] DetectOpenings: no vertices"));
		return;
	}

	// Get index buffer
	TArray<uint32> Indices;
	LOD.IndexBuffer.GetCopy(Indices);
	if (Indices.Num() < 3)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ArborAnchorAnalyzer] DetectOpenings: no triangles"));
		return;
	}

	// Build triangle list from mesh geometry
	struct FTri { FVector3f V0, V1, V2; };
	TArray<FTri> Triangles;
	Triangles.Reserve(Indices.Num() / 3);
	for (int32 i = 0; i + 2 < Indices.Num(); i += 3)
	{
		FTri T;
		T.V0 = PosBuffer.VertexPosition(Indices[i]);
		T.V1 = PosBuffer.VertexPosition(Indices[i + 1]);
		T.V2 = PosBuffer.VertexPosition(Indices[i + 2]);
		Triangles.Add(T);
	}

	UE_LOG(LogTemp, Log, TEXT("[ArborAnchorAnalyzer] DetectOpenings: %d triangles, %d vertices"),
		Triangles.Num(), NumVertices);

	const FVector Size = Bounds.GetSize();
	const FVector Min = Bounds.Min;
	const FVector Max = Bounds.Max;

	// Find the thin axis (wall thickness) — smallest bounding box dimension
	int32 ThinAxis = 0;
	if (Size.Y < Size.X && Size.Y < Size.Z) ThinAxis = 1;
	else if (Size.Z < Size.X && Size.Z < Size.Y) ThinAxis = 2;

	// Face axes: HAxis = horizontal span, VAxis = vertical span (prefer Z)
	int32 HAxis, VAxis;
	if (ThinAxis == 0)      { HAxis = 1; VAxis = 2; }
	else if (ThinAxis == 1) { HAxis = 0; VAxis = 2; }
	else                    { HAxis = 0; VAxis = 1; }

	// Grid raycasting through mesh geometry
	const float Step = 5.0f; // 5cm resolution
	const float HMin = (float)Min[HAxis];
	const float HMax = (float)Max[HAxis];
	const float VMin = (float)Min[VAxis];
	const float VMax = (float)Max[VAxis];
	const float ThinMin = (float)Min[ThinAxis];
	const float ThinMax = (float)Max[ThinAxis];
	const float Margin = (float)Size[ThinAxis] + 10.0f;

	const int32 HCount = FMath::Max(1, FMath::CeilToInt((HMax - HMin) / Step));
	const int32 VCount = FMath::Max(1, FMath::CeilToInt((VMax - VMin) / Step));

	// Safety: skip very large grids (> 100k rays)
	if ((int64)HCount * VCount > 100000)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ArborAnchorAnalyzer] DetectOpenings: grid too large (%dx%d), skipping"),
			HCount, VCount);
		return;
	}

	TArray<bool> Grid;
	Grid.SetNumZeroed(HCount * VCount);

	const float RayLength = (ThinMax - ThinMin) + 2.0f * Margin;

	for (int32 vi = 0; vi < VCount; ++vi)
	{
		const float V = VMin + (vi + 0.5f) * Step;
		for (int32 hi = 0; hi < HCount; ++hi)
		{
			const float H = HMin + (hi + 0.5f) * Step;

			FVector3f RayOrigin = FVector3f::ZeroVector;
			RayOrigin[HAxis] = H;
			RayOrigin[VAxis] = V;
			RayOrigin[ThinAxis] = ThinMin - Margin;

			FVector3f RayDir = FVector3f::ZeroVector;
			RayDir[ThinAxis] = 1.0f;

			bool bHit = false;
			for (const FTri& T : Triangles)
			{
				if (RayHitsTriangle(RayOrigin, RayDir, RayLength, T.V0, T.V1, T.V2))
				{
					bHit = true;
					break;
				}
			}
			Grid[vi * HCount + hi] = bHit;
		}
	}

	int32 HitCount = 0;
	for (bool b : Grid) { if (b) HitCount++; }
	UE_LOG(LogTemp, Log, TEXT("[ArborAnchorAnalyzer] DetectOpenings: grid %dx%d, hits=%d, misses=%d, ThinAxis=%d(%.1f), H=%d(%.1f-%.1f), V=%d(%.1f-%.1f)"),
		HCount, VCount, HitCount, HCount * VCount - HitCount,
		ThinAxis, Size[ThinAxis], HAxis, HMin, HMax, VAxis, VMin, VMax);

	// Phase 1: Flood-fill from boundary to mark exterior void cells
	TArray<bool> Exterior;
	Exterior.SetNumZeroed(HCount * VCount);
	{
		TArray<int32> BoundaryQueue;
		// Seed from all boundary miss cells
		for (int32 vi = 0; vi < VCount; ++vi)
		{
			for (int32 hi = 0; hi < HCount; ++hi)
			{
				if (vi == 0 || vi == VCount - 1 || hi == 0 || hi == HCount - 1)
				{
					const int32 Idx = vi * HCount + hi;
					if (!Grid[Idx] && !Exterior[Idx])
					{
						Exterior[Idx] = true;
						BoundaryQueue.Add(Idx);
					}
				}
			}
		}
		while (BoundaryQueue.Num() > 0)
		{
			const int32 CurIdx = BoundaryQueue.Pop();
			const int32 CurH = CurIdx % HCount;
			const int32 CurV = CurIdx / HCount;
			const int32 Neighbors[] = {
				(CurV > 0)            ? (CurV - 1) * HCount + CurH : -1,
				(CurV < VCount - 1)   ? (CurV + 1) * HCount + CurH : -1,
				(CurH > 0)            ? CurV * HCount + (CurH - 1)  : -1,
				(CurH < HCount - 1)   ? CurV * HCount + (CurH + 1)  : -1,
			};
			for (const int32 N : Neighbors)
			{
				if (N >= 0 && !Grid[N] && !Exterior[N])
				{
					Exterior[N] = true;
					BoundaryQueue.Add(N);
				}
			}
		}
	}

	int32 InteriorMisses = 0;
	for (int32 i = 0; i < HCount * VCount; ++i)
	{
		if (!Grid[i] && !Exterior[i]) InteriorMisses++;
	}
	UE_LOG(LogTemp, Log, TEXT("[ArborAnchorAnalyzer] DetectOpenings: exterior=%d, interior_misses=%d"),
		HCount * VCount - HitCount - InteriorMisses, InteriorMisses);

	// Phase 2: Flood-fill remaining interior miss cells into opening regions
	TArray<int32> Labels;
	Labels.SetNumZeroed(HCount * VCount);
	int32 NextLabel = 1;

	struct FOpeningRegion { float HMin, HMax, VMin, VMax; int32 CellCount; };
	TArray<FOpeningRegion> Regions;

	for (int32 vi = 0; vi < VCount; ++vi)
	{
		for (int32 hi = 0; hi < HCount; ++hi)
		{
			const int32 Idx = vi * HCount + hi;
			// Only process interior miss cells (not hit, not exterior)
			if (Grid[Idx] || Exterior[Idx] || Labels[Idx] != 0) continue;

			FOpeningRegion Region;
			Region.HMin = HMax; Region.HMax = HMin;
			Region.VMin = VMax; Region.VMax = VMin;
			Region.CellCount = 0;
			const int32 Label = NextLabel++;

			TArray<int32> Queue;
			Queue.Add(Idx);
			Labels[Idx] = Label;

			while (Queue.Num() > 0)
			{
				const int32 CurIdx = Queue.Pop();
				const int32 CurH = CurIdx % HCount;
				const int32 CurV = CurIdx / HCount;

				const float CellH = HMin + (CurH + 0.5f) * Step;
				const float CellV = VMin + (CurV + 0.5f) * Step;
				Region.HMin = FMath::Min(Region.HMin, CellH - Step * 0.5f);
				Region.HMax = FMath::Max(Region.HMax, CellH + Step * 0.5f);
				Region.VMin = FMath::Min(Region.VMin, CellV - Step * 0.5f);
				Region.VMax = FMath::Max(Region.VMax, CellV + Step * 0.5f);
				Region.CellCount++;

				const int32 Neighbors[] = {
					(CurV > 0)            ? (CurV - 1) * HCount + CurH : -1,
					(CurV < VCount - 1)   ? (CurV + 1) * HCount + CurH : -1,
					(CurH > 0)            ? CurV * HCount + (CurH - 1)  : -1,
					(CurH < HCount - 1)   ? CurV * HCount + (CurH + 1)  : -1,
				};
				for (const int32 N : Neighbors)
				{
					if (N >= 0 && !Grid[N] && !Exterior[N] && Labels[N] == 0)
					{
						Labels[N] = Label;
						Queue.Add(N);
					}
				}
			}

			Regions.Add(Region);
		}
	}

	// Log regions
	for (int32 i = 0; i < Regions.Num(); ++i)
	{
		const FOpeningRegion& R = Regions[i];
		UE_LOG(LogTemp, Log, TEXT("[ArborAnchorAnalyzer] DetectOpenings: Region %d: %.0fx%.0f cells=%d H=[%.1f..%.1f] V=[%.1f..%.1f]"),
			i, R.HMax - R.HMin, R.VMax - R.VMin, R.CellCount,
			R.HMin, R.HMax, R.VMin, R.VMax);
	}

	// Filter and classify regions into door/window anchors
	const float MinOpeningSize = 30.0f;
	int32 DoorIndex = 0;
	int32 WindowIndex = 0;

	for (const FOpeningRegion& R : Regions)
	{
		const float Width = R.HMax - R.HMin;
		const float Height = R.VMax - R.VMin;

		// Skip tiny gaps
		if (Width < MinOpeningSize || Height < MinOpeningSize) continue;

		// Center position in mesh-local space
		FVector Center = FVector::ZeroVector;
		Center[HAxis] = (R.HMin + R.HMax) * 0.5f;
		Center[VAxis] = (R.VMin + R.VMax) * 0.5f;
		Center[ThinAxis] = (ThinMin + ThinMax) * 0.5f;

		// Direction: outward normal along thin axis
		FVector Dir = FVector::ZeroVector;
		Dir[ThinAxis] = 1.0f;

		// Classify: door if bottom of opening is near wall base (within 20cm tolerance)
		const float DoorThreshold = 20.0f;
		const bool bIsDoor = (VAxis == 2)
			? (R.VMin <= Bounds.Min.Z + DoorThreshold)
			: (R.VMin <= VMin + DoorThreshold);

		const FString TypeStr = bIsDoor ? TEXT("door_opening") : TEXT("window_opening");
		int32& Index = bIsDoor ? DoorIndex : WindowIndex;
		const FString Id = FString::Printf(TEXT("%s_%d"), *TypeStr, Index++);

		OutAnchors.Add(MakeShared<FJsonValueObject>(
			MakeAnchor(Id, TypeStr, Center, Dir, Width, Height)));
	}

	UE_LOG(LogTemp, Log,
		TEXT("[ArborAnchorAnalyzer] DetectOpenings: %d doors, %d windows found"),
		DoorIndex, WindowIndex);
}

// ---------------------------------------------------------------------------
// Anchor color LUT
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
// DrawAnchors
// ---------------------------------------------------------------------------

FString UArborAnchorAnalyzer::DrawAnchors(const FString& ParamsJson)
{
	auto Params = ParseJson(ParamsJson);
	if (!Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	// Optional offset
	FVector Location = FVector::ZeroVector;
	const TSharedPtr<FJsonObject>* LocObj;
	if (Params->TryGetObjectField(TEXT("location"), LocObj))
	{
		Location.X = (*LocObj)->GetNumberField(TEXT("x"));
		Location.Y = (*LocObj)->GetNumberField(TEXT("y"));
		Location.Z = (*LocObj)->GetNumberField(TEXT("z"));
	}

	double Radius = 6.0;
	Params->TryGetNumberField(TEXT("radius"), Radius);
	double ArrowLength = 30.0;
	Params->TryGetNumberField(TEXT("arrow_length"), ArrowLength);
	double Duration = 60.0;  // Default 60s instead of persistent
	Params->TryGetNumberField(TEXT("duration"), Duration);
	double Thickness = 2.0;
	Params->TryGetNumberField(TEXT("thickness"), Thickness);
	double MaxDrawDistance = 5000.0;  // Only draw anchors within 50m of camera
	Params->TryGetNumberField(TEXT("max_draw_distance"), MaxDrawDistance);

	// Load anchor metadata
	FString MetaJson = GetAnchorMetadata(AssetPath);
	auto Meta = ParseJson(MetaJson);
	if (!Meta.IsValid() || !Meta->GetBoolField(TEXT("success")))
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"No anchor metadata for %s\"}"), *AssetPath);
	}

	const TArray<TSharedPtr<FJsonValue>>* AnchorsArr;
	if (!Meta->TryGetArrayField(TEXT("anchors"), AnchorsArr))
	{
		return TEXT("{\"success\":false,\"error\":\"No anchors array in metadata\"}");
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return TEXT("{\"success\":false,\"error\":\"No editor world\"}");
	}

	// Get editor viewport camera location for distance culling
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

	bool bPersistent = Duration < 0;
	float DrawDuration = bPersistent ? -1.0f : static_cast<float>(Duration);

	int32 AnchorCount = 0;
	int32 CulledCount = 0;
	for (const auto& AnchorVal : *AnchorsArr)
	{
		auto AnchorObj = AnchorVal->AsObject();
		if (!AnchorObj.IsValid()) continue;

		FString Id = AnchorObj->GetStringField(TEXT("id"));
		FString Type;
		AnchorObj->TryGetStringField(TEXT("type"), Type);

		const TSharedPtr<FJsonObject>* PosObj;
		FVector Pos = FVector::ZeroVector;
		if (AnchorObj->TryGetObjectField(TEXT("position"), PosObj))
		{
			Pos.X = (*PosObj)->GetNumberField(TEXT("x"));
			Pos.Y = (*PosObj)->GetNumberField(TEXT("y"));
			Pos.Z = (*PosObj)->GetNumberField(TEXT("z"));
		}

		const TSharedPtr<FJsonObject>* DirObj;
		FVector Dir = FVector::UpVector;
		if (AnchorObj->TryGetObjectField(TEXT("direction"), DirObj))
		{
			Dir.X = (*DirObj)->GetNumberField(TEXT("x"));
			Dir.Y = (*DirObj)->GetNumberField(TEXT("y"));
			Dir.Z = (*DirObj)->GetNumberField(TEXT("z"));
		}

		FVector WorldPos = Location + Pos;

		// Distance culling: skip anchors far from camera
		if (bHasCamera && MaxDrawDistance > 0)
		{
			double Dist = FVector::Dist(WorldPos, CameraLocation);
			if (Dist > MaxDrawDistance)
			{
				CulledCount++;
				continue;
			}
		}

		FColor Color = GetAnchorColor(Type);

		// Sphere
		DrawDebugSphere(World, WorldPos, Radius, 12, Color, bPersistent, DrawDuration, SDPG_Foreground, Thickness);

		// Direction arrow
		FVector ArrowEnd = WorldPos + Dir * ArrowLength;
		DrawDebugDirectionalArrow(World, WorldPos, ArrowEnd, 15.0f, Color, bPersistent, DrawDuration, SDPG_Foreground, Thickness);

		// Label
		DrawDebugString(World, WorldPos + FVector(0, 0, Radius + 5), Id, nullptr, Color, DrawDuration, false, 1.0f);

		AnchorCount++;
	}

	UE_LOG(LogTemp, Log, TEXT("[ArborAnchorAnalyzer] DrawAnchors: %s — %d drawn, %d culled"), *AssetPath, AnchorCount, CulledCount);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("anchor_count"), AnchorCount);
	Result->SetNumberField(TEXT("culled_count"), CulledCount);
	return SerializeJson(Result);
}

// ---------------------------------------------------------------------------
// FlushAnchors
// ---------------------------------------------------------------------------

void UArborAnchorAnalyzer::FlushAnchors()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World)
	{
		FlushPersistentDebugLines(World);
		FlushDebugStrings(World);
		UE_LOG(LogTemp, Log, TEXT("[ArborAnchorAnalyzer] FlushAnchors: cleared"));
	}
}

// ---------------------------------------------------------------------------
// AddAnchorDebugToActors
// ---------------------------------------------------------------------------

FString UArborAnchorAnalyzer::AddAnchorDebugToActors(const FString& LabelPrefix)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return TEXT("{\"success\":false,\"error\":\"No editor world\"}");
	}

	int32 Added = 0;
	int32 Skipped = 0;

	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		AStaticMeshActor* Actor = *It;
		if (!Actor) continue;

		// Filter by label prefix if provided
		if (!LabelPrefix.IsEmpty() && !Actor->GetActorLabel().StartsWith(LabelPrefix))
		{
			continue;
		}

		// Skip if already has the component
		if (Actor->FindComponentByClass<UArborAnchorComponent>())
		{
			Skipped++;
			continue;
		}

		// Create and register the component
		UArborAnchorComponent* Comp = NewObject<UArborAnchorComponent>(
			Actor, UArborAnchorComponent::StaticClass(),
			MakeUniqueObjectName(Actor, UArborAnchorComponent::StaticClass(), TEXT("ArborAnchorDebug")));
		Comp->RegisterComponent();
		Actor->AddInstanceComponent(Comp);
		Added++;
	}

	UE_LOG(LogTemp, Log, TEXT("[ArborAnchorAnalyzer] AddAnchorDebugToActors('%s'): added %d, skipped %d"),
		*LabelPrefix, Added, Skipped);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("added"), Added);
	Result->SetNumberField(TEXT("skipped"), Skipped);
	return SerializeJson(Result);
}

// ---------------------------------------------------------------------------
// AnalyzePack
// ---------------------------------------------------------------------------

FString UArborAnchorAnalyzer::AnalyzePack(const FString& ParamsJson)
{
	auto Params = ParseJson(ParamsJson);
	if (!Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	FString FolderPath;
	if (!Params->TryGetStringField(TEXT("folder_path"), FolderPath) || FolderPath.IsEmpty())
	{
		return TEXT("{\"success\":false,\"error\":\"folder_path required\"}");
	}

	FString AssetTypeOverride;
	Params->TryGetStringField(TEXT("asset_type"), AssetTypeOverride);

	// Find all static meshes under the folder
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByPath(FName(*FolderPath), Assets, /*bRecursive=*/true);

	int32 Analyzed = 0;
	int32 Failed = 0;
	TArray<TSharedPtr<FJsonValue>> Results;
	TArray<TSharedPtr<FJsonValue>> Errors;

	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetClassPath != UStaticMesh::StaticClass()->GetClassPathName())
		{
			continue;
		}

		FString MeshPath = Asset.GetObjectPathString();
		// Strip object suffix for clean path
		int32 DotIdx;
		if (MeshPath.FindChar('.', DotIdx))
		{
			MeshPath.LeftInline(DotIdx);
		}

		// Build params and call AnalyzeMesh
		TSharedPtr<FJsonObject> MeshParams = MakeShared<FJsonObject>();
		MeshParams->SetStringField(TEXT("asset_path"), MeshPath);
		if (!AssetTypeOverride.IsEmpty())
		{
			MeshParams->SetStringField(TEXT("asset_type"), AssetTypeOverride);
		}

		FString MeshResult = AnalyzeMesh(SerializeJson(MeshParams));
		auto ResultObj = ParseJson(MeshResult);

		if (ResultObj.IsValid() && ResultObj->GetBoolField(TEXT("success")))
		{
			const TArray<TSharedPtr<FJsonValue>>* AnchorsArr;
			int32 AnchorCount = 0;
			if (ResultObj->TryGetArrayField(TEXT("anchors"), AnchorsArr))
			{
				AnchorCount = AnchorsArr->Num();
			}

			int32 SockCount = 0;
			ResultObj->TryGetNumberField(TEXT("socket_count"), SockCount);

			FString ResultType;
			ResultObj->TryGetStringField(TEXT("asset_type"), ResultType);

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("asset_path"), MeshPath);
			Entry->SetStringField(TEXT("asset_type"), ResultType);
			Entry->SetNumberField(TEXT("anchor_count"), AnchorCount);
			Entry->SetNumberField(TEXT("socket_count"), SockCount);
			Results.Add(MakeShared<FJsonValueObject>(Entry));
			Analyzed++;
		}
		else
		{
			FString Error;
			if (ResultObj.IsValid())
			{
				ResultObj->TryGetStringField(TEXT("error"), Error);
			}
			Errors.Add(MakeShared<FJsonValueString>(
				Error.IsEmpty() ? FString::Printf(TEXT("Failed: %s"), *MeshPath) : Error));
			Failed++;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[ArborAnchorAnalyzer] AnalyzePack: %s → %d analyzed, %d failed"),
		*FolderPath, Analyzed, Failed);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("folder_path"), FolderPath);
	Result->SetNumberField(TEXT("analyzed"), Analyzed);
	Result->SetNumberField(TEXT("failed"), Failed);
	Result->SetArrayField(TEXT("results"), Results);
	Result->SetArrayField(TEXT("errors"), Errors);
	return SerializeJson(Result);
}

// ---------------------------------------------------------------------------
// ListAnalyzedAssets
// ---------------------------------------------------------------------------

FString UArborAnchorAnalyzer::ListAnalyzedAssets(const FString& FolderPath)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	// Find all UArborAnchorRegistry data assets
	TArray<FAssetData> RegistryAssets;
	AssetRegistry.GetAssetsByClass(UArborAnchorRegistry::StaticClass()->GetClassPathName(), RegistryAssets);

	TArray<TSharedPtr<FJsonValue>> Assets;

	for (const FAssetData& RegAsset : RegistryAssets)
	{
		// Filter by folder if requested
		FString RegPath = RegAsset.GetObjectPathString();
		if (!FolderPath.IsEmpty() && !RegPath.StartsWith(FolderPath))
		{
			continue;
		}

		UArborAnchorRegistry* Registry = Cast<UArborAnchorRegistry>(RegAsset.GetAsset());
		if (!Registry) continue;

		for (const auto& Pair : Registry->Entries)
		{
			const FString& AssetPath = Pair.Key;
			const FArborMeshAnchors& Data = Pair.Value;

			// Apply folder filter to individual assets too
			if (!FolderPath.IsEmpty() && !AssetPath.StartsWith(FolderPath))
			{
				continue;
			}

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("asset_path"), AssetPath);
			Entry->SetStringField(TEXT("asset_type"), Data.AssetType);
			Entry->SetNumberField(TEXT("anchor_count"), Data.Anchors.Num());
			Assets.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), Assets.Num());
	Result->SetArrayField(TEXT("assets"), Assets);
	return SerializeJson(Result);
}
