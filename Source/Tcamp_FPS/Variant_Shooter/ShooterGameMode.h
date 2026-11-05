// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ShooterGameMode.generated.h"

class AShooterPlayerState;

/**
 *  Shooter GameMode. Subscribes to ShooterGameState::OnKillEvent for scoring.
 *  Characters no longer cast to GameMode — the flow is:
 *    Character dies → HealthComponent::OnDeath → GameState::OnKillEvent → GameMode scores
 */
UCLASS(abstract)
class TCAMP_FPS_API AShooterGameMode : public AGameModeBase
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category = "Shooter|Victory", meta = (ClampMin = 1, ClampMax = 100))
	int32 KillGoal = 10;

	bool bGameOver = false;

protected:
	virtual void BeginPlay() override;

public:
	AShooterGameMode();

	/** Called via GameState::OnKillEvent — server-side scoring */
	UFUNCTION()
	void OnKillEvent(AController* Killer, AController* Victim);

	UFUNCTION(BlueprintPure, Category = "Shooter")
	bool IsGameOver() const { return bGameOver; }

	UFUNCTION(BlueprintPure, Category = "Shooter")
	int32 GetKillGoal() const { return KillGoal; }

protected:
	void TriggerVictory(AShooterPlayerState* Winner);
	void EndMatchForAll();
};
