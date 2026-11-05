// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterUI.generated.h"

/**
 * Scoreboard and game-over UI for a first person shooter game.
 * All display functions are BlueprintImplementableEvents — you design the visuals in Blueprint.
 */
UCLASS(abstract)
class TCAMP_FPS_API UShooterUI : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Called to update the score display */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta = (DisplayName = "Update Score"))
	void BP_UpdateScore(uint8 TeamByte, int32 Score);

	/** Called when this player wins the match */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta = (DisplayName = "On Victory"))
	void BP_OnVictory();

	/** Called when another player wins the match */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta = (DisplayName = "On Defeat"))
	void BP_OnDefeat();

	/** Called to update the kill count display */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta = (DisplayName = "Update Kills"))
	void BP_UpdateKills(int32 Kills, int32 KillGoal);

	/** Called when the player hits an enemy — crosshair feedback */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta = (DisplayName = "On Enemy Hit"))
	void BP_OnEnemyHit();
};
