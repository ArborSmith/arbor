#include "ArborLightingTools.h"
#include "Engine/World.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/DirectionalLight.h"
#include "Engine/RectLight.h"
#include "Engine/SkyLight.h"
#include "Engine/PostProcessVolume.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Components/LightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Atmosphere/AtmosphericFog.h"
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

static TSharedPtr<FJsonObject> ParseJson(const FString& Json)
{
	TSharedPtr<FJsonObject> Obj;
	auto Reader = TJsonReaderFactory<>::Create(Json);
	FJsonSerializer::Deserialize(Reader, Obj);
	return Obj;
}

static double GetOpt(const TSharedPtr<FJsonObject>& Obj, const FString& Key, double Default)
{
	double Val;
	if (Obj->TryGetNumberField(Key, Val)) return Val;
	return Default;
}

void UArborLightingTools::RemoveExistingByClass(UClass* ActorClass)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World || !ActorClass) return;

	TArray<AActor*> ToDestroy;
	for (TActorIterator<AActor> It(World, ActorClass); It; ++It)
	{
		ToDestroy.Add(*It);
	}
	for (AActor* A : ToDestroy)
	{
		A->Destroy();
	}
}

AActor* UArborLightingTools::SpawnActor(UClass* ActorClass, const FVector& Location,
	const FRotator& Rotation, const FString& Label)
{
	AActor* Actor = GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->SpawnActorFromClass(ActorClass, Location, Rotation);
	if (Actor && !Label.IsEmpty())
	{
		Actor->SetActorLabel(Label);
	}
	return Actor;
}

// ============================================================================
// Public API
// ============================================================================

FString UArborLightingTools::SetupOutdoorScene(const FString& ParamsJson)
{
	auto Params = ParseJson(ParamsJson);

	// Parse sun rotation
	double SunPitch = -45.0, SunYaw = 30.0, SunRoll = 0.0;
	double FogDensity = 0.01;

	if (Params.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* RotArr;
		if (Params->TryGetArrayField(TEXT("sun_rotation"), RotArr) && RotArr->Num() >= 2)
		{
			SunPitch = (*RotArr)[0]->AsNumber();
			SunYaw = (*RotArr)[1]->AsNumber();
			if (RotArr->Num() >= 3) SunRoll = (*RotArr)[2]->AsNumber();
		}
		FogDensity = GetOpt(Params, TEXT("fog_density"), 0.01);
	}

	// Remove existing
	RemoveExistingByClass(ADirectionalLight::StaticClass());
	RemoveExistingByClass(APostProcessVolume::StaticClass());

	// Try to find and remove atmosphere/sky/fog classes dynamically
	UClass* SkyAtmosphereClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.SkyAtmosphere"));
	UClass* SkyLightClass = ASkyLight::StaticClass();
	UClass* FogClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.ExponentialHeightFog"));
	UClass* CloudClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.VolumetricCloud"));

	if (SkyAtmosphereClass) RemoveExistingByClass(SkyAtmosphereClass);
	RemoveExistingByClass(SkyLightClass);
	if (FogClass) RemoveExistingByClass(FogClass);
	if (CloudClass) RemoveExistingByClass(CloudClass);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);

	// Sun (DirectionalLight)
	AActor* Sun = SpawnActor(ADirectionalLight::StaticClass(),
		FVector::ZeroVector, FRotator(SunPitch, SunYaw, SunRoll), TEXT("Sun"));
	if (Sun)
	{
		UDirectionalLightComponent* DLC = Sun->FindComponentByClass<UDirectionalLightComponent>();
		if (DLC)
		{
			DLC->SetIntensity(10.0f);
			DLC->SetLightColor(FLinearColor(1.0f, 0.95f, 0.85f));
		}
		Root->SetStringField(TEXT("sun"), TEXT("Sun"));
	}

	// Sky Atmosphere
	if (SkyAtmosphereClass)
	{
		AActor* SkyAtmo = SpawnActor(SkyAtmosphereClass, FVector::ZeroVector, FRotator::ZeroRotator, TEXT("SkyAtmosphere"));
		Root->SetStringField(TEXT("sky_atmosphere"), SkyAtmo ? TEXT("SkyAtmosphere") : TEXT(""));
	}

	// Sky Light
	AActor* SkyLightActor = SpawnActor(SkyLightClass, FVector(0, 0, 500), FRotator::ZeroRotator, TEXT("SkyLight"));
	if (SkyLightActor)
	{
		USkyLightComponent* SLC = SkyLightActor->FindComponentByClass<USkyLightComponent>();
		if (SLC) SLC->SetIntensity(1.0f);
		Root->SetStringField(TEXT("sky_light"), TEXT("SkyLight"));
	}

	// Fog
	if (FogClass)
	{
		AActor* FogActor = SpawnActor(FogClass, FVector::ZeroVector, FRotator::ZeroRotator, TEXT("Fog"));
		if (FogActor)
		{
			UExponentialHeightFogComponent* FogComp = FogActor->FindComponentByClass<UExponentialHeightFogComponent>();
			if (FogComp)
			{
				FogComp->SetFogDensity(FogDensity);
			}
			Root->SetStringField(TEXT("fog"), TEXT("Fog"));
		}
	}

	// Volumetric Cloud
	if (CloudClass)
	{
		AActor* CloudActor = SpawnActor(CloudClass, FVector::ZeroVector, FRotator::ZeroRotator, TEXT("VolumetricCloud"));
		Root->SetStringField(TEXT("clouds"), CloudActor ? TEXT("VolumetricCloud") : TEXT(""));
	}

	// Post Process
	AActor* PP = SpawnActor(APostProcessVolume::StaticClass(),
		FVector::ZeroVector, FRotator::ZeroRotator, TEXT("OutdoorPostProcess"));
	if (PP)
	{
		APostProcessVolume* PPV = Cast<APostProcessVolume>(PP);
		if (PPV)
		{
			PPV->bUnbound = true;
			PPV->Settings.bOverride_BloomIntensity = true;
			PPV->Settings.BloomIntensity = 0.675f;
			PPV->Settings.bOverride_AutoExposureMinBrightness = true;
			PPV->Settings.AutoExposureMinBrightness = 1.0f;
			PPV->Settings.bOverride_AutoExposureMaxBrightness = true;
			PPV->Settings.AutoExposureMaxBrightness = 1.0f;
		}
		Root->SetStringField(TEXT("post_process"), TEXT("OutdoorPostProcess"));
	}

	return SerializeJson(Root);
}

FString UArborLightingTools::SetupIndoorScene(const FString& ParamsJson)
{
	auto Params = ParseJson(ParamsJson);
	double AmbientIntensity = 0.5;
	if (Params.IsValid())
	{
		AmbientIntensity = GetOpt(Params, TEXT("ambient_intensity"), 0.5);
	}

	RemoveExistingByClass(ASkyLight::StaticClass());
	RemoveExistingByClass(APostProcessVolume::StaticClass());

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);

	// Sky Light
	AActor* SkyLightActor = SpawnActor(ASkyLight::StaticClass(),
		FVector(0, 0, 500), FRotator::ZeroRotator, TEXT("IndoorSkyLight"));
	if (SkyLightActor)
	{
		USkyLightComponent* SLC = SkyLightActor->FindComponentByClass<USkyLightComponent>();
		if (SLC) SLC->SetIntensity(AmbientIntensity);
		Root->SetStringField(TEXT("sky_light"), TEXT("IndoorSkyLight"));
	}

	// Post Process
	AActor* PP = SpawnActor(APostProcessVolume::StaticClass(),
		FVector::ZeroVector, FRotator::ZeroRotator, TEXT("IndoorPostProcess"));
	if (PP)
	{
		APostProcessVolume* PPV = Cast<APostProcessVolume>(PP);
		if (PPV)
		{
			PPV->bUnbound = true;
			PPV->Settings.bOverride_BloomIntensity = true;
			PPV->Settings.BloomIntensity = 0.5f;
			PPV->Settings.bOverride_AutoExposureMinBrightness = true;
			PPV->Settings.AutoExposureMinBrightness = 0.5f;
			PPV->Settings.bOverride_AutoExposureMaxBrightness = true;
			PPV->Settings.AutoExposureMaxBrightness = 2.0f;
		}
		Root->SetStringField(TEXT("post_process"), TEXT("IndoorPostProcess"));
	}

	// Rect Light (ceiling light)
	AActor* RectActor = SpawnActor(ARectLight::StaticClass(),
		FVector(0, 0, 250), FRotator(-90, 0, 0), TEXT("IndoorCeilingLight"));
	if (RectActor)
	{
		URectLightComponent* RLC = RectActor->FindComponentByClass<URectLightComponent>();
		if (RLC)
		{
			RLC->SetIntensity(3000.0f);
			RLC->SetSourceWidth(200.0f);
			RLC->SetSourceHeight(200.0f);
		}
		Root->SetStringField(TEXT("rect_light"), TEXT("IndoorCeilingLight"));
	}

	return SerializeJson(Root);
}

FString UArborLightingTools::AddPostProcessVolume(const FString& ParamsJson)
{
	auto Params = ParseJson(ParamsJson);
	if (!Params.IsValid())
	{
		return TEXT("{\"success\":false,\"error\":\"Invalid JSON\"}");
	}

	double X = 0, Y = 0, Z = 0;
	const TArray<TSharedPtr<FJsonValue>>* LocArr;
	if (Params->TryGetArrayField(TEXT("location"), LocArr) && LocArr->Num() >= 3)
	{
		X = (*LocArr)[0]->AsNumber();
		Y = (*LocArr)[1]->AsNumber();
		Z = (*LocArr)[2]->AsNumber();
	}

	bool bInfinite = true;
	Params->TryGetBoolField(TEXT("infinite_extent"), bInfinite);

	const double BloomIntensity = GetOpt(Params, TEXT("bloom_intensity"), 0.675);
	const double AutoExpMin = GetOpt(Params, TEXT("auto_exposure_min"), 1.0);
	const double AutoExpMax = GetOpt(Params, TEXT("auto_exposure_max"), 1.0);
	FString Label;
	Params->TryGetStringField(TEXT("label"), Label);
	if (Label.IsEmpty()) Label = TEXT("PostProcess");

	AActor* Actor = SpawnActor(APostProcessVolume::StaticClass(),
		FVector(X, Y, Z), FRotator::ZeroRotator, Label);
	if (!Actor)
	{
		return TEXT("{\"success\":false,\"error\":\"Failed to spawn PostProcessVolume\"}");
	}

	APostProcessVolume* PPV = Cast<APostProcessVolume>(Actor);
	if (PPV)
	{
		PPV->bUnbound = bInfinite;
		PPV->Settings.bOverride_BloomIntensity = true;
		PPV->Settings.BloomIntensity = BloomIntensity;
		PPV->Settings.bOverride_AutoExposureMinBrightness = true;
		PPV->Settings.AutoExposureMinBrightness = AutoExpMin;
		PPV->Settings.bOverride_AutoExposureMaxBrightness = true;
		PPV->Settings.AutoExposureMaxBrightness = AutoExpMax;
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("actor_path"), Actor->GetPathName());
	return SerializeJson(Root);
}
