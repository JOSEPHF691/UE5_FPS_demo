// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Economy/EconomyTypes.h"
#include "TacticalGameState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPhaseChanged, ERoundPhase, NewPhase, float, Duration);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnRoundEnded, uint8, WinningTeam, int32, TeamAScore, int32, TeamBScore, ERoundEndReason, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMatchEnded, uint8, WinningTeam);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTacticalKillEvent, AController*, Killer, AController*, Victim);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNPCKillEvent, AController*, Killer, class AShooterNPC*, Victim);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHuntIntelStateChanged, bool, bActive, bool, bEscalated);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTreasureStateChanged);

/**
 *  GameState for the tactical FPS — replication hub for round state.
 *  All round/phase data replicates from server to all clients.
 */
UCLASS()
class TCAMP_FPS_API ATacticalGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ATacticalGameState();

	// ---- Replicated Round State ----

	UPROPERTY(ReplicatedUsing = OnRep_RoundNumber, BlueprintReadOnly, Category = "Round")
	int32 RoundNumber = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Phase, BlueprintReadOnly, Category = "Round")
	ERoundPhase CurrentPhase = ERoundPhase::PreRound;

	/** Locally derived countdown value. Replicate the end timestamp instead. */
	UPROPERTY(BlueprintReadOnly, Category = "Round")
	float PhaseTimeRemaining = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_PhaseTiming)
	float PhaseEndServerTime = 0.0f;

	UPROPERTY(Replicated)
	int32 TeamScores[2] = { 0, 0 };

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Round")
	int32 MaxRounds = 9;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Round")
	uint8 LastRoundWinningTeam = 255;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Round")
	ERoundEndReason LastRoundEndReason = ERoundEndReason::Unknown;

	UPROPERTY(ReplicatedUsing = OnRep_RoundEnd, BlueprintReadOnly, Category = "Round")
	uint8 RoundEndSerial = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Hunt Intel")
	bool bHuntIntelActive = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Hunt Intel")
	bool bHuntIntelEscalated = false;

	UPROPERTY(ReplicatedUsing = OnRep_HuntIntelState)
	uint8 HuntIntelStateSerial = 0;

	/** Atomic public treasure state; safe to consume after a late join or reconnect. */
	UPROPERTY(ReplicatedUsing = OnRep_TreasureState, BlueprintReadOnly, Category = "Treasure")
	FTreasureRoundSnapshot TreasureState;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	uint8 LastMatchWinningTeam = 255;

	/** Match-level result; never inferred from a round result or a 255 winner alone. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	EMatchEndReason LastMatchEndReason = EMatchEndReason::Unknown;

	UPROPERTY(ReplicatedUsing = OnRep_MatchEnd, BlueprintReadOnly, Category = "Match")
	uint8 MatchEndSerial = 0;

	// ---- Delegates (BlueprintAssignable for client UI) ----

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnPhaseChanged OnPhaseChanged;

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnRoundEnded OnRoundEnded;

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnMatchEnded OnMatchEnded;

	/** Fired when a player is killed. GameMode subscribes for economy + victory checks. */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnTacticalKillEvent OnPlayerKilled;

	/** Fired when an NPC is killed. GameMode subscribes for reward + chest logic. */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnNPCKillEvent OnNPCKilled;

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnHuntIntelStateChanged OnHuntIntelStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnTreasureStateChanged OnTreasureStateChanged;

public:
	/** Resets per-round state before starting a new round */
	void ResetRoundState(int32 InRoundNumber);

	/** Advances the phase timer. Returns true if the timer just expired. */
	bool TickPhaseTimer(float DeltaSeconds);

	UFUNCTION(BlueprintPure, Category = "Round")
	float GetPhaseTimeRemaining() const;

	/** Sets the current phase and broadcasts */
	void SetPhase(ERoundPhase NewPhase, float Duration);

	/** Updates replicated hunt-intel warning state. */
	void SetHuntIntelState(bool bActive, bool bEscalated);

	/** Publishes a complete treasure snapshot and advances its revision. */
	void SetTreasureState(const FTreasureRoundSnapshot& NewState);

	/** Clears every public treasure field for a round. */
	void ResetTreasureState(int32 InRoundNumber);

	UFUNCTION(BlueprintPure, Category = "Treasure")
	float GetTreasureActivationTimeRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Treasure")
	float GetTreasureClaimTimeRemaining() const;

	/** Awards a round win to a team and broadcasts the reason */
	void AwardRoundWin(uint8 WinningTeam, ERoundEndReason Reason);

	/** Broadcasts a round ending without awarding score */
	void BroadcastRoundDraw(ERoundEndReason Reason);

	/** Checks if any team has won the match (RoundsToWin reached) */
	bool IsMatchOver() const;

	/** Returns 0/1 for a team winner or 255 for a tied match. */
	uint8 GetMatchWinner() const;

	/** Replicates the final match outcome and broadcasts it on server and clients. */
	void BroadcastMatchEnded(uint8 WinningTeam, EMatchEndReason Reason);

	/** Counts alive players for a given team */
	int32 CountPlayersAlive(uint8 Team) const;

	/** Returns true if the given team has been fully eliminated */
	bool IsTeamEliminated(uint8 Team) const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_RoundNumber();

	UFUNCTION()
	void OnRep_Phase();

	UFUNCTION()
	void OnRep_PhaseTiming();

	UFUNCTION()
	void OnRep_RoundEnd();

	UFUNCTION()
	void OnRep_HuntIntelState();

	UFUNCTION()
	void OnRep_TreasureState();

	UFUNCTION()
	void OnRep_MatchEnd();

	void BroadcastRoundEnded(uint8 WinningTeam, ERoundEndReason Reason);
};
