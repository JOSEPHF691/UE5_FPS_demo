// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterUI.h"
#include "Economy/EconomyTypes.h"
#include "TacticalPlayerState.h"
#include "TacticalUI.generated.h"

class UWorld;
class UWidget;

USTRUCT(BlueprintType)
struct FTeamAvatarStatus
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Tactical")
	uint8 TeamByte = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Tactical")
	int32 PlayerId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Tactical")
	bool bIsDead = false;

	UPROPERTY(BlueprintReadOnly, Category = "Tactical")
	bool bHasLivingPawn = false;

	UPROPERTY(BlueprintReadOnly, Category = "Tactical")
	bool bIsActiveParticipant = false;

	UPROPERTY(BlueprintReadOnly, Category = "Tactical")
	bool bIsLocalPlayer = false;

	UPROPERTY(BlueprintReadOnly, Category = "Tactical")
	ETacticalPublicConnectionStatus ConnectionStatus =
		ETacticalPublicConnectionStatus::Connected;

	/** Derived from a replicated server deadline and the synchronized GameState clock. */
	UPROPERTY(BlueprintReadOnly, Category = "Tactical")
	float ReconnectGraceTimeRemaining = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Tactical")
	FText PlayerName;
};

/**
 *  Tactical HUD widget extending ShooterUI.
 *  All display functions are BlueprintImplementableEvents.
 */
UCLASS(abstract)
class TCAMP_FPS_API UTacticalUI : public UShooterUI
{
	GENERATED_BODY()

	/** Branch roots hidden while only the existing crosshair should remain visible. */
	TMap<TWeakObjectPtr<UWidget>, ESlateVisibility> PreRoundHiddenBranches;
	bool bPreRoundCrosshairOnly = false;
	bool bAuthoredCrosshairSuppressed = false;
	bool bHasCachedAuthoredCrosshairVisibility = false;
	ESlateVisibility CachedAuthoredCrosshairVisibility = ESlateVisibility::Visible;

	void ApplyAuthoredCrosshairSuppression();

public:
	/** Keeps the authored Crosshair subtree visible while hiding its sibling HUD branches. */
	void SetPreRoundCrosshairOnly(bool bEnabled);

	/** Keeps the legacy named node for layout traversal while the native reticle replaces its visuals. */
	void SetAuthoredCrosshairSuppressed(bool bSuppressed);

	/** Update round display */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactical", meta = (DisplayName = "Update Round Number"))
	void BP_UpdateRoundNumber(int32 Round, int32 MaxRounds);

	/** Update team scores */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactical", meta = (DisplayName = "Update Team Scores"))
	void BP_UpdateTeamScores(int32 TeamAScore, int32 TeamBScore);

	/** Show why the round ended */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactical", meta = (DisplayName = "Show Round End Reason"))
	void BP_ShowRoundEndReason(uint8 WinningTeam, ERoundEndReason Reason);

	/** Converts a round end reason into player-facing text for widgets. */
	UFUNCTION(BlueprintPure, Category = "Tactical")
	static FText GetRoundEndReasonText(uint8 WinningTeam, ERoundEndReason Reason);

	/** Converts the independent match result into player-facing text. */
	UFUNCTION(BlueprintPure, Category = "Tactical")
	static FText GetMatchEndReasonText(uint8 WinningTeam, EMatchEndReason Reason);

	/** Converts a round phase into the short Chinese phase text shown on the HUD. */
	UFUNCTION(BlueprintPure, Category = "Tactical")
	static FText GetPhaseDisplayText(ERoundPhase Phase);

	/** Formats seconds as M:SS for the countdown text block. */
	UFUNCTION(BlueprintPure, Category = "Tactical")
	static FText GetCountdownDisplayText(float TimeRemaining);

	/** Formats the tactical score text as A : B. */
	UFUNCTION(BlueprintPure, Category = "Tactical")
	static FText GetScoreDisplayText(int32 TeamAScore, int32 TeamBScore);

	/** Reads the current GameState and returns score text for binding. */
	UFUNCTION(BlueprintPure, Category = "Tactical")
	FText GetCurrentScoreDisplayText() const;

	/** Reads the current GameState and returns phase text for binding. */
	UFUNCTION(BlueprintPure, Category = "Tactical")
	FText GetCurrentPhaseDisplayText() const;

	/** Reads the current GameState and returns countdown text for binding. */
	UFUNCTION(BlueprintPure, Category = "Tactical")
	FText GetCurrentCountdownDisplayText() const;

	/** Builds the player avatar list for one team. Each entry tells Blueprint whether to show the red death cross. */
	UFUNCTION(BlueprintPure, Category = "Tactical")
	TArray<FTeamAvatarStatus> GetTeamAvatarStatuses(uint8 TeamByte) const;

	/** Shared C++ source for both the Blueprint avatar hook and the native scoreboard. */
	static TArray<FTeamAvatarStatus> BuildTeamAvatarStatuses(
		const UWorld* World,
		uint8 TeamByte,
		const APlayerState* LocalPlayerState = nullptr);

	/** Update phase and remaining time */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactical", meta = (DisplayName = "Update Phase"))
	void BP_UpdatePhase(ERoundPhase Phase, float TimeRemaining);

	/** Legacy hook retained only so existing widget assets continue to load. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactical|Legacy", meta = (DeprecatedFunction, DeprecationMessage = "Elimination countdown was removed"))
	void BP_ShowTeamEliminationRoundEndCountdown(float TimeRemaining, uint8 WinningTeam);

	UFUNCTION(BlueprintImplementableEvent, Category = "Tactical|Legacy", meta = (DeprecatedFunction, DeprecationMessage = "Elimination countdown was removed"))
	void BP_HideTeamEliminationRoundEndCountdown();

	UFUNCTION(BlueprintImplementableEvent, Category = "Tactical|Legacy", meta = (DeprecatedFunction, DeprecationMessage = "Overtime was removed"))
	void BP_ShowOvertimeWarning();

	UFUNCTION(BlueprintImplementableEvent, Category = "Tactical|Legacy", meta = (DeprecatedFunction, DeprecationMessage = "Use hunt intel pulse events"))
	void BP_ShowWallhackEffect(bool bShow);

	/** Shows whether late-round hunt intel is active and whether it has escalated. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactical|Hunt Intel", meta = (DisplayName = "Set Hunt Intel State"))
	void BP_SetHuntIntelState(bool bActive, bool bEscalated);

	/** Renders one pulse of approximate enemy positions. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactical|Hunt Intel", meta = (DisplayName = "Show Hunt Intel Pulse"))
	void BP_ShowHuntIntelPulse(const TArray<FVector>& ApproximateEnemyLocations, bool bEscalated);

	/** Clears all pulse markers when combat or the round ends. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactical|Hunt Intel", meta = (DisplayName = "Clear Hunt Intel"))
	void BP_ClearHuntIntel();

	/** Legacy competitive result hook. It is not called for technical no-result endings. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactical", meta = (DisplayName = "Show Match Result"))
	void BP_ShowMatchResult(uint8 WinningTeam, int32 TeamAScore, int32 TeamBScore);

	/** Optional additive hook; keeps the existing match-result Blueprint signature compatible. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactical", meta = (DisplayName = "Show Match End Reason"))
	void BP_ShowMatchEndReason(uint8 WinningTeam, EMatchEndReason Reason);

	/** Update armor HUD */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactical", meta = (DisplayName = "Update Armor HUD"))
	void BP_UpdateArmorHUD(EArmorType Type, float DurabilityPercent);

	/** Update alive count per team */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactical", meta = (DisplayName = "Update Alive Count"))
	void BP_UpdateAliveCount(int32 TeamACount, int32 TeamBCount);

	/** Update the avatar lists for both teams. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactical", meta = (DisplayName = "Update Team Avatars"))
	void BP_UpdateTeamAvatars(const TArray<FTeamAvatarStatus>& TeamAPlayers, const TArray<FTeamAvatarStatus>& TeamBPlayers);

	/** Show speed boost timer */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactical", meta = (DisplayName = "Show Speed Boost Timer"))
	void BP_ShowSpeedBoostTimer(float TimeRemaining);

	/** Spectator camera switched to a new teammate */
	UFUNCTION(BlueprintImplementableEvent, Category = "Tactical", meta = (DisplayName = "On Spectator Switch"))
	void BP_OnSpectatorSwitch(int32 SpectatorIndex, int32 TotalTargets, APawn* TargetPawn);
};
