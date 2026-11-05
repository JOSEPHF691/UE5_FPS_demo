// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "ShooterPlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKillsUpdatedDelegate, int32, Kills, int32, KillGoal);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMatchOverDelegate);

/**
 * PlayerState for the shooter game. Tracks per-player kills and score.
 * Replicates to all clients so everyone can see the scoreboard.
 *
 * MULTIPLAYER FLOW:
 * 1. Server GameMode calls AddKill() → Kills increments
 * 2. Kills replicates to all clients via OnRep_Kills
 * 3. Each client's PlayerController updates their local UI
 * 4. When kill goal is reached, server sets bMatchOver & bIsWinner
 * 5. These replicate to clients → UI shows victory or defeat
 */
UCLASS()
class TCAMP_FPS_API AShooterPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	/** Number of enemy kills this player has achieved — REPLICATES TO ALL CLIENTS */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Score")
	int32 Kills = 0;

	/** Kill goal to reach for victory — replicated so client UI can show progress */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Score")
	int32 KillGoal = 10;

	/** True when the match has ended (someone won) — REPLICATES TO ALL CLIENTS */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Score")
	bool bMatchOver = false;

	/** True if THIS player is the winner — REPLICATES TO ALL CLIENTS */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Score")
	bool bIsWinner = false;

	/** Fires on owning client when kills change */
	UPROPERTY(BlueprintAssignable, Category="Score")
	FKillsUpdatedDelegate OnKillsUpdated;

	/** Fires on owning client when the match ends (win or lose) */
	UPROPERTY(BlueprintAssignable, Category="Score")
	FMatchOverDelegate OnMatchOver;

public:
	/** Adds a kill to this player's score — called on SERVER only */
	void AddKill();

	/** Called on SERVER only — marks this player as the winner and ends the match for all */
	void MarkAsWinner();

	/** Called on SERVER only — marks the match as over for this player (they lost) */
	void MarkMatchOver();

	/** Returns the current kill count */
	UFUNCTION(BlueprintPure, Category="Score")
	int32 GetKills() const { return Kills; }

	/** Returns true if the match has ended */
	UFUNCTION(BlueprintPure, Category="Score")
	bool IsMatchOver() const { return bMatchOver; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	/** Called on clients when Kills is replicated */
	UFUNCTION()
	void OnRep_Kills();

	/** Called on clients when bMatchOver is replicated */
	UFUNCTION()
	void OnRep_MatchOver();
};
