// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/GameInstance.h"
#include "Net/Core/Connection/NetEnums.h"
#include "TacticalGameInstance.generated.h"

class UNetDriver;
class UWorld;
class APlayerController;
class UReconnectStatusUI;
class UTacticalMultiplayerMenuUI;

/**
 * Game-instance lifetime owner for client reconnect attempts.
 *
 * The engine failure delegates are process-global, so every callback is
 * checked against this instance's FWorldContext before it can change state.
 */
UCLASS()
class TCAMP_FPS_API UTacticalGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;

	/** Cache an absolute remote server URL for a later reconnect attempt. */
	UFUNCTION(BlueprintCallable, Category = "Tactical|Network")
	bool CacheServerURL(const FString& ServerURL);

	/** Stop the current reconnect sequence. The cached server URL is retained. */
	UFUNCTION(BlueprintCallable, Category = "Tactical|Network")
	void CancelReconnect();

	/** Start a fresh bounded retry sequence after automatic attempts are exhausted. */
	UFUNCTION(BlueprintCallable, Category = "Tactical|Network")
	void ManualRetryReconnect();

	/** Per-client development switch used to hold a disconnected body offline for testing. */
	void SetAutomaticReconnectEnabledForTesting(bool bEnabled);

	/** Stop reconnecting, forget the server address, and return to the default map. */
	UFUNCTION(BlueprintCallable, Category = "Tactical|Network")
	void LeaveMatch();

	/** Native frontend/in-match menu entry points; no Blueprint setup is required. */
	void ShowMultiplayerMenu(bool bInMatch);
	void HideMultiplayerMenu();
	void ToggleMultiplayerMenu();
	void PrepareForOnlineTravel();
	bool IsLeaveMatchPending() const { return bLeaveMatchPending; }
	bool IsMultiplayerMenuBlocking() const
	{
		return bMultiplayerMenuInputBlockApplied;
	}
	/** Records phase/UI input state changes while the menu temporarily owns focus. */
	void SetPostMultiplayerMenuInputPreferences(
		bool bShowCursor,
		bool bEnableClick,
		bool bEnableMouseOver);
	/** Restores this menu's own stacked ignore-input contribution after ClientRestart. */
	void ReapplyMultiplayerMenuInputBlockAfterControllerReset(
		APlayerController* PlayerController);

	/** Terminal Steam Session/host-loss path. */
	void HandleOnlineSessionLost(const FText& UserFacingReason);
	void HandleHostSessionEnded();

	/** Owner-only admission material received from the authoritative controller. */
	bool CacheReconnectAdmission(
		const FString& MatchId,
		const FString& ReconnectCredential,
		int32 ProtocolVersion,
		ENetMode AdmissionSourceNetMode);

	/** Completes an acknowledged explicit leave without starting auto reconnect. */
	void CompleteExplicitLeave();

	UFUNCTION(BlueprintPure, Category = "Tactical|Network")
	bool IsReconnectActive() const { return bReconnectActive; }
	bool IsReconnectOverlayBlocking() const
	{
		return bReconnectActive || bReconnectActionsAvailable;
	}

	/** Rebuilds this system's stacked input lock after ClientRestart resets counters. */
	void ReapplyReconnectInputBlockAfterControllerReset(APlayerController* PlayerController);
	/** Records the mode gameplay UI wants restored after the blocking overlay closes. */
	void SetPostReconnectInputPreferences(
		bool bShowCursor,
		bool bEnableClick,
		bool bEnableMouseOver);

private:
	void HandleNetworkFailure(UWorld* FailureWorld, UNetDriver* NetDriver,
		ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* FailureWorld, ETravelFailure::Type FailureType,
		const FString& ErrorString);
	void HandlePreClientTravel(const FString& PendingURL, ETravelType TravelType,
		bool bIsSeamlessTravel);
	void HandlePostLoadMap(UWorld* LoadedWorld);

	bool IsClientFailureForThisInstance(UWorld* FailureWorld, UNetDriver* NetDriver) const;
	void BeginReconnectSequence(bool bManualRequest = false);
	void ScheduleNextReconnect();
	void ScheduleStaleConnectionRetry();
	bool HandleReconnectTicker(float DeltaTime);
	bool HandleReconnectConfirmationTicker(float DeltaTime);
	bool HandleReconnectOverlayGuardTicker(float DeltaTime);
	bool HandleExplicitLeaveFallbackTicker(float DeltaTime);
	void TryReconnect();
	void ResolveAndTravelReconnect(
		uint64 SequenceGeneration,
		uint64 AttemptGeneration,
		bool bSuccess,
		const FString& ConnectString);
	void IssueReconnectTravel(const FString& ReconnectURL, bool bPlatformResolved);
	void BeginAwaitingServerConfirmation();
	void CompleteReconnect();
	void FailReconnect(const TCHAR* Reason, const FText& UserFacingReason = FText::GetEmpty());
	void StopReconnectWork();
	void ResetReconnectState();
	FString BuildReconnectURL() const;
	void ClearReconnectAdmission();
	void BeginSessionCleanupAndReturn(const FText& FrontendNotice);
	void CompleteSessionCleanupAndReturn();
	bool HandleSessionCleanupFallbackTicker(float DeltaTime);
	bool HandleHostShutdownDelayTicker(float DeltaTime);
	bool HandleHostShutdownMaxWaitTicker(float DeltaTime);
	void HandleHostSessionClosingPrepared();
	void TryCompleteHostShutdown();

	void EnsureReconnectOverlay();
	void EnsureReconnectOverlayGuard();
	void ShowScheduledReconnect(float DelaySeconds);
	void ShowReconnectAttempt();
	void ShowAwaitingServerConfirmation();
	void ShowReconnectFailure(const FText& FailureReason, int32 AttemptsMade);
	void ApplyReconnectInputBlock();
	void RestoreInputAfterReconnectOverlay();
	FText GetDefaultFailureText(const TCHAR* Reason) const;
	FText GetNetworkFailureText(ENetworkFailure::Type FailureType, const FString& ErrorString) const;
	FText GetTravelFailureText(ETravelFailure::Type FailureType) const;

	FString CachedServerURL;
	/**
	 * Private, process-local admission state. Project code never logs these
	 * values. The current URL-option transport can still be printed by UE's
	 * internal travel logging, so it must be replaced by a platform ticket or
	 * custom login handshake before treating this as production secret storage.
	 */
	FString CachedMatchId;
	FString CachedReconnectCredential;
	int32 CachedAdmissionProtocolVersion = 0;
	int32 ReconnectAttemptsStarted = 0;
	/** Fences callbacks from a cancelled reconnect sequence and older attempts. */
	uint64 ReconnectSequenceGeneration = 0;
	uint64 ReconnectAttemptGeneration = 0;
	bool bReconnectActive = false;
	bool bReconnectAttemptInFlight = false;
	bool bSuppressAutomaticReconnect = false;
	bool bAutomaticReconnectEnabledForTesting = true;
	bool bReconnectActionsAvailable = false;
	/** Survives Cancel so a late Standalone fallback RPC cannot poison ManualRetry. */
	bool bReconnectAdmissionProtectionArmed = false;
	/** Set only by a fresh owner-only admission RPC during the active sequence. */
	bool bReconnectAdmissionConfirmed = false;
	double ConfirmationWaitStartedSeconds = 0.0;
	double ReconnectSequenceStartedSeconds = 0.0;

	UPROPERTY(Transient)
	TObjectPtr<UReconnectStatusUI> ReconnectOverlay;

	UPROPERTY(Transient)
	TObjectPtr<UTacticalMultiplayerMenuUI> MultiplayerMenu;

	TWeakObjectPtr<APlayerController> InputBlockedController;
	bool bReconnectInputBlockApplied = false;
	/** Original state of the controller that currently owns our stacked lock. */
	bool bSavedShowMouseCursor = false;
	bool bSavedEnableClickEvents = false;
	bool bSavedEnableMouseOverEvents = false;
	/** Gameplay/UI state to apply after the overlay closes; survives controller travel. */
	bool bHasPostReconnectInputPreferences = false;
	bool bPostReconnectShowMouseCursor = false;
	bool bPostReconnectEnableClickEvents = false;
	bool bPostReconnectEnableMouseOverEvents = false;
	TWeakObjectPtr<APlayerController> MultiplayerMenuInputBlockedController;
	bool bMultiplayerMenuInputBlockApplied = false;
	bool bMultiplayerMenuSavedShowMouseCursor = false;
	bool bMultiplayerMenuSavedEnableClickEvents = false;
	bool bMultiplayerMenuSavedEnableMouseOverEvents = false;
	bool bHasPostMultiplayerMenuInputPreferences = false;
	bool bPostMultiplayerMenuShowMouseCursor = false;
	bool bPostMultiplayerMenuEnableClickEvents = false;
	bool bPostMultiplayerMenuEnableMouseOverEvents = false;
	bool bSessionReconnectResolutionInFlight = false;
	bool bSessionCleanupReturnPending = false;
	bool bLeaveMatchPending = false;
	bool bHostShutdownPending = false;
	bool bHostShutdownDeliveryDelayElapsed = false;
	bool bHostSessionClosingPrepared = false;
	FText PendingFrontendNotice;

	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
	FDelegateHandle PreClientTravelHandle;
	FDelegateHandle PostLoadMapHandle;
	FTSTicker::FDelegateHandle ReconnectTickerHandle;
	FTSTicker::FDelegateHandle ReconnectConfirmationTickerHandle;
	FTSTicker::FDelegateHandle ReconnectOverlayGuardTickerHandle;
	FTSTicker::FDelegateHandle ExplicitLeaveFallbackTickerHandle;
	FTSTicker::FDelegateHandle SessionCleanupFallbackTickerHandle;
	FTSTicker::FDelegateHandle HostShutdownDelayTickerHandle;
	FTSTicker::FDelegateHandle HostShutdownMaxWaitTickerHandle;
};
