#include "ArborLevelTools.h"
#include "ArborPropertyJson.h"

#include "Editor.h"
#include "FileHelpers.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogArborLevel, Log, All);

namespace
{
	using Arbor::Json::MakeJsonResult;
	using Arbor::Json::JsonError;

	UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	FString GetWorldPackagePath(UWorld* World)
	{
		if (!World) return FString();
		if (UPackage* Package = World->GetOutermost())
		{
			return Package->GetName();
		}
		return FString();
	}
}

// ============================================================================
// LoadLevel
// ============================================================================

FString UArborLevelTools::LoadLevel(const FString& AssetPath, bool bForce)
{
	if (AssetPath.IsEmpty())
	{
		return JsonError(TEXT("AssetPath is empty"));
	}

	UWorld* CurrentWorld = GetEditorWorld();
	if (CurrentWorld)
	{
		UPackage* CurrentPackage = CurrentWorld->GetOutermost();
		const bool bDirty = CurrentPackage && CurrentPackage->IsDirty();
		if (bDirty && !bForce)
		{
			return JsonError(FString::Printf(
				TEXT("Current level '%s' has unsaved changes. ")
				TEXT("Call save_current first, or pass force=true to discard."),
				*GetWorldPackagePath(CurrentWorld)));
		}
	}

	const bool bLoadAsTemplate = false;
	const bool bShowProgress = true;
	FEditorFileUtils::LoadMap(AssetPath, bLoadAsTemplate, bShowProgress);

	UWorld* NewWorld = GetEditorWorld();
	const FString NewPath = GetWorldPackagePath(NewWorld);
	if (NewPath.IsEmpty() || !NewPath.Contains(FPaths::GetBaseFilename(AssetPath)))
	{
		return JsonError(FString::Printf(TEXT("LoadMap returned but world is '%s', expected '%s'"),
			*NewPath, *AssetPath));
	}

	const TSharedRef<FJsonObject> Extra = MakeShared<FJsonObject>();
	Extra->SetStringField(TEXT("asset_path"), NewPath);
	Extra->SetStringField(TEXT("level_name"), NewWorld ? NewWorld->GetMapName() : FString());
	return MakeJsonResult(true, FString(), Extra);
}

// ============================================================================
// SaveCurrentLevel
// ============================================================================

FString UArborLevelTools::SaveCurrentLevel()
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return JsonError(TEXT("No active editor world"));
	}

	const bool bSaved = FEditorFileUtils::SaveCurrentLevel();
	if (!bSaved)
	{
		return JsonError(FString::Printf(TEXT("SaveCurrentLevel failed for '%s'"),
			*GetWorldPackagePath(World)));
	}

	const TSharedRef<FJsonObject> Extra = MakeShared<FJsonObject>();
	Extra->SetStringField(TEXT("asset_path"), GetWorldPackagePath(World));
	Extra->SetStringField(TEXT("level_name"), World->GetMapName());
	return MakeJsonResult(true, FString(), Extra);
}

// ============================================================================
// GetCurrentLevel
// ============================================================================

FString UArborLevelTools::GetCurrentLevel()
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return JsonError(TEXT("No active editor world"));
	}

	UPackage* Package = World->GetOutermost();
	const bool bIsDirty = Package && Package->IsDirty();

	int32 ActorCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		++ActorCount;
	}

	const TSharedRef<FJsonObject> Extra = MakeShared<FJsonObject>();
	Extra->SetStringField(TEXT("asset_path"), GetWorldPackagePath(World));
	Extra->SetStringField(TEXT("level_name"), World->GetMapName());
	Extra->SetBoolField(TEXT("is_dirty"), bIsDirty);
	Extra->SetNumberField(TEXT("actor_count"), ActorCount);
	return MakeJsonResult(true, FString(), Extra);
}
