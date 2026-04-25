#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArborPlaytestTools.generated.h"

UCLASS()
class ARBOR_API UArborPlaytestTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Start a Play-In-Editor session.
	 *
	 * @return JSON: {success, mode:"PIE"|"SIE", note?}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Playtest")
	static FString StartPIE();

	/**
	 * Stop the current PIE/SIE session.
	 *
	 * @return JSON: {success}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Playtest")
	static FString StopPIE();

	/**
	 * Check if PIE is running and a player pawn exists.
	 *
	 * @return JSON: {running, has_player, player_location?:{x,y,z}}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Playtest")
	static FString IsPIERunning();

	/**
	 * Get player location, rotation, and velocity.
	 *
	 * @return JSON: {success, location:{x,y,z}, rotation:{pitch,yaw,roll}, velocity:{x,y,z}}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Playtest")
	static FString GetPlayerInfo();

	/**
	 * Teleport the player pawn to a specific location.
	 *
	 * @param ParamsJson  JSON: {location:[x,y,z], rotation?:[pitch,yaw,roll]}
	 * @return JSON: {success, location:{x,y,z}}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Playtest")
	static FString TeleportPlayer(const FString& ParamsJson);

	/**
	 * Get the current instantaneous framerate.
	 *
	 * @return JSON: {success, fps}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Playtest")
	static FString GetFramerate();

	/**
	 * Check if a navigation path exists between two locations.
	 *
	 * @param ParamsJson  JSON: {from:[x,y,z], to:[x,y,z]}
	 * @return JSON: {success, reachable, partial, path_length, path_points:[{x,y,z},...]}
	 */
	UFUNCTION(BlueprintCallable, Category = "Arbor|Playtest")
	static FString CheckPlayerCanReach(const FString& ParamsJson);

private:
	/** Find the player pawn and controller in the PIE world. */
	static bool GetPIEPlayerPawn(APawn*& OutPawn, APlayerController*& OutController);

	/** Check if PIE is active via any available method. */
	static bool IsPIEActive();
};
