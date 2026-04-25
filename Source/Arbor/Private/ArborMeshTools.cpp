#include "ArborMeshTools.h"
#include "Engine/StaticMesh.h"
#include "EditorAssetLibrary.h"
#include "StaticMeshDescription.h"
#include "MeshDescription.h"
#include "MeshDescriptionBase.h"
#include "StaticMeshAttributes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "PhysicsEngine/BodySetup.h"

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

static TSharedPtr<FJsonObject> VecToJson(const FVector& V)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("x"), V.X);
	Obj->SetNumberField(TEXT("y"), V.Y);
	Obj->SetNumberField(TEXT("z"), V.Z);
	return Obj;
}

static TSharedPtr<FJsonObject> BoxToJson(const FBox& Box)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetObjectField(TEXT("min"), VecToJson(Box.Min));
	Obj->SetObjectField(TEXT("max"), VecToJson(Box.Max));
	return Obj;
}

// ============================================================================
// Vertex Manipulation
// ============================================================================

bool UArborMeshTools::ApplyVertexOffset(UStaticMesh* Mesh, const FVector& Offset)
{
	if (!Mesh) return false;

	const int32 NumLODs = Mesh->GetNumLODs();
	bool bSuccess = false;

	for (int32 LOD = 0; LOD < NumLODs; LOD++)
	{
		FMeshDescription* MD = Mesh->GetMeshDescription(LOD);
		if (!MD) continue;

		FStaticMeshAttributes Attributes(*MD);
		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();

		for (FVertexID VID : MD->Vertices().GetElementIDs())
		{
			Positions[VID] += FVector3f(Offset);
		}
		bSuccess = true;
	}

	if (bSuccess)
	{
		Mesh->CommitMeshDescription(0);
		Mesh->Build(false);
		Mesh->MarkPackageDirty();
	}

	return bSuccess;
}

bool UArborMeshTools::ApplyVertexScale(UStaticMesh* Mesh, float Scale)
{
	if (!Mesh) return false;

	const int32 NumLODs = Mesh->GetNumLODs();
	bool bSuccess = false;

	for (int32 LOD = 0; LOD < NumLODs; LOD++)
	{
		FMeshDescription* MD = Mesh->GetMeshDescription(LOD);
		if (!MD) continue;

		FStaticMeshAttributes Attributes(*MD);
		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();

		for (FVertexID VID : MD->Vertices().GetElementIDs())
		{
			Positions[VID] *= Scale;
		}
		bSuccess = true;
	}

	if (bSuccess)
	{
		Mesh->CommitMeshDescription(0);
		Mesh->Build(false);
		Mesh->MarkPackageDirty();
	}

	return bSuccess;
}

// ============================================================================
// Public API
// ============================================================================

FString UArborMeshTools::FixMeshPivot(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> Params;
	auto Reader = TJsonReaderFactory<>::Create(ParamsJson);
	if (!FJsonSerializer::Deserialize(Reader, Params) || !Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const FString PivotMode = Params->HasField(TEXT("pivot"))
		? Params->GetStringField(TEXT("pivot")) : TEXT("bottom");

	UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Mesh)
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"Cannot load mesh: %s\"}"), *AssetPath);
	}

	const FBox BoundsBefore = Mesh->GetBoundingBox();

	// Calculate offset based on pivot mode
	FVector Offset = FVector::ZeroVector;
	if (PivotMode == TEXT("bottom"))
	{
		// Move pivot to bottom center — shift vertices up by -Min.Z
		Offset.Z = -BoundsBefore.Min.Z;
	}
	else if (PivotMode == TEXT("center"))
	{
		FVector Center = BoundsBefore.GetCenter();
		Offset = -Center;
	}
	else if (PivotMode == TEXT("top"))
	{
		Offset.Z = -BoundsBefore.Max.Z;
	}

	if (!ApplyVertexOffset(Mesh, Offset))
	{
		return TEXT("{\"success\":false,\"error\":\"Failed to modify vertex data\"}");
	}

	UEditorAssetLibrary::SaveLoadedAsset(Mesh);

	const FBox BoundsAfter = Mesh->GetBoundingBox();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetObjectField(TEXT("offset"), VecToJson(Offset));
	Result->SetObjectField(TEXT("bounds_before"), BoxToJson(BoundsBefore));
	Result->SetObjectField(TEXT("bounds_after"), BoxToJson(BoundsAfter));
	return SerializeJson(Result);
}

FString UArborMeshTools::FixMeshScale(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> Params;
	auto Reader = TJsonReaderFactory<>::Create(ParamsJson);
	if (!FJsonSerializer::Deserialize(Reader, Params) || !Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const float Scale = Params->HasField(TEXT("scale"))
		? Params->GetNumberField(TEXT("scale")) : 100.0f;

	UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Mesh)
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"Cannot load mesh: %s\"}"), *AssetPath);
	}

	const FBox BoundsBefore = Mesh->GetBoundingBox();

	// Try build_scale3d first (non-destructive)
	// UE 5.7: BuildScale3D moved from UStaticMesh to FStaticMeshSourceModel::BuildSettings
	if (Mesh->GetNumSourceModels() == 0)
	{
		return TEXT("{\"success\":false,\"error\":\"Mesh has no source models\"}");
	}
	FVector OriginalBuildScale = Mesh->GetSourceModel(0).BuildSettings.BuildScale3D;
	Mesh->GetSourceModel(0).BuildSettings.BuildScale3D = OriginalBuildScale * Scale;
	Mesh->Build(false);

	FBox BoundsAfter = Mesh->GetBoundingBox();
	FVector SizeBefore = BoundsBefore.GetSize();
	FVector SizeAfter = BoundsAfter.GetSize();

	FString Approach = TEXT("build_scale3d");

	// Check if build_scale3d actually worked
	float SizeRatio = (SizeBefore.Size() > 0.01f) ? SizeAfter.Size() / SizeBefore.Size() : 0.0f;
	if (FMath::Abs(SizeRatio - Scale) > Scale * 0.1f)
	{
		// build_scale3d didn't work — revert and use vertex scaling
		Mesh->GetSourceModel(0).BuildSettings.BuildScale3D = OriginalBuildScale;

		if (!ApplyVertexScale(Mesh, Scale))
		{
			return TEXT("{\"success\":false,\"error\":\"Failed to scale vertex data\"}");
		}

		BoundsAfter = Mesh->GetBoundingBox();
		Approach = TEXT("vertex_scale");
	}

	UEditorAssetLibrary::SaveLoadedAsset(Mesh);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("scale"), Scale);
	Result->SetStringField(TEXT("approach"), Approach);
	Result->SetObjectField(TEXT("bounds_before"), BoxToJson(BoundsBefore));
	Result->SetObjectField(TEXT("bounds_after"), BoxToJson(BoundsAfter));
	return SerializeJson(Result);
}

FString UArborMeshTools::SetupCollision(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> Params;
	auto Reader = TJsonReaderFactory<>::Create(ParamsJson);
	if (!FJsonSerializer::Deserialize(Reader, Params) || !Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	const FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	const FString Mode = Params->HasField(TEXT("mode"))
		? Params->GetStringField(TEXT("mode")) : TEXT("complex_simple");

	UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Mesh)
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"Cannot load mesh: %s\"}"), *AssetPath);
	}

	UBodySetup* BodySetup = Mesh->GetBodySetup();
	if (!BodySetup)
	{
		Mesh->CreateBodySetup();
		BodySetup = Mesh->GetBodySetup();
	}

	if (!BodySetup)
	{
		return TEXT("{\"success\":false,\"error\":\"Cannot create BodySetup\"}");
	}

	if (Mode == TEXT("complex_simple"))
	{
		BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
	}
	else if (Mode == TEXT("complex_only"))
	{
		BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
	}
	else if (Mode == TEXT("box"))
	{
		BodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		// Add a box collision based on mesh bounds
		FKBoxElem BoxElem;
		FBox Box = Mesh->GetBoundingBox();
		FVector Size = Box.GetSize();
		BoxElem.X = Size.X;
		BoxElem.Y = Size.Y;
		BoxElem.Z = Size.Z;
		BoxElem.Center = Box.GetCenter();
		BodySetup->AggGeom.BoxElems.Add(BoxElem);
	}
	else if (Mode == TEXT("sphere"))
	{
		BodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		FKSphereElem SphereElem;
		FBox Box = Mesh->GetBoundingBox();
		SphereElem.Radius = Box.GetExtent().GetMax();
		SphereElem.Center = Box.GetCenter();
		BodySetup->AggGeom.SphereElems.Add(SphereElem);
	}
	else if (Mode == TEXT("convex"))
	{
		BodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		BodySetup->bMeshCollideAll = true;
		// Convex decomposition needs full editor mesh processing
		Mesh->Build(false);
	}

	BodySetup->InvalidatePhysicsData();
	BodySetup->CreatePhysicsMeshes();
	Mesh->MarkPackageDirty();
	UEditorAssetLibrary::SaveLoadedAsset(Mesh);

	UE_LOG(LogTemp, Log, TEXT("[ArborMeshTools] SetupCollision: %s → %s"), *AssetPath, *Mode);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("mode"), Mode);
	return SerializeJson(Result);
}

FString UArborMeshTools::GetMeshBounds(const FString& AssetPath)
{
	UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Mesh)
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"Cannot load mesh: %s\"}"), *AssetPath);
	}

	const FBox Box = Mesh->GetBoundingBox();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetObjectField(TEXT("min"), VecToJson(Box.Min));
	Result->SetObjectField(TEXT("max"), VecToJson(Box.Max));
	Result->SetObjectField(TEXT("center"), VecToJson(Box.GetCenter()));
	Result->SetObjectField(TEXT("extent"), VecToJson(Box.GetExtent()));
	return SerializeJson(Result);
}
