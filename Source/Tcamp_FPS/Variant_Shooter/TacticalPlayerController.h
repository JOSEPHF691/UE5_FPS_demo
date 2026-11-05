// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterPlayerController.h"
#include "Economy/EconomyTypes.h"
#include "UI/SpectatorStatusTypes.h"
#include "Components/SlateWrapperTypes.h"
#include "TacticalPlayerController.generated.h"

class UBuyMenuUI;
class UTacticalUI;
class UReadyLobbyUI;
class UArmorStatusUI;
class USpectatorStatusUI;
class UTacticalRosterStatusUI;
class UMatchResultStatusUI;
class UHuntIntelStatusUI;
class UTreasureStatusUI;
class UHealthComponent;
class UArmorComponent;
class UWeaponInventoryComponent;
class UAnimInstance;
class USkeletalMesh;
class ATacticalPlayerState;
class ATacticalGameState;
class ATacticalCharacter;
class AShooterWeapon;
class ATacticalSpectatorAnimationController;
class UDataTable;
struct FWeaponTierRow;
struct FArmorDataRow;

namespace TacticalSpectatorAcquisition
{
	/** Fast retries cover the short window between the spectator RPC and roster/pawn replication. */
	inline constexpr float RetryIntervalSeconds = 0.15f;
	inline constexpr int32 MaxRetryAttempts = 12;

	/**
	 * An empty client-side target list is not authoritative while the initial
	 * acquisition is running or the owning PlayerState has no replicated team.
	 */
	inline constexpr bool ShouldDeferEmptyTarget(bool bAcquisitionInProgress, uint8 ViewerTeam)
	{
		return bAcquisitionInProgress || ViewerTeam > 1;
	}
}

/**
 *  PlayerController for the tactical FPS.
 *  Manages buy menu UI, authoritative purchase RPCs, hunt intel, and teammate-only spectating.
 */
UCLASS(abstract, config = "Game")
class TCAMP_FPS_API ATacticalPlayerController : public AShooterPlayerController
{
	GENERATED_BODY()

protected:
	// ---- UI ----

	UPROPERTY(EditAnywhere, Category = "Tactical|UI")
	TSubclassOf<UBuyMenuUI> BuyMenuUIClass;

	UPROPERTY(EditAnywhere, Category = "Tactical|UI")
	TSubclassOf<UTacticalUI> TacticalUIClass;

	UPROPERTY(EditAnywhere, Category = "Tactical|Weapons")
	TMap<EWeaponTier, TSubclassOf<AShooterWeapon>> WeaponTierMap;

	UPROPERTY(EditAnywhere, Category = "Tactical|Weapons")
	TObjectPtr<UDataTable> WeaponDataTable;

	UPROPERTY(EditAnywhere, Category = "Tactical|Armor")
	TObjectPtr<UDataTable> ArmorDataTable;

	UPROPERTY()
	TObjectPtr<UBuyMenuUI> BuyMenuUI;

	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UTacticalUI> TacticalUI;

	/** Native fallback lobby UI so readiness is usable without extra Blueprint wiring. */
	UPROPERTY()
	TObjectPtr<UReadyLobbyUI> ReadyLobbyUI;

	/** Native armor HUD fallback; does not require UI_Tactical Blueprint wiring. */
	UPROPERTY()
	TObjectPtr<UArmorStatusUI> ArmorStatusUI;

	/** Native spectator overlay; it is populated exclusively by the validated server snapshot. */
	UPROPERTY()
	TObjectPtr<USpectatorStatusUI> SpectatorStatusUI;

	/** Native Tab scoreboard for team, life, and public connection state. */
	UPROPERTY()
	TObjectPtr<UTacticalRosterStatusUI> RosterStatusUI;

	/** Native additive banner for disconnect-forfeit and technical-abort results. */
	UPROPERTY()
	TObjectPtr<UMatchResultStatusUI> MatchResultStatusUI;

	/** Native warning and approximate world-marker presentation for hunt intel. */
	UPROPERTY()
	TObjectPtr<UHuntIntelStatusUI> HuntIntelStatusUI;

	/** Native replicated treasure timeline, world marker, and claim progress. */
	UPROPERTY()
	TObjectPtr<UTreasureStatusUI> TreasureStatusUI;

	// ---- Spectator ----

	/** Delay between spectator camera switches */
	UPROPERTY(EditAnywhere, Category = "Tactical|Spectator", meta = (ClampMin = 0.5f, ClampMax = 10.0f))
	float SpectatorSwitchDelay = 3.0f;

	/** Current spectator target index */
	int32 SpectatorIndex = -1;

	/** Cached replicated teammate pawns for spectator cycling. */
	UPROPERTY()
	TArray<TObjectPtr<APawn>> SpectatorTargets;

	FTimerHandle SpectatorSwitchTimer;
	FTimerHandle SpectatorAcquisitionTimer;
	bool bSpectatorInputLockApplied = false;
	bool bSpectatorAcquisitionInProgress = false;
	int32 SpectatorAcquisitionAttempts = 0;

	/** Server-authored observer state. PlayerControllers replicate only to their owning connection. */
	UPROPERTY(ReplicatedUsing = OnRep_SpectatorHudSnapshot)
	FSpectatorHudSnapshot SpectatorHudSnapshot;
	int32 LastAppliedSpectatorSnapshotRevision = INDEX_NONE;
	int32 PendingSpectatorTargetPlayerId = INDEX_NONE;

	/** Target whose combat delegates are currently observed on the authority. */
	UPROPERTY(Transient)
	TObjectPtr<ATacticalCharacter> ServerSpectatorTarget;

	/** Target receiving the reversible first-person presentation override in this local world. */
	UPROPERTY(Transient)
	TObjectPtr<ATacticalCharacter> LocalSpectatorPresentationTarget;

	UPROPERTY(Transient)
	TObjectPtr<AShooterWeapon> LocalSpectatorPresentationWeapon;

	/** Exact pre-spectator widget visibility is restored on respawn/round transition. */
	bool bPlayerHudVisibilityCached = false;
	ESlateVisibility CachedBulletCounterVisibility = ESlateVisibility::Visible;
	ESlateVisibility CachedArmorStatusVisibility = ESlateVisibility::Visible;

	/** Reversible local first-person presentation state for the observed pawn. */
	bool bCachedPresentationTargetTick = false;
	bool bCachedPresentationArmsOnlyOwnerSee = true;
	bool bCachedPresentationArmsHidden = false;
	bool bCachedPresentationArmsVisible = true;
	uint8 CachedPresentationArmsTickOption = 0;
	bool bCachedPresentationBodyHidden = false;
	bool bCachedPresentationBodyVisible = true;
	UPROPERTY(Transient)
	TSubclassOf<UAnimInstance> CachedPresentationArmsAnimClass;
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMesh> CachedPresentationArmsMesh;

	/** Local animation-only controller used inside the remote first-person mesh tick. */
	UPROPERTY(Transient)
	TObjectPtr<ATacticalSpectatorAnimationController> LocalSpectatorAnimationController;

	/** Reversible local first-person presentation state for the observed weapon. */
	bool bCachedPresentationWeaponTick = false;
	bool bCachedPresentationWeaponOnlyOwnerSee = true;
	bool bCachedPresentationWeaponHidden = false;
	bool bCachedPresentationWeaponVisible = true;
	UPROPERTY(Transient)
	TSubclassOf<UAnimInstance> CachedPresentationWeaponAnimClass;

	float UIStateRefreshAccumulator = 0.0f;
	static constexpr float UIStateRefreshInterval = 0.2f;
	ERoundPhase LastDisplayedPhase = ERoundPhase::PreRound;
	int32 LastDisplayedPhaseSecond = INDEX_NONE;
	int32 LastReadyPlayers = INDEX_NONE;
	int32 LastAssignedPlayers = INDEX_NONE;
	int32 LastLobbyPlayers = INDEX_NONE;
	int32 LastLobbyTeamAPlayers = INDEX_NONE;
	int32 LastLobbyTeamBPlayers = INDEX_NONE;
	int32 LastDisplayedRound = INDEX_NONE;
	int32 LastDisplayedMaxRounds = INDEX_NONE;
	int32 LastAliveTeamA = INDEX_NONE;
	int32 LastAliveTeamB = INDEX_NONE;
	uint32 LastAvatarSignature = 0;
	bool bHasAvatarCache = false;
	bool bLastLocalReady = false;
	bool bHasReadyDisplayCache = false;
	bool bLastActiveParticipant = false;
	bool bLastReadyPromptVisible = false;
	uint8 LastDisplayedTeam = 255;
	bool bHasLoggedObservedPhase = false;
	ERoundPhase LastLoggedObservedPhase = ERoundPhase::PreRound;
	uint8 LastHandledRoundEndSerial = 0;
	uint8 LastHandledMatchEndSerial = 0;
	TWeakObjectPtr<ATacticalGameState> BoundTacticalGameState;
	int32 LastAcceptedHuntPulseRound = INDEX_NONE;
	uint32 LastAcceptedHuntPulseSequence = 0;
	bool bHuntPulseReceptionClosed = true;

public:
	ATacticalPlayerController();

	/** Development helpers routed through this exact local PIE player. */
	UFUNCTION(Exec)
	void TacticalPauseAutoReconnect();

	UFUNCTION(Exec)
	void TacticalResumeAutoReconnect();

	/** Atomically pauses auto reconnect for this client and disconnects GameNetDriver. */
	UFUNCTION(Exec)
	void TacticalTestDisconnect();

	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void AcknowledgePossession(APawn* InPawn) override;
	virtual void OnRep_PlayerState() override;
	virtual void SetupInputComponent() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void ResetIgnoreInputFlags() override;
	virtual void PawnLeavingGame() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Explicit online departure; the authority marks the participant abandoned. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Tactical|Network")
	void ServerLeaveMatch();

	/** Private admission material, delivered only over this controller's owner channel. */
	UFUNCTION(Client, Reliable)
	void ClientReceiveReconnectAdmission(
		const FString& MatchId,
		const FString& ReconnectCredential,
		int32 ProtocolVersion);

	/** Confirms that the owning client persisted the most recently issued bearer. */
	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeReconnectAdmission(
		const FString& MatchId,
		const FString& ReconnectCredential);

	/** Acknowledges that the authority committed the explicit departure. */
	UFUNCTION(Client, Reliable)
	void ClientCompleteExplicitLeave();

	/** Graceful listen-host shutdown: suppress reconnect and return to frontend. */
	UFUNCTION(Client, Reliable)
	void ClientHostSessionEnded();

	/** Shared Tab-scoreboard entry point for gameplay and focused UMG input. */
	void SetRosterScoreboardVisible(bool bVisible);
	void ToggleOnlineMenu();

	// ---- Buy Menu ----

	void ShowBuyMenu(bool bShow);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Tactical")
	void ServerRequestPurchaseWeapon(EWeaponTier Tier);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Tactical")
	void ServerRequestPurchaseArmor(EArmorType Type);

	UFUNCTION(Client, Reliable)
	void ClientPurchaseResult(bool bSuccess, const FText& Message);

	/** Receives one server-authored approximate enemy-location pulse. */
	UFUNCTION(Client, Unreliable)
	void ClientReceiveHuntIntelPulse(
		const TArray<FVector>& ApproximateEnemyLocations,
		bool bEscalated,
		int32 SourceRoundNumber,
		uint32 PulseSequence);

	/** Removes markers after a death and fences delayed pulses from that moment. */
	UFUNCTION(Client, Reliable)
	void ClientClearHuntIntelMarkers(int32 SourceRoundNumber, uint32 ClearedPulseSequence);

	/** Authority-only eligibility gate for alive players and validated teammate spectators. */
	bool TryGetHuntIntelViewingTeam(uint8& OutViewingTeam) const;

	UFUNCTION(BlueprintPure, Category = "Tactical|Weapons")
	int32 GetWeaponCostForTier(EWeaponTier Tier) const;

	UFUNCTION(BlueprintPure, Category = "Tactical|Weapons")
	bool GetWeaponDataForTier(EWeaponTier Tier, FWeaponTierRow& OutData) const;

	TSubclassOf<AShooterWeapon> GetWeaponClassForTier(EWeaponTier Tier) const;

	UFUNCTION(BlueprintPure, Category = "Tactical|Armor")
	bool GetArmorDataForType(EArmorType Type, FArmorDataRow& OutData) const;

	UFUNCTION(BlueprintCallable, Category = "Tactical|UI")
	void CloseBuyMenu();

	// ---- Spectator ----

	/** Start spectating teammates after pawn death */
	void StartSpectating();

	/** Enter the project's teammate-only spectator policy on the owning client. */
	UFUNCTION(Client, Reliable)
	void ClientStartRestrictedSpectating();

	/** Called by the controlled tactical character when it enters death state */
	void HandleControlledPawnDeath(APawn* DeadPawn);

	/** Switch to the next teammate camera */
	UFUNCTION(BlueprintCallable, Category = "Spectator")
	void SpectateNextTeammate();

	/** Stop spectating (called on respawn) */
	void StopSpectating();

	/** True only for the weapon currently exposed to this local first-person spectator. */
	bool IsLocallyPresentingWeaponFirstPerson(const AShooterWeapon* Weapon) const;

protected:
	UFUNCTION()
	void OnPhaseChanged(ERoundPhase NewPhase, float Duration);

	UFUNCTION()
	void OnMoneyUpdated(int32 NewMoney);

	void BindMoneyUpdates();
	void BindTacticalGameStateEvents(bool bForcePresentationCatchUp = false);
	void UnbindTacticalGameStateEvents();
	/** Hides combat HUD layers during team-selection/readiness, while keeping the lobby overlay visible. */
	void ApplyPhaseDrivenHudVisibility(ERoundPhase Phase);

	void BindArmorUpdates();

	UFUNCTION()
	void OnArmorChanged(EArmorType Type, float DurabilityPercent);

	UFUNCTION()
	void OnMatchEnded(uint8 WinningTeam);

	UFUNCTION()
	void OnRoundEnded(uint8 WinningTeam, int32 TeamAScore, int32 TeamBScore, ERoundEndReason Reason);

	UFUNCTION()
	void OnHuntIntelStateChanged(bool bActive, bool bEscalated);

	UFUNCTION()
	void OnTreasureStateChanged();
	void ClearHuntIntelPulsePresentation();
	void ClearAllHuntIntelPresentation();

	/** Rebuild the list of live teammates to spectate */
	void RefreshSpectatorTargets();

	/** Quickly bridge initial/empty-target replication without changing normal 3-second cycling. */
	void BeginSpectatorTargetAcquisition();
	void RetrySpectatorTargetAcquisition();
	void StopSpectatorTargetAcquisition();
	bool TryAcquireSpectatorTarget();
	void HandleEmptySpectatorTargets();

	void SpectateTarget(int32 Index);
	void ApplySpectatorInputLock();

	/** Request observer data for a target; the server never trusts the client-side target list. */
	UFUNCTION(Server, Reliable)
	void ServerSetSpectatorTarget(APawn* RequestedTarget);

	UFUNCTION()
	void OnRep_SpectatorHudSnapshot();

	UFUNCTION()
	void OnServerSpectatedHealthChanged(float HealthPercent);

	UFUNCTION()
	void OnServerSpectatedHealthDepleted();

	UFUNCTION()
	void OnServerSpectatedArmorChanged(EArmorType Type, float DurabilityPercent);

	UFUNCTION()
	void OnServerSpectatedAmmoChanged(int32 CurrentAmmo, int32 MagazineSize, int32 ReserveAmmo);

	UFUNCTION()
	void OnServerSpectatedWeaponChanged(AShooterWeapon* NewWeapon, AShooterWeapon* OldWeapon);

	UFUNCTION()
	void OnServerSpectatedTargetDestroyed(AActor* DestroyedActor);

	UFUNCTION()
	void OnLocalSpectatedWeaponChanged(AShooterWeapon* NewWeapon, AShooterWeapon* OldWeapon);

	UFUNCTION()
	void OnLocalSpectatedTargetDestroyed(AActor* DestroyedActor);

	bool IsValidServerSpectatorTarget(const ATacticalCharacter* RequestedTarget) const;
	void BindServerSpectatorTarget(ATacticalCharacter* Target);
	void UnbindServerSpectatorTarget();
	void PublishServerSpectatorSnapshot();
	void ClearServerSpectatorSnapshot();
	void ApplySpectatorHudSnapshot();
	void CacheAndHidePlayerHud();
	void RestorePlayerHud();
	void BeginLocalSpectatorPresentation(ATacticalCharacter* Target);
	void EndLocalSpectatorPresentation();
	void ApplyLocalSpectatorWeaponPresentation(AShooterWeapon* Weapon);
	void RestoreLocalSpectatorWeaponPresentation();
	bool EnsureLocalSpectatorAnimationController(ATacticalCharacter* Target);
	void ReleaseLocalSpectatorAnimationController(ATacticalCharacter* Target);

	const FWeaponTierRow* FindWeaponData(EWeaponTier Tier) const;

	int32 GetWeaponCostFromData(EWeaponTier Tier) const;

	const FArmorDataRow* FindArmorData(EArmorType Type) const;

	bool CanPurchaseNow(FText& OutFailureMessage) const;
	void ApplyGameOnlyInputMode();
	void EnsureDefaultSidearm(APawn* InPawn);
	void ShowRosterScoreboard();
	void HideRosterScoreboard();
	void RefreshRosterPresentation(const ATacticalGameState* GameState);

	virtual bool CanPresentDamageFeedback() const override;
	virtual bool ShouldAutoRespawnPawn() const override { return false; }
};
