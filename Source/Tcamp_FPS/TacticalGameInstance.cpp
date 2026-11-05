// Copyright Epic Games, Inc. All Rights Reserved.

#include "TacticalGameInstance.h"

#include "Online/TacticalSessionSubsystem.h"
#include "UI/ReconnectStatusUI.h"
#include "Variant_Shooter/TacticalGameMode.h"
#include "Variant_Shooter/Network/TacticalNetworkAdmission.h"
#include "Variant_Shooter/TacticalDiagnostics.h"
#include "Variant_Shooter/TacticalPlayerController.h"
#include "Variant_Shooter/UI/TacticalMultiplayerMenuUI.h"
#include "Blueprint/GameViewportSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogTacticalReconnect, Log, All);

namespace TacticalReconnect
{
	constexpr float RetryDelays[] = { 0.0f, 1.0f, 2.0f, 4.0f, 8.0f };
	constexpr int32 MaxAttempts = UE_ARRAY_COUNT(RetryDelays);
	constexpr float StaleConnectionRetryDelay = 2.0f;
	constexpr double StaleConnectionRetryBudgetSeconds = 35.0;
	constexpr float ConfirmationPollInterval = 0.10f;
	constexpr double ConfirmationTimeoutSeconds = 15.0;
	constexpr int32 OverlayZOrder = 10000;
	constexpr float ExplicitLeaveAcknowledgementTimeout = 2.0f;
	constexpr float SessionCleanupTimeout = 3.0f;
	constexpr float HostShutdownDeliveryDelay = 0.35f;
	constexpr float HostShutdownMaximumPreparationWait = 2.0f;
}

#if !UE_BUILD_SHIPPING
namespace TacticalReconnect
{
	FAutoConsoleCommandWithWorldAndArgs SetAutomaticRetryCommand(
		TEXT("tactical.Reconnect.AutoRetry"),
		TEXT("Set automatic reconnect for this PIE/game client. ")
		TEXT("Usage: tactical.Reconnect.AutoRetry 0|1. Manual Retry always remains available."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (!World || Args.Num() != 1
					|| (Args[0] != TEXT("0") && Args[0] != TEXT("1")))
				{
					UE_LOG(LogTacticalReconnect, Warning,
						TEXT("Usage: tactical.Reconnect.AutoRetry 0|1"));
					return;
				}

				if (UTacticalGameInstance* GameInstance =
					Cast<UTacticalGameInstance>(World->GetGameInstance()))
				{
					GameInstance->SetAutomaticReconnectEnabledForTesting(
						Args[0] == TEXT("1"));
				}
			}),
		ECVF_Cheat);
}
#endif

void UTacticalGameInstance::Init()
{
	Super::Init();

	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(
			this, &UTacticalGameInstance::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(
			this, &UTacticalGameInstance::HandleTravelFailure);
	}

	PreClientTravelHandle = OnNotifyPreClientTravel().AddUObject(
		this, &UTacticalGameInstance::HandlePreClientTravel);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(
		this, &UTacticalGameInstance::HandlePostLoadMap);
}

void UTacticalGameInstance::Shutdown()
{
	ResetReconnectState();
	if (ExplicitLeaveFallbackTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(ExplicitLeaveFallbackTickerHandle);
		ExplicitLeaveFallbackTickerHandle.Reset();
	}
	if (SessionCleanupFallbackTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(SessionCleanupFallbackTickerHandle);
		SessionCleanupFallbackTickerHandle.Reset();
	}
	if (HostShutdownDelayTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(HostShutdownDelayTickerHandle);
		HostShutdownDelayTickerHandle.Reset();
	}
	if (HostShutdownMaxWaitTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(HostShutdownMaxWaitTickerHandle);
		HostShutdownMaxWaitTickerHandle.Reset();
	}
	HideMultiplayerMenu();
	if (ReconnectOverlay)
	{
		ReconnectOverlay->RemoveFromParent();
		ReconnectOverlay = nullptr;
	}

	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	OnNotifyPreClientTravel().Remove(PreClientTravelHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);

	NetworkFailureHandle.Reset();
	TravelFailureHandle.Reset();
	PreClientTravelHandle.Reset();
	PostLoadMapHandle.Reset();

	Super::Shutdown();
}

void UTacticalGameInstance::ShowMultiplayerMenu(bool bInMatch)
{
	if (IsReconnectOverlayBlocking() || bLeaveMatchPending)
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* PlayerController = World
		? GetFirstLocalPlayerController(World)
		: nullptr;
	if (!World || !PlayerController)
	{
		return;
	}

	if (!IsValid(MultiplayerMenu) || MultiplayerMenu->GetWorld() != World)
	{
		if (MultiplayerMenu)
		{
			MultiplayerMenu->RemoveFromParent();
		}
		MultiplayerMenu = CreateWidget<UTacticalMultiplayerMenuUI>(
			PlayerController, UTacticalMultiplayerMenuUI::StaticClass(),
			TEXT("TacticalMultiplayerMenu"));
	}
	if (!MultiplayerMenu)
	{
		return;
	}

	if (bMultiplayerMenuInputBlockApplied
		&& MultiplayerMenuInputBlockedController.Get() != PlayerController)
	{
		// A hard travel can replace the local controller while the GameInstance
		// survives. Release only the lock owned on the old controller before the
		// new controller becomes this menu's input owner.
		if (APlayerController* PreviousController =
			MultiplayerMenuInputBlockedController.Get())
		{
			PreviousController->SetIgnoreMoveInput(false);
			PreviousController->SetIgnoreLookInput(false);
			PreviousController->bShowMouseCursor =
				bMultiplayerMenuSavedShowMouseCursor;
			PreviousController->bEnableClickEvents =
				bMultiplayerMenuSavedEnableClickEvents;
			PreviousController->bEnableMouseOverEvents =
				bMultiplayerMenuSavedEnableMouseOverEvents;
		}
		MultiplayerMenuInputBlockedController.Reset();
		bMultiplayerMenuInputBlockApplied = false;
		bHasPostMultiplayerMenuInputPreferences = false;
	}

	MultiplayerMenu->InitializeMenu(bInMatch);
	if (!MultiplayerMenu->IsInViewport())
	{
		MultiplayerMenu->AddToViewport(9000);
	}
	if (!bMultiplayerMenuInputBlockApplied)
	{
		MultiplayerMenuInputBlockedController = PlayerController;
		bMultiplayerMenuSavedShowMouseCursor = PlayerController->bShowMouseCursor;
		bMultiplayerMenuSavedEnableClickEvents = PlayerController->bEnableClickEvents;
		bMultiplayerMenuSavedEnableMouseOverEvents =
			PlayerController->bEnableMouseOverEvents;
		bPostMultiplayerMenuShowMouseCursor =
			bMultiplayerMenuSavedShowMouseCursor;
		bPostMultiplayerMenuEnableClickEvents =
			bMultiplayerMenuSavedEnableClickEvents;
		bPostMultiplayerMenuEnableMouseOverEvents =
			bMultiplayerMenuSavedEnableMouseOverEvents;
		bHasPostMultiplayerMenuInputPreferences = true;
		PlayerController->SetIgnoreMoveInput(true);
		PlayerController->SetIgnoreLookInput(true);
		bMultiplayerMenuInputBlockApplied = true;
	}
	PlayerController->bShowMouseCursor = true;
	PlayerController->bEnableClickEvents = true;
	PlayerController->bEnableMouseOverEvents = true;
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(MultiplayerMenu->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerController->SetInputMode(InputMode);
}

void UTacticalGameInstance::HideMultiplayerMenu()
{
	if (MultiplayerMenu)
	{
		MultiplayerMenu->RemoveFromParent();
		MultiplayerMenu = nullptr;
	}

	APlayerController* PlayerController =
		MultiplayerMenuInputBlockedController.Get();
	if (PlayerController && bMultiplayerMenuInputBlockApplied)
	{
		PlayerController->SetIgnoreMoveInput(false);
		PlayerController->SetIgnoreLookInput(false);
	}
	if (PlayerController && !IsReconnectOverlayBlocking())
	{
		const bool bRestoreShowMouseCursor =
			bHasPostMultiplayerMenuInputPreferences
				? bPostMultiplayerMenuShowMouseCursor
				: bMultiplayerMenuSavedShowMouseCursor;
		const bool bRestoreClickEvents =
			bHasPostMultiplayerMenuInputPreferences
				? bPostMultiplayerMenuEnableClickEvents
				: bMultiplayerMenuSavedEnableClickEvents;
		const bool bRestoreMouseOverEvents =
			bHasPostMultiplayerMenuInputPreferences
				? bPostMultiplayerMenuEnableMouseOverEvents
				: bMultiplayerMenuSavedEnableMouseOverEvents;
		PlayerController->bShowMouseCursor = bRestoreShowMouseCursor;
		PlayerController->bEnableClickEvents = bRestoreClickEvents;
		PlayerController->bEnableMouseOverEvents = bRestoreMouseOverEvents;
		if (bRestoreShowMouseCursor || bRestoreClickEvents
			|| bRestoreMouseOverEvents)
		{
			FInputModeGameAndUI InputMode;
			InputMode.SetHideCursorDuringCapture(false);
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			PlayerController->SetInputMode(InputMode);
		}
		else
		{
			FInputModeGameOnly InputMode;
			InputMode.SetConsumeCaptureMouseDown(false);
			PlayerController->SetInputMode(InputMode);
		}
	}

	MultiplayerMenuInputBlockedController.Reset();
	bMultiplayerMenuInputBlockApplied = false;
	bMultiplayerMenuSavedShowMouseCursor = false;
	bMultiplayerMenuSavedEnableClickEvents = false;
	bMultiplayerMenuSavedEnableMouseOverEvents = false;
	bHasPostMultiplayerMenuInputPreferences = false;
	bPostMultiplayerMenuShowMouseCursor = false;
	bPostMultiplayerMenuEnableClickEvents = false;
	bPostMultiplayerMenuEnableMouseOverEvents = false;
}

void UTacticalGameInstance::SetPostMultiplayerMenuInputPreferences(
	bool bShowCursor,
	bool bEnableClick,
	bool bEnableMouseOver)
{
	if (!IsMultiplayerMenuBlocking())
	{
		return;
	}
	bPostMultiplayerMenuShowMouseCursor = bShowCursor;
	bPostMultiplayerMenuEnableClickEvents = bEnableClick;
	bPostMultiplayerMenuEnableMouseOverEvents = bEnableMouseOver;
	bHasPostMultiplayerMenuInputPreferences = true;
}

void UTacticalGameInstance::ReapplyMultiplayerMenuInputBlockAfterControllerReset(
	APlayerController* PlayerController)
{
	if (!PlayerController || !bMultiplayerMenuInputBlockApplied
		|| MultiplayerMenuInputBlockedController.Get() != PlayerController)
	{
		return;
	}

	// ResetIgnoreInputFlags clears every engine counter without notifying the
	// logical owners. Re-add exactly this menu's contribution even when another
	// owner (for example restricted spectating) already restored its own lock.
	PlayerController->SetIgnoreMoveInput(true);
	PlayerController->SetIgnoreLookInput(true);
}

void UTacticalGameInstance::ToggleMultiplayerMenu()
{
	if (IsReconnectOverlayBlocking() || bLeaveMatchPending)
	{
		return;
	}
	if (MultiplayerMenu && MultiplayerMenu->IsInViewport())
	{
		HideMultiplayerMenu();
	}
	else
	{
		ShowMultiplayerMenu(true);
	}
}

void UTacticalGameInstance::PrepareForOnlineTravel()
{
	HideMultiplayerMenu();
	bSuppressAutomaticReconnect = false;
}

bool UTacticalGameInstance::CacheServerURL(const FString& ServerURL)
{
	FURL ParsedURL(nullptr, *ServerURL, TRAVEL_Absolute);
	if (!ParsedURL.Valid || ParsedURL.Host.IsEmpty())
	{
		UE_LOG(LogTacticalReconnect, Warning,
			TEXT("Event=ReconnectURLRejected Reason=InvalidOrNonRemote"));
		return false;
	}

	// Credentials have a shorter lifetime than the transport address. Store
	// them only in dedicated private fields so a rotated token cannot survive in
	// the cached base URL.
	ParsedURL.RemoveOption(TacticalNetworkAdmission::MatchIdOption);
	ParsedURL.RemoveOption(TacticalNetworkAdmission::ReconnectCredentialOption);
	ParsedURL.RemoveOption(TacticalNetworkAdmission::ProtocolVersionOption);
	CachedServerURL = ParsedURL.ToString();
	UE_LOG(LogTacticalReconnect, Display,
		TEXT("Event=ReconnectURLCached Host=\"%s\" Port=%d"),
		*ParsedURL.Host, ParsedURL.Port);
	return true;
}

void UTacticalGameInstance::SetAutomaticReconnectEnabledForTesting(bool bEnabled)
{
	bAutomaticReconnectEnabledForTesting = bEnabled;
	const UWorld* World = GetWorld();
	UE_LOG(LogTacticalReconnect, Display,
		TEXT("Event=ReconnectAutomaticRetryTestSetting Enabled=%d World=\"%s\" NetMode=%s"),
		bEnabled,
		World ? *World->GetName() : TEXT("None"),
		TacticalDiagnostics::GetNetModeName(World ? World->GetNetMode() : NM_MAX));
}

void UTacticalGameInstance::CancelReconnect()
{
	const bool bHadReconnectWork = bReconnectActive
		|| bReconnectAttemptInFlight
		|| ReconnectTickerHandle.IsValid()
		|| ReconnectConfirmationTickerHandle.IsValid()
		|| ReconnectOverlayGuardTickerHandle.IsValid();
	const int32 AttemptsBeforeCancel = ReconnectAttemptsStarted;
	bSuppressAutomaticReconnect = true;
	ResetReconnectState();

	if (bHadReconnectWork)
	{
		UE_LOG(LogTacticalReconnect, Display,
			TEXT("Event=ReconnectCancelled Attempts=%d"), AttemptsBeforeCancel);
	}
}

void UTacticalGameInstance::ManualRetryReconnect()
{
	if (bReconnectActive)
	{
		return;
	}

	bSuppressAutomaticReconnect = false;
	ResetReconnectState();
	UE_LOG(LogTacticalReconnect, Display,
		TEXT("Event=ReconnectManualRetryRequested"));
	BeginReconnectSequence(true);
}

void UTacticalGameInstance::LeaveMatch()
{
	if (bLeaveMatchPending)
	{
		return;
	}
	bLeaveMatchPending = true;
	const int32 AttemptsBeforeLeave = ReconnectAttemptsStarted;
	bSuppressAutomaticReconnect = true;
	ResetReconnectState();
	CachedServerURL.Reset();
	ClearReconnectAdmission();

	UE_LOG(LogTacticalReconnect, Display,
		TEXT("Event=ReconnectLeaveMatchRequested Attempts=%d"), AttemptsBeforeLeave);

	UWorld* World = GetWorld();
	ATacticalPlayerController* TacticalController = World
		? Cast<ATacticalPlayerController>(GetFirstLocalPlayerController(World))
		: nullptr;
	const UNetConnection* ServerConnection = TacticalController
		? TacticalController->GetNetConnection()
		: nullptr;
	UTacticalSessionSubsystem* SessionSubsystem =
		GetSubsystem<UTacticalSessionSubsystem>();
	if (World && World->GetNetMode() == NM_ListenServer
		&& SessionSubsystem && SessionSubsystem->IsSessionHost())
	{
		if (ATacticalGameMode* TacticalMode =
			World->GetAuthGameMode<ATacticalGameMode>())
		{
			TacticalMode->HandleHostShutdown();
		}
		bHostShutdownPending = true;
		bHostShutdownDeliveryDelayElapsed = false;
		bHostSessionClosingPrepared = false;
		HostShutdownDelayTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(
				this, &ThisClass::HandleHostShutdownDelayTicker),
			TacticalReconnect::HostShutdownDeliveryDelay);
		HostShutdownMaxWaitTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(
				this, &ThisClass::HandleHostShutdownMaxWaitTicker),
			TacticalReconnect::HostShutdownMaximumPreparationWait);
		SessionSubsystem->PrepareHostSessionShutdown(
			FOnTacticalSessionCleanupComplete::CreateUObject(
				this, &ThisClass::HandleHostSessionClosingPrepared));
		return;
	}

	const bool bCanNotifyAuthority = World && TacticalController
		&& (World->GetNetMode() == NM_ListenServer
			|| (World->GetNetMode() == NM_Client && ServerConnection
				&& ServerConnection->GetConnectionState() == USOCK_Open));
	if (bCanNotifyAuthority)
	{
		// The server owns abandonment and elimination. Keep this client from
		// interpreting the intentional disconnect as a recoverable network loss.
		// Reliable RPC delivery still depends on this connection surviving. If
		// acknowledgement is lost, complete the local leave instead of stranding
		// the player with automatic reconnect suppressed.
		if (ExplicitLeaveFallbackTickerHandle.IsValid())
		{
			FTSTicker::RemoveTicker(ExplicitLeaveFallbackTickerHandle);
		}
		ExplicitLeaveFallbackTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(
				this, &UTacticalGameInstance::HandleExplicitLeaveFallbackTicker),
			TacticalReconnect::ExplicitLeaveAcknowledgementTimeout);
		// Install the fallback before the RPC: listen-server RPC/ack execution can
		// be synchronous and CompleteExplicitLeave must be able to cancel it.
		TacticalController->ServerLeaveMatch();
		return;
	}

	CompleteExplicitLeave();
}

bool UTacticalGameInstance::CacheReconnectAdmission(
	const FString& MatchId,
	const FString& ReconnectCredential,
	int32 ProtocolVersion,
	ENetMode AdmissionSourceNetMode)
{
	if (MatchId.IsEmpty() || ReconnectCredential.IsEmpty()
		|| !TacticalNetworkAdmission::IsCompatibleProtocolVersion(ProtocolVersion))
	{
		UE_LOG(LogTacticalReconnect, Warning,
			TEXT("Event=ReconnectAdmissionRejected Reason=MalformedOwnerPayload"));
		return false;
	}

	const bool bSourceIsRemoteClient = AdmissionSourceNetMode == NM_Client;
	const bool bMatchesCachedMatch = CachedMatchId.IsEmpty()
		|| TacticalNetworkAdmission::ConstantTimeCredentialEquals(
			MatchId, CachedMatchId);
	if (!TacticalNetworkAdmission::ShouldAcceptOwnerAdmission(
		bReconnectAdmissionProtectionArmed, bSourceIsRemoteClient, bMatchesCachedMatch))
	{
		UE_LOG(LogTacticalReconnect, Warning,
			TEXT("Event=ReconnectAdmissionRejected Reason=%s NetMode=%s ProtectionArmed=%d"),
			!bSourceIsRemoteClient ? TEXT("NonRemoteFallbackWorld") : TEXT("MatchChangedDuringRecovery"),
			TacticalDiagnostics::GetNetModeName(AdmissionSourceNetMode),
			bReconnectAdmissionProtectionArmed);
		return false;
	}

	CachedMatchId = MatchId;
	CachedReconnectCredential = ReconnectCredential;
	CachedAdmissionProtocolVersion = ProtocolVersion;
	if (bReconnectActive)
	{
		bReconnectAdmissionConfirmed = true;
	}
	UE_LOG(LogTacticalReconnect, Display,
		TEXT("Event=ReconnectAdmissionCached Protocol=%d"), ProtocolVersion);
	return true;
}

void UTacticalGameInstance::CompleteExplicitLeave()
{
	if (!bLeaveMatchPending)
	{
		return;
	}
	if (ExplicitLeaveFallbackTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(ExplicitLeaveFallbackTickerHandle);
		ExplicitLeaveFallbackTickerHandle.Reset();
	}
	bSuppressAutomaticReconnect = true;
	ResetReconnectState();
	CachedServerURL.Reset();
	ClearReconnectAdmission();

	BeginSessionCleanupAndReturn(NSLOCTEXT(
		"TacticalSession", "VoluntaryLeaveComplete", "已离开上一场比赛。"));
}

bool UTacticalGameInstance::HandleHostShutdownDelayTicker(float DeltaTime)
{
	HostShutdownDelayTickerHandle.Reset();
	bHostShutdownDeliveryDelayElapsed = true;
	TryCompleteHostShutdown();
	return false;
}

bool UTacticalGameInstance::HandleHostShutdownMaxWaitTicker(float DeltaTime)
{
	HostShutdownMaxWaitTickerHandle.Reset();
	if (bHostShutdownPending)
	{
		UE_LOG(LogTacticalReconnect, Warning,
			TEXT("Event=HostShutdownPreparationTimedOut Action=DestroySession"));
		bHostShutdownDeliveryDelayElapsed = true;
		bHostSessionClosingPrepared = true;
		TryCompleteHostShutdown();
	}
	return false;
}

void UTacticalGameInstance::HandleHostSessionClosingPrepared()
{
	bHostSessionClosingPrepared = true;
	TryCompleteHostShutdown();
}

void UTacticalGameInstance::TryCompleteHostShutdown()
{
	if (!bHostShutdownPending || !bHostShutdownDeliveryDelayElapsed
		|| !bHostSessionClosingPrepared)
	{
		return;
	}
	bHostShutdownPending = false;
	if (HostShutdownDelayTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(HostShutdownDelayTickerHandle);
		HostShutdownDelayTickerHandle.Reset();
	}
	if (HostShutdownMaxWaitTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(HostShutdownMaxWaitTickerHandle);
		HostShutdownMaxWaitTickerHandle.Reset();
	}
	BeginSessionCleanupAndReturn(NSLOCTEXT(
		"TacticalSession", "HostClosedOwnRoom", "你已结束房间，比赛未记录结果。"));
}

void UTacticalGameInstance::HandleOnlineSessionLost(const FText& UserFacingReason)
{
	if (bSessionCleanupReturnPending)
	{
		return;
	}
	bSuppressAutomaticReconnect = true;
	ResetReconnectState();
	CachedServerURL.Reset();
	ClearReconnectAdmission();
	BeginSessionCleanupAndReturn(UserFacingReason);
}

void UTacticalGameInstance::HandleHostSessionEnded()
{
	HandleOnlineSessionLost(NSLOCTEXT(
		"TacticalSession", "HostEndedMatch",
		"房主已结束房间，比赛不计结果。"));
}

void UTacticalGameInstance::BeginSessionCleanupAndReturn(
	const FText& FrontendNotice)
{
	if (bSessionCleanupReturnPending)
	{
		return;
	}
	bLeaveMatchPending = true;
	bHostShutdownPending = false;
	bHostShutdownDeliveryDelayElapsed = false;
	bHostSessionClosingPrepared = false;
	if (ExplicitLeaveFallbackTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(ExplicitLeaveFallbackTickerHandle);
		ExplicitLeaveFallbackTickerHandle.Reset();
	}
	if (HostShutdownDelayTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(HostShutdownDelayTickerHandle);
		HostShutdownDelayTickerHandle.Reset();
	}
	if (HostShutdownMaxWaitTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(HostShutdownMaxWaitTickerHandle);
		HostShutdownMaxWaitTickerHandle.Reset();
	}
	bSessionCleanupReturnPending = true;
	PendingFrontendNotice = FrontendNotice;
	HideMultiplayerMenu();

	if (SessionCleanupFallbackTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(SessionCleanupFallbackTickerHandle);
	}
	SessionCleanupFallbackTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(
			this, &ThisClass::HandleSessionCleanupFallbackTicker),
		TacticalReconnect::SessionCleanupTimeout);

	if (UTacticalSessionSubsystem* SessionSubsystem =
		GetSubsystem<UTacticalSessionSubsystem>())
	{
		SessionSubsystem->CleanupSession(
			FOnTacticalSessionCleanupComplete::CreateUObject(
				this, &ThisClass::CompleteSessionCleanupAndReturn));
	}
	else
	{
		CompleteSessionCleanupAndReturn();
	}
}

void UTacticalGameInstance::CompleteSessionCleanupAndReturn()
{
	if (!bSessionCleanupReturnPending)
	{
		return;
	}
	bSessionCleanupReturnPending = false;
	bLeaveMatchPending = false;
	bHostShutdownPending = false;
	bHostShutdownDeliveryDelayElapsed = false;
	bHostSessionClosingPrepared = false;
	if (SessionCleanupFallbackTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(SessionCleanupFallbackTickerHandle);
		SessionCleanupFallbackTickerHandle.Reset();
	}
	if (UTacticalSessionSubsystem* SessionSubsystem =
		GetSubsystem<UTacticalSessionSubsystem>())
	{
		SessionSubsystem->PublishFrontendNotice(PendingFrontendNotice);
	}
	PendingFrontendNotice = FText::GetEmpty();
	if (GetWorld())
	{
		ReturnToMainMenu();
	}
}

bool UTacticalGameInstance::HandleSessionCleanupFallbackTicker(float DeltaTime)
{
	SessionCleanupFallbackTickerHandle.Reset();
	UE_LOG(LogTacticalReconnect, Warning,
		TEXT("Event=SessionCleanupTimedOut Action=ReturnToFrontend"));
	if (UTacticalSessionSubsystem* SessionSubsystem =
		GetSubsystem<UTacticalSessionSubsystem>())
	{
		SessionSubsystem->ForceResetAfterCleanupTimeout(PendingFrontendNotice);
	}
	CompleteSessionCleanupAndReturn();
	return false;
}

bool UTacticalGameInstance::HandleExplicitLeaveFallbackTicker(float DeltaTime)
{
	ExplicitLeaveFallbackTickerHandle.Reset();
	UE_LOG(LogTacticalReconnect, Warning,
		TEXT("Event=ExplicitLeaveAcknowledgementTimedOut Action=ReturnToMenu"));
	CompleteExplicitLeave();
	return false;
}

void UTacticalGameInstance::HandleNetworkFailure(
	UWorld* FailureWorld,
	UNetDriver* NetDriver,
	ENetworkFailure::Type FailureType,
	const FString& ErrorString)
{
	UTacticalSessionSubsystem* SessionSubsystem =
		GetSubsystem<UTacticalSessionSubsystem>();
	const bool bIsSessionHost = SessionSubsystem
		&& SessionSubsystem->IsSessionHost();
	const bool bClientFailureForThisInstance =
		IsClientFailureForThisInstance(FailureWorld, NetDriver);
	const bool bHostListenStartupFailure = bIsSessionHost
		&& FailureWorld && FailureWorld->GetGameInstance() == this
		&& (FailureType == ENetworkFailure::NetDriverCreateFailure
			|| FailureType == ENetworkFailure::NetDriverListenFailure);
	const bool bInitialSessionTravel = SessionSubsystem
		&& SessionSubsystem->GetOperation() == ETacticalSessionOperation::Traveling
		&& !bReconnectActive
		&& (bIsSessionHost
			? bHostListenStartupFailure
			: bClientFailureForThisInstance);
	if (!bSuppressAutomaticReconnect && bInitialSessionTravel)
	{
		HandleOnlineSessionLost(SessionSubsystem->IsSessionHost()
			? NSLOCTEXT("TacticalSession", "ListenTravelFailed",
				"房间已创建，但监听服务器启动失败。")
			: NSLOCTEXT("TacticalSession", "InitialJoinFailed",
				"无法连接房主，请刷新房间后重试。"));
		return;
	}
	if (bSuppressAutomaticReconnect
		|| !IsClientFailureForThisInstance(FailureWorld, NetDriver))
	{
		return;
	}

	UE_LOG(LogTacticalReconnect, Warning,
		TEXT("Event=ClientNetworkFailure Type=%s ReconnectActive=%d AttemptInFlight=%d Attempts=%d AutoRetryEnabled=%d"),
		ENetworkFailure::ToString(FailureType), bReconnectActive,
		bReconnectAttemptInFlight, ReconnectAttemptsStarted,
		bAutomaticReconnectEnabledForTesting);

	switch (FailureType)
	{
	case ENetworkFailure::ConnectionLost:
	case ENetworkFailure::ConnectionTimeout:
		bReconnectAttemptInFlight = false;
		if (ReconnectConfirmationTickerHandle.IsValid())
		{
			FTSTicker::RemoveTicker(ReconnectConfirmationTickerHandle);
			ReconnectConfirmationTickerHandle.Reset();
		}
		if (!bReconnectActive)
		{
			BeginReconnectSequence();
		}
		else
		{
			ScheduleNextReconnect();
		}
		break;

	case ENetworkFailure::PendingConnectionFailure:
		// A pending-connect failure is retryable only when it belongs to an
		// already-authorized reconnect sequence. It must never turn an arbitrary
		// failed first connection into an automatic retry loop.
		if (bReconnectActive && bReconnectAttemptInFlight)
		{
			bReconnectAttemptInFlight = false;
			if (ReconnectConfirmationTickerHandle.IsValid())
			{
				FTSTicker::RemoveTicker(ReconnectConfirmationTickerHandle);
				ReconnectConfirmationTickerHandle.Reset();
			}
			if (TacticalNetworkAdmission::IsTransientStaleConnectionRejection(
				ErrorString))
			{
				ReconnectAttemptsStarted =
					FMath::Max(0, ReconnectAttemptsStarted - 1);
				UE_LOG(LogTacticalReconnect, Display,
					TEXT("Event=ReconnectDeferred Reason=StaleConnectionStillActive Attempt=%d"),
					ReconnectAttemptsStarted);
				ScheduleStaleConnectionRetry();
			}
			else if (TacticalNetworkAdmission::IsTacticalAdmissionRejection(
				ErrorString))
			{
				FailReconnect(TEXT("ServerRejected"),
					GetNetworkFailureText(
						ENetworkFailure::FailureReceived, ErrorString));
			}
			else
			{
				ScheduleNextReconnect();
			}
		}
		break;

	case ENetworkFailure::FailureReceived:
		if (bReconnectActive && bReconnectAttemptInFlight
			&& TacticalNetworkAdmission::IsTransientStaleConnectionRejection(
				ErrorString))
		{
			// The server still sees the old connection. This is transient during
			// disconnect detection, so continue the same bounded backoff sequence.
			bReconnectAttemptInFlight = false;
			// A server-side stale connection is not a failed transport attempt.
			// Refund this attempt and govern this condition with its own wall-clock
			// budget so normal 5-attempt failure semantics remain unchanged.
			ReconnectAttemptsStarted = FMath::Max(0, ReconnectAttemptsStarted - 1);
			if (ReconnectConfirmationTickerHandle.IsValid())
			{
				FTSTicker::RemoveTicker(ReconnectConfirmationTickerHandle);
				ReconnectConfirmationTickerHandle.Reset();
			}
			UE_LOG(LogTacticalReconnect, Display,
				TEXT("Event=ReconnectDeferred Reason=StaleConnectionStillActive Attempt=%d"),
				ReconnectAttemptsStarted);
			ScheduleStaleConnectionRetry();
		}
		else if (bReconnectActive
			&& TacticalNetworkAdmission::IsTacticalAdmissionRejection(
				ErrorString))
		{
			FailReconnect(TEXT("ServerRejected"),
				GetNetworkFailureText(FailureType, ErrorString));
		}
		else if (bReconnectActive && bReconnectAttemptInFlight)
		{
			// Keep this path symmetrical with PendingConnectionFailure. Some
			// transports/providers surface a connection-stage transport error as
			// FailureReceived even though it is not a deterministic admission deny.
			bReconnectAttemptInFlight = false;
			if (ReconnectConfirmationTickerHandle.IsValid())
			{
				FTSTicker::RemoveTicker(ReconnectConfirmationTickerHandle);
				ReconnectConfirmationTickerHandle.Reset();
			}
			ScheduleNextReconnect();
		}
		break;

	default:
		// Server rejection, version/checksum errors, and driver construction
		// errors are deterministic or intentional and must not be retried.
		if (bReconnectActive)
		{
			FailReconnect(TEXT("NonRetryableNetworkFailure"),
				GetNetworkFailureText(FailureType, ErrorString));
		}
		break;
	}
}

void UTacticalGameInstance::HandleTravelFailure(
	UWorld* FailureWorld,
	ETravelFailure::Type FailureType,
	const FString& ErrorString)
{
	if (!FailureWorld || FailureWorld->GetGameInstance() != this)
	{
		return;
	}
	if (!bReconnectActive)
	{
		if (UTacticalSessionSubsystem* SessionSubsystem =
			GetSubsystem<UTacticalSessionSubsystem>();
			SessionSubsystem
			&& SessionSubsystem->GetOperation() ==
				ETacticalSessionOperation::Traveling)
		{
			HandleOnlineSessionLost(GetTravelFailureText(FailureType));
		}
		return;
	}

	UE_LOG(LogTacticalReconnect, Warning,
		TEXT("Event=ReconnectTravelFailure Type=%s"),
		ETravelFailure::ToString(FailureType));
	FailReconnect(TEXT("TravelFailure"), GetTravelFailureText(FailureType));
}

void UTacticalGameInstance::HandlePreClientTravel(
	const FString& PendingURL,
	ETravelType TravelType,
	bool bIsSeamlessTravel)
{
	// Non-seamless restart replaces the controller and viewport widgets. Release
	// this GameInstance-owned menu lock before either host or client travels so
	// it cannot remain attached to the controller from the previous world.
	HideMultiplayerMenu();
	FURL ParsedURL(nullptr, *PendingURL, TravelType);
	if (!bSuppressAutomaticReconnect)
	{
		UWorld* CurrentWorld = GetWorld();
		if (CurrentWorld && CurrentWorld->GetNetMode() == NM_Client)
		{
			if (UTacticalSessionSubsystem* SessionSubsystem =
				GetSubsystem<UTacticalSessionSubsystem>())
			{
				SessionSubsystem->NotifyClientServerTravelStarted();
			}
		}
	}
	if (ParsedURL.Valid && !ParsedURL.Host.IsEmpty())
	{
		// An explicit new remote travel is also the boundary that re-enables
		// automatic failure handling after CancelReconnect/LeaveMatch. Never
		// carry one server's bearer credential into an unrelated remote travel.
		if (!bReconnectActive)
		{
			ClearReconnectAdmission();
		}
		bSuppressAutomaticReconnect = false;
		CacheServerURL(ParsedURL.ToString());
	}
}

void UTacticalGameInstance::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (!LoadedWorld || LoadedWorld->GetGameInstance() != this || !GEngine)
	{
		return;
	}
	if (UTacticalSessionSubsystem* SessionSubsystem =
		GetSubsystem<UTacticalSessionSubsystem>())
	{
		// World-scoped OSS interfaces can change across hard travel. Global
		// invite/failure delegates must be live on Entry too, including directly
		// after startup and after a forced cleanup timeout.
		SessionSubsystem->RefreshOnlineBindings();
		if (LoadedWorld->GetMapName().Contains(TEXT("Lvl_FirstPerson")))
		{
			// A failed remote Browse can load the Entry fallback before UE broadcasts
			// TravelFailure. Do not mark that fallback as successful Session travel.
			SessionSubsystem->NotifyTravelComplete();
		}
	}

	if (LoadedWorld->GetNetMode() == NM_Client)
	{
		const FWorldContext* Context = GEngine->GetWorldContextFromWorld(LoadedWorld);
		if (Context && Context->LastRemoteURL.Valid && !Context->LastRemoteURL.Host.IsEmpty())
		{
			CacheServerURL(Context->LastRemoteURL.ToString());
			if (bReconnectActive)
			{
				BeginAwaitingServerConfirmation();
			}
		}
		return;
	}

	if (LoadedWorld->GetNetMode() == NM_Standalone
		&& LoadedWorld->GetMapName().Contains(TEXT("Entry")))
	{
		ShowMultiplayerMenu(false);
	}

	// A connection failure normally sends the client through a local fallback
	// map. If the zero-delay retry ran while that travel was still pending, this
	// is the first safe point at which to issue it again.
	if (bReconnectActive && !bReconnectAttemptInFlight && !ReconnectTickerHandle.IsValid())
	{
		EnsureReconnectOverlay();
		ApplyReconnectInputBlock();
		ScheduleNextReconnect();
	}
}

bool UTacticalGameInstance::IsClientFailureForThisInstance(
	UWorld* FailureWorld,
	UNetDriver* NetDriver) const
{
	if (!GEngine || !NetDriver || NetDriver->GetNetMode() != NM_Client)
	{
		return false;
	}

	const FWorldContext* Context = FailureWorld
		? GEngine->GetWorldContextFromWorld(FailureWorld)
		: GEngine->GetWorldContextFromPendingNetGameNetDriver(NetDriver);
	return Context && Context->OwningGameInstance == this;
}

void UTacticalGameInstance::BeginReconnectSequence(bool bManualRequest)
{
	// A transport loss can occur while the Escape menu owns an additional input
	// lock. Release that owner before the reconnect overlay acquires its own lock.
	HideMultiplayerMenu();
	++ReconnectSequenceGeneration;
	ReconnectAttemptGeneration = 0;
	bReconnectActive = true;
	bReconnectAttemptInFlight = false;
	ReconnectAttemptsStarted = 0;
	ReconnectSequenceStartedSeconds = FPlatformTime::Seconds();
	bReconnectAdmissionConfirmed = false;
	bSuppressAutomaticReconnect = false;

	const UTacticalSessionSubsystem* SessionSubsystem =
		GetSubsystem<UTacticalSessionSubsystem>();
	const bool bHasSessionTarget = SessionSubsystem
		&& SessionSubsystem->HasReconnectTarget();
	if (CachedServerURL.IsEmpty() && !bHasSessionTarget)
	{
		UE_LOG(LogTacticalReconnect, Warning,
			TEXT("Event=ReconnectUnavailable Reason=NoCachedRemoteURL"));
		FailReconnect(TEXT("NoCachedRemoteURL"));
		return;
	}
	bReconnectAdmissionProtectionArmed = true;

	if (!bManualRequest && !bAutomaticReconnectEnabledForTesting)
	{
		UE_LOG(LogTacticalReconnect, Display,
			TEXT("Event=ReconnectAutomaticRetryPausedForTesting"));
		FailReconnect(
			TEXT("AutomaticRetryPausedForTesting"),
			NSLOCTEXT(
				"TacticalReconnect",
				"AutomaticRetryPausedForTesting",
				"Automatic reconnect is paused for testing. The server still retains this player; choose Retry when you want to reconnect."));
		return;
	}

	UE_LOG(LogTacticalReconnect, Display,
		TEXT("Event=ReconnectStarted MaxAttempts=%d Manual=%d"),
		TacticalReconnect::MaxAttempts, bManualRequest);
	ScheduleNextReconnect();
}

void UTacticalGameInstance::ScheduleNextReconnect()
{
	if (!bReconnectActive || bReconnectAttemptInFlight || ReconnectTickerHandle.IsValid())
	{
		return;
	}

	if (ReconnectAttemptsStarted >= TacticalReconnect::MaxAttempts)
	{
		FailReconnect(TEXT("AttemptsExhausted"));
		return;
	}

	const float Delay = TacticalReconnect::RetryDelays[ReconnectAttemptsStarted];
	ShowScheduledReconnect(Delay);
	ReconnectTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UTacticalGameInstance::HandleReconnectTicker),
		Delay);
	UE_LOG(LogTacticalReconnect, Display,
		TEXT("Event=ReconnectScheduled Attempt=%d Delay=%.1f"),
		ReconnectAttemptsStarted + 1, Delay);
}

void UTacticalGameInstance::ScheduleStaleConnectionRetry()
{
	if (!bReconnectActive || bReconnectAttemptInFlight
		|| ReconnectTickerHandle.IsValid())
	{
		return;
	}

	const double ElapsedSeconds = FPlatformTime::Seconds()
		- ReconnectSequenceStartedSeconds;
	if (ReconnectSequenceStartedSeconds <= 0.0
		|| ElapsedSeconds >= TacticalReconnect::StaleConnectionRetryBudgetSeconds)
	{
		FailReconnect(TEXT("StaleConnectionRetirementTimeout"));
		return;
	}

	const float RetryDelay = static_cast<float>(FMath::Min(
		static_cast<double>(TacticalReconnect::StaleConnectionRetryDelay),
		TacticalReconnect::StaleConnectionRetryBudgetSeconds - ElapsedSeconds));
	ShowScheduledReconnect(RetryDelay);
	ReconnectTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(
			this, &UTacticalGameInstance::HandleReconnectTicker),
		RetryDelay);
}

bool UTacticalGameInstance::HandleReconnectTicker(float DeltaTime)
{
	ReconnectTickerHandle.Reset();
	TryReconnect();
	return false;
}

void UTacticalGameInstance::TryReconnect()
{
	if (!bReconnectActive)
	{
		return;
	}

	if (ReconnectAttemptsStarted >= TacticalReconnect::MaxAttempts)
	{
		FailReconnect(TEXT("AttemptsExhausted"));
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !GEngine)
	{
		// PostLoadMap will resume the sequence when this GameInstance owns a
		// stable world again.
		UE_LOG(LogTacticalReconnect, Verbose,
			TEXT("Event=ReconnectDeferred Reason=WorldUnavailable"));
		return;
	}

	const FWorldContext* Context = GEngine->GetWorldContextFromWorld(World);
	if (!Context || Context->OwningGameInstance != this)
	{
		FailReconnect(TEXT("WorldContextUnavailable"));
		return;
	}
	if (Context->PendingNetGame || !Context->TravelURL.IsEmpty())
	{
		// The engine's disconnect/fallback travel still owns this context.
		// HandlePostLoadMap resumes without consuming an attempt.
		UE_LOG(LogTacticalReconnect, Verbose,
			TEXT("Event=ReconnectDeferred Reason=TravelInProgress"));
		return;
	}

	++ReconnectAttemptsStarted;
	const uint64 ThisSequenceGeneration = ReconnectSequenceGeneration;
	const uint64 ThisAttemptGeneration = ++ReconnectAttemptGeneration;
	bReconnectAttemptInFlight = true;
	// Admission confirmation belongs to one transport attempt. A previous
	// connection can receive its owner RPC and then fail before the confirmation
	// ticker runs; never let that acknowledgement complete a later attempt.
	bReconnectAdmissionConfirmed = false;
	ShowReconnectAttempt();

	UTacticalSessionSubsystem* SessionSubsystem =
		GetSubsystem<UTacticalSessionSubsystem>();
	if (SessionSubsystem && SessionSubsystem->IsSteamSubsystemActive()
		&& SessionSubsystem->HasReconnectTarget())
	{
		bSessionReconnectResolutionInFlight = true;
		SessionSubsystem->ResolveReconnectTarget(
			FOnTacticalReconnectTargetResolved::CreateWeakLambda(
				this,
				[this, ThisSequenceGeneration, ThisAttemptGeneration](
					bool bSuccess, const FString& ConnectString)
				{
					ResolveAndTravelReconnect(
						ThisSequenceGeneration,
						ThisAttemptGeneration,
						bSuccess,
						ConnectString);
				}));
		return;
	}

	IssueReconnectTravel(BuildReconnectURL(), false);
}

void UTacticalGameInstance::ResolveAndTravelReconnect(
	uint64 SequenceGeneration,
	uint64 AttemptGeneration,
	bool bSuccess,
	const FString& ConnectString)
{
	if (SequenceGeneration != ReconnectSequenceGeneration
		|| AttemptGeneration != ReconnectAttemptGeneration)
	{
		UE_LOG(LogTacticalReconnect, Verbose,
			TEXT("Event=ReconnectResolutionIgnored Reason=StaleCallback Sequence=%llu Attempt=%llu"),
			SequenceGeneration, AttemptGeneration);
		return;
	}
	bSessionReconnectResolutionInFlight = false;
	if (!bReconnectActive)
	{
		return;
	}
	if (!bSuccess || ConnectString.IsEmpty())
	{
		bReconnectAttemptInFlight = false;
		ScheduleNextReconnect();
		return;
	}
	IssueReconnectTravel(ConnectString, true);
}

void UTacticalGameInstance::IssueReconnectTravel(
	const FString& ReconnectURL,
	bool bPlatformResolved)
{
	UWorld* World = GetWorld();
	FURL ParsedURL(nullptr, *ReconnectURL, TRAVEL_Absolute);
	if (!World || !GEngine || !ParsedURL.Valid || ParsedURL.Host.IsEmpty())
	{
		bReconnectAttemptInFlight = false;
		if (ReconnectAttemptsStarted >= TacticalReconnect::MaxAttempts)
		{
			FailReconnect(TEXT("CachedURLInvalid"));
		}
		else
		{
			ScheduleNextReconnect();
		}
		return;
	}
	if (bPlatformResolved)
	{
		if (UTacticalSessionSubsystem* SessionSubsystem =
			GetSubsystem<UTacticalSessionSubsystem>())
		{
			SessionSubsystem->NotifyClientServerTravelStarted();
		}
	}

	UE_LOG(LogTacticalReconnect, Display,
		TEXT("Event=ReconnectAttempt Attempt=%d MaxAttempts=%d Transport=%s Host=\"%s\" Port=%d"),
		ReconnectAttemptsStarted, TacticalReconnect::MaxAttempts,
		bPlatformResolved ? TEXT("Session") : TEXT("CompatibilityURL"),
		*ParsedURL.Host, ParsedURL.Port);
	if (APlayerController* PlayerController = GetFirstLocalPlayerController(World))
	{
		PlayerController->ClientTravel(ReconnectURL, TRAVEL_Absolute);
	}
	else
	{
		GEngine->SetClientTravel(World, *ReconnectURL, TRAVEL_Absolute);
	}
}

FString UTacticalGameInstance::BuildReconnectURL() const
{
	// Compatibility transport only: UE's internal Browse/Started Browse logging
	// may print the complete travel URL. Project logging intentionally redacts
	// admission material, but a release build needs a platform ticket or custom
	// handshake so a long-lived bearer never appears in a URL at all.
	FURL ParsedURL(nullptr, *CachedServerURL, TRAVEL_Absolute);
	if (!ParsedURL.Valid)
	{
		return FString();
	}

	ParsedURL.RemoveOption(TacticalNetworkAdmission::MatchIdOption);
	ParsedURL.RemoveOption(TacticalNetworkAdmission::ReconnectCredentialOption);
	ParsedURL.RemoveOption(TacticalNetworkAdmission::ProtocolVersionOption);
	if (!CachedMatchId.IsEmpty() && !CachedReconnectCredential.IsEmpty()
		&& TacticalNetworkAdmission::IsCompatibleProtocolVersion(
			CachedAdmissionProtocolVersion))
	{
		ParsedURL.AddOption(*FString::Printf(TEXT("%s=%s"),
			TacticalNetworkAdmission::MatchIdOption, *CachedMatchId));
		ParsedURL.AddOption(*FString::Printf(TEXT("%s=%s"),
			TacticalNetworkAdmission::ReconnectCredentialOption,
			*CachedReconnectCredential));
		ParsedURL.AddOption(*FString::Printf(TEXT("%s=%d"),
			TacticalNetworkAdmission::ProtocolVersionOption,
			CachedAdmissionProtocolVersion));
	}
	return ParsedURL.ToString();
}

void UTacticalGameInstance::ClearReconnectAdmission()
{
	CachedMatchId.Reset();
	CachedReconnectCredential.Reset();
	CachedAdmissionProtocolVersion = 0;
	bReconnectAdmissionProtectionArmed = false;
}

void UTacticalGameInstance::BeginAwaitingServerConfirmation()
{
	if (!bReconnectActive)
	{
		return;
	}

	bReconnectAttemptInFlight = false;
	ConfirmationWaitStartedSeconds = FPlatformTime::Seconds();
	ShowAwaitingServerConfirmation();

	if (!ReconnectConfirmationTickerHandle.IsValid())
	{
		ReconnectConfirmationTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(
				this, &UTacticalGameInstance::HandleReconnectConfirmationTicker),
			TacticalReconnect::ConfirmationPollInterval);
	}

	UE_LOG(LogTacticalReconnect, Display,
		TEXT("Event=ReconnectTransportConnected Attempt=%d AwaitingGameplayAdmission=1"),
		ReconnectAttemptsStarted);
}

bool UTacticalGameInstance::HandleReconnectConfirmationTicker(float DeltaTime)
{
	if (!bReconnectActive)
	{
		ReconnectConfirmationTickerHandle.Reset();
		return false;
	}

	EnsureReconnectOverlay();
	ApplyReconnectInputBlock();

	UWorld* World = GetWorld();
	APlayerController* PlayerController = World
		? GetFirstLocalPlayerController(World)
		: nullptr;
	const bool bServerConfirmedGameplayState =
		World && World->GetNetMode() == NM_Client
		&& bReconnectAdmissionConfirmed
		&& PlayerController
		&& PlayerController->GetPlayerState<APlayerState>()
		&& (PlayerController->GetPawn() || PlayerController->IsInState(NAME_Spectating));

	if (bServerConfirmedGameplayState)
	{
		ReconnectConfirmationTickerHandle.Reset();
		CompleteReconnect();
		return false;
	}

	if (FPlatformTime::Seconds() - ConfirmationWaitStartedSeconds
		>= TacticalReconnect::ConfirmationTimeoutSeconds)
	{
		ReconnectConfirmationTickerHandle.Reset();
		FailReconnect(TEXT("GameplayAdmissionTimeout"));
		return false;
	}

	return true;
}

void UTacticalGameInstance::CompleteReconnect()
{
	const int32 CompletedAttempts = ReconnectAttemptsStarted;
	bSuppressAutomaticReconnect = false;
	ResetReconnectState();
	bReconnectAdmissionProtectionArmed = false;
	UE_LOG(LogTacticalReconnect, Display,
		TEXT("Event=ReconnectSucceeded Attempts=%d"), CompletedAttempts);
}

void UTacticalGameInstance::FailReconnect(const TCHAR* Reason, const FText& UserFacingReason)
{
	const int32 FailedAttempts = ReconnectAttemptsStarted;
	// Latch automatic handling off before the engine emits any secondary
	// failure callbacks for the same pending net driver. Only the explicit
	// Retry button or a brand-new remote travel may start another sequence.
	bSuppressAutomaticReconnect = true;
	StopReconnectWork();
	const UTacticalSessionSubsystem* SessionSubsystem =
		GetSubsystem<UTacticalSessionSubsystem>();
	const bool bSteamAttemptsExhausted = Reason
		&& FCString::Stricmp(Reason, TEXT("AttemptsExhausted")) == 0
		&& SessionSubsystem && SessionSubsystem->IsSteamSubsystemActive();
	if (bSteamAttemptsExhausted)
	{
		UE_LOG(LogTacticalReconnect, Warning,
			TEXT("Event=ReconnectStopped Reason=HostLost Attempts=%d Action=ReturnToFrontend"),
			FailedAttempts);
		BeginSessionCleanupAndReturn(NSLOCTEXT(
			"TacticalSession", "HostLostAfterRetries",
			"无法重新连接房主，Steam 房间已结束；本场比赛不计结果。"));
		return;
	}
	ShowReconnectFailure(
		UserFacingReason.IsEmpty() ? GetDefaultFailureText(Reason) : UserFacingReason,
		FailedAttempts);
	UE_LOG(LogTacticalReconnect, Warning,
		TEXT("Event=ReconnectStopped Reason=%s Attempts=%d"),
		Reason ? Reason : TEXT("Unknown"), FailedAttempts);
}

void UTacticalGameInstance::StopReconnectWork()
{
	++ReconnectSequenceGeneration;
	ReconnectAttemptGeneration = 0;
	if (ReconnectTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(ReconnectTickerHandle);
	}
	ReconnectTickerHandle.Reset();
	if (ReconnectConfirmationTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(ReconnectConfirmationTickerHandle);
	}
	ReconnectConfirmationTickerHandle.Reset();
	if (ReconnectOverlayGuardTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(ReconnectOverlayGuardTickerHandle);
	}
	ReconnectOverlayGuardTickerHandle.Reset();
	bReconnectActive = false;
	bReconnectAttemptInFlight = false;
	bSessionReconnectResolutionInFlight = false;
	ConfirmationWaitStartedSeconds = 0.0;
	ReconnectSequenceStartedSeconds = 0.0;
}

void UTacticalGameInstance::ResetReconnectState()
{
	StopReconnectWork();
	ReconnectAttemptsStarted = 0;
	bReconnectActionsAvailable = false;
	bReconnectAdmissionConfirmed = false;
	if (ReconnectOverlay)
	{
		ReconnectOverlay->HideReconnectOverlay();
	}
	RestoreInputAfterReconnectOverlay();
}

void UTacticalGameInstance::EnsureReconnectOverlay()
{
	if (!IsValid(ReconnectOverlay))
	{
		ReconnectOverlay = nullptr;
		ReconnectOverlay = CreateWidget<UReconnectStatusUI>(
			this, UReconnectStatusUI::StaticClass(), TEXT("TacticalReconnectOverlay"));
		if (ReconnectOverlay)
		{
			ReconnectOverlay->InitializeReconnectOwner(this);
			UE_LOG(LogTacticalReconnect, Display,
				TEXT("Event=ReconnectOverlayCreated World=\"%s\""),
				*GetNameSafe(GetWorld()));
		}
	}

	if (ReconnectOverlay && !ReconnectOverlay->IsInViewport())
	{
		// AddToViewport uses a slot that is removed automatically when the old
		// network world begins tearing down. Reusing that UUserWidget afterwards
		// is unsafe because UUserWidget caches its first World. The GameInstance
		// and GameViewport survive the engine's local `?closed` fallback travel,
		// so keep this narrowly scoped recovery overlay registered with that
		// migrating viewport instead.
		FGameViewportWidgetSlot ViewportSlot;
		ViewportSlot.ZOrder = TacticalReconnect::OverlayZOrder;
		ViewportSlot.bAutoRemoveOnWorldRemoved = false;
		UGameViewportSubsystem* ViewportSubsystem =
			UGameViewportSubsystem::Get(GetWorld());
		const bool bAdded = ViewportSubsystem
			&& ViewportSubsystem->AddWidget(ReconnectOverlay, ViewportSlot);
		if (bAdded)
		{
			UE_LOG(LogTacticalReconnect, Display,
				TEXT("Event=ReconnectOverlayRegistered Added=1 PersistentAcrossWorldTravel=1 World=\"%s\""),
				*GetNameSafe(GetWorld()));
		}
		else
		{
			UE_LOG(LogTacticalReconnect, Warning,
				TEXT("Event=ReconnectOverlayRegistered Added=0 PersistentAcrossWorldTravel=1 World=\"%s\""),
				*GetNameSafe(GetWorld()));
		}
	}
}

void UTacticalGameInstance::EnsureReconnectOverlayGuard()
{
	if (!ReconnectOverlayGuardTickerHandle.IsValid())
	{
		ReconnectOverlayGuardTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(
				this, &UTacticalGameInstance::HandleReconnectOverlayGuardTicker),
			TacticalReconnect::ConfirmationPollInterval);
	}
}

bool UTacticalGameInstance::HandleReconnectOverlayGuardTicker(float DeltaTime)
{
	EnsureReconnectOverlay();
	ApplyReconnectInputBlock();
	return true;
}

void UTacticalGameInstance::ShowScheduledReconnect(float DelaySeconds)
{
	bReconnectActionsAvailable = false;
	EnsureReconnectOverlay();
	EnsureReconnectOverlayGuard();
	if (ReconnectOverlay)
	{
		ReconnectOverlay->ShowAutomaticRetry(
			ReconnectAttemptsStarted + 1, TacticalReconnect::MaxAttempts, DelaySeconds);
	}
	ApplyReconnectInputBlock();
}

void UTacticalGameInstance::ShowReconnectAttempt()
{
	bReconnectActionsAvailable = false;
	EnsureReconnectOverlay();
	EnsureReconnectOverlayGuard();
	if (ReconnectOverlay)
	{
		ReconnectOverlay->ShowAttemptInProgress(
			ReconnectAttemptsStarted, TacticalReconnect::MaxAttempts);
	}
	ApplyReconnectInputBlock();
}

void UTacticalGameInstance::ShowAwaitingServerConfirmation()
{
	bReconnectActionsAvailable = false;
	EnsureReconnectOverlay();
	EnsureReconnectOverlayGuard();
	if (ReconnectOverlay)
	{
		ReconnectOverlay->ShowAwaitingServerConfirmation(
			ReconnectAttemptsStarted, TacticalReconnect::MaxAttempts);
	}
	ApplyReconnectInputBlock();
}

void UTacticalGameInstance::ShowReconnectFailure(const FText& FailureReason, int32 AttemptsMade)
{
	bReconnectActionsAvailable = true;
	EnsureReconnectOverlay();
	EnsureReconnectOverlayGuard();
	if (ReconnectOverlay)
	{
		ReconnectOverlay->ShowReconnectFailure(
			FailureReason, AttemptsMade, TacticalReconnect::MaxAttempts);
	}
	ApplyReconnectInputBlock();
}

void UTacticalGameInstance::ApplyReconnectInputBlock()
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World
		? GetFirstLocalPlayerController(World)
		: nullptr;
	if (!PlayerController)
	{
		return;
	}
	bool bNeedsInputModeApplication = false;

	if (bReconnectInputBlockApplied && InputBlockedController.Get() != PlayerController)
	{
		// Client travel replaces the local controller. Release only this system's
		// lock and restore the old controller's own flags, but retain the pending
		// post-overlay preference that replicated phase/UI callbacks may already
		// have authored for the new controller.
		if (APlayerController* PreviousController = InputBlockedController.Get())
		{
			PreviousController->SetIgnoreMoveInput(false);
			PreviousController->SetIgnoreLookInput(false);
			PreviousController->bShowMouseCursor = bSavedShowMouseCursor;
			PreviousController->bEnableClickEvents = bSavedEnableClickEvents;
			PreviousController->bEnableMouseOverEvents = bSavedEnableMouseOverEvents;
		}
		InputBlockedController.Reset();
		bReconnectInputBlockApplied = false;
		bSavedShowMouseCursor = false;
		bSavedEnableClickEvents = false;
		bSavedEnableMouseOverEvents = false;
	}

	if (!bReconnectInputBlockApplied)
	{
		InputBlockedController = PlayerController;
		bSavedShowMouseCursor = PlayerController->bShowMouseCursor;
		bSavedEnableClickEvents = PlayerController->bEnableClickEvents;
		bSavedEnableMouseOverEvents = PlayerController->bEnableMouseOverEvents;
		if (!bHasPostReconnectInputPreferences)
		{
			bPostReconnectShowMouseCursor = bSavedShowMouseCursor;
			bPostReconnectEnableClickEvents = bSavedEnableClickEvents;
			bPostReconnectEnableMouseOverEvents = bSavedEnableMouseOverEvents;
			bHasPostReconnectInputPreferences = true;
		}
		PlayerController->SetIgnoreMoveInput(true);
		PlayerController->SetIgnoreLookInput(true);
		bReconnectInputBlockApplied = true;
		bNeedsInputModeApplication = true;
	}
	if (bReconnectActionsAvailable)
	{
		PlayerController->bShowMouseCursor = true;
		PlayerController->bEnableClickEvents = true;
		PlayerController->bEnableMouseOverEvents = true;
	}

	// SetInputMode also changes the process-wide Slate user's focus and mouse
	// capture. Multiple PIE windows can share that Slate user, so repeating this
	// from the overlay guard would continuously steal input from another window.
	// The move/look lock above remains guarded and is reapplied separately after
	// ResetIgnoreInputFlags; UI focus only needs applying for a newly locked PC.
	if (bNeedsInputModeApplication)
	{
		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		if (ReconnectOverlay)
		{
			InputMode.SetWidgetToFocus(ReconnectOverlay->TakeWidget());
		}
		PlayerController->SetInputMode(InputMode);
	}
}

void UTacticalGameInstance::ReapplyReconnectInputBlockAfterControllerReset(
	APlayerController* PlayerController)
{
	if (!PlayerController || (!bReconnectActive && !bReconnectActionsAvailable))
	{
		return;
	}

	if (bReconnectInputBlockApplied
		&& InputBlockedController.Get() == PlayerController)
	{
		// ResetIgnoreInputFlags clears the engine's shared counters without
		// informing their logical owners. Restore exactly this owner's contribution;
		// the spectator policy restores its own contribution independently.
		PlayerController->SetIgnoreMoveInput(true);
		PlayerController->SetIgnoreLookInput(true);
		return;
	}

	ApplyReconnectInputBlock();
}

void UTacticalGameInstance::SetPostReconnectInputPreferences(
	bool bShowCursor,
	bool bEnableClick,
	bool bEnableMouseOver)
{
	if (!IsReconnectOverlayBlocking())
	{
		return;
	}

	bPostReconnectShowMouseCursor = bShowCursor;
	bPostReconnectEnableClickEvents = bEnableClick;
	bPostReconnectEnableMouseOverEvents = bEnableMouseOver;
	bHasPostReconnectInputPreferences = true;
}

void UTacticalGameInstance::RestoreInputAfterReconnectOverlay()
{
	APlayerController* PlayerController = InputBlockedController.Get();
	if (bReconnectInputBlockApplied && PlayerController)
	{
		const bool bRestoreShowMouseCursor = bHasPostReconnectInputPreferences
			? bPostReconnectShowMouseCursor
			: bSavedShowMouseCursor;
		const bool bRestoreClickEvents = bHasPostReconnectInputPreferences
			? bPostReconnectEnableClickEvents
			: bSavedEnableClickEvents;
		const bool bRestoreMouseOverEvents = bHasPostReconnectInputPreferences
			? bPostReconnectEnableMouseOverEvents
			: bSavedEnableMouseOverEvents;
		// IgnoreMove/Look are stacked. Remove only the single lock owned here so
		// spectator or phase-specific locks remain intact.
		PlayerController->SetIgnoreMoveInput(false);
		PlayerController->SetIgnoreLookInput(false);
		PlayerController->bShowMouseCursor = bRestoreShowMouseCursor;
		PlayerController->bEnableClickEvents = bRestoreClickEvents;
		PlayerController->bEnableMouseOverEvents = bRestoreMouseOverEvents;

		if (bRestoreShowMouseCursor)
		{
			FInputModeGameAndUI InputMode;
			InputMode.SetHideCursorDuringCapture(false);
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			PlayerController->SetInputMode(InputMode);
		}
		else
		{
			FInputModeGameOnly InputMode;
			InputMode.SetConsumeCaptureMouseDown(false);
			PlayerController->SetInputMode(InputMode);
		}
	}

	InputBlockedController.Reset();
	bReconnectInputBlockApplied = false;
	bSavedShowMouseCursor = false;
	bSavedEnableClickEvents = false;
	bSavedEnableMouseOverEvents = false;
	bHasPostReconnectInputPreferences = false;
	bPostReconnectShowMouseCursor = false;
	bPostReconnectEnableClickEvents = false;
	bPostReconnectEnableMouseOverEvents = false;
}

FText UTacticalGameInstance::GetDefaultFailureText(const TCHAR* Reason) const
{
	if (Reason && FCString::Stricmp(Reason, TEXT("NoCachedRemoteURL")) == 0)
	{
		return NSLOCTEXT("TacticalReconnect", "MissingAddress",
			"服务器状态：没有可用的上次对局地址");
	}
	if (Reason && FCString::Stricmp(Reason, TEXT("GameplayAdmissionTimeout")) == 0)
	{
		return NSLOCTEXT("TacticalReconnect", "AdmissionTimeout",
			"服务器状态：已连接，但未确认角色或观战状态");
	}
	if (Reason && FCString::Stricmp(Reason, TEXT("CachedURLInvalid")) == 0)
	{
		return NSLOCTEXT("TacticalReconnect", "InvalidAddress",
			"服务器状态：保存的服务器地址无效");
	}
	if (Reason && FCString::Stricmp(Reason, TEXT("WorldContextUnavailable")) == 0)
	{
		return NSLOCTEXT("TacticalReconnect", "WorldUnavailable",
			"服务器状态：客户端场景尚未准备完成");
	}

	return NSLOCTEXT("TacticalReconnect", "AttemptsExhausted",
		"服务器状态：自动重连次数已用尽，服务器可能暂时不可用");
}

FText UTacticalGameInstance::GetNetworkFailureText(
	ENetworkFailure::Type FailureType,
	const FString& ErrorString) const
{
	switch (FailureType)
	{
	case ENetworkFailure::OutdatedClient:
	case ENetworkFailure::OutdatedServer:
	case ENetworkFailure::NetGuidMismatch:
	case ENetworkFailure::NetChecksumMismatch:
		return NSLOCTEXT("TacticalReconnect", "VersionMismatch",
			"服务器状态：客户端与服务器版本不一致");

	case ENetworkFailure::FailureReceived:
		return NSLOCTEXT("TacticalReconnect", "ServerRejected",
			"服务器状态：连接被拒绝，对局可能已结束或身份无效");

	case ENetworkFailure::NetDriverAlreadyExists:
	case ENetworkFailure::NetDriverCreateFailure:
	case ENetworkFailure::NetDriverListenFailure:
	default:
		return NSLOCTEXT("TacticalReconnect", "NetworkUnavailable",
			"服务器状态：网络驱动不可用，无法建立连接");
	}
}

FText UTacticalGameInstance::GetTravelFailureText(ETravelFailure::Type FailureType) const
{
	switch (FailureType)
	{
	case ETravelFailure::PackageMissing:
	case ETravelFailure::PackageVersion:
	case ETravelFailure::NoDownload:
	case ETravelFailure::NoLevel:
	case ETravelFailure::LoadMapFailure:
		return NSLOCTEXT("TacticalReconnect", "TravelContentMismatch",
			"服务器状态：地图内容缺失或版本不一致");

	case ETravelFailure::InvalidURL:
		return NSLOCTEXT("TacticalReconnect", "TravelInvalidURL",
			"服务器状态：服务器地址无效");

	default:
		return NSLOCTEXT("TacticalReconnect", "TravelFailed",
			"服务器状态：无法进入服务器地图");
	}
}
