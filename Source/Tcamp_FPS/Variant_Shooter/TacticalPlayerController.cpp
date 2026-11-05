// Copyright Epic Games, Inc. All Rights Reserved.

#include "TacticalPlayerController.h"
#include "TacticalGameInstance.h"
#include "TacticalGameMode.h"
#include "TacticalGameState.h"
#include "TacticalPlayerState.h"
#include "TacticalCharacter.h"
#include "TacticalDiagnostics.h"
#include "TacticalRules.h"
#include "ShooterWeapon.h"
#include "Weapons/TacticalWeapon.h"
#include "Components/HealthComponent.h"
#include "Components/TacticalFirstPersonMeshComponent.h"
#include "Components/WeaponInventoryComponent.h"
#include "Armor/ArmorComponent.h"
#include "Animation/TacticalSpectatorAnimationController.h"
#include "Animation/TacticalSpectatorFPAnimInstance.h"
#include "UI/BuyMenuUI.h"
#include "UI/ReadyLobbyUI.h"
#include "UI/ArmorStatusUI.h"
#include "UI/ShooterBulletCounterUI.h"
#include "UI/SpectatorStatusUI.h"
#include "UI/TacticalRosterStatusUI.h"
#include "UI/MatchResultStatusUI.h"
#include "UI/HuntIntelStatusUI.h"
#include "UI/TreasureStatusUI.h"
#include "UI/ShooterCombatFeedbackUI.h"
#include "UI/TacticalUI.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "Tcamp_FPS.h"

ATacticalPlayerController::ATacticalPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	// PlayerController::PlayerTick owns local input sampling and control rotation.
	// It must run every frame; the heavier HUD polling below has its own timer.
	PrimaryActorTick.TickInterval = 0.0f;

	// Keep hard references so these contexts are also included in cooked builds.
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> DefaultContext(
		TEXT("/Game/Input/IMC_Default.IMC_Default"));
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> WeaponContext(
		TEXT("/Game/Variant_Shooter/Input/IMC_Weapons.IMC_Weapons"));
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> MouseLookContext(
		TEXT("/Game/Input/IMC_MouseLook.IMC_MouseLook"));
	if (DefaultContext.Succeeded())
	{
		DefaultMappingContexts.AddUnique(DefaultContext.Object);
	}
	if (WeaponContext.Succeeded())
	{
		DefaultMappingContexts.AddUnique(WeaponContext.Object);
	}
	if (MouseLookContext.Succeeded())
	{
		MobileExcludedMappingContexts.AddUnique(MouseLookContext.Object);
	}
}

void ATacticalPlayerController::TacticalPauseAutoReconnect()
{
#if !UE_BUILD_SHIPPING
	if (IsLocalPlayerController())
	{
		if (UTacticalGameInstance* TacticalInstance =
			GetGameInstance<UTacticalGameInstance>())
		{
			TacticalInstance->SetAutomaticReconnectEnabledForTesting(false);
		}
	}
#endif
}

void ATacticalPlayerController::TacticalResumeAutoReconnect()
{
#if !UE_BUILD_SHIPPING
	if (IsLocalPlayerController())
	{
		if (UTacticalGameInstance* TacticalInstance =
			GetGameInstance<UTacticalGameInstance>())
		{
			TacticalInstance->SetAutomaticReconnectEnabledForTesting(true);
		}
	}
#endif
}

void ATacticalPlayerController::TacticalTestDisconnect()
{
#if !UE_BUILD_SHIPPING
	if (!IsLocalPlayerController() || GetNetMode() != NM_Client)
	{
		UE_LOG(LogTacticalFlow, Warning,
			TEXT("Event=ReconnectTestDisconnectRejected Reason=NotRemoteClient Controller=\"%s\" NetMode=%s"),
			*GetName(), TacticalDiagnostics::GetNetModeName(GetNetMode()));
		return;
	}

	TacticalPauseAutoReconnect();
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=ReconnectTestDisconnectRequested Controller=\"%s\" NetMode=%s"),
		*GetName(), TacticalDiagnostics::GetNetModeName(GetNetMode()));
	ConsoleCommand(TEXT("NET GameNetDriver NETDISCONNECT"), true);
#endif
}

void ATacticalPlayerController::BeginPlay()
{
	Super::BeginPlay();
	SetActorTickInterval(0.0f);

	if (!IsLocalPlayerController())
	{
		SetActorTickEnabled(false);
		return;
	}

	if (TacticalUIClass)
	{
		TacticalUI = CreateWidget<UTacticalUI>(this, TacticalUIClass);
		if (TacticalUI)
		{
			ShooterUI = TacticalUI;
			// Keep every gameplay HUD in the same per-player layer so their numeric
			// Z order is deterministic in both network PIE and split viewports.
			TacticalUI->AddToPlayerScreen(0);
		}
	}

	EnsureCombatFeedbackUI();
	if (TacticalUI && CombatFeedbackUI)
	{
		// The named widget stays in the tree because readiness isolation walks its
		// parent chain. Only its old circular rendering is suppressed.
		TacticalUI->SetAuthoredCrosshairSuppressed(true);
	}

	if (BuyMenuUIClass)
	{
		BuyMenuUI = CreateWidget<UBuyMenuUI>(this, BuyMenuUIClass);
		if (BuyMenuUI)
		{
			BuyMenuUI->AddToPlayerScreen(10);
			if (BuyMenuUI->HasNativeCatalog())
			{
				BuyMenuUI->SetCatalogVisible(false);
			}
			else
			{
				BuyMenuUI->BP_Hide();
			}
		}
	}

	ReadyLobbyUI = CreateWidget<UReadyLobbyUI>(this, UReadyLobbyUI::StaticClass());
	if (ReadyLobbyUI)
	{
		// Keep the native readiness/team overlay above the existing shooter HUD.
		ReadyLobbyUI->AddToPlayerScreen(100);
		ReadyLobbyUI->SetVisibility(ESlateVisibility::Collapsed);
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=ReadyUICreated Controller=\"%s\" NetMode=%s AddedToScreen=%d"),
			*GetName(), TacticalDiagnostics::GetNetModeName(GetNetMode()), ReadyLobbyUI->IsInViewport());
	}

	ArmorStatusUI = CreateWidget<UArmorStatusUI>(this, UArmorStatusUI::StaticClass());
	if (ArmorStatusUI)
	{
		ArmorStatusUI->AddToPlayerScreen(101);
	}

	SpectatorStatusUI = CreateWidget<USpectatorStatusUI>(this, USpectatorStatusUI::StaticClass());
	if (SpectatorStatusUI)
	{
		SpectatorStatusUI->AddToPlayerScreen(102);
		SpectatorStatusUI->ClearSpectatorDisplay();
	}

	RosterStatusUI = CreateWidget<UTacticalRosterStatusUI>(
		this, UTacticalRosterStatusUI::StaticClass());
	if (RosterStatusUI)
	{
		RosterStatusUI->AddToPlayerScreen(103);
		RosterStatusUI->HideScoreboard();
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=RosterUICreated Controller=\"%s\" NetMode=%s AddedToScreen=%d"),
			*GetName(), TacticalDiagnostics::GetNetModeName(GetNetMode()),
			RosterStatusUI->IsInViewport());
	}
	else
	{
		UE_LOG(LogTacticalFlow, Error,
			TEXT("Event=RosterUICreationFailed Controller=\"%s\" NetMode=%s"),
			*GetName(), TacticalDiagnostics::GetNetModeName(GetNetMode()));
	}

	MatchResultStatusUI = CreateWidget<UMatchResultStatusUI>(
		this, UMatchResultStatusUI::StaticClass());
	if (MatchResultStatusUI)
	{
		MatchResultStatusUI->AddToPlayerScreen(104);
		MatchResultStatusUI->HideResult();
	}

	HuntIntelStatusUI = CreateWidget<UHuntIntelStatusUI>(
		this, UHuntIntelStatusUI::StaticClass());
	if (HuntIntelStatusUI)
	{
		// Hunt information remains beneath modal/scoreboard/spectator overlays but
		// above the legacy tactical HUD. It never receives hit tests.
		const bool bHuntUIAdded = HuntIntelStatusUI->AddToPlayerScreen(99);
		HuntIntelStatusUI->ClearHuntIntel();
		UE_LOG(LogTacticalFlow, Log,
			TEXT("Event=HuntIntelUICreated Controller=\"%s\" NetMode=%s AddedToScreen=%d"),
			*GetName(), TacticalDiagnostics::GetNetModeName(GetNetMode()),
			bHuntUIAdded && HuntIntelStatusUI->IsInViewport());
	}

	TreasureStatusUI = CreateWidget<UTreasureStatusUI>(
		this, UTreasureStatusUI::StaticClass());
	if (TreasureStatusUI)
	{
		TreasureStatusUI->AddToPlayerScreen(98);
		TreasureStatusUI->ClearTreasure();
		UE_LOG(LogTacticalFlow, Log,
			TEXT("Event=TreasureUICreated Controller=\"%s\" NetMode=%s AddedToScreen=%d"),
			*GetName(), TacticalDiagnostics::GetNetModeName(GetNetMode()),
			TreasureStatusUI->IsInViewport());
	}

	const ATacticalGameState* InitialGameState = GetWorld()
		? GetWorld()->GetGameState<ATacticalGameState>()
		: nullptr;
	// Default to the hidden lobby presentation until replicated phase state is
	// available. BindTacticalGameStateEvents restores the HUD immediately for a
	// player joining a phase already in progress.
	ApplyPhaseDrivenHudVisibility(
		InitialGameState ? InitialGameState->CurrentPhase : ERoundPhase::PreRound);

	BindTacticalGameStateEvents(true);

	BindMoneyUpdates();
	BindArmorUpdates();

	// A reliable spectator RPC may run while this reconnecting controller is
	// entering the world, before its UI and restored PlayerState are replicated.
	// Catch that state up after local initialization instead of waiting for the
	// normal three-second camera-cycle timer.
	if (bSpectatorInputLockApplied)
	{
		CacheAndHidePlayerHud();
		BeginSpectatorTargetAcquisition();
		if (SpectatorHudSnapshot.bIsValid)
		{
			ApplySpectatorHudSnapshot();
		}
	}
}

bool ATacticalPlayerController::CanPresentDamageFeedback() const
{
	if (!Super::CanPresentDamageFeedback() || bSpectatorInputLockApplied)
	{
		return false;
	}

	const ATacticalGameState* GameState =
		GetWorld() ? GetWorld()->GetGameState<ATacticalGameState>() : nullptr;
	return GameState && GameState->CurrentPhase == ERoundPhase::Combat;
}

void ATacticalPlayerController::ServerLeaveMatch_Implementation()
{
	if (ATacticalGameMode* TacticalMode =
		GetWorld() ? GetWorld()->GetAuthGameMode<ATacticalGameMode>() : nullptr)
	{
		TacticalMode->HandleExplicitLeave(this);
	}
}

void ATacticalPlayerController::ClientReceiveReconnectAdmission_Implementation(
	const FString& MatchId,
	const FString& ReconnectCredential,
	int32 ProtocolVersion)
{
	if (UTacticalGameInstance* TacticalInstance =
		GetGameInstance<UTacticalGameInstance>())
	{
		if (TacticalInstance->CacheReconnectAdmission(
			MatchId, ReconnectCredential, ProtocolVersion, GetNetMode()))
		{
			ServerAcknowledgeReconnectAdmission(MatchId, ReconnectCredential);
		}
	}
}

void ATacticalPlayerController::ServerAcknowledgeReconnectAdmission_Implementation(
	const FString& MatchId,
	const FString& ReconnectCredential)
{
	if (ATacticalGameMode* TacticalMode =
		GetWorld() ? GetWorld()->GetAuthGameMode<ATacticalGameMode>() : nullptr)
	{
		TacticalMode->HandleReconnectAdmissionAcknowledgement(
			this, MatchId, ReconnectCredential);
	}
}

void ATacticalPlayerController::ClientCompleteExplicitLeave_Implementation()
{
	if (UTacticalGameInstance* TacticalInstance =
		GetGameInstance<UTacticalGameInstance>())
	{
		TacticalInstance->CompleteExplicitLeave();
	}
}

void ATacticalPlayerController::ClientHostSessionEnded_Implementation()
{
	if (UTacticalGameInstance* TacticalInstance =
		GetGameInstance<UTacticalGameInstance>())
	{
		TacticalInstance->HandleHostSessionEnded();
	}
}

void ATacticalPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	StopSpectating();

	// The server must own the phase gate. Client-side input suppression alone can be bypassed.
	if (HasAuthority())
	{
		if (ATacticalCharacter* TacticalCharacter = Cast<ATacticalCharacter>(InPawn))
		{
			if (IsLocalPlayerController())
			{
				TacticalCharacter->SetActorTickEnabled(true);
			}
			const ATacticalGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATacticalGameState>() : nullptr;
			const bool bShouldFreeze = GS && (GS->CurrentPhase == ERoundPhase::Buy
				|| GS->CurrentPhase == ERoundPhase::RoundEnd
				|| GS->CurrentPhase == ERoundPhase::MatchEnd);
			TacticalCharacter->SetFrozen(bShouldFreeze);
		}

		EnsureDefaultSidearm(InPawn);
	}

	if (IsLocalPlayerController())
	{
		BindMoneyUpdates();
		BindArmorUpdates();
	}
}

void ATacticalPlayerController::AcknowledgePossession(APawn* InPawn)
{
	Super::AcknowledgePossession(InPawn);
	// ClientRestart can acknowledge nullptr while a torn-off/missing pawn is
	// being retried. Only a real tactical pawn proves that restricted spectating
	// has ended and that it is safe to release the camera/input lock.
	if (IsValid(Cast<ATacticalCharacter>(InPawn)))
	{
		StopSpectating();
	}
	BindMoneyUpdates();
	BindArmorUpdates();

	// Weapons may have been created during the character's BeginPlay, before
	// Possess gave the character a controller. Re-activate the current weapon
	// now so OnWeaponActivated sees IsLocallyControlled() == true and sets up
	// the first-person mesh anim instance correctly.
	if (ATacticalCharacter* Char = Cast<ATacticalCharacter>(InPawn))
	{
		Char->SetActorTickEnabled(true);
		if (UWeaponInventoryComponent* Inv = Char->GetWeaponInventory())
		{
			if (Inv->CurrentWeapon)
			{
				Inv->CurrentWeapon->ActivateWeapon();
			}
		}
	}
}

void ATacticalPlayerController::ResetIgnoreInputFlags()
{
	Super::ResetIgnoreInputFlags();

	// ClientRestart resets the engine's input-ignore counters before it knows
	// whether the replicated pawn is available. Preserve the restricted camera
	// if that restart must retry; AcknowledgePossession releases it on success.
	if (bSpectatorInputLockApplied)
	{
		if (!IsMoveInputIgnored())
		{
			SetIgnoreMoveInput(true);
		}
		if (!IsLookInputIgnored())
		{
			SetIgnoreLookInput(true);
		}
	}

	if (UTacticalGameInstance* TacticalInstance =
		GetGameInstance<UTacticalGameInstance>())
	{
		TacticalInstance->ReapplyReconnectInputBlockAfterControllerReset(this);
		TacticalInstance->ReapplyMultiplayerMenuInputBlockAfterControllerReset(this);
	}
}

void ATacticalPlayerController::PawnLeavingGame()
{
	// APlayerController destroys its Pawn before GameMode::Logout. Let the
	// authoritative tactical mode defer that destruction when this connection is
	// eligible for current-round retained-body recovery.
	if (HasAuthority())
	{
		if (const ATacticalGameMode* TacticalMode =
			GetWorld() ? GetWorld()->GetAuthGameMode<ATacticalGameMode>() : nullptr;
			TacticalMode && TacticalMode->ShouldRetainPawnOnDisconnect(this))
		{
			return;
		}
	}

	Super::PawnLeavingGame();
}

void ATacticalPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindTacticalGameStateEvents();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SpectatorSwitchTimer);
		World->GetTimerManager().ClearTimer(SpectatorAcquisitionTimer);
	}
	bSpectatorAcquisitionInProgress = false;
	SpectatorAcquisitionAttempts = 0;

	const bool bHasLocalSpectatorPresentation =
		IsValid(LocalSpectatorPresentationTarget)
		|| IsValid(LocalSpectatorAnimationController);
	if (IsLocalPlayerController() || bHasLocalSpectatorPresentation)
	{
		EndLocalSpectatorPresentation();
	}
	if (IsLocalPlayerController())
	{
		ClearAllHuntIntelPresentation();
		if (TreasureStatusUI)
		{
			TreasureStatusUI->ClearTreasure();
		}
		HideRosterScoreboard();
		RestorePlayerHud();
		if (SpectatorStatusUI)
		{
			SpectatorStatusUI->ClearSpectatorDisplay();
		}
	}

	if (HasAuthority())
	{
		UnbindServerSpectatorTarget();
	}

	Super::EndPlay(EndPlayReason);
}

void ATacticalPlayerController::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ATacticalPlayerController, SpectatorHudSnapshot, COND_OwnerOnly);
}

void ATacticalPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	BindTacticalGameStateEvents(true);
	BindMoneyUpdates();
	BindArmorUpdates();
	if (const ATacticalGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATacticalGameState>() : nullptr)
	{
		OnPhaseChanged(GS->CurrentPhase, GS->GetPhaseTimeRemaining());
		OnHuntIntelStateChanged(GS->bHuntIntelActive, GS->bHuntIntelEscalated);
		OnTreasureStateChanged();
	}

	// Reconnecting spectators commonly receive the owning PlayerState after the
	// restricted-spectator RPC. Retry immediately when that dependency arrives.
	if (IsLocalPlayerController() && bSpectatorInputLockApplied
		&& !IsValid(LocalSpectatorPresentationTarget))
	{
		BeginSpectatorTargetAcquisition();
	}
}

void ATacticalPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!IsLocalPlayerController())
	{
		return;
	}

	InputComponent->BindKey(EKeys::Tab, IE_Pressed,
		this, &ATacticalPlayerController::ShowRosterScoreboard);
	InputComponent->BindKey(EKeys::Tab, IE_Released,
		this, &ATacticalPlayerController::HideRosterScoreboard);
	InputComponent->BindKey(EKeys::Escape, IE_Pressed,
		this, &ATacticalPlayerController::ToggleOnlineMenu);
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=RosterInputBound Controller=\"%s\" NetMode=%s Key=Tab"),
		*GetName(), TacticalDiagnostics::GetNetModeName(GetNetMode()));

	UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (!Subsystem)
	{
		return;
	}

	// BP_TacticalPlayerController may have empty inherited context arrays. Keep
	// the core controls usable from native code so listen-server and client PIE
	// instances receive exactly the same movement/look/weapon mappings.
	auto AddFallbackContext = [Subsystem](const TCHAR* AssetPath, int32 Priority)
	{
		if (UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, AssetPath))
		{
			Subsystem->AddMappingContext(Context, Priority);
		}
	};

	AddFallbackContext(TEXT("/Game/Input/IMC_Default.IMC_Default"), 0);
	AddFallbackContext(TEXT("/Game/Variant_Shooter/Input/IMC_Weapons.IMC_Weapons"), 0);
	if (!ShouldUseTouchControls())
	{
		AddFallbackContext(TEXT("/Game/Input/IMC_MouseLook.IMC_MouseLook"), 1);
	}
}

void ATacticalPlayerController::ToggleOnlineMenu()
{
	if (UTacticalGameInstance* TacticalInstance =
		GetGameInstance<UTacticalGameInstance>())
	{
		TacticalInstance->ToggleMultiplayerMenu();
	}
}

void ATacticalPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsLocalPlayerController())
	{
		return;
	}
	BindTacticalGameStateEvents();
	UIStateRefreshAccumulator += DeltaSeconds;
	const bool bRefreshDiscreteState = UIStateRefreshAccumulator >= UIStateRefreshInterval;
	if (bRefreshDiscreteState)
	{
		UIStateRefreshAccumulator = 0.0f;
		if (HuntIntelStatusUI && !HuntIntelStatusUI->IsInViewport())
		{
			const bool bAdded = HuntIntelStatusUI->AddToPlayerScreen(99);
			if (bAdded)
			{
				UE_LOG(LogTacticalFlow, Display,
					TEXT("Event=HuntIntelUIRegistrationRetry Controller=\"%s\" AddedToScreen=1"),
					*GetName());
			}
		}
	}

	if (ATacticalGameState* GS = GetWorld()->GetGameState<ATacticalGameState>())
	{
		// Saved weapon tiers are replicated without their own notify. Reconcile
		// affordable/owned button states on the existing 0.2s UI cadence while
		// shopping, including when money and equipment arrive in separate bunches.
		if (bRefreshDiscreteState && BuyMenuUI && BuyMenuUI->HasNativeCatalog()
			&& GS->CurrentPhase == ERoundPhase::Buy)
		{
			BuyMenuUI->RefreshCatalog();
		}
		const float PhaseTimeRemaining = GS->GetPhaseTimeRemaining();
		const int32 DisplayedSecond = FMath::CeilToInt(FMath::Max(0.0f, PhaseTimeRemaining));
		if (LastDisplayedPhase != GS->CurrentPhase || LastDisplayedPhaseSecond != DisplayedSecond)
		{
			LastDisplayedPhase = GS->CurrentPhase;
			LastDisplayedPhaseSecond = DisplayedSecond;
			if (TacticalUI)
			{
				TacticalUI->BP_UpdatePhase(GS->CurrentPhase, PhaseTimeRemaining);
			}
			if (BuyMenuUI && GS->CurrentPhase == ERoundPhase::Buy)
			{
				if (BuyMenuUI->HasNativeCatalog())
				{
					BuyMenuUI->UpdateCatalogTimer(PhaseTimeRemaining);
				}
				else
				{
					BuyMenuUI->BP_UpdateTimer(PhaseTimeRemaining);
				}
			}
		}

		if (TacticalUI)
		{
			if (bRefreshDiscreteState)
			{
				if (LastDisplayedRound != GS->RoundNumber || LastDisplayedMaxRounds != GS->MaxRounds)
				{
					TacticalUI->BP_UpdateRoundNumber(GS->RoundNumber, GS->MaxRounds);
					LastDisplayedRound = GS->RoundNumber;
					LastDisplayedMaxRounds = GS->MaxRounds;
				}

				const int32 AliveTeamA = GS->CountPlayersAlive(0);
				const int32 AliveTeamB = GS->CountPlayersAlive(1);
				if (LastAliveTeamA != AliveTeamA || LastAliveTeamB != AliveTeamB)
				{
					TacticalUI->BP_UpdateAliveCount(AliveTeamA, AliveTeamB);
					LastAliveTeamA = AliveTeamA;
					LastAliveTeamB = AliveTeamB;
				}
			}
		}
		if (bRefreshDiscreteState)
		{
			RefreshRosterPresentation(GS);
		}
		if (bRefreshDiscreteState && ReadyLobbyUI)
		{
			int32 ReadyPlayers = 0;
			int32 AssignedPlayers = 0;
			int32 LobbyPlayers = 0;
			int32 LobbyTeamAPlayers = 0;
			int32 LobbyTeamBPlayers = 0;
			const bool bShowReadyPrompt = GS->CurrentPhase == ERoundPhase::PreRound;
			if (bShowReadyPrompt)
			{
				for (const APlayerState* LobbyPlayerState : GS->PlayerArray)
				{
					const ATacticalPlayerState* LobbyPS = Cast<ATacticalPlayerState>(LobbyPlayerState);
					if (!LobbyPS || !LobbyPS->bIsActiveParticipant)
					{
						continue;
					}

					++LobbyPlayers;
					if (LobbyPS->TeamByte > 1)
					{
						continue;
					}
					++AssignedPlayers;
					ReadyPlayers += LobbyPS->bIsReady ? 1 : 0;
					LobbyTeamAPlayers += LobbyPS->TeamByte == 0 ? 1 : 0;
					LobbyTeamBPlayers += LobbyPS->TeamByte == 1 ? 1 : 0;
				}
			}

			const ATacticalPlayerState* LocalPS = GetPlayerState<ATacticalPlayerState>();
			const bool bLobbyCandidate = LocalPS && LocalPS->bIsActiveParticipant;
			const bool bHasTeam = bLobbyCandidate && LocalPS->TeamByte <= 1;
			const uint8 LocalTeam = bHasTeam ? LocalPS->TeamByte : 255;
			const bool bLocalReady = bHasTeam && LocalPS->bIsReady;
			const bool bLobbyIdentityChanged = !bHasReadyDisplayCache
				|| bLastActiveParticipant != bLobbyCandidate || LastDisplayedTeam != LocalTeam;
			if (!bHasReadyDisplayCache || bLastLocalReady != bLocalReady
				|| LastReadyPlayers != ReadyPlayers || LastAssignedPlayers != AssignedPlayers
				|| LastLobbyPlayers != LobbyPlayers
				|| LastLobbyTeamAPlayers != LobbyTeamAPlayers || LastLobbyTeamBPlayers != LobbyTeamBPlayers
				|| bLastActiveParticipant != bLobbyCandidate || LastDisplayedTeam != LocalTeam
				|| bLastReadyPromptVisible != bShowReadyPrompt)
			{
				ReadyLobbyUI->SetReadyDisplay(
					bLocalReady, ReadyPlayers, AssignedPlayers, LobbyPlayers,
					LobbyTeamAPlayers, LobbyTeamBPlayers, LocalTeam,
					bLobbyCandidate, bShowReadyPrompt);
				ReadyLobbyUI->SetVisibility(bLobbyCandidate && (bShowReadyPrompt || bHasTeam)
					? ESlateVisibility::HitTestInvisible
					: ESlateVisibility::Collapsed);
				bLastLocalReady = bLocalReady;
				LastReadyPlayers = ReadyPlayers;
				LastAssignedPlayers = AssignedPlayers;
				LastLobbyPlayers = LobbyPlayers;
				LastLobbyTeamAPlayers = LobbyTeamAPlayers;
				LastLobbyTeamBPlayers = LobbyTeamBPlayers;
				bLastActiveParticipant = bLobbyCandidate;
				LastDisplayedTeam = LocalTeam;
				bLastReadyPromptVisible = bShowReadyPrompt;
				bHasReadyDisplayCache = true;
				if (bLobbyIdentityChanged)
				{
					UE_LOG(LogTacticalFlow, Display,
						TEXT("Event=ReadyUIState Controller=\"%s\" NetMode=%s Team=%s LobbyCandidate=%d Ready=%d Visible=%d"),
						*GetName(), TacticalDiagnostics::GetNetModeName(GetNetMode()),
						TacticalDiagnostics::GetTeamName(LocalTeam), bLobbyCandidate, bLocalReady,
						ReadyLobbyUI->GetVisibility() != ESlateVisibility::Collapsed);
				}
			}
		}
	}
}

void ATacticalPlayerController::ShowRosterScoreboard()
{
	SetRosterScoreboardVisible(true);
}

void ATacticalPlayerController::HideRosterScoreboard()
{
	SetRosterScoreboardVisible(false);
}

void ATacticalPlayerController::SetRosterScoreboardVisible(bool bVisible)
{
	if (!IsLocalPlayerController() || !RosterStatusUI)
	{
		return;
	}

	const bool bWasVisible = RosterStatusUI->IsScoreboardVisible();
	if (bWasVisible == bVisible)
	{
		return;
	}

	if (bVisible)
	{
		RosterStatusUI->ShowScoreboard();
		if (const ATacticalGameState* GS =
			GetWorld() ? GetWorld()->GetGameState<ATacticalGameState>() : nullptr)
		{
			RefreshRosterPresentation(GS);
		}
	}
	else
	{
		RosterStatusUI->HideScoreboard();
	}

	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=RosterVisibilityChanged Controller=\"%s\" NetMode=%s Visible=%d Phase=%s"),
		*GetName(), TacticalDiagnostics::GetNetModeName(GetNetMode()), bVisible,
		TacticalDiagnostics::GetPhaseName(LastDisplayedPhase));
}

void ATacticalPlayerController::RefreshRosterPresentation(
	const ATacticalGameState* GameState)
{
	if (!GameState)
	{
		return;
	}

	const APlayerState* LocalPlayerState = PlayerState;
	const TArray<FTeamAvatarStatus> TeamAAvatars =
		UTacticalUI::BuildTeamAvatarStatuses(GetWorld(), 0, LocalPlayerState);
	const TArray<FTeamAvatarStatus> TeamBAvatars =
		UTacticalUI::BuildTeamAvatarStatuses(GetWorld(), 1, LocalPlayerState);

	auto HashAvatar = [](uint32 Signature, const FTeamAvatarStatus& Status)
	{
		Signature = HashCombine(Signature, GetTypeHash(Status.PlayerId));
		Signature = HashCombine(Signature, GetTypeHash(Status.PlayerName.ToString()));
		Signature = HashCombine(Signature, GetTypeHash(Status.bIsDead));
		Signature = HashCombine(Signature, GetTypeHash(Status.bHasLivingPawn));
		Signature = HashCombine(Signature, GetTypeHash(Status.bIsActiveParticipant));
		Signature = HashCombine(Signature, GetTypeHash(Status.bIsLocalPlayer));
		Signature = HashCombine(Signature,
			GetTypeHash(static_cast<uint8>(Status.ConnectionStatus)));
		Signature = HashCombine(Signature, GetTypeHash(FMath::CeilToInt(
			FMath::Max(0.0f, Status.ReconnectGraceTimeRemaining))));
		return Signature;
	};

	uint32 AvatarSignature = HashCombine(
		GetTypeHash(TeamAAvatars.Num()), GetTypeHash(TeamBAvatars.Num()));
	for (const FTeamAvatarStatus& Status : TeamAAvatars)
	{
		AvatarSignature = HashAvatar(AvatarSignature, Status);
	}
	for (const FTeamAvatarStatus& Status : TeamBAvatars)
	{
		AvatarSignature = HashAvatar(AvatarSignature, Status);
	}

	if (!bHasAvatarCache || LastAvatarSignature != AvatarSignature)
	{
		if (TacticalUI)
		{
			TacticalUI->BP_UpdateTeamAvatars(TeamAAvatars, TeamBAvatars);
		}
		LastAvatarSignature = AvatarSignature;
		bHasAvatarCache = true;
	}

	if (RosterStatusUI && RosterStatusUI->IsScoreboardVisible())
	{
		RosterStatusUI->SetRosterDisplay(
			TeamAAvatars,
			TeamBAvatars,
			GameState->TeamScores[0],
			GameState->TeamScores[1],
			GameState->RoundNumber,
			GameState->MaxRounds);
	}
}

// ---- Buy Menu ----

void ATacticalPlayerController::ShowBuyMenu(bool bShow)
{
	if (!bShow)
	{
		// If Tab was consumed by a focused Buy control, closing that widget can
		// also remove the key-up route. Never carry its roster overlay forward.
		SetRosterScoreboardVisible(false);
	}

	if (BuyMenuUI)
	{
		if (BuyMenuUI->HasNativeCatalog())
		{
			BuyMenuUI->SetCatalogVisible(bShow);
		}
		else if (bShow)
		{
			BuyMenuUI->BP_Show();
		}
		else
		{
			BuyMenuUI->BP_Hide();
		}
	}

	UTacticalGameInstance* TacticalInstance =
		GetGameInstance<UTacticalGameInstance>();
	if (IsLocalPlayerController())
	{
		if (TacticalInstance && TacticalInstance->IsReconnectOverlayBlocking())
		{
			TacticalInstance->SetPostReconnectInputPreferences(bShow, bShow, bShow);
			return;
		}
		if (TacticalInstance && TacticalInstance->IsMultiplayerMenuBlocking())
		{
			TacticalInstance->SetPostMultiplayerMenuInputPreferences(
				bShow, bShow, bShow);
			return;
		}

		bShowMouseCursor = bShow;
		bEnableClickEvents = bShow;
		bEnableMouseOverEvents = bShow;

		if (bShow)
		{
			FInputModeGameAndUI InputMode;
			InputMode.SetHideCursorDuringCapture(false);
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			SetInputMode(InputMode);
		}
		else
		{
			ApplyGameOnlyInputMode();
		}
	}
}

void ATacticalPlayerController::ApplyGameOnlyInputMode()
{
	UTacticalGameInstance* TacticalInstance =
		GetGameInstance<UTacticalGameInstance>();
	if (!IsLocalPlayerController())
	{
		return;
	}
	if (TacticalInstance && TacticalInstance->IsReconnectOverlayBlocking())
	{
		TacticalInstance->SetPostReconnectInputPreferences(false, false, false);
		return;
	}
	if (TacticalInstance && TacticalInstance->IsMultiplayerMenuBlocking())
	{
		TacticalInstance->SetPostMultiplayerMenuInputPreferences(
			false, false, false);
		return;
	}

	bShowMouseCursor = false;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;

	FInputModeGameOnly InputMode;
	// In multi-window PIE, the click that returns focus to the listen-server
	// viewport must also reach IA_Shoot instead of being consumed for capture.
	InputMode.SetConsumeCaptureMouseDown(false);
	SetInputMode(InputMode);
}

void ATacticalPlayerController::ServerRequestPurchaseWeapon_Implementation(EWeaponTier Tier)
{
	ATacticalPlayerState* TPS = GetPlayerState<ATacticalPlayerState>();
	auto RejectPurchase = [this, TPS, Tier](const TCHAR* Reason, const FText& Message, int32 Cost)
	{
		const int32 CurrentMoney = TPS ? TPS->Money : 0;
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=PurchaseResult Player=\"%s\" Team=%s Kind=Weapon Item=%s Cost=%d MoneyBefore=%d MoneyAfter=%d Success=0 Reason=%s"),
			TPS ? *TPS->GetPlayerName() : *GetName(),
			TacticalDiagnostics::GetTeamName(TPS ? TPS->TeamByte : 255),
			TacticalDiagnostics::GetWeaponTierName(Tier), Cost, CurrentMoney, CurrentMoney, Reason);
		ClientPurchaseResult(false, Message);
	};

	FText FailureMessage;
	if (!TPS || !CanPurchaseNow(FailureMessage))
	{
		RejectPurchase(TEXT("RuleGate"),
			FailureMessage.IsEmpty() ? FText::FromString(TEXT("Purchase unavailable")) : FailureMessage, 0);
		return;
	}

	const FWeaponTierRow* WeaponData = FindWeaponData(Tier);
	const int32 Cost = WeaponData && WeaponData->Cost > 0
		? WeaponData->Cost
		: ATacticalPlayerState::GetWeaponCost(Tier);
	if (Cost <= 0 || TPS->Money < Cost)
	{
		RejectPurchase(Cost <= 0 ? TEXT("InvalidCost") : TEXT("InsufficientFunds"),
			FText::FromString(TEXT("Not enough money")), Cost);
		return;
	}

	TSubclassOf<AShooterWeapon> WeaponClass = WeaponData ? WeaponData->WeaponClass : nullptr;
	if (!WeaponClass)
	{
		const TSubclassOf<AShooterWeapon>* WeaponClassPtr = WeaponTierMap.Find(Tier);
		WeaponClass = WeaponClassPtr ? *WeaponClassPtr : nullptr;
	}

	if (!WeaponClass)
	{
		RejectPurchase(TEXT("WeaponClassMissing"), FText::FromString(TEXT("Weapon not configured")), Cost);
		return;
	}

	ATacticalCharacter* TacChar = Cast<ATacticalCharacter>(GetPawn());
	if (!TacChar)
	{
		RejectPurchase(TEXT("CharacterMissing"), FText::FromString(TEXT("No character available")), Cost);
		return;
	}

	const int32 MoneyBefore = TPS->Money;
	if (!TPS->SpendMoney(Cost))
	{
		RejectPurchase(TEXT("SpendRejected"), FText::FromString(TEXT("Not enough money")), Cost);
		return;
	}

	if (!TacChar->EquipPurchasedWeapon(WeaponClass, Tier))
	{
		TPS->AddMoney(Cost);
		RejectPurchase(TEXT("EquipRejected"),
			FText::FromString(TEXT("Weapon is already equipped or could not be spawned")), Cost);
		return;
	}

	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=PurchaseResult Player=\"%s\" Team=%s Kind=Weapon Item=%s Cost=%d MoneyBefore=%d MoneyAfter=%d Success=1 Reason=None"),
		*TPS->GetPlayerName(), TacticalDiagnostics::GetTeamName(TPS->TeamByte),
		TacticalDiagnostics::GetWeaponTierName(Tier), Cost, MoneyBefore, TPS->Money);
	ClientPurchaseResult(true, FText::FromString(TEXT("Purchased!")));
}

void ATacticalPlayerController::ServerRequestPurchaseArmor_Implementation(EArmorType Type)
{
	ATacticalPlayerState* TPS = GetPlayerState<ATacticalPlayerState>();
	auto RejectPurchase = [this, TPS, Type](const TCHAR* Reason, const FText& Message, int32 Cost)
	{
		const int32 CurrentMoney = TPS ? TPS->Money : 0;
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=PurchaseResult Player=\"%s\" Team=%s Kind=Armor Item=%s Cost=%d MoneyBefore=%d MoneyAfter=%d Success=0 Reason=%s"),
			TPS ? *TPS->GetPlayerName() : *GetName(),
			TacticalDiagnostics::GetTeamName(TPS ? TPS->TeamByte : 255),
			TacticalDiagnostics::GetArmorTypeName(Type), Cost, CurrentMoney, CurrentMoney, Reason);
		ClientPurchaseResult(false, Message);
	};

	FText FailureMessage;
	if (!TPS || !CanPurchaseNow(FailureMessage))
	{
		RejectPurchase(TEXT("RuleGate"),
			FailureMessage.IsEmpty() ? FText::FromString(TEXT("Purchase unavailable")) : FailureMessage, 0);
		return;
	}

	if (Type == EArmorType::None)
	{
		RejectPurchase(TEXT("InvalidArmorType"), FText::FromString(TEXT("Armor not configured")), 0);
		return;
	}

	const FArmorDataRow* ArmorData = FindArmorData(Type);
	const int32 Cost = ArmorData && ArmorData->Cost > 0
		? ArmorData->Cost
		: ATacticalPlayerState::GetArmorCost(Type);
	if (Cost <= 0 || TPS->Money < Cost)
	{
		RejectPurchase(Cost <= 0 ? TEXT("InvalidCost") : TEXT("InsufficientFunds"),
			FText::FromString(TEXT("Not enough money")), Cost);
		return;
	}

	ATacticalCharacter* TacChar = Cast<ATacticalCharacter>(GetPawn());
	UArmorComponent* Armor = TacChar ? TacChar->GetArmorComponent() : nullptr;
	if (!Armor)
	{
		RejectPurchase(TEXT("ArmorComponentMissing"),
			FText::FromString(TEXT("Armor component unavailable")), Cost);
		return;
	}

	const int32 MoneyBefore = TPS->Money;
	if (!TPS->SpendMoney(Cost))
	{
		RejectPurchase(TEXT("SpendRejected"), FText::FromString(TEXT("Not enough money")), Cost);
		return;
	}

	if (ArmorDataTable)
	{
		Armor->ArmorDataTable = ArmorDataTable;
	}
	Armor->EquipArmor(Type);
	TPS->SetSavedArmor(Type, Armor->CurrentDurability);

	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=PurchaseResult Player=\"%s\" Team=%s Kind=Armor Item=%s Cost=%d MoneyBefore=%d MoneyAfter=%d Success=1 Reason=None Durability=%.1f"),
		*TPS->GetPlayerName(), TacticalDiagnostics::GetTeamName(TPS->TeamByte),
		TacticalDiagnostics::GetArmorTypeName(Type), Cost, MoneyBefore, TPS->Money, Armor->CurrentDurability);
	ClientPurchaseResult(true, FText::FromString(TEXT("Armor equipped!")));
}

void ATacticalPlayerController::ClientPurchaseResult_Implementation(bool bSuccess, const FText& Message)
{
	if (BuyMenuUI)
	{
		if (BuyMenuUI->HasNativeCatalog())
		{
			BuyMenuUI->ShowCatalogPurchaseResult(bSuccess, Message);
			BuyMenuUI->RefreshCatalog();
		}
		else
		{
			BuyMenuUI->BP_PurchaseResult(bSuccess, Message);
		}
	}
}

void ATacticalPlayerController::ClientReceiveHuntIntelPulse_Implementation(
	const TArray<FVector>& ApproximateEnemyLocations,
	bool bEscalated,
	int32 SourceRoundNumber,
	uint32 PulseSequence)
{
	const ATacticalGameState* GS = GetWorld()
		? GetWorld()->GetGameState<ATacticalGameState>()
		: nullptr;
	const ATacticalPlayerState* TPS = GetPlayerState<ATacticalPlayerState>();
	const bool bValidRoundAndPhase = GS
		&& GS->CurrentPhase == ERoundPhase::Combat
		&& GS->RoundNumber == SourceRoundNumber
		&& GS->GetPhaseTimeRemaining() > 0.0f
		&& GS->GetPhaseTimeRemaining() <= TacticalEconomy::HuntIntelFirstThreshold + 0.5f;
	const ATacticalCharacter* LocalPawn = Cast<ATacticalCharacter>(GetPawn());
	const bool bLivingLocalParticipant = TPS && TPS->bIsActiveParticipant
		&& TPS->TeamByte <= 1 && IsValid(LocalPawn) && !LocalPawn->IsDead();
	const ATacticalPlayerState* SpectatedState = IsValid(LocalSpectatorPresentationTarget)
		? LocalSpectatorPresentationTarget->GetPlayerState<ATacticalPlayerState>()
		: nullptr;
	const bool bValidLocalSpectator = TPS && TPS->TeamByte <= 1
		&& bSpectatorInputLockApplied
		&& IsValid(LocalSpectatorPresentationTarget)
		&& !LocalSpectatorPresentationTarget->IsDead()
		&& SpectatedState && SpectatedState->bIsActiveParticipant
		&& SpectatedState->TeamByte == TPS->TeamByte;
	const bool bValidTeam = bLivingLocalParticipant || bValidLocalSpectator;
	const bool bSequenceIsNew = LastAcceptedHuntPulseRound != SourceRoundNumber
		|| PulseSequence > LastAcceptedHuntPulseSequence;
	if (bHuntPulseReceptionClosed || !bValidRoundAndPhase || !bValidTeam || !bSequenceIsNew)
	{
		UE_LOG(LogTacticalFlow, Verbose,
			TEXT("Event=HuntIntelPulseDiscarded Controller=\"%s\" SourceRound=%d LocalRound=%d Sequence=%u LastSequence=%u Phase=%s ValidTeam=%d ReceptionClosed=%d"),
			*GetName(), SourceRoundNumber, GS ? GS->RoundNumber : INDEX_NONE,
			PulseSequence, LastAcceptedHuntPulseSequence,
			TacticalDiagnostics::GetPhaseName(GS ? GS->CurrentPhase : ERoundPhase::PreRound),
			bValidTeam, bHuntPulseReceptionClosed);
		return;
	}

	LastAcceptedHuntPulseRound = SourceRoundNumber;
	LastAcceptedHuntPulseSequence = PulseSequence;
	if (HuntIntelStatusUI)
	{
		HuntIntelStatusUI->ShowPulse(ApproximateEnemyLocations, bEscalated);
	}
	if (TacticalUI)
	{
		TacticalUI->BP_ShowHuntIntelPulse(ApproximateEnemyLocations, bEscalated);
	}
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=HuntIntelPulsePresented Controller=\"%s\" Team=%s Round=%d Sequence=%u Escalated=%d Markers=%d"),
		*GetName(), TacticalDiagnostics::GetTeamName(TPS->TeamByte),
		SourceRoundNumber, PulseSequence, bEscalated,
		FMath::Min(ApproximateEnemyLocations.Num(), 3));
}

void ATacticalPlayerController::ClientClearHuntIntelMarkers_Implementation(
	int32 SourceRoundNumber,
	uint32 ClearedPulseSequence)
{
	const ATacticalGameState* GS = GetWorld()
		? GetWorld()->GetGameState<ATacticalGameState>()
		: nullptr;
	if (!GS || GS->RoundNumber != SourceRoundNumber)
	{
		return;
	}
	if (LastAcceptedHuntPulseRound == SourceRoundNumber
		&& LastAcceptedHuntPulseSequence > ClearedPulseSequence)
	{
		// A newer post-death pulse already arrived and excludes the dead target.
		// Never let an older reliable clear erase that newer valid information.
		return;
	}

	if (LastAcceptedHuntPulseRound != SourceRoundNumber)
	{
		LastAcceptedHuntPulseRound = SourceRoundNumber;
		LastAcceptedHuntPulseSequence = ClearedPulseSequence;
	}
	else
	{
		LastAcceptedHuntPulseSequence = FMath::Max(
			LastAcceptedHuntPulseSequence, ClearedPulseSequence);
	}
	ClearHuntIntelPulsePresentation();
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=HuntIntelMarkersCleared Controller=\"%s\" Round=%d FenceSequence=%u Reason=PlayerDeath"),
		*GetName(), SourceRoundNumber, ClearedPulseSequence);
}

bool ATacticalPlayerController::TryGetHuntIntelViewingTeam(uint8& OutViewingTeam) const
{
	OutViewingTeam = 255;
	if (!HasAuthority())
	{
		return false;
	}

	const ATacticalPlayerState* ViewerState = GetPlayerState<ATacticalPlayerState>();
	if (!ViewerState || ViewerState->TeamByte > 1)
	{
		return false;
	}

	const ATacticalCharacter* ViewerCharacter = Cast<ATacticalCharacter>(GetPawn());
	const bool bLivingParticipant = ViewerState->bIsActiveParticipant
		&& IsValid(ViewerCharacter)
		&& ViewerState->GetPawn() == ViewerCharacter
		&& !ViewerCharacter->IsDead();
	const bool bValidatedTeammateSpectator =
		IsValidServerSpectatorTarget(ServerSpectatorTarget);
	if (!bLivingParticipant && !bValidatedTeammateSpectator)
	{
		return false;
	}

	OutViewingTeam = ViewerState->TeamByte;
	return true;
}

int32 ATacticalPlayerController::GetWeaponCostForTier(EWeaponTier Tier) const
{
	return GetWeaponCostFromData(Tier);
}

bool ATacticalPlayerController::GetWeaponDataForTier(EWeaponTier Tier, FWeaponTierRow& OutData) const
{
	if (const FWeaponTierRow* WeaponData = FindWeaponData(Tier))
	{
		OutData = *WeaponData;
		OutData.Cost = GetWeaponCostFromData(Tier);
		return true;
	}

	return false;
}

TSubclassOf<AShooterWeapon> ATacticalPlayerController::GetWeaponClassForTier(EWeaponTier Tier) const
{
	// Allow DefaultPistol to be looked up from the DataTable / WeaponTierMap
	// just like any other tier, so RespawnAllPlayers() can grant the default sidearm.

	if (const FWeaponTierRow* WeaponData = FindWeaponData(Tier))
	{
		if (WeaponData->WeaponClass)
		{
			return WeaponData->WeaponClass;
		}
	}

	if (const TSubclassOf<AShooterWeapon>* WeaponClassPtr = WeaponTierMap.Find(Tier))
	{
		return *WeaponClassPtr;
	}

	return nullptr;
}

bool ATacticalPlayerController::GetArmorDataForType(EArmorType Type, FArmorDataRow& OutData) const
{
	if (const FArmorDataRow* ArmorData = FindArmorData(Type))
	{
		OutData = *ArmorData;
		OutData.Cost = OutData.Cost > 0 ? OutData.Cost : ATacticalPlayerState::GetArmorCost(Type);
		return true;
	}

	return false;
}

void ATacticalPlayerController::CloseBuyMenu()
{
	ShowBuyMenu(false);
}

const FWeaponTierRow* ATacticalPlayerController::FindWeaponData(EWeaponTier Tier) const
{
	if (!WeaponDataTable)
	{
		return nullptr;
	}

	static const FString ContextStr(TEXT("TacticalPlayerController::FindWeaponData"));
	for (const FName RowName : WeaponDataTable->GetRowNames())
	{
		const FWeaponTierRow* Row = WeaponDataTable->FindRow<FWeaponTierRow>(RowName, ContextStr);
		if (Row && Row->Tier == Tier)
		{
			return Row;
		}
	}

	return nullptr;
}

int32 ATacticalPlayerController::GetWeaponCostFromData(EWeaponTier Tier) const
{
	if (const FWeaponTierRow* WeaponData = FindWeaponData(Tier))
	{
		if (WeaponData->Cost > 0)
		{
			return WeaponData->Cost;
		}
	}
	return ATacticalPlayerState::GetWeaponCost(Tier);
}

bool ATacticalPlayerController::CanPurchaseNow(FText& OutFailureMessage) const
{
	const ATacticalGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATacticalGameState>() : nullptr;
	const ATacticalPlayerState* TPS = GetPlayerState<ATacticalPlayerState>();
	const ATacticalCharacter* ControlledCharacter = Cast<ATacticalCharacter>(GetPawn());
	if (!GS || GS->CurrentPhase != ERoundPhase::Buy)
	{
		OutFailureMessage = FText::FromString(TEXT("Purchases are only available during the buy phase"));
		return false;
	}
	if (!TPS || !TPS->bIsActiveParticipant || TPS->TeamByte > 1)
	{
		OutFailureMessage = FText::FromString(TEXT("Spectators cannot purchase"));
		return false;
	}
	if (!ControlledCharacter || ControlledCharacter->IsDead())
	{
		OutFailureMessage = FText::FromString(TEXT("No living character available"));
		return false;
	}
	return true;
}

const FArmorDataRow* ATacticalPlayerController::FindArmorData(EArmorType Type) const
{
	if (!ArmorDataTable)
	{
		return nullptr;
	}

	static const FString ContextStr(TEXT("TacticalPlayerController::FindArmorData"));
	for (const FName RowName : ArmorDataTable->GetRowNames())
	{
		const FArmorDataRow* Row = ArmorDataTable->FindRow<FArmorDataRow>(RowName, ContextStr);
		if (Row && Row->Type == Type)
		{
			return Row;
		}
	}

	return nullptr;
}

// ---- Spectator ----

void ATacticalPlayerController::StartSpectating()
{
	if (!IsLocalPlayerController())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ApplySpectatorInputLock();
	CacheAndHidePlayerHud();
	if (CombatFeedbackUI)
	{
		CombatFeedbackUI->ResetTransientFeedback();
		CombatFeedbackUI->SetCrosshairVisible(false);
	}

	StopSpectatorTargetAcquisition();
	if (!TryAcquireSpectatorTarget())
	{
		BeginSpectatorTargetAcquisition();
	}

	// This remains the normal camera-cycle cadence. Initial/empty target
	// replication is handled by the separate short-lived acquisition timer.
	World->GetTimerManager().ClearTimer(SpectatorSwitchTimer);
	World->GetTimerManager().SetTimer(
		SpectatorSwitchTimer, this, &ATacticalPlayerController::SpectateNextTeammate,
		SpectatorSwitchDelay, true);
}

void ATacticalPlayerController::ClientStartRestrictedSpectating_Implementation()
{
	// Apply the lock before entering NAME_Spectating. Doing both in one owning-
	// client RPC avoids even a single network-frame window with a free camera.
	ApplySpectatorInputLock();
	ChangeState(NAME_Spectating);
	StartSpectating();
}

void ATacticalPlayerController::HandleControlledPawnDeath(APawn* DeadPawn)
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	if (DeadPawn)
	{
		SetViewTargetWithBlend(DeadPawn, 0.0f);
	}
	// Position samples describe living enemies at one point in time. Drop every
	// current marker as this controller changes into spectator policy; later
	// pulses require a server-validated living teammate target.
	ClearHuntIntelPulsePresentation();

	if (ATacticalGameState* GS = GetWorld()->GetGameState<ATacticalGameState>())
	{
		if (GS->CurrentPhase == ERoundPhase::Combat)
		{
			StartSpectating();
		}
	}
}

void ATacticalPlayerController::SpectateNextTeammate()
{
	RefreshSpectatorTargets();

	if (SpectatorTargets.Num() == 0)
	{
		HandleEmptySpectatorTargets();
		return;
	}

	SpectatorIndex = (SpectatorIndex + 1) % SpectatorTargets.Num();
	SpectateTarget(SpectatorIndex);
}

void ATacticalPlayerController::StopSpectating()
{
	StopSpectatorTargetAcquisition();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SpectatorSwitchTimer);
	}

	if (HasAuthority())
	{
		ClearServerSpectatorSnapshot();
	}
	else if (IsLocalPlayerController())
	{
		ServerSetSpectatorTarget(nullptr);
	}

	const bool bHasLocalSpectatorPresentation =
		IsValid(LocalSpectatorPresentationTarget)
		|| IsValid(LocalSpectatorAnimationController);
	if (IsLocalPlayerController() || bHasLocalSpectatorPresentation)
	{
		EndLocalSpectatorPresentation();
	}
	if (IsLocalPlayerController())
	{
		ClearHuntIntelPulsePresentation();
		PendingSpectatorTargetPlayerId = INDEX_NONE;
		LastAppliedSpectatorSnapshotRevision = INDEX_NONE;
		if (SpectatorStatusUI)
		{
			SpectatorStatusUI->ClearSpectatorDisplay();
		}
		RestorePlayerHud();
		if (CombatFeedbackUI)
		{
			CombatFeedbackUI->SetCrosshairVisible(true);
		}
	}

	SpectatorTargets.Empty();
	SpectatorIndex = -1;
	if (IsLocalPlayerController() && bSpectatorInputLockApplied)
	{
		// The ignore-input flags are counters, so undo exactly the lock owned by
		// the spectator policy without clearing locks installed by other systems.
		SetIgnoreMoveInput(false);
		SetIgnoreLookInput(false);
		bSpectatorInputLockApplied = false;
	}

	if (IsLocalPlayerController() && GetPawn())
	{
		SetViewTargetWithBlend(GetPawn(), 0.0f);
	}
}

void ATacticalPlayerController::RefreshSpectatorTargets()
{
	SpectatorTargets.Empty();

	if (!GetWorld())
	{
		return;
	}

	const ATacticalPlayerState* LocalTPS = GetPlayerState<ATacticalPlayerState>();
	if (!LocalTPS || LocalTPS->TeamByte > 1)
	{
		// Never guess a team while PlayerState replication is still pending.
		// The looping spectator refresh will retry after the authoritative team
		// arrives; guessing RED here would briefly leak an opponent camera.
		return;
	}
	const uint8 MyTeam = LocalTPS->TeamByte;

	const ATacticalGameState* GS = GetWorld()->GetGameState<ATacticalGameState>();
	if (!GS)
	{
		return;
	}

	for (APlayerState* CandidatePlayerState : GS->PlayerArray)
	{
		ATacticalPlayerState* TPS = Cast<ATacticalPlayerState>(CandidatePlayerState);
		if (!TPS || TPS == GetPlayerState<ATacticalPlayerState>())
		{
			continue;
		}

		if (!TPS || !TPS->bIsActiveParticipant || TPS->TeamByte != MyTeam)
		{
			continue;
		}

		APawn* TargetPawn = TPS->GetPawn();
		if (!TargetPawn)
		{
			continue;
		}

		if (const AShooterCharacter* TargetCharacter = Cast<AShooterCharacter>(TargetPawn))
		{
			if (TargetCharacter->IsDead())
			{
				continue;
			}
		}

		SpectatorTargets.Add(TargetPawn);
	}
}

void ATacticalPlayerController::BeginSpectatorTargetAcquisition()
{
	if (!IsLocalPlayerController() || !bSpectatorInputLockApplied
		|| IsValid(LocalSpectatorPresentationTarget))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!bSpectatorAcquisitionInProgress)
	{
		bSpectatorAcquisitionInProgress = true;
		SpectatorAcquisitionAttempts = 0;
		World->GetTimerManager().ClearTimer(SpectatorAcquisitionTimer);
		World->GetTimerManager().SetTimer(
			SpectatorAcquisitionTimer,
			this,
			&ATacticalPlayerController::RetrySpectatorTargetAcquisition,
			TacticalSpectatorAcquisition::RetryIntervalSeconds,
			true);
	}

	// Do not wait for the first timer interval when BeginPlay or OnRep_PlayerState
	// has just made the required replicated state available.
	RetrySpectatorTargetAcquisition();
}

void ATacticalPlayerController::RetrySpectatorTargetAcquisition()
{
	if (!IsLocalPlayerController() || !bSpectatorInputLockApplied)
	{
		StopSpectatorTargetAcquisition();
		return;
	}

	if (TryAcquireSpectatorTarget())
	{
		StopSpectatorTargetAcquisition();
		if (SpectatorStatusUI && SpectatorHudSnapshot.bIsValid)
		{
			const ATacticalPlayerState* ObservedState =
				LocalSpectatorPresentationTarget
					? LocalSpectatorPresentationTarget->GetPlayerState<ATacticalPlayerState>()
					: nullptr;
			if (ObservedState
				&& ObservedState->GetPlayerId() == SpectatorHudSnapshot.TargetPlayerId)
			{
				PendingSpectatorTargetPlayerId = INDEX_NONE;
				SpectatorStatusUI->SetSpectatorDisplay(SpectatorHudSnapshot);
			}
		}
		return;
	}

	++SpectatorAcquisitionAttempts;
	HandleEmptySpectatorTargets();
	if (SpectatorAcquisitionAttempts >=
		TacticalSpectatorAcquisition::MaxRetryAttempts)
	{
		// The regular three-second timer remains as a low-frequency fallback for
		// unusually late replication or for a teammate becoming available later.
		StopSpectatorTargetAcquisition();
	}
}

void ATacticalPlayerController::StopSpectatorTargetAcquisition()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SpectatorAcquisitionTimer);
	}
	bSpectatorAcquisitionInProgress = false;
	SpectatorAcquisitionAttempts = 0;
}

bool ATacticalPlayerController::TryAcquireSpectatorTarget()
{
	if (!IsLocalPlayerController() || !bSpectatorInputLockApplied)
	{
		return false;
	}

	RefreshSpectatorTargets();
	if (SpectatorTargets.Num() == 0)
	{
		return false;
	}

	int32 TargetIndex = 0;
	const int32 PreferredPlayerId = PendingSpectatorTargetPlayerId != INDEX_NONE
		? PendingSpectatorTargetPlayerId
		: SpectatorHudSnapshot.bIsValid
			? SpectatorHudSnapshot.TargetPlayerId
			: INDEX_NONE;
	if (PreferredPlayerId != INDEX_NONE)
	{
		for (int32 Index = 0; Index < SpectatorTargets.Num(); ++Index)
		{
			const ATacticalPlayerState* CandidateState =
				SpectatorTargets[Index]
					? SpectatorTargets[Index]->GetPlayerState<ATacticalPlayerState>()
					: nullptr;
			if (CandidateState && CandidateState->GetPlayerId() == PreferredPlayerId)
			{
				TargetIndex = Index;
				break;
			}
		}
	}

	SpectatorIndex = TargetIndex;
	SpectateTarget(TargetIndex);
	return IsValid(LocalSpectatorPresentationTarget);
}

void ATacticalPlayerController::HandleEmptySpectatorTargets()
{
	EndLocalSpectatorPresentation();

	const ATacticalPlayerState* LocalState = GetPlayerState<ATacticalPlayerState>();
	const uint8 ViewerTeam = LocalState ? LocalState->TeamByte : 255;
	if (TacticalSpectatorAcquisition::ShouldDeferEmptyTarget(
		bSpectatorAcquisitionInProgress, ViewerTeam))
	{
		// An empty list during initial replication is unknown, not proof that the
		// team has no living pawn. Keep the screen neutral and do not erase a
		// server snapshot with a premature nullptr RPC.
		if (SpectatorStatusUI)
		{
			SpectatorStatusUI->ClearSpectatorDisplay();
		}
		return;
	}

	// Only discard the requested target after the owning roster/team state has
	// settled. During reconnect bootstrap it can be the only correlation key
	// available before that pawn reaches this client.
	PendingSpectatorTargetPlayerId = INDEX_NONE;
	if (SpectatorStatusUI)
	{
		SpectatorStatusUI->ShowNoTarget();
	}
	if (HasAuthority())
	{
		ClearServerSpectatorSnapshot();
	}
	else
	{
		ServerSetSpectatorTarget(nullptr);
	}
}

void ATacticalPlayerController::ApplySpectatorInputLock()
{
	if (!IsLocalPlayerController() || bSpectatorInputLockApplied)
	{
		return;
	}

	SetIgnoreMoveInput(true);
	SetIgnoreLookInput(true);
	bSpectatorInputLockApplied = true;
}

void ATacticalPlayerController::SpectateTarget(int32 Index)
{
	if (!SpectatorTargets.IsValidIndex(Index))
	{
		return;
	}

	APawn* TargetPawn = SpectatorTargets[Index];
	if (!TargetPawn)
	{
		RefreshSpectatorTargets();
		if (SpectatorTargets.Num() == 0)
		{
			return;
		}
		Index = FMath::Min(Index, SpectatorTargets.Num() - 1);
		TargetPawn = SpectatorTargets[Index];
		if (!TargetPawn)
		{
			return;
		}
	}

	if (const AShooterCharacter* TargetCharacter = Cast<AShooterCharacter>(TargetPawn))
	{
		if (TargetCharacter->IsDead())
		{
			RefreshSpectatorTargets();
			if (SpectatorTargets.Num() == 0)
			{
				return;
			}

			Index = FMath::Min(Index, SpectatorTargets.Num() - 1);
			TargetPawn = SpectatorTargets[Index];
		}
	}

	ATacticalCharacter* TacticalTarget = Cast<ATacticalCharacter>(TargetPawn);
	if (!IsValid(TacticalTarget) || TacticalTarget->IsDead())
	{
		return;
	}

	const bool bTargetChanged = GetViewTarget() != TargetPawn;
	const bool bPresentationChanged = LocalSpectatorPresentationTarget != TacticalTarget;
	const ATacticalPlayerState* TargetState = TargetPawn->GetPlayerState<ATacticalPlayerState>();
	const int32 TargetPlayerId = TargetState ? TargetState->GetPlayerId() : INDEX_NONE;
	const bool bServerSnapshotNeedsRefresh = !SpectatorHudSnapshot.bIsValid
		|| SpectatorHudSnapshot.TargetPlayerId != TargetPlayerId;
	if (bTargetChanged || bPresentationChanged)
	{
		ClearHuntIntelPulsePresentation();
		EndLocalSpectatorPresentation();
		if (bTargetChanged)
		{
			SetViewTargetWithBlend(TargetPawn, 0.3f);
		}
		BeginLocalSpectatorPresentation(TacticalTarget);

		const ATacticalPlayerState* ViewerState = GetPlayerState<ATacticalPlayerState>();
		PendingSpectatorTargetPlayerId = TargetPlayerId;
		if (SpectatorStatusUI)
		{
			SpectatorStatusUI->ShowPending(
				TargetState ? TargetState->GetPlayerName() : GetNameSafe(TargetPawn),
				TargetState ? TargetState->TeamByte : 255,
				Index,
				SpectatorTargets.Num());
		}

		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=SpectatorTargetSelected World=\"%s\" NetMode=%s ViewerId=%d ViewerTeam=%s TargetId=%d TargetTeam=%s TargetPawn=\"%s\" AimPitch=%.2f PitchSource=%s TargetCount=%d"),
			*GetNameSafe(GetWorld()), TacticalDiagnostics::GetNetModeName(GetNetMode()),
			ViewerState ? ViewerState->GetPlayerId() : -1,
			TacticalDiagnostics::GetTeamName(ViewerState ? ViewerState->TeamByte : 255),
			TargetState ? TargetState->GetPlayerId() : -1,
			TacticalDiagnostics::GetTeamName(TargetState ? TargetState->TeamByte : 255),
			*GetNameSafe(TargetPawn), TacticalTarget->GetSpectatorViewPitch(),
			TacticalTarget->GetController() ? TEXT("ControlRotation") : TEXT("RemoteViewPitch16"),
			SpectatorTargets.Num());

		if (TacticalUI)
		{
			TacticalUI->BP_OnSpectatorSwitch(Index, SpectatorTargets.Num(), TargetPawn);
		}
	}

	// A rejected or out-of-order initial request must be retried even when the
	// camera is already on the same pawn. The periodic target refresh reaches
	// this path, while a healthy matching snapshot produces no extra RPC traffic.
	if (bTargetChanged || bPresentationChanged || bServerSnapshotNeedsRefresh)
	{
		PendingSpectatorTargetPlayerId = TargetPlayerId;
		ServerSetSpectatorTarget(TargetPawn);
	}
}

void ATacticalPlayerController::ServerSetSpectatorTarget_Implementation(APawn* RequestedTarget)
{
	if (!HasAuthority())
	{
		return;
	}

	if (!RequestedTarget)
	{
		ClearServerSpectatorSnapshot();
		return;
	}

	ATacticalCharacter* TacticalTarget = Cast<ATacticalCharacter>(RequestedTarget);
	if (!IsValidServerSpectatorTarget(TacticalTarget))
	{
		const ATacticalPlayerState* ViewerState = GetPlayerState<ATacticalPlayerState>();
		const ATacticalPlayerState* TargetState = IsValid(RequestedTarget)
			? RequestedTarget->GetPlayerState<ATacticalPlayerState>()
			: nullptr;
		UE_LOG(LogTacticalFlow, Warning,
			TEXT("Event=SpectatorTargetRejected ViewerId=%d ViewerTeam=%s TargetId=%d TargetTeam=%s Target=\"%s\" Reason=ServerValidation"),
			ViewerState ? ViewerState->GetPlayerId() : INDEX_NONE,
			TacticalDiagnostics::GetTeamName(ViewerState ? ViewerState->TeamByte : 255),
			TargetState ? TargetState->GetPlayerId() : INDEX_NONE,
			TacticalDiagnostics::GetTeamName(TargetState ? TargetState->TeamByte : 255),
			*GetNameSafe(RequestedTarget));
		ClearServerSpectatorSnapshot();
		return;
	}

	if (ServerSpectatorTarget != TacticalTarget)
	{
		UnbindServerSpectatorTarget();
		BindServerSpectatorTarget(TacticalTarget);
	}

	PublishServerSpectatorSnapshot();
	const ATacticalPlayerState* ViewerState = GetPlayerState<ATacticalPlayerState>();
	const ATacticalPlayerState* TargetState = TacticalTarget->GetPlayerState<ATacticalPlayerState>();
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=SpectatorHudTargetAccepted ViewerId=%d ViewerTeam=%s TargetId=%d TargetTeam=%s Target=\"%s\""),
		ViewerState ? ViewerState->GetPlayerId() : INDEX_NONE,
		TacticalDiagnostics::GetTeamName(ViewerState ? ViewerState->TeamByte : 255),
		TargetState ? TargetState->GetPlayerId() : INDEX_NONE,
		TacticalDiagnostics::GetTeamName(TargetState ? TargetState->TeamByte : 255),
		*GetNameSafe(TacticalTarget));
}

bool ATacticalPlayerController::IsValidServerSpectatorTarget(
	const ATacticalCharacter* RequestedTarget) const
{
	if (!HasAuthority() || !IsValid(RequestedTarget) || RequestedTarget->IsDead())
	{
		return false;
	}

	const ATacticalPlayerState* ViewerState = GetPlayerState<ATacticalPlayerState>();
	const ATacticalPlayerState* TargetState = RequestedTarget->GetPlayerState<ATacticalPlayerState>();
	if (!ViewerState || !TargetState)
	{
		return false;
	}

	// A living pawn may never request teammate combat state. Normal deaths retain
	// their pawn until the round boundary; reconnect/overflow observers have none.
	const APawn* ViewerPawn = GetPawn();
	const AShooterCharacter* ViewerCharacter = Cast<AShooterCharacter>(ViewerPawn);
	return TacticalRules::CanReceiveTeammateSpectatorData(
		ViewerState->TeamByte,
		ViewerPawn != nullptr,
		ViewerCharacter && ViewerCharacter->IsDead(),
		TargetState->TeamByte,
		TargetState->bIsActiveParticipant,
		ViewerState != TargetState,
		TargetState->GetPawn() == RequestedTarget,
		!RequestedTarget->IsDead());
}

void ATacticalPlayerController::BindServerSpectatorTarget(ATacticalCharacter* Target)
{
	if (!HasAuthority() || !IsValid(Target))
	{
		return;
	}

	ServerSpectatorTarget = Target;
	Target->OnDestroyed.RemoveDynamic(this, &ATacticalPlayerController::OnServerSpectatedTargetDestroyed);
	Target->OnDestroyed.AddDynamic(this, &ATacticalPlayerController::OnServerSpectatedTargetDestroyed);

	if (UHealthComponent* Health = Target->GetHealthComponent())
	{
		Health->OnDamaged.RemoveDynamic(this, &ATacticalPlayerController::OnServerSpectatedHealthChanged);
		Health->OnDamaged.AddDynamic(this, &ATacticalPlayerController::OnServerSpectatedHealthChanged);
		Health->OnHealed.RemoveDynamic(this, &ATacticalPlayerController::OnServerSpectatedHealthChanged);
		Health->OnHealed.AddDynamic(this, &ATacticalPlayerController::OnServerSpectatedHealthChanged);
		Health->OnDeath.RemoveDynamic(this, &ATacticalPlayerController::OnServerSpectatedHealthDepleted);
		Health->OnDeath.AddDynamic(this, &ATacticalPlayerController::OnServerSpectatedHealthDepleted);
	}

	if (UArmorComponent* Armor = Target->GetArmorComponent())
	{
		Armor->OnArmorChanged.RemoveDynamic(this, &ATacticalPlayerController::OnServerSpectatedArmorChanged);
		Armor->OnArmorChanged.AddDynamic(this, &ATacticalPlayerController::OnServerSpectatedArmorChanged);
	}

	if (UWeaponInventoryComponent* Inventory = Target->GetWeaponInventory())
	{
		Inventory->OnAmmoUpdated.RemoveDynamic(this, &ATacticalPlayerController::OnServerSpectatedAmmoChanged);
		Inventory->OnAmmoUpdated.AddDynamic(this, &ATacticalPlayerController::OnServerSpectatedAmmoChanged);
		Inventory->OnWeaponChanged.RemoveDynamic(this, &ATacticalPlayerController::OnServerSpectatedWeaponChanged);
		Inventory->OnWeaponChanged.AddDynamic(this, &ATacticalPlayerController::OnServerSpectatedWeaponChanged);
	}
}

void ATacticalPlayerController::UnbindServerSpectatorTarget()
{
	ATacticalCharacter* Target = ServerSpectatorTarget;
	ServerSpectatorTarget = nullptr;
	if (!IsValid(Target))
	{
		return;
	}

	Target->OnDestroyed.RemoveDynamic(this, &ATacticalPlayerController::OnServerSpectatedTargetDestroyed);
	if (UHealthComponent* Health = Target->GetHealthComponent())
	{
		Health->OnDamaged.RemoveDynamic(this, &ATacticalPlayerController::OnServerSpectatedHealthChanged);
		Health->OnHealed.RemoveDynamic(this, &ATacticalPlayerController::OnServerSpectatedHealthChanged);
		Health->OnDeath.RemoveDynamic(this, &ATacticalPlayerController::OnServerSpectatedHealthDepleted);
	}
	if (UArmorComponent* Armor = Target->GetArmorComponent())
	{
		Armor->OnArmorChanged.RemoveDynamic(this, &ATacticalPlayerController::OnServerSpectatedArmorChanged);
	}
	if (UWeaponInventoryComponent* Inventory = Target->GetWeaponInventory())
	{
		Inventory->OnAmmoUpdated.RemoveDynamic(this, &ATacticalPlayerController::OnServerSpectatedAmmoChanged);
		Inventory->OnWeaponChanged.RemoveDynamic(this, &ATacticalPlayerController::OnServerSpectatedWeaponChanged);
	}
}

void ATacticalPlayerController::PublishServerSpectatorSnapshot()
{
	if (!HasAuthority() || !IsValidServerSpectatorTarget(ServerSpectatorTarget))
	{
		ClearServerSpectatorSnapshot();
		return;
	}

	ATacticalCharacter* Target = ServerSpectatorTarget;
	ATacticalPlayerState* TargetState = Target->GetPlayerState<ATacticalPlayerState>();
	const ATacticalPlayerState* ViewerState = GetPlayerState<ATacticalPlayerState>();

	FSpectatorHudSnapshot NewSnapshot;
	NewSnapshot.bIsValid = true;
	NewSnapshot.TargetPlayerId = TargetState ? TargetState->GetPlayerId() : INDEX_NONE;
	NewSnapshot.TargetPlayerName = TargetState ? TargetState->GetPlayerName() : GetNameSafe(Target);
	NewSnapshot.TeamByte = TargetState ? TargetState->TeamByte : 255;
	NewSnapshot.TargetIndex = 0;
	NewSnapshot.TargetCount = 1;

	if (const ATacticalGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATacticalGameState>() : nullptr)
	{
		int32 ValidIndex = 0;
		for (APlayerState* CandidateStateBase : GS->PlayerArray)
		{
			const ATacticalPlayerState* CandidateState = Cast<ATacticalPlayerState>(CandidateStateBase);
			const ATacticalCharacter* Candidate = CandidateState
				? Cast<ATacticalCharacter>(CandidateState->GetPawn())
				: nullptr;
			if (!CandidateState || CandidateState == ViewerState
				|| !CandidateState->bIsActiveParticipant
				|| CandidateState->TeamByte != NewSnapshot.TeamByte
				|| !IsValid(Candidate) || Candidate->IsDead())
			{
				continue;
			}

			if (Candidate == Target)
			{
				NewSnapshot.TargetIndex = ValidIndex;
			}
			++ValidIndex;
		}
		NewSnapshot.TargetCount = FMath::Max(1, ValidIndex);
	}

	if (const UHealthComponent* Health = Target->GetHealthComponent())
	{
		NewSnapshot.CurrentHealth = FMath::Max(0.0f, Health->CurrentHP);
		NewSnapshot.MaxHealth = FMath::Max(0.0f, Health->MaxHP);
	}
	if (const UArmorComponent* Armor = Target->GetArmorComponent())
	{
		NewSnapshot.ArmorType = Armor->ArmorType;
		NewSnapshot.CurrentArmor = FMath::Max(0.0f, Armor->CurrentDurability);
		NewSnapshot.MaxArmor = FMath::Max(0.0f, Armor->MaxDurability);
	}
	if (const UWeaponInventoryComponent* Inventory = Target->GetWeaponInventory())
	{
		if (const AShooterWeapon* Weapon = Inventory->CurrentWeapon; IsValid(Weapon))
		{
			NewSnapshot.bHasWeapon = true;
			NewSnapshot.WeaponTier = static_cast<EWeaponTier>(Weapon->GetWeaponTier());
			NewSnapshot.CurrentMagazineAmmo = FMath::Max(0, Weapon->GetBulletCount());
			NewSnapshot.MagazineCapacity = FMath::Max(0, Weapon->GetMagazineSize());
			NewSnapshot.ReserveAmmo = FMath::Max(0, Weapon->GetReserveBulletCount());
		}
	}

	NewSnapshot.Revision = SpectatorHudSnapshot.Revision == MAX_int32
		? 1
		: SpectatorHudSnapshot.Revision + 1;
	SpectatorHudSnapshot = MoveTemp(NewSnapshot);
	ForceNetUpdate();
	if (IsLocalPlayerController())
	{
		ApplySpectatorHudSnapshot();
	}
}

void ATacticalPlayerController::ClearServerSpectatorSnapshot()
{
	if (!HasAuthority())
	{
		return;
	}

	UnbindServerSpectatorTarget();
	const int32 NextRevision = SpectatorHudSnapshot.Revision == MAX_int32
		? 1
		: SpectatorHudSnapshot.Revision + 1;
	SpectatorHudSnapshot = FSpectatorHudSnapshot();
	SpectatorHudSnapshot.Revision = NextRevision;
	ForceNetUpdate();
	if (IsLocalPlayerController())
	{
		ApplySpectatorHudSnapshot();
	}
}

void ATacticalPlayerController::OnRep_SpectatorHudSnapshot()
{
	ApplySpectatorHudSnapshot();
}

void ATacticalPlayerController::ApplySpectatorHudSnapshot()
{
	if (!IsLocalPlayerController() || !SpectatorStatusUI)
	{
		return;
	}
	if (LastAppliedSpectatorSnapshotRevision != INDEX_NONE
		&& SpectatorHudSnapshot.Revision < LastAppliedSpectatorSnapshotRevision)
	{
		return;
	}
	if (SpectatorHudSnapshot.bIsValid
		&& PendingSpectatorTargetPlayerId != INDEX_NONE
		&& SpectatorHudSnapshot.TargetPlayerId != PendingSpectatorTargetPlayerId)
	{
		return;
	}

	LastAppliedSpectatorSnapshotRevision = SpectatorHudSnapshot.Revision;
	if (!bSpectatorInputLockApplied)
	{
		SpectatorStatusUI->ClearSpectatorDisplay();
		return;
	}

	if (SpectatorHudSnapshot.bIsValid)
	{
		const ATacticalPlayerState* ObservedState =
			LocalSpectatorPresentationTarget
				? LocalSpectatorPresentationTarget->GetPlayerState<ATacticalPlayerState>()
				: nullptr;
		if (!ObservedState
			|| ObservedState->GetPlayerId() != SpectatorHudSnapshot.TargetPlayerId)
		{
			PendingSpectatorTargetPlayerId = SpectatorHudSnapshot.TargetPlayerId;
			BeginSpectatorTargetAcquisition();
			ObservedState = LocalSpectatorPresentationTarget
				? LocalSpectatorPresentationTarget->GetPlayerState<ATacticalPlayerState>()
				: nullptr;
			if (!ObservedState
				|| ObservedState->GetPlayerId() != SpectatorHudSnapshot.TargetPlayerId)
			{
				SpectatorStatusUI->ShowPending(
					SpectatorHudSnapshot.TargetPlayerName,
					SpectatorHudSnapshot.TeamByte,
					SpectatorHudSnapshot.TargetIndex,
					SpectatorHudSnapshot.TargetCount);
				return;
			}
		}
		PendingSpectatorTargetPlayerId = INDEX_NONE;
		SpectatorStatusUI->SetSpectatorDisplay(SpectatorHudSnapshot);
	}
	else
	{
		const ATacticalPlayerState* LocalState = GetPlayerState<ATacticalPlayerState>();
		const uint8 ViewerTeam = LocalState ? LocalState->TeamByte : 255;
		if (TacticalSpectatorAcquisition::ShouldDeferEmptyTarget(
			bSpectatorAcquisitionInProgress, ViewerTeam))
		{
			SpectatorStatusUI->ClearSpectatorDisplay();
			return;
		}
		PendingSpectatorTargetPlayerId = INDEX_NONE;
		SpectatorStatusUI->ShowNoTarget();
	}
}

void ATacticalPlayerController::OnServerSpectatedHealthChanged(float HealthPercent)
{
	PublishServerSpectatorSnapshot();
}

void ATacticalPlayerController::OnServerSpectatedHealthDepleted()
{
	ClearServerSpectatorSnapshot();
}

void ATacticalPlayerController::OnServerSpectatedArmorChanged(
	EArmorType Type, float DurabilityPercent)
{
	PublishServerSpectatorSnapshot();
}

void ATacticalPlayerController::OnServerSpectatedAmmoChanged(
	int32 CurrentAmmo, int32 MagazineSize, int32 ReserveAmmo)
{
	PublishServerSpectatorSnapshot();
}

void ATacticalPlayerController::OnServerSpectatedWeaponChanged(
	AShooterWeapon* NewWeapon, AShooterWeapon* OldWeapon)
{
	PublishServerSpectatorSnapshot();
}

void ATacticalPlayerController::OnServerSpectatedTargetDestroyed(AActor* DestroyedActor)
{
	if (DestroyedActor == ServerSpectatorTarget)
	{
		ClearServerSpectatorSnapshot();
	}
}

void ATacticalPlayerController::CacheAndHidePlayerHud()
{
	if (!IsLocalPlayerController())
	{
		return;
	}
	if (!bPlayerHudVisibilityCached)
	{
		if (BulletCounterUI)
		{
			CachedBulletCounterVisibility = BulletCounterUI->GetVisibility();
		}
		if (ArmorStatusUI)
		{
			CachedArmorStatusVisibility = ArmorStatusUI->GetVisibility();
		}
		bPlayerHudVisibilityCached = true;
	}

	if (BulletCounterUI)
	{
		BulletCounterUI->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (ArmorStatusUI)
	{
		ArmorStatusUI->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void ATacticalPlayerController::RestorePlayerHud()
{
	if (!IsLocalPlayerController() || !bPlayerHudVisibilityCached)
	{
		return;
	}
	if (BulletCounterUI)
	{
		BulletCounterUI->SetVisibility(CachedBulletCounterVisibility);
	}
	if (ArmorStatusUI)
	{
		ArmorStatusUI->SetVisibility(CachedArmorStatusVisibility);
	}
	bPlayerHudVisibilityCached = false;

	// A spectator presentation may end while the match is back in the lobby.
	// Reapply the phase rule so restoring the cached combat visibility cannot
	// expose health/ammo/armor during readiness.
	if (const ATacticalGameState* GS = GetWorld()
		? GetWorld()->GetGameState<ATacticalGameState>()
		: nullptr)
	{
		ApplyPhaseDrivenHudVisibility(GS->CurrentPhase);
	}
}

void ATacticalPlayerController::BeginLocalSpectatorPresentation(ATacticalCharacter* Target)
{
	if (!IsLocalPlayerController() || !IsValid(Target))
	{
		return;
	}
	if (LocalSpectatorPresentationTarget == Target)
	{
		return;
	}

	EndLocalSpectatorPresentation();
	LocalSpectatorPresentationTarget = Target;
	bCachedPresentationTargetTick = Target->IsActorTickEnabled();
	Target->SetActorTickEnabled(true);
	Target->OnDestroyed.RemoveDynamic(this, &ATacticalPlayerController::OnLocalSpectatedTargetDestroyed);
	Target->OnDestroyed.AddDynamic(this, &ATacticalPlayerController::OnLocalSpectatedTargetDestroyed);

	if (USkeletalMeshComponent* Arms = Target->GetFirstPersonMesh())
	{
		bCachedPresentationArmsOnlyOwnerSee = Arms->bOnlyOwnerSee;
		bCachedPresentationArmsHidden = Arms->bHiddenInGame;
		bCachedPresentationArmsVisible = Arms->IsVisible();
		CachedPresentationArmsTickOption =
			static_cast<uint8>(Arms->VisibilityBasedAnimTickOption);
		CachedPresentationArmsAnimClass = Arms->GetAnimClass();
		CachedPresentationArmsMesh = Arms->GetSkeletalMeshAsset();
		Arms->VisibilityBasedAnimTickOption =
			EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		// The observed pawn does not own this local view, so temporarily expose its
		// first-person arms to the spectator only in this world.
		Arms->SetOnlyOwnerSee(false);
		// Do not propagate to the camera child or PIE will render its editor proxy.
		Arms->SetHiddenInGame(false, false);
		Arms->SetVisibility(true, false);
	}
	if (USkeletalMeshComponent* ThirdPersonBody = Target->GetMesh())
	{
		bCachedPresentationBodyHidden = ThirdPersonBody->bHiddenInGame;
		bCachedPresentationBodyVisible = ThirdPersonBody->IsVisible();
		// The spectator camera is intentionally first-person. Hide only this
		// client's third-person Manny so its head cannot occlude the view.
		ThirdPersonBody->SetHiddenInGame(true, false);
		ThirdPersonBody->SetVisibility(false, false);
	}
	// The corrected support-hand body is a separate child component. Hiding the
	// source Manny without propagation does not hide it from this local camera.
	Target->SetThirdPersonGripOverlaySuppressedForSpectator(true);

	if (UWeaponInventoryComponent* Inventory = Target->GetWeaponInventory())
	{
		Inventory->OnWeaponChanged.RemoveDynamic(this, &ATacticalPlayerController::OnLocalSpectatedWeaponChanged);
		Inventory->OnWeaponChanged.AddDynamic(this, &ATacticalPlayerController::OnLocalSpectatedWeaponChanged);
		ApplyLocalSpectatorWeaponPresentation(Inventory->CurrentWeapon);
	}
}

bool ATacticalPlayerController::IsLocallyPresentingWeaponFirstPerson(
	const AShooterWeapon* Weapon) const
{
	return IsLocalPlayerController() && bSpectatorInputLockApplied
		&& IsValid(Weapon)
		&& LocalSpectatorPresentationWeapon == Weapon
		&& IsValid(LocalSpectatorPresentationTarget)
		&& Weapon->GetOwner() == LocalSpectatorPresentationTarget;
}

void ATacticalPlayerController::EndLocalSpectatorPresentation()
{
	ATacticalCharacter* Target = LocalSpectatorPresentationTarget;
	if (IsValid(Target))
	{
		Target->OnDestroyed.RemoveDynamic(this, &ATacticalPlayerController::OnLocalSpectatedTargetDestroyed);
		if (UWeaponInventoryComponent* Inventory = Target->GetWeaponInventory())
		{
			Inventory->OnWeaponChanged.RemoveDynamic(this, &ATacticalPlayerController::OnLocalSpectatedWeaponChanged);
		}
	}

	RestoreLocalSpectatorWeaponPresentation();
	if (IsValid(Target))
	{
		if (USkeletalMeshComponent* ThirdPersonBody = Target->GetMesh())
		{
			ThirdPersonBody->SetHiddenInGame(bCachedPresentationBodyHidden, false);
			ThirdPersonBody->SetVisibility(bCachedPresentationBodyVisible, false);
		}
		Target->SetThirdPersonGripOverlaySuppressedForSpectator(false);
		if (USkeletalMeshComponent* Arms = Target->GetFirstPersonMesh())
		{
			if (Arms->GetSkeletalMeshAsset() != CachedPresentationArmsMesh.Get())
			{
				Arms->SetSkeletalMeshAsset(CachedPresentationArmsMesh.Get());
			}
			Target->RefreshStableSpectatorCameraTransform();
			if (UWeaponInventoryComponent* Inventory = Target->GetWeaponInventory())
			{
				Target->RefreshFirstPersonWeaponAttachment(Inventory->CurrentWeapon.Get());
			}
			if (UTacticalFirstPersonMeshComponent* FirstPersonMesh =
				Cast<UTacticalFirstPersonMeshComponent>(Arms))
			{
				// Restore while the scoped animation controller is still available;
				// this also covers an unexpected non-null cached AnimBP safely.
				FirstPersonMesh->SetSpectatorAnimInstanceClass(
					CachedPresentationArmsAnimClass);
			}
			else
			{
				Arms->SetAnimInstanceClass(CachedPresentationArmsAnimClass);
			}
			Arms->SetOnlyOwnerSee(bCachedPresentationArmsOnlyOwnerSee);
			Arms->VisibilityBasedAnimTickOption =
				static_cast<EVisibilityBasedAnimTickOption>(CachedPresentationArmsTickOption);
			// Death legitimately hides the first-person mesh while it is under the
			// spectator override. Do not undo that newer gameplay state on cleanup.
			if (!Target->IsDead())
			{
				Arms->SetHiddenInGame(bCachedPresentationArmsHidden, false);
				Arms->SetVisibility(bCachedPresentationArmsVisible, false);
			}
		}
		Target->SetActorTickEnabled(bCachedPresentationTargetTick);
	}
	ReleaseLocalSpectatorAnimationController(Target);

	LocalSpectatorPresentationTarget = nullptr;
	CachedPresentationArmsAnimClass = nullptr;
	CachedPresentationArmsMesh = nullptr;
}

void ATacticalPlayerController::ApplyLocalSpectatorWeaponPresentation(AShooterWeapon* Weapon)
{
	RestoreLocalSpectatorWeaponPresentation();
	if (!IsValid(Weapon))
	{
		if (IsValid(LocalSpectatorPresentationTarget))
		{
			if (USkeletalMeshComponent* Arms = LocalSpectatorPresentationTarget->GetFirstPersonMesh())
			{
				if (UTacticalFirstPersonMeshComponent* FirstPersonMesh =
					Cast<UTacticalFirstPersonMeshComponent>(Arms))
				{
					FirstPersonMesh->SetSpectatorAnimInstanceClass(nullptr);
				}
				else
				{
					Arms->SetAnimInstanceClass(nullptr);
				}
				Arms->SetHiddenInGame(true, false);
				Arms->SetVisibility(false, false);
			}
			ReleaseLocalSpectatorAnimationController(LocalSpectatorPresentationTarget);
		}
		return;
	}

	LocalSpectatorPresentationWeapon = Weapon;
	bCachedPresentationWeaponTick = Weapon->IsActorTickEnabled();
	Weapon->SetActorTickEnabled(true);

	if (USkeletalMeshComponent* WeaponMesh = Weapon->GetFirstPersonMesh())
	{
		bCachedPresentationWeaponOnlyOwnerSee = WeaponMesh->bOnlyOwnerSee;
		bCachedPresentationWeaponHidden = WeaponMesh->bHiddenInGame;
		bCachedPresentationWeaponVisible = WeaponMesh->IsVisible();
		CachedPresentationWeaponAnimClass = WeaponMesh->GetAnimClass();
		UClass* DesiredWeaponAnimClass =
			Weapon->GetFirstPersonWeaponAnimInstanceClass().Get();
		if (DesiredWeaponAnimClass
			&& WeaponMesh->GetAnimClass() != DesiredWeaponAnimClass)
		{
			WeaponMesh->SetAnimInstanceClass(DesiredWeaponAnimClass);
		}
		WeaponMesh->SetOnlyOwnerSee(false);
		if (ATacticalWeapon* TacticalWeapon = Cast<ATacticalWeapon>(Weapon))
		{
			TacticalWeapon->SetRARFirstPersonPartsOnlyOwnerSee(false);
		}
		WeaponMesh->SetHiddenInGame(false, true);
		WeaponMesh->SetVisibility(true, true);
	}

	if (IsValid(LocalSpectatorPresentationTarget))
	{
		if (USkeletalMeshComponent* Arms = LocalSpectatorPresentationTarget->GetFirstPersonMesh())
		{
			USkeletalMesh* DesiredArmsMesh = Weapon->GetFirstPersonCharacterMeshOverride();
			if (!DesiredArmsMesh)
			{
				DesiredArmsMesh = CachedPresentationArmsMesh.Get();
			}
			if (Arms->GetSkeletalMeshAsset() != DesiredArmsMesh)
			{
				Arms->SetSkeletalMeshAsset(DesiredArmsMesh);
			}
			LocalSpectatorPresentationTarget->RefreshStableSpectatorCameraTransform();
			LocalSpectatorPresentationTarget->RefreshFirstPersonWeaponAttachment(Weapon);
			Arms->SetHiddenInGame(false, false);
			Arms->SetVisibility(true, false);
			UClass* FirstPersonAnimClass = Weapon->GetFirstPersonAnimInstanceClass().Get();
			// Run the observed player's weapon-specific first-person graph, whether
			// it uses the legacy Control Rig or the RAR authored animation set.
			if (FirstPersonAnimClass)
			{
				const bool bHasSafeAnimationController =
					EnsureLocalSpectatorAnimationController(LocalSpectatorPresentationTarget);
				if (bHasSafeAnimationController)
				{
					if (UTacticalFirstPersonMeshComponent* FirstPersonMesh =
						Cast<UTacticalFirstPersonMeshComponent>(Arms))
					{
						FirstPersonMesh->SetSpectatorAnimInstanceClass(FirstPersonAnimClass);
					}
					else
					{
						// A real controller (for example on a listen server) makes the
						// original class safe even on an unexpected legacy component.
						Arms->SetAnimInstanceClass(FirstPersonAnimClass);
					}
				}
				else
				{
					Arms->SetAnimInstanceClass(UTacticalSpectatorFPAnimInstance::StaticClass());
				}
			}
			else
			{
				ReleaseLocalSpectatorAnimationController(LocalSpectatorPresentationTarget);
				Arms->SetAnimInstanceClass(UTacticalSpectatorFPAnimInstance::StaticClass());
			}
		}
	}
}

bool ATacticalPlayerController::EnsureLocalSpectatorAnimationController(
	ATacticalCharacter* Target)
{
	if (!IsValid(Target))
	{
		return false;
	}

	const bool bHasRealController = IsValid(Target->GetController());
	// Always install a backup animation controller, including on a listen server.
	// The mesh never replaces a valid real controller, but the backup keeps the
	// AnimBP safe if the observed pawn is unpossessed before it is destroyed.

	if (!IsValid(LocalSpectatorAnimationController))
	{
		UWorld* World = GetWorld();
		if (!World)
		{
			return bHasRealController;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = this;
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		LocalSpectatorAnimationController =
			World->SpawnActor<ATacticalSpectatorAnimationController>(
				ATacticalSpectatorAnimationController::StaticClass(),
				FTransform::Identity,
				SpawnParameters);
	}

	if (!IsValid(LocalSpectatorAnimationController))
	{
		return bHasRealController;
	}

	LocalSpectatorAnimationController->SetAnimationSourcePawn(Target);
	if (UTacticalFirstPersonMeshComponent* FirstPersonMesh =
		Cast<UTacticalFirstPersonMeshComponent>(Target->GetFirstPersonMesh()))
	{
		FirstPersonMesh->SetSpectatorAnimationController(LocalSpectatorAnimationController);
		return true;
	}

	ReleaseLocalSpectatorAnimationController(Target);
	return bHasRealController;
}

void ATacticalPlayerController::ReleaseLocalSpectatorAnimationController(
	ATacticalCharacter* Target)
{
	if (IsValid(Target))
	{
		if (UTacticalFirstPersonMeshComponent* FirstPersonMesh =
			Cast<UTacticalFirstPersonMeshComponent>(Target->GetFirstPersonMesh()))
		{
			FirstPersonMesh->SetSpectatorAnimationController(nullptr);
		}
	}

	if (IsValid(LocalSpectatorAnimationController))
	{
		LocalSpectatorAnimationController->SetAnimationSourcePawn(nullptr);
		LocalSpectatorAnimationController->Destroy();
	}
	LocalSpectatorAnimationController = nullptr;
}

void ATacticalPlayerController::RestoreLocalSpectatorWeaponPresentation()
{
	AShooterWeapon* Weapon = LocalSpectatorPresentationWeapon;
	LocalSpectatorPresentationWeapon = nullptr;
	if (!IsValid(Weapon))
	{
		return;
	}

	if (USkeletalMeshComponent* WeaponMesh = Weapon->GetFirstPersonMesh())
	{
		if (WeaponMesh->GetAnimClass() != CachedPresentationWeaponAnimClass.Get())
		{
			WeaponMesh->SetAnimInstanceClass(CachedPresentationWeaponAnimClass.Get());
		}
		WeaponMesh->SetOnlyOwnerSee(bCachedPresentationWeaponOnlyOwnerSee);
		if (ATacticalWeapon* TacticalWeapon = Cast<ATacticalWeapon>(Weapon))
		{
			TacticalWeapon->SetRARFirstPersonPartsOnlyOwnerSee(true);
		}
		WeaponMesh->SetHiddenInGame(bCachedPresentationWeaponHidden, true);
		WeaponMesh->SetVisibility(bCachedPresentationWeaponVisible, true);
	}
	CachedPresentationWeaponAnimClass = nullptr;
	Weapon->SetActorTickEnabled(bCachedPresentationWeaponTick);
}

void ATacticalPlayerController::OnLocalSpectatedWeaponChanged(
	AShooterWeapon* NewWeapon, AShooterWeapon* OldWeapon)
{
	if (!IsValid(LocalSpectatorPresentationTarget))
	{
		return;
	}
	ApplyLocalSpectatorWeaponPresentation(NewWeapon);
}

void ATacticalPlayerController::OnLocalSpectatedTargetDestroyed(AActor* DestroyedActor)
{
	if (DestroyedActor != LocalSpectatorPresentationTarget)
	{
		return;
	}
	EndLocalSpectatorPresentation();
	PendingSpectatorTargetPlayerId = INDEX_NONE;
	if (bSpectatorInputLockApplied)
	{
		BeginSpectatorTargetAcquisition();
	}
}

// ---- Phase / Money / Match ----

void ATacticalPlayerController::OnPhaseChanged(ERoundPhase NewPhase, float Duration)
{
	const ATacticalPlayerState* TPS = GetPlayerState<ATacticalPlayerState>();
	const bool bObservedNewPhase = !bHasLoggedObservedPhase || LastLoggedObservedPhase != NewPhase;
	if (bObservedNewPhase)
	{
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=ClientPhaseObserved World=\"%s\" NetMode=%s Controller=\"%s\" Team=%s Active=%d Phase=%s Remaining=%.2f Observation=%s"),
			*GetNameSafe(GetWorld()), TacticalDiagnostics::GetNetModeName(GetNetMode()), *GetName(),
			TacticalDiagnostics::GetTeamName(TPS ? TPS->TeamByte : 255),
			TPS ? TPS->bIsActiveParticipant : false, TacticalDiagnostics::GetPhaseName(NewPhase), Duration,
			bHasLoggedObservedPhase ? TEXT("Transition") : TEXT("Initial"));
		bHasLoggedObservedPhase = true;
		LastLoggedObservedPhase = NewPhase;
	}
	ShowBuyMenu(NewPhase == ERoundPhase::Buy && TPS && TPS->bIsActiveParticipant);
	// OnPhaseChanged also seeds LastDisplayedPhaseSecond below. Push the first
	// purchase countdown immediately instead of waiting for the next second.
	if (NewPhase == ERoundPhase::Buy && BuyMenuUI)
	{
		if (BuyMenuUI->HasNativeCatalog())
		{
			BuyMenuUI->UpdateCatalogTimer(Duration);
		}
		else
		{
			BuyMenuUI->BP_UpdateTimer(Duration);
		}
	}
	ApplyPhaseDrivenHudVisibility(NewPhase);
	if (MatchResultStatusUI && NewPhase != ERoundPhase::MatchEnd)
	{
		MatchResultStatusUI->HideResult();
	}
	if (ReadyLobbyUI)
	{
		const bool bShowForLobbySelection = NewPhase == ERoundPhase::PreRound
			&& TPS && TPS->bIsActiveParticipant;
		const bool bShowTeamBadge = TPS && TPS->bIsActiveParticipant && TPS->TeamByte <= 1;
		ReadyLobbyUI->SetVisibility(bShowForLobbySelection || bShowTeamBadge
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed);
	}

	// A sampled coordinate is valid only inside the phase and round in which the
	// server authored it. State may be restored from GameState; old coordinates
	// deliberately are not restored and wait for the next pulse.
	if (bObservedNewPhase)
	{
		ClearHuntIntelPulsePresentation();
		bHuntPulseReceptionClosed = NewPhase != ERoundPhase::Combat;
		if (NewPhase != ERoundPhase::Combat)
		{
			ClearAllHuntIntelPresentation();
			LastAcceptedHuntPulseRound = INDEX_NONE;
			LastAcceptedHuntPulseSequence = 0;
		}
	}

	if (IsLocalPlayerController() && NewPhase == ERoundPhase::PreRound && TPS && TPS->bIsActiveParticipant)
	{
		ApplyGameOnlyInputMode();
	}

	if (ATacticalCharacter* TacChar = Cast<ATacticalCharacter>(GetPawn()))
	{
		const bool bShouldFreeze =
			NewPhase == ERoundPhase::Buy ||
			NewPhase == ERoundPhase::RoundEnd ||
			NewPhase == ERoundPhase::MatchEnd;
		TacChar->ApplyFrozenLocally(bShouldFreeze);
	}

	if (NewPhase == ERoundPhase::Combat && TPS && TPS->bIsActiveParticipant && GetPawn())
	{
		StopSpectating();
	}

	if (TacticalUI)
	{
		TacticalUI->BP_UpdatePhase(NewPhase, Duration);
	}
	LastDisplayedPhase = NewPhase;
	LastDisplayedPhaseSecond = FMath::CeilToInt(FMath::Max(0.0f, Duration));
	bHasReadyDisplayCache = false;
}

void ATacticalPlayerController::ApplyPhaseDrivenHudVisibility(ERoundPhase Phase)
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	const bool bShowGameplayHud = Phase != ERoundPhase::PreRound;
	if (TacticalUI)
	{
		// Crosshair, score, timer, phase and avatar presentation share the same
		// authored widget. Keep that widget alive and isolate its Crosshair branch
		// instead of collapsing the whole tree during readiness.
		TacticalUI->SetVisibility(ESlateVisibility::HitTestInvisible);
		TacticalUI->SetPreRoundCrosshairOnly(!bShowGameplayHud);
	}
	if (CombatFeedbackUI)
	{
		if (Phase != ERoundPhase::Combat)
		{
			// Damage and hit flashes belong to the combat moment that produced them;
			// never carry a late packet into buy, round-end, or the lobby.
			CombatFeedbackUI->ResetTransientFeedback();
		}
		CombatFeedbackUI->SetCrosshairVisible(!bPlayerHudVisibilityCached);
	}

	// Spectator presentation owns the health/ammo and armor visibility while it
	// is active. A phase transition must never reveal the local-player HUD over
	// the server-authored spectator overlay.
	const ESlateVisibility PlayerStatusVisibility =
		bShowGameplayHud && !bPlayerHudVisibilityCached
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed;
	if (BulletCounterUI)
	{
		BulletCounterUI->SetVisibility(PlayerStatusVisibility);
	}
	if (ArmorStatusUI)
	{
		ArmorStatusUI->SetVisibility(PlayerStatusVisibility);
	}
}

void ATacticalPlayerController::EnsureDefaultSidearm(APawn* InPawn)
{
	if (!HasAuthority())
	{
		return;
	}

	ATacticalCharacter* TacticalPawn = Cast<ATacticalCharacter>(InPawn);
	UWeaponInventoryComponent* Inventory = TacticalPawn ? TacticalPawn->GetWeaponInventory() : nullptr;
	if (!TacticalPawn || !Inventory || Inventory->CurrentWeapon)
	{
		return;
	}

	if (const TSubclassOf<AShooterWeapon> DefaultPistolClass = GetWeaponClassForTier(EWeaponTier::DefaultPistol))
	{
		if (!TacticalPawn->TryAddWeaponClass(DefaultPistolClass))
		{
			UE_LOG(LogTcamp_FPS, Warning, TEXT("Unable to grant default pistol to %s"), *GetNameSafe(TacticalPawn));
		}
	}
}

void ATacticalPlayerController::OnMoneyUpdated(int32 NewMoney)
{
	if (BuyMenuUI)
	{
		if (BuyMenuUI->HasNativeCatalog())
		{
			BuyMenuUI->UpdateCatalogMoney(NewMoney);
			BuyMenuUI->RefreshCatalog();
		}
		else
		{
			BuyMenuUI->BP_UpdateMoney(NewMoney);
		}
	}
}

void ATacticalPlayerController::BindTacticalGameStateEvents(bool bForcePresentationCatchUp)
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	ATacticalGameState* GS = GetWorld()
		? GetWorld()->GetGameState<ATacticalGameState>()
		: nullptr;
	if (!GS)
	{
		return;
	}
	const bool bNewBinding = BoundTacticalGameState.Get() != GS;
	if (bNewBinding)
	{
		UnbindTacticalGameStateEvents();
		BoundTacticalGameState = GS;
		bHasLoggedObservedPhase = false;
		LastAcceptedHuntPulseRound = INDEX_NONE;
		LastAcceptedHuntPulseSequence = 0;
		bHuntPulseReceptionClosed = true;
		LastHandledRoundEndSerial = 0;
		LastHandledMatchEndSerial = 0;
		GS->OnPhaseChanged.AddUniqueDynamic(this, &ATacticalPlayerController::OnPhaseChanged);
		GS->OnRoundEnded.AddUniqueDynamic(this, &ATacticalPlayerController::OnRoundEnded);
		GS->OnMatchEnded.AddUniqueDynamic(this, &ATacticalPlayerController::OnMatchEnded);
		GS->OnHuntIntelStateChanged.AddUniqueDynamic(
			this, &ATacticalPlayerController::OnHuntIntelStateChanged);
		GS->OnTreasureStateChanged.AddUniqueDynamic(
			this, &ATacticalPlayerController::OnTreasureStateChanged);
	}
	else if (!bForcePresentationCatchUp)
	{
		return;
	}

	OnPhaseChanged(GS->CurrentPhase, GS->GetPhaseTimeRemaining());
	if (TacticalUI)
	{
		TacticalUI->BP_UpdateTeamScores(GS->TeamScores[0], GS->TeamScores[1]);
	}
	// A late-created controller restores only the public warning state. Sampled
	// coordinates are intentionally not stored in GameState or replayed.
	OnHuntIntelStateChanged(GS->bHuntIntelActive, GS->bHuntIntelEscalated);
	OnTreasureStateChanged();

	if (GS->CurrentPhase == ERoundPhase::RoundEnd && GS->RoundEndSerial != 0)
	{
		OnRoundEnded(GS->LastRoundWinningTeam, GS->TeamScores[0],
			GS->TeamScores[1], GS->LastRoundEndReason);
	}
	if (GS->CurrentPhase == ERoundPhase::MatchEnd && GS->MatchEndSerial != 0)
	{
		OnMatchEnded(GS->LastMatchWinningTeam);
	}

	if (bNewBinding)
	{
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=TacticalGameStateUIBound Controller=\"%s\" GameState=\"%s\" Phase=%s Round=%d"),
			*GetName(), *GetNameSafe(GS), TacticalDiagnostics::GetPhaseName(GS->CurrentPhase),
			GS->RoundNumber);
	}
}

void ATacticalPlayerController::UnbindTacticalGameStateEvents()
{
	ATacticalGameState* GS = BoundTacticalGameState.Get();
	BoundTacticalGameState.Reset();
	if (!IsValid(GS))
	{
		return;
	}

	GS->OnPhaseChanged.RemoveDynamic(this, &ATacticalPlayerController::OnPhaseChanged);
	GS->OnRoundEnded.RemoveDynamic(this, &ATacticalPlayerController::OnRoundEnded);
	GS->OnMatchEnded.RemoveDynamic(this, &ATacticalPlayerController::OnMatchEnded);
	GS->OnHuntIntelStateChanged.RemoveDynamic(
		this, &ATacticalPlayerController::OnHuntIntelStateChanged);
	GS->OnTreasureStateChanged.RemoveDynamic(
		this, &ATacticalPlayerController::OnTreasureStateChanged);
}

void ATacticalPlayerController::BindMoneyUpdates()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	ATacticalPlayerState* TPS = GetPlayerState<ATacticalPlayerState>();
	if (!TPS)
	{
		return;
	}

	TPS->OnMoneyUpdated.RemoveDynamic(this, &ATacticalPlayerController::OnMoneyUpdated);
	TPS->OnMoneyUpdated.AddDynamic(this, &ATacticalPlayerController::OnMoneyUpdated);
	OnMoneyUpdated(TPS->Money);
}

void ATacticalPlayerController::BindArmorUpdates()
{
	if (!IsLocalPlayerController())
	{
		return;
	}

	ATacticalPlayerState* TPS = GetPlayerState<ATacticalPlayerState>();
	if (TPS)
	{
		TPS->OnArmorUpdated.RemoveDynamic(this, &ATacticalPlayerController::OnArmorChanged);
		TPS->OnArmorUpdated.AddDynamic(this, &ATacticalPlayerController::OnArmorChanged);
		OnArmorChanged(TPS->ArmorType, TPS->GetArmorDurabilityPercent());
	}
	else
	{
		OnArmorChanged(EArmorType::None, 0.0f);
	}
}

void ATacticalPlayerController::OnArmorChanged(EArmorType Type, float DurabilityPercent)
{
	float CurrentDurability = 0.0f;
	float MaxDurability = 0.0f;
	if (const ATacticalPlayerState* TPS = GetPlayerState<ATacticalPlayerState>())
	{
		CurrentDurability = TPS->ArmorDurability;
		MaxDurability = Type == EArmorType::None
			? 0.0f
			: ATacticalPlayerState::GetArmorMaxDurability(Type);
	}

	if (ArmorStatusUI)
	{
		ArmorStatusUI->SetArmorDisplay(Type, CurrentDurability, MaxDurability);
	}
	if (TacticalUI)
	{
		// Keep the existing Blueprint extension point alive for future visual polish.
		TacticalUI->BP_UpdateArmorHUD(Type, FMath::Clamp(DurabilityPercent, 0.0f, 1.0f));
	}
}

void ATacticalPlayerController::OnRoundEnded(uint8 WinningTeam, int32 TeamAScore, int32 TeamBScore, ERoundEndReason Reason)
{
	bHuntPulseReceptionClosed = true;
	ClearAllHuntIntelPresentation();
	const ATacticalGameState* RoundGS = GetWorld()
		? GetWorld()->GetGameState<ATacticalGameState>()
		: nullptr;
	if (RoundGS && RoundGS->RoundEndSerial != 0)
	{
		if (LastHandledRoundEndSerial == RoundGS->RoundEndSerial)
		{
			return;
		}
		LastHandledRoundEndSerial = RoundGS->RoundEndSerial;
	}
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=ClientRoundResult World=\"%s\" NetMode=%s Controller=\"%s\" Winner=%s Reason=%s ScoreA=%d ScoreB=%d"),
		*GetNameSafe(GetWorld()), TacticalDiagnostics::GetNetModeName(GetNetMode()), *GetName(),
		TacticalDiagnostics::GetOutcomeName(WinningTeam),
		TacticalDiagnostics::GetRoundEndReasonName(Reason), TeamAScore, TeamBScore);
	if (TacticalUI)
	{
		TacticalUI->BP_UpdateTeamScores(TeamAScore, TeamBScore);
		TacticalUI->BP_ShowRoundEndReason(WinningTeam, Reason);
	}
}

void ATacticalPlayerController::OnMatchEnded(uint8 WinningTeam)
{
	bHuntPulseReceptionClosed = true;
	ClearAllHuntIntelPresentation();
	const ATacticalGameState* MatchGS = GetWorld() ? GetWorld()->GetGameState<ATacticalGameState>() : nullptr;
	if (MatchGS && MatchGS->MatchEndSerial != 0)
	{
		if (LastHandledMatchEndSerial == MatchGS->MatchEndSerial)
		{
			return;
		}
		LastHandledMatchEndSerial = MatchGS->MatchEndSerial;
	}
	StopSpectating();
	const EMatchEndReason MatchEndReason = MatchGS
		? MatchGS->LastMatchEndReason
		: EMatchEndReason::Unknown;
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=ClientMatchResult World=\"%s\" NetMode=%s Controller=\"%s\" Winner=%s Reason=%s ScoreA=%d ScoreB=%d"),
		*GetNameSafe(GetWorld()), TacticalDiagnostics::GetNetModeName(GetNetMode()), *GetName(),
		TacticalDiagnostics::GetMatchOutcomeName(WinningTeam, MatchEndReason),
		TacticalDiagnostics::GetMatchEndReasonName(MatchEndReason),
		MatchGS ? MatchGS->TeamScores[0] : 0, MatchGS ? MatchGS->TeamScores[1] : 0);

	if (TacticalUI)
	{
		if (ATacticalGameState* GS = GetWorld()->GetGameState<ATacticalGameState>())
		{
			TacticalUI->BP_UpdateTeamScores(GS->TeamScores[0], GS->TeamScores[1]);
			// The legacy one-parameter result hook cannot distinguish DRAW from
			// technical no-result. Suppress it for abort/host-loss so it cannot
			// contradict the native reason-aware banner.
			if (MatchEndReason != EMatchEndReason::AllTeamsDisconnectedAbort
				&& MatchEndReason != EMatchEndReason::HostLost)
			{
				TacticalUI->BP_ShowMatchResult(
					WinningTeam, GS->TeamScores[0], GS->TeamScores[1]);
			}
			TacticalUI->BP_ShowMatchEndReason(WinningTeam, GS->LastMatchEndReason);
		}
	}
	if (MatchResultStatusUI && MatchGS)
	{
		MatchResultStatusUI->ShowSpecialResult(
			WinningTeam, MatchGS->LastMatchEndReason,
			MatchGS->TeamScores[0], MatchGS->TeamScores[1]);
	}
}

void ATacticalPlayerController::OnHuntIntelStateChanged(bool bActive, bool bEscalated)
{
	const ATacticalGameState* GS = GetWorld()
		? GetWorld()->GetGameState<ATacticalGameState>()
		: nullptr;
	const ATacticalPlayerState* TPS = GetPlayerState<ATacticalPlayerState>();
	const bool bEligibleLocalTeam = TPS && TPS->TeamByte <= 1;
	const bool bShouldDisplay = bActive && bEligibleLocalTeam
		&& GS && GS->CurrentPhase == ERoundPhase::Combat;
	if (HuntIntelStatusUI)
	{
		HuntIntelStatusUI->SetHuntIntelState(bShouldDisplay, bEscalated);
	}
	if (TacticalUI)
	{
		TacticalUI->BP_SetHuntIntelState(bShouldDisplay, bEscalated);
		if (!bShouldDisplay)
		{
			TacticalUI->BP_ClearHuntIntel();
		}
	}
}

void ATacticalPlayerController::OnTreasureStateChanged()
{
	if (!TreasureStatusUI)
	{
		return;
	}

	const ATacticalGameState* GS = GetWorld()
		? GetWorld()->GetGameState<ATacticalGameState>()
		: nullptr;
	if (!GS || GS->CurrentPhase != ERoundPhase::Combat
		|| GS->TreasureState.State == ETreasureRoundState::Disabled
		|| GS->TreasureState.State == ETreasureRoundState::WaitingForAnnouncement)
	{
		TreasureStatusUI->ClearTreasure();
		return;
	}

	TreasureStatusUI->SetTreasureState(GS->TreasureState);
}

void ATacticalPlayerController::ClearHuntIntelPulsePresentation()
{
	if (HuntIntelStatusUI)
	{
		HuntIntelStatusUI->ClearPulseMarkers();
	}
	if (TacticalUI)
	{
		TacticalUI->BP_ClearHuntIntel();
		// The legacy hook may combine its marker and warning teardown. Restore the
		// public warning while the current Combat state remains active.
		if (const ATacticalGameState* GS = GetWorld()
			? GetWorld()->GetGameState<ATacticalGameState>()
			: nullptr;
			GS && GS->CurrentPhase == ERoundPhase::Combat && GS->bHuntIntelActive)
		{
			TacticalUI->BP_SetHuntIntelState(true, GS->bHuntIntelEscalated);
		}
	}
}

void ATacticalPlayerController::ClearAllHuntIntelPresentation()
{
	if (HuntIntelStatusUI)
	{
		HuntIntelStatusUI->ClearHuntIntel();
	}
	if (TacticalUI)
	{
		TacticalUI->BP_SetHuntIntelState(false, false);
		TacticalUI->BP_ClearHuntIntel();
	}
}
