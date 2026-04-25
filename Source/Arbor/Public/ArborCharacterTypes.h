#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/Texture2D.h"
#include "ArborGameContextTypes.h"
#include "ArborCharacterTypes.generated.h"

UENUM(BlueprintType)
enum class EArborCharacterRole : uint8
{
	Player      UMETA(DisplayName = "Player"),
	NPC         UMETA(DisplayName = "NPC"),
	Enemy       UMETA(DisplayName = "Enemy"),
	Boss        UMETA(DisplayName = "Boss"),
	Companion   UMETA(DisplayName = "Companion"),
	Vehicle     UMETA(DisplayName = "Vehicle"),
	Other       UMETA(DisplayName = "Other"),
};

/**
 * Data asset storing character definition for Arbor's narrative system.
 * Created by ArborCharacterBuilder from JSON, with an .arbor.json sidecar for version control.
 */
UCLASS(BlueprintType)
class ARBOR_API UCharacterDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character")
	FString CharacterName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character")
	EArborCharacterRole Role = EArborCharacterRole::NPC;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character", meta=(MultiLine=true))
	FString Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character")
	TArray<FString> Tags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character")
	TSoftObjectPtr<UArborGameContextAsset> GameContext;

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
