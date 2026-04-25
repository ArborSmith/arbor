#include "ArborPlaytestTools.h"
#include "Engine/World.h"
#include "Editor.h"
#include "LevelEditorSubsystem.h"
#include "EditorLevelLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

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

static TSharedPtr<FJsonObject> RotToJson(const FRotator& R)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("pitch"), R.Pitch);
	Obj->SetNumberField(TEXT("yaw"), R.Yaw);
	Obj->SetNumberField(TEXT("roll"), R.Roll);
	return Obj;
}

static FVector ParseJsonVector(const TArray<TSharedPtr<FJsonValue>>& Arr)
{
	if (Arr.Num() >= 3)
	{
		return FVector(Arr[0]->AsNumber(), Arr[1]->AsNumber(), Arr[2]->AsNumber());
	}
	return FVector::ZeroVector;
}

// ============================================================================
// Private: PIE World/Player Access
// ============================================================================

bool UArborPlaytestTools::IsPIEActive()
{
	if (!GEditor) return false;

	// Check via LevelEditorSubsystem
	ULevelEditorSubsystem* LESub = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	if (LESub)
	{
		// Check if PIE world exists
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE && Context.World())
			{
				return true;
			}
		}
	}

	// Fallback: check if a game world has a player pawn
	APawn* Pawn = nullptr;
	APlayerController* PC = nullptr;
	return GetPIEPlayerPawn(Pawn, PC);
}

bool UArborPlaytestTools::GetPIEPlayerPawn(APawn*& OutPawn, APlayerController*& OutController)
{
	OutPawn = nullptr;
	OutController = nullptr;

	if (!GEngine) return false;

	// Search PIE world contexts first
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE && Context.World())
		{
			OutController = UGameplayStatics::GetPlayerController(Context.World(), 0);
			if (OutController)
			{
				OutPawn = OutController->GetPawn();
				if (OutPawn)
				{
					return true;
				}
			}
		}
	}

	// Fallback: game world
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Game && Context.World())
		{
			OutController = UGameplayStatics::GetPlayerController(Context.World(), 0);
			if (OutController)
			{
				OutPawn = OutController->GetPawn();
				if (OutPawn)
				{
					return true;
				}
			}
		}
	}

	return false;
}

// ============================================================================
// Public API
// ============================================================================

FString UArborPlaytestTools::StartPIE()
{
	if (!GEditor)
	{
		return TEXT("{\"success\":false,\"error\":\"No editor\"}");
	}

	// Already running?
	if (IsPIEActive())
	{
		APawn* Pawn = nullptr;
		APlayerController* PC = nullptr;
		GetPIEPlayerPawn(Pawn, PC);
		FString Mode = Pawn ? TEXT("PIE") : TEXT("SIE");

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("mode"), Mode);
		Result->SetStringField(TEXT("note"), TEXT("PIE already running"));
		return SerializeJson(Result);
	}

	// Request PIE via LevelEditorSubsystem
	ULevelEditorSubsystem* LESub = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	if (LESub)
	{
		FRequestPlaySessionParams Params;
		Params.WorldType = EPlaySessionWorldType::PlayInEditor;
		GEditor->RequestPlaySession(Params);

		UE_LOG(LogTemp, Log, TEXT("[ArborPlaytestTools] PIE started via RequestPlaySession"));

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("mode"), TEXT("PIE"));
		return SerializeJson(Result);
	}

	return TEXT("{\"success\":false,\"error\":\"LevelEditorSubsystem not available\"}");
}

FString UArborPlaytestTools::StopPIE()
{
	if (!GEditor)
	{
		return TEXT("{\"success\":false,\"error\":\"No editor\"}");
	}

	if (GEditor->PlayWorld)
	{
		GEditor->RequestEndPlayMap();
		UE_LOG(LogTemp, Log, TEXT("[ArborPlaytestTools] PIE stopped"));
	}

	return TEXT("{\"success\":true}");
}

FString UArborPlaytestTools::IsPIERunning()
{
	bool bRunning = IsPIEActive();

	APawn* Pawn = nullptr;
	APlayerController* PC = nullptr;
	bool bHasPlayer = GetPIEPlayerPawn(Pawn, PC);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("running"), bRunning);
	Result->SetBoolField(TEXT("has_player"), bHasPlayer);

	if (bHasPlayer && Pawn)
	{
		Result->SetObjectField(TEXT("player_location"), VecToJson(Pawn->GetActorLocation()));
	}

	return SerializeJson(Result);
}

FString UArborPlaytestTools::GetPlayerInfo()
{
	APawn* Pawn = nullptr;
	APlayerController* PC = nullptr;
	if (!GetPIEPlayerPawn(Pawn, PC))
	{
		return TEXT("{\"success\":false,\"error\":\"PIE is not running or no player pawn\"}");
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetObjectField(TEXT("location"), VecToJson(Pawn->GetActorLocation()));
	Result->SetObjectField(TEXT("rotation"), RotToJson(Pawn->GetActorRotation()));
	Result->SetObjectField(TEXT("velocity"), VecToJson(Pawn->GetVelocity()));

	return SerializeJson(Result);
}

FString UArborPlaytestTools::TeleportPlayer(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> Params;
	auto Reader = TJsonReaderFactory<>::Create(ParamsJson);
	if (!FJsonSerializer::Deserialize(Reader, Params) || !Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	APawn* Pawn = nullptr;
	APlayerController* PC = nullptr;
	if (!GetPIEPlayerPawn(Pawn, PC))
	{
		return TEXT("{\"success\":false,\"error\":\"PIE is not running or no player pawn\"}");
	}

	const TArray<TSharedPtr<FJsonValue>>* LocArr;
	if (Params->TryGetArrayField(TEXT("location"), LocArr))
	{
		FVector Loc = ParseJsonVector(*LocArr);
		Pawn->SetActorLocation(Loc, false, nullptr, ETeleportType::TeleportPhysics);
	}

	const TArray<TSharedPtr<FJsonValue>>* RotArr;
	if (Params->TryGetArrayField(TEXT("rotation"), RotArr) && RotArr->Num() >= 3)
	{
		FRotator Rot(
			(*RotArr)[0]->AsNumber(),  // pitch
			(*RotArr)[1]->AsNumber(),  // yaw
			(*RotArr)[2]->AsNumber()   // roll
		);
		if (PC)
		{
			PC->SetControlRotation(Rot);
		}
		else
		{
			Pawn->SetActorRotation(Rot);
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetObjectField(TEXT("location"), VecToJson(Pawn->GetActorLocation()));
	return SerializeJson(Result);
}

FString UArborPlaytestTools::GetFramerate()
{
	float FPS = 0.0f;

	// Get from PIE world delta
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game)
				&& Context.World())
			{
				float DT = Context.World()->GetDeltaSeconds();
				if (DT > 0.0f)
				{
					FPS = 1.0f / DT;
					break;
				}
			}
		}
	}

	// Fallback: use GEngine frame time
	if (FPS <= 0.0f && GEngine)
	{
		double DT = FApp::GetDeltaTime();
		if (DT > 0.0)
		{
			FPS = 1.0 / DT;
		}
	}

	return FString::Printf(TEXT("{\"success\":true,\"fps\":%.1f}"),
		FMath::RoundToFloat(FPS * 10.0f) / 10.0f);
}

FString UArborPlaytestTools::CheckPlayerCanReach(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> Params;
	auto Reader = TJsonReaderFactory<>::Create(ParamsJson);
	if (!FJsonSerializer::Deserialize(Reader, Params) || !Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	const TArray<TSharedPtr<FJsonValue>>* FromArr;
	const TArray<TSharedPtr<FJsonValue>>* ToArr;
	if (!Params->TryGetArrayField(TEXT("from"), FromArr) ||
		!Params->TryGetArrayField(TEXT("to"), ToArr))
	{
		return TEXT("{\"success\":false,\"error\":\"Missing 'from' or 'to' arrays\"}");
	}

	FVector Start = ParseJsonVector(*FromArr);
	FVector End = ParseJsonVector(*ToArr);

	// Find a world with nav system
	UWorld* NavWorld = nullptr;
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.World())
			{
				UNavigationSystemV1* NS = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Context.World());
				if (NS)
				{
					NavWorld = Context.World();
					break;
				}
			}
		}
	}

	if (!NavWorld)
	{
		// Try editor world
		NavWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	if (!NavWorld)
	{
		return TEXT("{\"success\":false,\"error\":\"No world with navigation system\"}");
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(NavWorld);
	if (!NavSys)
	{
		return TEXT("{\"success\":false,\"error\":\"No NavigationSystem found\"}");
	}

	UNavigationPath* NavPath = NavSys->FindPathToLocationSynchronously(NavWorld, Start, End);
	if (!NavPath)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("reachable"), false);
		Result->SetBoolField(TEXT("partial"), false);
		Result->SetNumberField(TEXT("path_length"), 0.0);
		Result->SetArrayField(TEXT("path_points"), TArray<TSharedPtr<FJsonValue>>());
		return SerializeJson(Result);
	}

	bool bValid = NavPath->IsValid();
	bool bPartial = NavPath->IsPartial();

	TArray<TSharedPtr<FJsonValue>> PointsArray;
	float PathLength = 0.0f;
	const TArray<FVector>& PathPoints = NavPath->PathPoints;

	for (int32 i = 0; i < PathPoints.Num(); i++)
	{
		PointsArray.Add(MakeShared<FJsonValueObject>(VecToJson(PathPoints[i])));
		if (i > 0)
		{
			PathLength += FVector::Dist(PathPoints[i], PathPoints[i - 1]);
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("reachable"), bValid && !bPartial);
	Result->SetBoolField(TEXT("partial"), bPartial);
	Result->SetNumberField(TEXT("path_length"), FMath::RoundToFloat(PathLength * 10.0f) / 10.0f);
	Result->SetArrayField(TEXT("path_points"), PointsArray);
	return SerializeJson(Result);
}
