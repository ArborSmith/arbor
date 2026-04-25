#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/Texture2D.h"
#include "ArborGameContextTypes.generated.h"

UENUM(BlueprintType)
enum class EArborPillarType : uint8
{
	Theme   UMETA(DisplayName = "Theme"),
	Pillar  UMETA(DisplayName = "Pillar"),
};

UENUM(BlueprintType)
enum class EArborCodexStatus : uint8
{
	None            UMETA(DisplayName = "None"),
	Ideation        UMETA(DisplayName = "Ideation"),
	PreProduction   UMETA(DisplayName = "Pre-Production"),
	Prototype       UMETA(DisplayName = "Prototype"),
	Production      UMETA(DisplayName = "Production"),
	Polish          UMETA(DisplayName = "Polish"),
	Complete        UMETA(DisplayName = "Complete"),
	Cut             UMETA(DisplayName = "Cut"),
};

/**
 * Data asset storing reusable game/world context for character generation.
 * Contains title, genre, setting, tone, lore, themes, and a keyword pool.
 */
UCLASS(BlueprintType)
class ARBOR_API UArborGameContextAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Game Context")
	FString GameTitle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Game Context")
	FString Genre;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Game Context")
	FString Setting;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Game Context")
	FString Tone;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Game Context", meta=(MultiLine=true))
	FString WorldDescription;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Game Context", meta=(MultiLine=true))
	FString PlayerControls;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Concept Art")
	TSoftObjectPtr<UTexture2D> ConceptArt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Concept Art")
	TArray<TSoftObjectPtr<UTexture2D>> ConceptArtGallery;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Concept Art")
	FString ConceptArtPrompt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style Images")
	TArray<TSoftObjectPtr<UTexture2D>> StyleImages;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style Images")
	FString StyleImagePrompt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Game Context")
	TArray<FString> Tags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status")
	EArborCodexStatus Status = EArborCodexStatus::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lock")
	TSet<FString> LockedFields;
};

/**
 * Data asset storing a location that belongs to a Game Context.
 * Sent in the AI prompt alongside the parent context during character generation.
 */
UCLASS(BlueprintType)
class ARBOR_API UArborLocationAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Location")
	TSoftObjectPtr<UArborGameContextAsset> GameContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Location")
	FString LocationName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Location", meta=(MultiLine=true))
	FString Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Location")
	FString Region;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Location")
	FString Atmosphere;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Location")
	TArray<FString> Tags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Concept Art")
	TSoftObjectPtr<UTexture2D> ConceptArt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Concept Art")
	TArray<TSoftObjectPtr<UTexture2D>> ConceptArtGallery;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Concept Art")
	FString ConceptArtPrompt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status")
	EArborCodexStatus Status = EArborCodexStatus::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lock")
	TSet<FString> LockedFields;
};

/**
 * Data asset for a game feature (formerly "system") belonging to a Game Context.
 * Features are the universal catch-all for game mechanics: core loop, camera, controls,
 * physics, economy, AI, combat, crafting, progression, social, exploration, UI, etc.
 */
UCLASS(BlueprintType)
class ARBOR_API UArborFeatureAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feature")
	TSoftObjectPtr<UArborGameContextAsset> GameContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feature")
	FString FeatureName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feature")
	FString Category;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feature", meta=(MultiLine=true))
	FString Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Feature")
	TArray<FString> Tags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Concept Art")
	TSoftObjectPtr<UTexture2D> ConceptArt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Concept Art")
	TArray<TSoftObjectPtr<UTexture2D>> ConceptArtGallery;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Concept Art")
	FString ConceptArtPrompt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status")
	EArborCodexStatus Status = EArborCodexStatus::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lock")
	TSet<FString> LockedFields;
};

/**
 * Data asset for a design pillar (theme or game pillar) belonging to a Game Context.
 * Themes describe emotional/narrative throughlines. Pillars describe design constraints.
 */
UCLASS(BlueprintType)
class ARBOR_API UArborPillarAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pillar")
	TSoftObjectPtr<UArborGameContextAsset> GameContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pillar")
	FString PillarName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pillar")
	EArborPillarType PillarType = EArborPillarType::Pillar;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pillar", meta=(MultiLine=true))
	FString Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pillar")
	TArray<FString> Tags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Concept Art")
	TSoftObjectPtr<UTexture2D> ConceptArt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Concept Art")
	TArray<TSoftObjectPtr<UTexture2D>> ConceptArtGallery;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Concept Art")
	FString ConceptArtPrompt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Status")
	EArborCodexStatus Status = EArborCodexStatus::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lock")
	TSet<FString> LockedFields;
};
