#include "ArborActorTools.h"
#include "ArborPropertyJson.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "EditorSubsystem.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "CollisionQueryParams.h"
#include "ILiveCodingModule.h"
#include "Components/StaticMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"

// ============================================================================
// Private helpers
// ============================================================================

AActor* UArborActorTools::FindActorByLabel(const FString& Label)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return nullptr;

	const FString Target = Label.ToLower();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel().ToLower() == Target)
		{
			return *It;
		}
	}
	return nullptr;
}

TArray<AActor*> UArborActorTools::GetAllLevelActors()
{
	TArray<AActor*> Result;
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return Result;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		Result.Add(*It);
	}
	return Result;
}

bool UArborActorTools::LineTraceGroundZ(UWorld* World, AActor* Actor,
	const TArray<AActor*>& IgnoreActors, float& OutGroundZ)
{
	if (!World || !Actor) return false;

	const FVector Loc = Actor->GetActorLocation();
	const FVector Start(Loc.X, Loc.Y, Loc.Z + 10000.0f);
	const FVector End(Loc.X, Loc.Y, Loc.Z - 50000.0f);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ArborSnap), true);
	for (AActor* Ignored : IgnoreActors)
	{
		if (Ignored)
		{
			Params.AddIgnoredActor(Ignored);
		}
	}

	FHitResult HitResult;
	if (World->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility, Params))
	{
		OutGroundZ = HitResult.ImpactPoint.Z;
		return true;
	}

	return false;
}

TSharedPtr<FJsonObject> UArborActorTools::SnapActorInternal(UWorld* World, AActor* Actor,
	float Offset, bool PreserveRotation, const TArray<AActor*>& IgnoreActors)
{
	if (!Actor) return nullptr;

	const FString Label = Actor->GetActorLabel();
	const FVector Loc = Actor->GetActorLocation();
	const FRotator OriginalRot = Actor->GetActorRotation();
	const float OldZ = Loc.Z;

	float GroundZ;
	if (!LineTraceGroundZ(World, Actor, IgnoreActors, GroundZ))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ArborActorTools] SnapToGround: no ground hit for '%s'"), *Label);
		return nullptr;
	}

	// Account for bounding box bottom.
	FVector Origin, Extent;
	Actor->GetActorBounds(false, Origin, Extent);
	const float BottomOffset = (Origin.Z - Extent.Z) - Loc.Z;

	const float NewZ = GroundZ - BottomOffset + Offset;
	Actor->SetActorLocation(FVector(Loc.X, Loc.Y, NewZ));

	if (PreserveRotation)
	{
		Actor->SetActorRotation(OriginalRot);
	}

	UE_LOG(LogTemp, Log, TEXT("[ArborActorTools] SnapToGround: '%s' Z %.1f -> %.1f (ground=%.1f)"),
		*Label, OldZ, NewZ, GroundZ);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("name"), Label);
	Result->SetNumberField(TEXT("old_z"), FMath::RoundToFloat(OldZ * 100.0f) / 100.0f);
	Result->SetNumberField(TEXT("new_z"), FMath::RoundToFloat(NewZ * 100.0f) / 100.0f);
	Result->SetNumberField(TEXT("ground_z"), FMath::RoundToFloat(GroundZ * 100.0f) / 100.0f);
	return Result;
}

// ============================================================================
// Public API
// ============================================================================

FString UArborActorTools::SnapToGround(const FString& ActorLabel, float Offset,
	bool PreserveRotation, const FString& IgnoreLabels)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return TEXT("{\"success\":false,\"error\":\"No editor world\"}");
	}

	AActor* Actor = FindActorByLabel(ActorLabel);
	if (!Actor)
	{
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Actor '%s' not found\"}"), *ActorLabel);
	}

	// Build ignore list.
	TArray<AActor*> IgnoreActors;
	IgnoreActors.Add(Actor);
	if (!IgnoreLabels.IsEmpty())
	{
		TArray<FString> Labels;
		IgnoreLabels.ParseIntoArray(Labels, TEXT(","), true);
		for (const FString& L : Labels)
		{
			FString Trimmed = L.TrimStartAndEnd();
			AActor* IgnoredActor = FindActorByLabel(Trimmed);
			if (IgnoredActor)
			{
				IgnoreActors.AddUnique(IgnoredActor);
			}
		}
	}

	TSharedPtr<FJsonObject> Result = SnapActorInternal(World, Actor, Offset, PreserveRotation, IgnoreActors);
	if (!Result.IsValid())
	{
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"No ground hit for '%s'\"}"), *ActorLabel);
	}

	FString Output;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	return Output;
}

FString UArborActorTools::SnapAllToGround(const FString& FilterLabels, float Offset)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return TEXT("{\"snapped\":[],\"failed\":[\"No editor world\"]}");
	}

	// Parse filter labels.
	TArray<FString> Filters;
	if (!FilterLabels.IsEmpty())
	{
		FilterLabels.ParseIntoArray(Filters, TEXT(","), true);
		for (FString& F : Filters)
		{
			F.TrimStartAndEndInline();
		}
	}

	// Collect target actors.
	TArray<AActor*> Targets;
	TArray<AActor*> AllActors = GetAllLevelActors();
	for (AActor* Actor : AllActors)
	{
		const FString Label = Actor->GetActorLabel();
		if (Filters.Num() > 0)
		{
			bool bMatch = false;
			for (const FString& F : Filters)
			{
				if (Label.Contains(F))
				{
					bMatch = true;
					break;
				}
			}
			if (!bMatch) continue;
		}
		Targets.Add(Actor);
	}

	// Snap each, ignoring all other targets.
	TArray<TSharedPtr<FJsonValue>> SnappedArray;
	TArray<TSharedPtr<FJsonValue>> FailedArray;

	for (AActor* Actor : Targets)
	{
		TSharedPtr<FJsonObject> Result = SnapActorInternal(World, Actor, Offset, true, Targets);
		if (Result.IsValid())
		{
			SnappedArray.Add(MakeShared<FJsonValueObject>(Result));
		}
		else
		{
			FailedArray.Add(MakeShared<FJsonValueString>(Actor->GetActorLabel()));
		}
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("snapped"), SnappedArray);
	Root->SetArrayField(TEXT("failed"), FailedArray);

	FString Output;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}

FString UArborActorTools::SnapSelectedToGround(float Offset)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return TEXT("{\"snapped\":[],\"failed\":[\"No editor world\"]}");
	}

	// Get selected actors.
	TArray<AActor*> Selected;
	UEditorActorSubsystem* Sub = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	if (Sub)
	{
		Selected = Sub->GetSelectedLevelActors();
	}

	TArray<TSharedPtr<FJsonValue>> SnappedArray;
	TArray<TSharedPtr<FJsonValue>> FailedArray;

	for (AActor* Actor : Selected)
	{
		TSharedPtr<FJsonObject> Result = SnapActorInternal(World, Actor, Offset, true, Selected);
		if (Result.IsValid())
		{
			SnappedArray.Add(MakeShared<FJsonValueObject>(Result));
		}
		else
		{
			FailedArray.Add(MakeShared<FJsonValueString>(Actor->GetActorLabel()));
		}
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("snapped"), SnappedArray);
	Root->SetArrayField(TEXT("failed"), FailedArray);

	FString Output;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}

FString UArborActorTools::SampleTerrainHeight(float X, float Y)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return TEXT("{\"success\":false,\"error\":\"No editor world\"}");
	}

	const FVector Start(X, Y, 100000.0f);
	const FVector End(X, Y, -100000.0f);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ArborSampleHeight), true);

	FHitResult HitResult;
	if (World->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility, Params))
	{
		return FString::Printf(TEXT("{\"success\":true,\"z\":%.2f}"), HitResult.ImpactPoint.Z);
	}

	return TEXT("{\"success\":false,\"error\":\"No terrain hit\"}");
}

// ============================================================================
// FindActorByAnyIdentifier
// ============================================================================

AActor* UArborActorTools::FindActorByAnyIdentifier(const FString& Identifier)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return nullptr;

	// 1. Exact path match
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetPathName() == Identifier)
		{
			return *It;
		}
	}

	// 2. Exact object name match
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetName() == Identifier)
		{
			return *It;
		}
	}

	// 3. Case-insensitive label match
	const FString Target = Identifier.ToLower();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel().ToLower() == Target)
		{
			return *It;
		}
	}

	return nullptr;
}

// ============================================================================
// Actor Operations
// ============================================================================

static FString SerializeJsonCompact(TSharedPtr<FJsonObject> Root)
{
	FString Output;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}

FString UArborActorTools::DeleteActors(const FString& ActorNamesJson)
{
	// Parse JSON array of names
	TArray<TSharedPtr<FJsonValue>> NamesArray;
	auto Reader = TJsonReaderFactory<>::Create(ActorNamesJson);
	if (!FJsonSerializer::Deserialize(Reader, NamesArray))
	{
		return TEXT("{\"deleted\":[],\"not_found\":[\"Invalid JSON array\"]}");
	}

	TArray<TSharedPtr<FJsonValue>> DeletedArray;
	TArray<TSharedPtr<FJsonValue>> NotFoundArray;

	for (const auto& Val : NamesArray)
	{
		FString Name = Val->AsString();
		AActor* Actor = FindActorByAnyIdentifier(Name);
		if (Actor)
		{
			FString DisplayName = Actor->GetName();
			Actor->Destroy();
			DeletedArray.Add(MakeShared<FJsonValueString>(DisplayName));
		}
		else
		{
			NotFoundArray.Add(MakeShared<FJsonValueString>(Name));
		}
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("deleted"), DeletedArray);
	Root->SetArrayField(TEXT("not_found"), NotFoundArray);
	return SerializeJsonCompact(Root);
}

FString UArborActorTools::GetSceneInfo(const FString& FilterClass, const FString& FilterPrefix)
{
	TArray<AActor*> AllActors = GetAllLevelActors();
	TArray<TSharedPtr<FJsonValue>> ActorsArray;

	for (AActor* Actor : AllActors)
	{
		const FString ClassName = Actor->GetClass()->GetName();

		// Class filter
		if (!FilterClass.IsEmpty() && ClassName != FilterClass)
		{
			continue;
		}

		// Name: prefer label, fallback to object name
		FString Name = Actor->GetActorLabel();
		if (Name.IsEmpty())
		{
			Name = Actor->GetName();
		}

		// Prefix filter
		if (!FilterPrefix.IsEmpty() && !Name.StartsWith(FilterPrefix))
		{
			continue;
		}

		const FVector Loc = Actor->GetActorLocation();
		const FRotator Rot = Actor->GetActorRotation();
		const FVector Sc = Actor->GetActorScale3D();

		TSharedPtr<FJsonObject> PosObj = MakeShared<FJsonObject>();
		PosObj->SetNumberField(TEXT("x"), Loc.X);
		PosObj->SetNumberField(TEXT("y"), Loc.Y);
		PosObj->SetNumberField(TEXT("z"), Loc.Z);

		TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
		RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch);
		RotObj->SetNumberField(TEXT("yaw"), Rot.Yaw);
		RotObj->SetNumberField(TEXT("roll"), Rot.Roll);

		TSharedPtr<FJsonObject> ScObj = MakeShared<FJsonObject>();
		ScObj->SetNumberField(TEXT("x"), Sc.X);
		ScObj->SetNumberField(TEXT("y"), Sc.Y);
		ScObj->SetNumberField(TEXT("z"), Sc.Z);

		TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
		ActorObj->SetStringField(TEXT("name"), Name);
		ActorObj->SetStringField(TEXT("class"), ClassName);
		ActorObj->SetObjectField(TEXT("position"), PosObj);
		ActorObj->SetObjectField(TEXT("rotation"), RotObj);
		ActorObj->SetObjectField(TEXT("scale"), ScObj);

		ActorsArray.Add(MakeShared<FJsonValueObject>(ActorObj));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("actors"), ActorsArray);
	return SerializeJsonCompact(Root);
}

FString UArborActorTools::ListAllActors()
{
	TArray<AActor*> AllActors = GetAllLevelActors();
	TArray<TSharedPtr<FJsonValue>> ActorsArray;

	for (AActor* Actor : AllActors)
	{
		FString Name = Actor->GetActorLabel();
		if (Name.IsEmpty())
		{
			Name = Actor->GetName();
		}

		TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
		ActorObj->SetStringField(TEXT("name"), Name);
		ActorObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		ActorObj->SetStringField(TEXT("path"), Actor->GetPathName());

		ActorsArray.Add(MakeShared<FJsonValueObject>(ActorObj));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("actors"), ActorsArray);
	return SerializeJsonCompact(Root);
}

FString UArborActorTools::ModifyActor(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> Params;
	auto Reader = TJsonReaderFactory<>::Create(ParamsJson);
	if (!FJsonSerializer::Deserialize(Reader, Params) || !Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	const FString ActorName = Params->GetStringField(TEXT("actor_name"));
	AActor* Actor = FindActorByAnyIdentifier(ActorName);
	if (!Actor)
	{
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Actor not found: %s\",\"actor_path\":\"\",\"changes_applied\":[]}"), *ActorName);
	}

	TArray<TSharedPtr<FJsonValue>> ChangesArray;

	// Position
	const TSharedPtr<FJsonObject>* PosObj;
	if (Params->TryGetObjectField(TEXT("position"), PosObj))
	{
		const double X = (*PosObj)->GetNumberField(TEXT("x"));
		const double Y = (*PosObj)->GetNumberField(TEXT("y"));
		const double Z = (*PosObj)->GetNumberField(TEXT("z"));
		Actor->SetActorLocation(FVector(X, Y, Z));
		ChangesArray.Add(MakeShared<FJsonValueString>(TEXT("position")));
	}

	// Rotation
	const TSharedPtr<FJsonObject>* RotObj;
	if (Params->TryGetObjectField(TEXT("rotation"), RotObj))
	{
		const double Pitch = (*RotObj)->GetNumberField(TEXT("pitch"));
		const double Yaw = (*RotObj)->GetNumberField(TEXT("yaw"));
		const double Roll = (*RotObj)->GetNumberField(TEXT("roll"));
		Actor->SetActorRotation(FRotator(Pitch, Yaw, Roll));
		ChangesArray.Add(MakeShared<FJsonValueString>(TEXT("rotation")));
	}

	// Scale
	const TSharedPtr<FJsonObject>* ScObj;
	if (Params->TryGetObjectField(TEXT("scale"), ScObj))
	{
		const double X = (*ScObj)->GetNumberField(TEXT("x"));
		const double Y = (*ScObj)->GetNumberField(TEXT("y"));
		const double Z = (*ScObj)->GetNumberField(TEXT("z"));
		Actor->SetActorScale3D(FVector(X, Y, Z));
		ChangesArray.Add(MakeShared<FJsonValueString>(TEXT("scale")));
	}

	// Visibility
	bool bVisible;
	if (Params->TryGetBoolField(TEXT("visible"), bVisible))
	{
		Actor->SetIsTemporarilyHiddenInEditor(!bVisible);
		Actor->SetHidden(!bVisible);
		ChangesArray.Add(MakeShared<FJsonValueString>(TEXT("visible")));
	}

	// Label
	FString NewLabel;
	if (Params->TryGetStringField(TEXT("label"), NewLabel))
	{
		Actor->SetActorLabel(NewLabel);
		ChangesArray.Add(MakeShared<FJsonValueString>(TEXT("label")));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("actor_path"), Actor->GetPathName());
	Root->SetArrayField(TEXT("changes_applied"), ChangesArray);
	return SerializeJsonCompact(Root);
}

// ============================================================================
// Ground Tracing (public API for Python)
// ============================================================================

static bool TraceGroundAtXY(UWorld* World, float X, float Y,
	float StartZ, float TraceDistance,
	const TArray<AActor*>& IgnoreActors,
	FVector& OutImpactPoint)
{
	if (!World) return false;

	const FVector Start(X, Y, StartZ);
	const FVector End(X, Y, StartZ - TraceDistance);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ArborTraceGround), true);
	for (AActor* Ignored : IgnoreActors)
	{
		if (Ignored) Params.AddIgnoredActor(Ignored);
	}

	FHitResult HitResult;
	if (World->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility, Params))
	{
		OutImpactPoint = HitResult.ImpactPoint;
		return true;
	}
	return false;
}

static TArray<AActor*> ParseIgnoreActors(const TArray<TSharedPtr<FJsonValue>>* IgnoreArray)
{
	TArray<AActor*> Result;
	if (IgnoreArray)
	{
		for (const auto& Val : *IgnoreArray)
		{
			FString Label = Val->AsString();
			AActor* A = UArborActorTools::FindActorByAnyIdentifier(Label);
			if (A) Result.Add(A);
		}
	}
	return Result;
}

// ============================================================================
// SetActorProperty (reflection-based UPROPERTY edit)
// ============================================================================

FString UArborActorTools::SetActorProperty(const FString& ActorName, const FString& PropertyName, const FString& ValueJson)
{
	using namespace Arbor::Json;

	AActor* Actor = UArborActorTools::FindActorByAnyIdentifier(ActorName);
	if (!Actor)
	{
		return JsonError(FString::Printf(TEXT("Actor '%s' not found in current level"), *ActorName));
	}

	TSharedPtr<FJsonValue> Parsed;
	if (!ParseJsonValue(ValueJson, Parsed))
	{
		return JsonError(FString::Printf(TEXT("Could not parse JSON value: %s"), *ValueJson));
	}

	Actor->Modify();
	FString Error;
	if (!ApplyJsonToProperty(Actor, PropertyName, Parsed, Error))
	{
		return JsonError(Error);
	}
	Actor->PostEditChange();

	if (UPackage* Package = Actor->GetOutermost())
	{
		Package->MarkPackageDirty();
	}

	const TSharedRef<FJsonObject> Extra = MakeShared<FJsonObject>();
	Extra->SetStringField(TEXT("actor_path"), Actor->GetPathName());
	Extra->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
	Extra->SetStringField(TEXT("property_name"), PropertyName);
	return MakeJsonResult(true, FString(), Extra);
}

FString UArborActorTools::TraceGroundZ(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> Params;
	auto Reader = TJsonReaderFactory<>::Create(ParamsJson);
	if (!FJsonSerializer::Deserialize(Reader, Params) || !Params.IsValid())
	{
		return TEXT("{\"hit\":false,\"error\":\"Invalid JSON\"}");
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return TEXT("{\"hit\":false,\"error\":\"No editor world\"}");
	}

	const float X = Params->GetNumberField(TEXT("x"));
	const float Y = Params->GetNumberField(TEXT("y"));
	const float StartZ = Params->HasField(TEXT("start_z"))
		? Params->GetNumberField(TEXT("start_z")) : 50000.0f;
	const float TraceDistance = Params->HasField(TEXT("trace_distance"))
		? Params->GetNumberField(TEXT("trace_distance")) : 100000.0f;

	const TArray<TSharedPtr<FJsonValue>>* IgnoreArray = nullptr;
	Params->TryGetArrayField(TEXT("ignore_actors"), IgnoreArray);
	TArray<AActor*> IgnoreActors = ParseIgnoreActors(IgnoreArray);

	FVector ImpactPoint;
	if (TraceGroundAtXY(World, X, Y, StartZ, TraceDistance, IgnoreActors, ImpactPoint))
	{
		return FString::Printf(
			TEXT("{\"hit\":true,\"z\":%.2f,\"impact_point\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f}}"),
			ImpactPoint.Z, ImpactPoint.X, ImpactPoint.Y, ImpactPoint.Z);
	}

	return TEXT("{\"hit\":false,\"z\":0}");
}

FString UArborActorTools::BatchTraceGround(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> Params;
	auto Reader = TJsonReaderFactory<>::Create(ParamsJson);
	if (!FJsonSerializer::Deserialize(Reader, Params) || !Params.IsValid())
	{
		return TEXT("{\"results\":[],\"error\":\"Invalid JSON\"}");
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return TEXT("{\"results\":[],\"error\":\"No editor world\"}");
	}

	const TArray<TSharedPtr<FJsonValue>>* PointsArray;
	if (!Params->TryGetArrayField(TEXT("points"), PointsArray))
	{
		return TEXT("{\"results\":[],\"error\":\"Missing 'points' array\"}");
	}

	const float StartZ = Params->HasField(TEXT("start_z"))
		? Params->GetNumberField(TEXT("start_z")) : 50000.0f;
	const float TraceDistance = Params->HasField(TEXT("trace_distance"))
		? Params->GetNumberField(TEXT("trace_distance")) : 100000.0f;

	const TArray<TSharedPtr<FJsonValue>>* IgnoreArray = nullptr;
	Params->TryGetArrayField(TEXT("ignore_actors"), IgnoreArray);
	TArray<AActor*> IgnoreActors = ParseIgnoreActors(IgnoreArray);

	// Pre-allocate result string for efficiency.
	FString Output = TEXT("{\"results\":[");
	bool bFirst = true;

	for (const auto& PointVal : *PointsArray)
	{
		const TArray<TSharedPtr<FJsonValue>>* Coord;
		if (!PointVal->TryGetArray(Coord) || Coord->Num() < 2)
		{
			if (!bFirst) Output += TEXT(",");
			Output += TEXT("{\"hit\":false,\"z\":0}");
			bFirst = false;
			continue;
		}

		const float X = (*Coord)[0]->AsNumber();
		const float Y = (*Coord)[1]->AsNumber();

		FVector ImpactPoint;
		if (!bFirst) Output += TEXT(",");
		bFirst = false;

		if (TraceGroundAtXY(World, X, Y, StartZ, TraceDistance, IgnoreActors, ImpactPoint))
		{
			Output += FString::Printf(TEXT("{\"hit\":true,\"z\":%.2f}"), ImpactPoint.Z);
		}
		else
		{
			Output += TEXT("{\"hit\":false,\"z\":0}");
		}
	}

	Output += TEXT("]}");
	return Output;
}

// ============================================================================
// InspectActor — property introspection
// ============================================================================

static FString PropertyValueToString(FProperty* Prop, const void* Container)
{
	FString Value;
	Prop->ExportTextItem_Direct(Value, Prop->ContainerPtrToValuePtr<void>(Container), nullptr, nullptr, PPF_None);
	// Truncate very long values (e.g. large arrays)
	if (Value.Len() > 512)
	{
		Value = Value.Left(509) + TEXT("...");
	}
	return Value;
}

static void CollectProperties(UObject* Object, const FString& Filter,
	TArray<TSharedPtr<FJsonValue>>& OutArray)
{
	if (!Object) return;

	for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;
		const FString Name = Prop->GetName();

		// Skip private/internal properties
		if (Prop->HasAnyPropertyFlags(CPF_Deprecated | CPF_Transient))
		{
			continue;
		}

		// Apply name filter
		if (!Filter.IsEmpty() && !Name.Contains(Filter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		const FString TypeName = Prop->GetCPPType();
		const FString Category = Prop->HasMetaData(TEXT("Category"))
			? Prop->GetMetaData(TEXT("Category")) : TEXT("");
		const FString Value = PropertyValueToString(Prop, Object);

		TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
		PropObj->SetStringField(TEXT("name"), Name);
		PropObj->SetStringField(TEXT("type"), TypeName);
		PropObj->SetStringField(TEXT("value"), Value);
		if (!Category.IsEmpty())
		{
			PropObj->SetStringField(TEXT("category"), Category);
		}

		OutArray.Add(MakeShared<FJsonValueObject>(PropObj));
	}
}

FString UArborActorTools::InspectActor(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> Params;
	auto Reader = TJsonReaderFactory<>::Create(ParamsJson);
	if (!FJsonSerializer::Deserialize(Reader, Params) || !Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	const FString ActorName = Params->GetStringField(TEXT("actor_name"));
	if (ActorName.IsEmpty())
	{
		return TEXT("{\"success\":false,\"error\":\"actor_name is required\"}");
	}

	AActor* Actor = FindActorByAnyIdentifier(ActorName);
	if (!Actor)
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"Actor not found: %s\"}"), *ActorName);
	}

	FString PropertyFilter;
	Params->TryGetStringField(TEXT("property_filter"), PropertyFilter);

	FString ComponentFilter;
	Params->TryGetStringField(TEXT("component_filter"), ComponentFilter);

	// Collect actor-level properties
	TArray<TSharedPtr<FJsonValue>> ActorProps;
	CollectProperties(Actor, PropertyFilter, ActorProps);

	// Collect component properties
	TArray<TSharedPtr<FJsonValue>> ComponentsArray;
	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);

	for (UActorComponent* Comp : Components)
	{
		if (!Comp) continue;

		const FString CompName = Comp->GetName();
		const FString CompClass = Comp->GetClass()->GetName();

		// Apply component filter
		if (!ComponentFilter.IsEmpty()
			&& !CompName.Contains(ComponentFilter, ESearchCase::IgnoreCase)
			&& !CompClass.Contains(ComponentFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		TArray<TSharedPtr<FJsonValue>> CompProps;
		CollectProperties(Comp, PropertyFilter, CompProps);

		TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
		CompObj->SetStringField(TEXT("name"), CompName);
		CompObj->SetStringField(TEXT("class"), CompClass);
		CompObj->SetArrayField(TEXT("properties"), CompProps);

		ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("actor_name"), Actor->GetActorLabel().IsEmpty()
		? Actor->GetName() : Actor->GetActorLabel());
	Root->SetStringField(TEXT("actor_class"), Actor->GetClass()->GetName());
	Root->SetArrayField(TEXT("properties"), ActorProps);
	Root->SetArrayField(TEXT("components"), ComponentsArray);

	return SerializeJsonCompact(Root);
}

FString UArborActorTools::LiveCompile()
{
	ILiveCodingModule* LC = FModuleManager::GetModulePtr<ILiveCodingModule>("LiveCoding");
	if (!LC)
	{
		return TEXT("{\"success\":false,\"error\":\"Live Coding module not loaded\"}");
	}
	if (!LC->IsEnabledForSession())
	{
		return TEXT("{\"success\":false,\"error\":\"Live Coding not enabled for this session\"}");
	}
	if (LC->IsCompiling())
	{
		return TEXT("{\"success\":true,\"message\":\"Compile already in progress\"}");
	}
	LC->Compile();
	return TEXT("{\"success\":true,\"message\":\"Live Coding compile triggered\"}");
}

// ============================================================================
// SetMeshMaterial — override a material slot on an actor's mesh component
// ============================================================================

FString UArborActorTools::SetMeshMaterial(const FString& ActorName, int32 Slot, const FString& MaterialPath)
{
	if (Slot < 0)
	{
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Slot must be >= 0 (got %d)\"}"), Slot);
	}

	AActor* Actor = UArborActorTools::FindActorByAnyIdentifier(ActorName);
	if (!Actor)
	{
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Actor '%s' not found\"}"), *ActorName);
	}

	UMaterialInterface* Material = Cast<UMaterialInterface>(StaticLoadObject(
		UMaterialInterface::StaticClass(), nullptr, *MaterialPath));
	if (!Material && !MaterialPath.IsEmpty() && !MaterialPath.Contains(TEXT(".")))
	{
		// Retry with full object path (e.g. /Game/Materials/M_Foo → /Game/Materials/M_Foo.M_Foo)
		const FString Leaf = FPaths::GetBaseFilename(MaterialPath);
		Material = Cast<UMaterialInterface>(StaticLoadObject(
			UMaterialInterface::StaticClass(), nullptr, *(MaterialPath + TEXT(".") + Leaf)));
	}
	if (!Material)
	{
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Could not load material '%s'\"}"), *MaterialPath);
	}

	// Prefer StaticMeshComponent (the common case for placed mesh actors);
	// fall back to any UMeshComponent (skeletal, instanced, etc.).
	UMeshComponent* MeshComp = Actor->FindComponentByClass<UStaticMeshComponent>();
	if (!MeshComp)
	{
		MeshComp = Actor->FindComponentByClass<UMeshComponent>();
	}
	if (!MeshComp)
	{
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Actor '%s' has no MeshComponent\"}"), *ActorName);
	}

	if (Slot >= MeshComp->GetNumMaterials())
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"Slot %d out of range (component has %d slots)\"}"),
			Slot, MeshComp->GetNumMaterials());
	}

	Actor->Modify();
	MeshComp->Modify();
	MeshComp->SetMaterial(Slot, Material);
	Actor->PostEditChange();

	if (UPackage* Package = Actor->GetOutermost())
	{
		Package->MarkPackageDirty();
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("actor_path"), Actor->GetPathName());
	Root->SetStringField(TEXT("component"), MeshComp->GetName());
	Root->SetNumberField(TEXT("slot"), Slot);
	Root->SetStringField(TEXT("material_path"), Material->GetPathName());

	FString Output;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Output;
}
