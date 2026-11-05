// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Economy/EconomyTypes.h"
#include "TimerManager.h"
#include "TacticalGameMode.generated.h"

/** Saved player state for disconnect/reconnect */
USTRUCT()
struct FPlayerDisconnectState
{
	GENERATED_BODY()

	/** Exact roster generation that owns this compatibility snapshot. */
	UPROPERTY()
	FGuid ParticipantId;

	UPROPERTY()
	int32 Money = 0;

	UPROPERTY()
	EArmorType ArmorType = EArmorType::None;

	UPROPERTY()
	float ArmorDurability = 0.0f;

	UPROPERTY()
	int32 LossStreak = 0;

	UPROPERTY()
	int32 RoundsWon = 0;

	UPROPERTY()
	uint8 TeamByte = 255;

	UPROPERTY()
	EWeaponTier PrimaryTier = EWeaponTier::DefaultPistol;

	UPROPERTY()
	EWeaponTier PistolTier = EWeaponTier::DefaultPistol;

	/** Round still owed one settlement. Zero means no pending settlement. */
	UPROPERTY()
	int32 PendingSettlementRound = 0;

	/** Diagnostic label only; reconnect identity remains keyed by UniqueNetId. */
	UPROPERTY()
	FString PlayerName;
};

class ATacticalGameState;
class ATacticalPlayerState;
class ATacticalCharacter;
class ATreasureChest;
class AShooterNPC;
class ATacticalReadyTarget;
class ATacticalSpawnPoint;
enum class ETacticalSpawnGroup : uint8;
class APlayerStart;
class ATacticalDisconnectedController;
class ATacticalPlayerController;
class APawn;
class UTacticalMatchRosterSubsystem;
struct FTacticalMatchParticipantRecord;

/** Immutable ownership snapshot for one reconnect handoff in progress. */
struct FPendingRosterReconnectTransaction
{
	FString IdentityKey;
	FGuid ParticipantId;
	uint8 TeamByte = 255;
	bool bRosterSlotReserved = false;
	bool bRetainedBodyState = false;
	int32 EnteredRound = 0;
	int32 LastSettledRound = 0;
	float ReconnectDeadlineServerTime = 0.0f;
	FRotator RetainedControlRotation = FRotator::ZeroRotator;
	TWeakObjectPtr<ATacticalPlayerState> PreservedState;
	TWeakObjectPtr<ATacticalDisconnectedController> DisconnectedController;
	TWeakObjectPtr<ATacticalCharacter> RetainedPawn;
	TWeakObjectPtr<APawn> BootstrapPawn;
};

/** Reconnect proof bound to the exact participant generation it validated. */
struct FValidatedRosterReconnectAdmission
{
	FString IdentityKey;
	FGuid ParticipantId;
};

/**
 *  Server-authoritative GameMode for tactical FPS.
 *  Manages round flow, economy, team assignment, and victory conditions.
 *
 *  Inherits from AGameModeBase directly (not AShooterGameMode) because the
 *  deathmatch KillGoal / instant-respawn loop in AShooterGameMode is incompatible
 *  with tactical round-based flow. Both share ATcamp_FPSGameMode as the common
 *  ancestor; kill events are routed through ATacticalGameState delegates instead.
 */
UCLASS()
class TCAMP_FPS_API ATacticalGameMode : public AGameModeBase
{
	GENERATED_BODY()

protected:
	// ---- Duration Configuration ----

	UPROPERTY(EditAnywhere, Category = "Tactical|Timing", meta = (ClampMin = "1.0", ClampMax = "300.0"))
	float BuyPhaseDuration = 10.0f;

	UPROPERTY(EditAnywhere, Category = "Tactical|Timing", meta = (ClampMin = "1.0", ClampMax = "600.0"))
	float CombatPhaseDuration = 105.0f;

	UPROPERTY(EditAnywhere, Category = "Tactical|Timing", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float RoundEndDuration = 6.0f;

	UPROPERTY(EditAnywhere, Category = "Tactical|Timing", meta = (ClampMin = "1.0", ClampMax = "60.0"))
	float MatchEndDuration = 15.0f;

	/** Public reconnect-status countdown; a living in-round Pawn outlives this timer. */
	UPROPERTY(EditAnywhere, Category = "Tactical|Network", meta = (ClampMin = "5.0", ClampMax = "120.0"))
	float ReconnectGraceDuration = 30.0f;

	// ---- Gameplay Configuration ----


	UPROPERTY(EditAnywhere, Category = "Tactical|Match")
	bool bAutoStartMatch = true;

	/** At least two lobby players are required; every candidate must select a team and be ready. */
	UPROPERTY(EditAnywhere, Category = "Tactical|Match", meta = (ClampMin = "2", ClampMax = "6"))
	int32 MinPlayersToStart = 2;

	/** Enables the accepted fixed-layout treasure objective. */
	UPROPERTY(EditAnywhere, Category = "Tactical|Treasure")
	bool bTreasureEnabled = true;

	// ---- Runtime State ----

	UPROPERTY()
	TObjectPtr<ATacticalGameState> TacticalGS;

	UPROPERTY(Transient)
	TObjectPtr<UTacticalMatchRosterSubsystem> RosterSubsystem;

	/**
	 * Currently connected team controllers, including valid reconnect spectators.
	 * Offline reserved slots live in the roster subsystem, not these arrays.
	 */
	UPROPERTY()
	TArray<TObjectPtr<APlayerController>> TeamAPlayers;

	UPROPERTY()
	TArray<TObjectPtr<APlayerController>> TeamBPlayers;

	/** Runtime-spawned wall targets used to ready players without releasing mouse capture. */
	UPROPERTY()
	TArray<TObjectPtr<ATacticalReadyTarget>> ReadyTargets;

	/** Fallback starts for maps that do not yet provide separate RED/BLUE spawns. */
	UPROPERTY()
	TArray<TObjectPtr<APlayerStart>> RuntimeTeamStarts;

	/** Editable one-slot markers; when present, all twelve slots are required. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ATacticalSpawnPoint>> PlacedSpawnPoints;
	bool bUsePlacedSpawnPoints = false;
	bool bPlacedSpawnLayoutValid = false;
	TMap<TWeakObjectPtr<AController>, int32> LobbySlotAssignments;

	/** Current round number (1-based) */
	int32 CurrentRound = 0;

	bool bMatchStarted = false;
	/** Fail-closed admission fence set before the listen host begins shutdown. */
	bool bHostShutdownInProgress = false;

	FTimerHandle TeamEliminationRoundEndTimer;
	FTimerHandle MatchRestartTimer;

	bool bTeamEliminationRoundEndPending = false;

	/** Teams that qualified when the match started; an empty qualified roster is eliminated, not ignored. */
	bool bTeamQualifiedForMatch[2] = { false, false };

	float HuntPulseCountdown = 0.0f;
	bool bHuntIntelStarted = false;
	bool bHuntIntelEscalated = false;
	uint32 HuntPulseSequence = 0;

	/** Single placed chest; its fixed location stays private until announcement. */
	UPROPERTY()
	TObjectPtr<ATreasureChest> SelectedTreasureChest;

	FTransform SelectedTreasureTransform = FTransform::Identity;
	uint8 SelectedTreasureSiteIndex = 255;
	float TreasureActivationServerTime = 0.0f;
	bool bTreasureAnnounced = false;
	bool bTreasureActivated = false;

	/** Belt-and-suspenders idempotency fences around the chest's own commit flag. */
	int32 TreasureCommittedRound = INDEX_NONE;
	TSet<FGuid> TreasureRewardedParticipantIds;
	TSet<TWeakObjectPtr<ATacticalPlayerState>> TreasureRewardedFallbackStates;

public:
	ATacticalGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void PreLogin(const FString& Options, const FString& Address,
		const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual FString InitNewPlayer(APlayerController* NewPlayerController,
		const FUniqueNetIdRepl& UniqueId, const FString& Options,
		const FString& Portal = TEXT("")) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual bool ReadyToStartMatch_Implementation();
	virtual bool PlayerCanRestart_Implementation(APlayerController* Player) override;

	/** Called before APlayerController destroys its Pawn during a network disconnect. */
	bool ShouldRetainPawnOnDisconnect(
		const ATacticalPlayerController* PlayerController,
		const FTacticalMatchParticipantRecord* KnownRecord = nullptr) const;

	/** Re-evaluates the lobby gate after a player changes their ready state. */
	void NotifyPlayerReadyStateChanged();

	/** Selects (or switches) a lobby player's team and marks them ready in one authoritative operation. */
	bool TrySelectTeamAndReady(APlayerController* PlayerController, uint8 DesiredTeam);

	/** Commits a voluntary online departure without granting disconnect grace. */
	void HandleExplicitLeave(ATacticalPlayerController* PlayerController);
	/** Terminates a listen-hosted Session as HostLost without awarding a result. */
	void HandleHostShutdown();
	void HandleReconnectAdmissionAcknowledgement(
		ATacticalPlayerController* PlayerController,
		const FString& MatchId,
		const FString& ReconnectCredential);

	/** Called when a player is killed */
	UFUNCTION()
	void OnPlayerKilled(AController* Killer, AController* Victim);

	/** Called when an NPC is killed */
	UFUNCTION()
	void OnNPCKilled(AController* Killer, AShooterNPC* Victim);


	/** Returns the current round phase */
	UFUNCTION(BlueprintPure, Category = "Tactical")
	ERoundPhase GetCurrentPhase() const;

	/** Returns the tactical game state */
	UFUNCTION(BlueprintPure, Category = "Tactical")
	ATacticalGameState* GetTacticalGameState() const { return TacticalGS.Get(); }

	/** Called by the selected chest whenever guards/claim/loot state changes. */
	void NotifyTreasureChestStateChanged(ATreasureChest* Chest);

	/** Commits one 500-money award per eligible team participant for this round. */
	int32 CommitTreasureTeamReward(uint8 Team);


protected:
	// ---- Round Flow ----

	void StartMatch();
	void TryStartMatch();
	void StartRound(int32 RoundNumber);
	void EndRound(uint8 WinningTeam, ERoundEndReason Reason);
	void EndRoundDraw(ERoundEndReason Reason);
	void EndMatch(uint8 WinningTeam, EMatchEndReason Reason);
	void SetPhase(ERoundPhase NewPhase);
	void OnPhaseTimerExpired();
	void ScheduleTeamEliminationRoundEnd();
	void FinishTeamEliminationRoundEnd();
	void ClearTeamEliminationRoundEnd();
	void TickHuntIntel(float DeltaSeconds);
	void ResetHuntIntel();
	void BroadcastHuntIntelPulse(bool bEscalated);
	void SelectTreasureForRound();
	void TickTreasureTimeline();
	void DeactivateTreasureForRound();
	void PublishTreasureState(
		ETreasureRoundState State,
		uint8 RemainingGuards = 0,
		const ATacticalPlayerState* ClaimingPlayer = nullptr,
		uint8 ClaimedTeam = 255);

	// ---- Victory Conditions ----

	void CheckVictoryConditions();
	bool EvaluateTeamEliminated(uint8& OutWinningTeam) const;
	bool IsTeamEliminatedForRound(uint8 Team) const;

	// ---- Economy ----

	void AwardRoundEconomy(uint8 WinningTeam);
	void AwardDrawEconomy();
	void InitializeMatchEconomy();

	// ---- Teams ----

	void RemoveFromTeam(AController* Exiting);
	ATacticalPlayerState* GetTacticalPS(AController* Controller) const;

	// ---- Player State ----

	void FreezeAllPlayers(bool bFreeze);
	void RespawnAllPlayers();

	// ---- Helpers ----

	int32 CountPlayersAlive(uint8 Team) const;
	int32 CountLobbyCandidates() const;
	bool AreAllLobbyPlayersSelectedAndReady() const;
	void LogFlowSnapshot(const TCHAR* Event) const;
	void ValidateTacticalState(const TCHAR* Context) const;
	void EnsureReadyTargets();
	void EnsureTeamPlayerStarts();
	void CacheAndValidatePlacedSpawnPoints();
	ATacticalSpawnPoint* FindPlacedSpawnPoint(ETacticalSpawnGroup Group, int32 SlotIndex) const;
	void MovePlayerToAssignedStart(APlayerController* PlayerController, uint8 Team);
	void MovePlayerToSpectator(APlayerController* PlayerController, bool bCanJoinNextRound);
	void InitializeParticipantPawn(APlayerController* PlayerController, uint8 Team, bool bResetVitals);
	int32 CountReservedTeamSlots(uint8 Team) const;

	// ---- Disconnect Preservation ----

	/** Saved states keyed by player unique net ID string */
	TMap<FString, FPlayerDisconnectState> DisconnectedPlayers;

	void SavePlayerState(ATacticalPlayerState* TPS, const FString& IdentityOverride = FString());
	bool SavePlayerStateForParticipantGeneration(
		ATacticalPlayerState* TPS,
		const FString& IdentityKey,
		const FGuid& ExpectedParticipantId);
	void RestorePlayerState(ATacticalPlayerState* TPS, const FPlayerDisconnectState& State);
	FString ResolveReconnectIdentity(const APlayerController* PlayerController) const;
	FString ResolveReconnectIdentity(const FUniqueNetIdRepl& UniqueId,
		const FString& Options) const;
	bool ValidateAdmissionOptions(
		const FString& Options,
		const FUniqueNetIdRepl& UniqueId,
		FString& OutErrorMessage,
		FString* OutIdentityKey = nullptr,
		bool* bOutIsReconnect = nullptr,
		FGuid* OutParticipantId = nullptr) const;
	void SendReconnectAdmission(
		APlayerController* PlayerController,
		FTacticalMatchParticipantRecord& Record,
		bool bRotateCredential,
		const FString& CredentialKnownByClient = FString());
	bool PrepareRosterParticipantReconnect(
		APlayerController* NewPlayer,
		const FString& IdentityKey,
		const FGuid& ExpectedParticipantId);
	bool TryRestoreRosterParticipant(APlayerController* NewPlayer, const FString& IdentityKey);
	void ServiceDisconnectedParticipants();
	void MarkDisconnectedParticipantDead(AController* VictimController);
	void FinalizeDisconnectedParticipantsAtRoundBoundary();
	void ReleaseExpiredLobbyParticipant(
		const FString& IdentityKey,
		const FGuid& ExpectedParticipantId = FGuid());

#if WITH_EDITOR
	/** Explicit PIE-only identity supplied as ?TacticalPIEReconnectId=... for end-to-end tests. */
	TMap<TWeakObjectPtr<APlayerController>, FString> EditorReconnectIdentityOverrides;
#endif

	/** Generation-bound handoffs whose preserved PlayerState awaits PostLogin completion. */
	TMap<TWeakObjectPtr<APlayerController>, FPendingRosterReconnectTransaction>
		PendingRosterReconnects;

	/** Reconnect claims validated from the same InitNewPlayer option envelope. */
	TMap<TWeakObjectPtr<APlayerController>, FValidatedRosterReconnectAdmission>
		ValidatedReconnectAdmissions;
	/** Credential presented by each validated reconnect, kept server-only through commit. */
	TMap<TWeakObjectPtr<APlayerController>, FString> ValidatedReconnectCredentials;

	/** Server-originated identity for provider-neutral guest connections. */
	TMap<TWeakObjectPtr<APlayerController>, FString> GuestIdentityOverrides;

	/** Controllers whose next Logout is an intentional abandonment, not a network loss. */
	TSet<TWeakObjectPtr<APlayerController>> ExplicitlyLeavingPlayers;
};
