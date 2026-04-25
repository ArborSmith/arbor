#include "ArborCaptureTools.h"
#include "Engine/World.h"
#include "Engine/SceneCapture2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Editor.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#include "LevelEditorSubsystem.h"
#include "ImageWriteBlueprintLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/DateTime.h"

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
		return FVector(Arr[0]->AsNumber(), Arr[1]->AsNumber(), Arr[2]->AsNumber());
	return FVector::ZeroVector;
}

static FRotator ParseJsonRotator(const TArray<TSharedPtr<FJsonValue>>& Arr)
{
	if (Arr.Num() >= 3)
		return FRotator(Arr[0]->AsNumber(), Arr[1]->AsNumber(), Arr[2]->AsNumber());
	return FRotator::ZeroRotator;
}

// ============================================================================
// Viewport Camera (version-safe)
// ============================================================================

bool UArborCaptureTools::GetViewportCameraInfo(FVector& OutLocation, FRotator& OutRotation)
{
	if (!GEditor) return false;

	// Primary: UnrealEditorSubsystem
	UUnrealEditorSubsystem* UESub = GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>();
	if (UESub)
	{
		FVector Loc;
		FRotator Rot;
		if (UESub->GetLevelViewportCameraInfo(Loc, Rot))
		{
			OutLocation = Loc;
			OutRotation = Rot;
			return true;
		}
	}

	// Fallback: iterate viewports directly
	if (GEditor->GetActiveViewport())
	{
		FEditorViewportClient* Client = static_cast<FEditorViewportClient*>(
			GEditor->GetActiveViewport()->GetClient());
		if (Client)
		{
			OutLocation = Client->GetViewLocation();
			OutRotation = Client->GetViewRotation();
			return true;
		}
	}

	return false;
}

bool UArborCaptureTools::SetViewportCameraInfo(const FVector& Location, const FRotator& Rotation)
{
	if (!GEditor) return false;

	// Primary: UnrealEditorSubsystem
	UUnrealEditorSubsystem* UESub = GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>();
	if (UESub)
	{
		UESub->SetLevelViewportCameraInfo(Location, Rotation);
		return true;
	}

	// Fallback: direct viewport client
	if (GEditor->GetActiveViewport())
	{
		FEditorViewportClient* Client = static_cast<FEditorViewportClient*>(
			GEditor->GetActiveViewport()->GetClient());
		if (Client)
		{
			Client->SetViewLocation(Location);
			Client->SetViewRotation(Rotation);
			return true;
		}
	}

	return false;
}

// ============================================================================
// Screenshot Directory
// ============================================================================

FString UArborCaptureTools::EnsureScreenshotDir(const FString& OutputPath)
{
	if (!OutputPath.IsEmpty())
	{
		FString AbsPath = FPaths::ConvertRelativePathToFull(OutputPath);
		IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
		PF.CreateDirectoryTree(*AbsPath);
		return AbsPath;
	}

	FString SavedDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
	FString OutDir = FPaths::Combine(SavedDir, TEXT("Arbor"), TEXT("Screenshots"));
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	PF.CreateDirectoryTree(*OutDir);
	return OutDir;
}

// ============================================================================
// Core Capture
// ============================================================================

FString UArborCaptureTools::CaptureToFile(const FVector& Location, const FRotator& Rotation,
	const FString& OutputDir, const FString& Filename,
	int32 Width, int32 Height)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return TEXT("{\"success\":false,\"error\":\"No editor world\"}");
	}

	// Create render target
	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>();
	RT->InitAutoFormat(Width, Height);
	RT->UpdateResourceImmediate(true);

	// Spawn SceneCapture2D
	ASceneCapture2D* CaptureActor = World->SpawnActor<ASceneCapture2D>(
		ASceneCapture2D::StaticClass(), Location, Rotation);
	if (!CaptureActor)
	{
		return TEXT("{\"success\":false,\"error\":\"Failed to spawn SceneCapture2D\"}");
	}

	USceneCaptureComponent2D* Comp = CaptureActor->GetCaptureComponent2D();
	Comp->TextureTarget = RT;
	Comp->bCaptureEveryFrame = false;
	Comp->bAlwaysPersistRenderingState = true;
	Comp->CaptureSource = ESceneCaptureSource::SCS_FinalToneCurveHDR;

	// Exposure compensation (SceneCapture has no exposure history)
	Comp->PostProcessSettings.bOverride_AutoExposureBias = true;
	Comp->PostProcessSettings.AutoExposureBias = 2.0f;

	// Capture
	Comp->CaptureScene();

	// Build output path
	FString Dir = EnsureScreenshotDir(OutputDir);
	FString FinalFilename = Filename;
	if (FinalFilename.IsEmpty())
	{
		FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
		FinalFilename = FString::Printf(TEXT("screenshot_%s.jpg"), *Timestamp);
	}
	FString FullPath = FPaths::Combine(Dir, FinalFilename);

	// Export to disk (synchronous)
	FImageWriteOptions Options;
	Options.Format = EDesiredImageFormat::JPG;
	Options.CompressionQuality = 85;
	Options.bOverwriteFile = true;
	Options.bAsync = false;

	UImageWriteBlueprintLibrary::ExportToDisk(RT, FullPath, Options);

	// Cleanup
	CaptureActor->Destroy();

	UE_LOG(LogTemp, Log, TEXT("[ArborCaptureTools] Captured screenshot to: %s"), *FullPath);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("file_path"), FullPath);
	return SerializeJson(Result);
}

// ============================================================================
// Public API
// ============================================================================

FString UArborCaptureTools::GetViewportCamera()
{
	FVector Location;
	FRotator Rotation;
	if (!GetViewportCameraInfo(Location, Rotation))
	{
		return TEXT("{\"success\":false,\"error\":\"Cannot get viewport camera info\"}");
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetObjectField(TEXT("location"), VecToJson(Location));
	Result->SetObjectField(TEXT("rotation"), RotToJson(Rotation));
	return SerializeJson(Result);
}

FString UArborCaptureTools::SetViewportCamera(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> Params;
	auto Reader = TJsonReaderFactory<>::Create(ParamsJson);
	if (!FJsonSerializer::Deserialize(Reader, Params) || !Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	const TArray<TSharedPtr<FJsonValue>>* LocArr;
	const TArray<TSharedPtr<FJsonValue>>* RotArr;

	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;

	if (Params->TryGetArrayField(TEXT("location"), LocArr))
	{
		Location = ParseJsonVector(*LocArr);
	}
	if (Params->TryGetArrayField(TEXT("rotation"), RotArr))
	{
		Rotation = ParseJsonRotator(*RotArr);
	}

	if (!SetViewportCameraInfo(Location, Rotation))
	{
		return TEXT("{\"success\":false,\"error\":\"Cannot set viewport camera\"}");
	}

	return TEXT("{\"success\":true}");
}

FString UArborCaptureTools::CaptureFromPosition(const FString& ParamsJson)
{
	TSharedPtr<FJsonObject> Params;
	auto Reader = TJsonReaderFactory<>::Create(ParamsJson);
	if (!FJsonSerializer::Deserialize(Reader, Params) || !Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	const TArray<TSharedPtr<FJsonValue>>* LocArr;
	const TArray<TSharedPtr<FJsonValue>>* RotArr;
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;

	if (Params->TryGetArrayField(TEXT("location"), LocArr))
	{
		Location = ParseJsonVector(*LocArr);
	}
	if (Params->TryGetArrayField(TEXT("rotation"), RotArr))
	{
		Rotation = ParseJsonRotator(*RotArr);
	}

	FString OutputPath = Params->HasField(TEXT("output_path"))
		? Params->GetStringField(TEXT("output_path")) : TEXT("");
	FString Filename = Params->HasField(TEXT("filename"))
		? Params->GetStringField(TEXT("filename")) : TEXT("");
	int32 Width = Params->HasField(TEXT("width"))
		? (int32)Params->GetNumberField(TEXT("width")) : 960;
	int32 Height = Params->HasField(TEXT("height"))
		? (int32)Params->GetNumberField(TEXT("height")) : 540;

	return CaptureToFile(Location, Rotation, OutputPath, Filename, Width, Height);
}

FString UArborCaptureTools::CaptureViewport(const FString& ParamsJson)
{
	FVector Location;
	FRotator Rotation;
	if (!GetViewportCameraInfo(Location, Rotation))
	{
		return TEXT("{\"success\":false,\"error\":\"Cannot get viewport camera\"}");
	}

	TSharedPtr<FJsonObject> Params;
	auto Reader = TJsonReaderFactory<>::Create(ParamsJson);
	FString OutputPath, Filename;

	if (FJsonSerializer::Deserialize(Reader, Params) && Params.IsValid())
	{
		OutputPath = Params->HasField(TEXT("output_path"))
			? Params->GetStringField(TEXT("output_path")) : TEXT("");
		Filename = Params->HasField(TEXT("filename"))
			? Params->GetStringField(TEXT("filename")) : TEXT("");
	}

	return CaptureToFile(Location, Rotation, OutputPath, Filename, 960, 540);
}
