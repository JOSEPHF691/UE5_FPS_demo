// Copyright Epic Games, Inc. All Rights Reserved.

#include "TacticalGameMode.h"

#include "Online/TacticalSessionSubsystem.h"
#include "TacticalGameState.h"
#include "TacticalPlayerState.h"
#include "TacticalCharacter.h"
#include "TacticalPlayerController.h"
#include "TacticalReadyTarget.h"
#include "TacticalSpawnPoint.h"
#include "TacticalDiagnostics.h"
#include "TacticalRules.h"
#include "Network/TacticalDisconnectedController.h"
#include "Network/TacticalMatchRosterSubsystem.h"
#include "Network/TacticalNetworkAdmission.h"
#include "Tcamp_FPS.h"
#include "ShooterWeapon.h"
#include "Armor/ArmorComponent.h"
#include "Components/HealthComponent.h"
#include "Treasure/TreasureChest.h"
#include "AI/ShooterNPC.h"
#include "AI/TacticalGuardArena.h"
#include "AI/TacticalGuardSpawnPoint.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY(LogTacticalFlow);

namespace
{
	FString BuildProviderQualifiedIdentity(const FUniqueNetIdRepl& UniqueId)
	{
		if (!UniqueId.IsValid() || !UniqueId.GetUniqueNetId().IsValid())
		{
			return FString();
		}
		const TSharedPtr<const FUniqueNetId> NetId = UniqueId.GetUniqueNetId();
		return FString::Printf(TEXT("UID:%s:%s"),
			*NetId->GetType().ToString(), *NetId->ToString());
	}

	bool IsSteamProviderIdentity(const FString& IdentityKey)
	{
		return IdentityKey.StartsWith(TEXT("UID:STEAM:"), ESearchCase::IgnoreCase);
	}

	bool IsAllowedPhaseTransition(ERoundPhase From, ERoundPhase To)
	{
		if (From == To)
		{
			return true;
		}

		switch (From)
		{
		case ERoundPhase::PreRound:
			return To == ERoundPhase::Buy;
		case ERoundPhase::Buy:
			return To == ERoundPhase::Combat;
		case ERoundPhase::Combat:
			return To == ERoundPhase::RoundEnd;
		case ERoundPhase::RoundEnd:
			return To == ERoundPhase::Buy || To == ERoundPhase::MatchEnd;
		default:
			return false;
		}
	}

	bool HasCoherentReplacementOwner(
		const FTacticalMatchParticipantRecord* Record,
		const APlayerController* DepartingController)
	{
		if (!Record)
		{
			return false;
		}
		APlayerController* CurrentOwner = Record->PlayerController.Get();
		ATacticalPlayerState* CurrentState = Record->PlayerState.Get();
		return IsValid(CurrentOwner)
			&& CurrentOwner != DepartingController
			&& IsValid(CurrentState)
			&& CurrentOwner->GetPlayerState<ATacticalPlayerState>() == CurrentState;
	}
}

ATacticalGameMode::ATacticalGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;
	GameStateClass = ATacticalGameState::StaticClass();
	PlayerStateClass = ATacticalPlayerState::StaticClass();

	// Treasure site is now authored by the placed chest. No runtime relocation.
}

void ATacticalGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	TacticalGS = Cast<ATacticalGameState>(GameState);
	RosterSubsystem = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UTacticalMatchRosterSubsystem>()
		: nullptr;
	if (RosterSubsystem)
	{
		RosterSubsystem->ResetForNewMatch();
	}
}

void ATacticalGameMode::PreLogin(
	const FString& Options,
	const FString& Address,
	const FUniqueNetIdRepl& UniqueId,
	FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		return;
	}

	FString IdentityKey;
	bool bIsReconnect = false;
	if (!ValidateAdmissionOptions(
		Options, UniqueId, ErrorMessage, &IdentityKey, &bIsReconnect))
	{
		const FTacticalMatchParticipantRecord* Record = RosterSubsystem
			? RosterSubsystem->FindByIdentity(IdentityKey)
			: nullptr;
		UE_LOG(LogTacticalFlow, Warning,
			TEXT("Event=ConnectionAdmissionRejected Participant=%s Reason=%s"),
			Record ? *Record->ParticipantId.ToString() : TEXT("Unknown"),
			*ErrorMessage);
	}
}

FString ATacticalGameMode::InitNewPlayer(
	APlayerController* NewPlayerController,
	const FUniqueNetIdRepl& UniqueId,
	const FString& Options,
	const FString& Portal)
{
	FString AdmissionError;
	FString AdmissionIdentity;
	bool bIsReconnect = false;
	FGuid AdmissionParticipantId;
	if (!ValidateAdmissionOptions(
		Options, UniqueId, AdmissionError, &AdmissionIdentity, &bIsReconnect,
		&AdmissionParticipantId))
	{
		return AdmissionError;
	}

	const FString Result = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	if (!Result.IsEmpty())
	{
		return Result;
	}
#if WITH_EDITOR
	if (NewPlayerController)
	{
		const FString PIEIdentity = UGameplayStatics::ParseOption(
			Options, TEXT("TacticalPIEReconnectId"));
		if (!PIEIdentity.IsEmpty())
		{
			EditorReconnectIdentityOverrides.Add(NewPlayerController, PIEIdentity);
			UE_LOG(LogTacticalFlow, Display,
				TEXT("Event=PIEReconnectIdentityAccepted Controller=\"%s\" TestIdentity=\"%s\""),
				*GetNameSafe(NewPlayerController), *PIEIdentity);
		}
	}
#endif
	#if !UE_BUILD_SHIPPING
	if (NewPlayerController && AdmissionIdentity.IsEmpty() && RosterSubsystem)
	{
		AdmissionIdentity = RosterSubsystem->CreateGuestIdentityKey();
	}
	if (NewPlayerController && AdmissionIdentity.StartsWith(TEXT("GUEST:")))
	{
		GuestIdentityOverrides.Add(NewPlayerController, AdmissionIdentity);
	}
	#endif
	if (NewPlayerController && bIsReconnect && !AdmissionIdentity.IsEmpty())
	{
		FValidatedRosterReconnectAdmission Admission;
		Admission.IdentityKey = AdmissionIdentity;
		Admission.ParticipantId = AdmissionParticipantId;
		ValidatedReconnectAdmissions.Add(NewPlayerController, MoveTemp(Admission));
		ValidatedReconnectCredentials.Add(
			NewPlayerController,
			UGameplayStatics::ParseOption(
				Options, TacticalNetworkAdmission::ReconnectCredentialOption));
	}
	return Result;
}

void ATacticalGameMode::BeginPlay()
{
	Super::BeginPlay();
	TacticalGS = Cast<ATacticalGameState>(GameState);
	if (TacticalGS)
	{
		TacticalGS->MaxRounds = TacticalEconomy::MaxRounds;

		// Subscribe to GameState kill events instead of expecting direct calls from characters
		TacticalGS->OnPlayerKilled.AddDynamic(this, &ATacticalGameMode::OnPlayerKilled);
		TacticalGS->OnNPCKilled.AddDynamic(this, &ATacticalGameMode::OnNPCKilled);
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UTacticalSessionSubsystem* SessionSubsystem =
			GameInstance->GetSubsystem<UTacticalSessionSubsystem>())
		{
			// A completed match stays closed throughout MatchEnd. Only the fresh
			// restarted gameplay world may reopen the existing host lobby.
			SessionSubsystem->SetGameplayMatchInProgress(false);
		}
	}

	CacheAndValidatePlacedSpawnPoints();
	EnsureReadyTargets();
	EnsureTeamPlayerStarts();
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=ServerInitialized World=\"%s\" NetMode=%s BuyDuration=%.2f CombatDuration=%.2f RoundEndDuration=%.2f MatchEndDuration=%.2f MinPlayers=%d MaxPlayersPerTeam=%d TreasureEnabled=%d"),
		*GetNameSafe(GetWorld()), TacticalDiagnostics::GetNetModeName(GetNetMode()),
		BuyPhaseDuration, CombatPhaseDuration, RoundEndDuration, MatchEndDuration,
		FMath::Clamp(MinPlayersToStart, 2, TacticalEconomy::MaxPlayersPerTeam * 2),
		TacticalEconomy::MaxPlayersPerTeam, bTreasureEnabled);
	LogFlowSnapshot(TEXT("ServerInitialized"));
	TryStartMatch();
}

void ATacticalGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!TacticalGS || !HasAuthority())
	{
		return;
	}

	if (TacticalGS->CurrentPhase == ERoundPhase::Combat)
	{
		TickTreasureTimeline();
		TickHuntIntel(DeltaSeconds);
	}

	ServiceDisconnectedParticipants();

	if (TacticalGS->TickPhaseTimer(DeltaSeconds))
	{
		OnPhaseTimerExpired();
	}
}

void ATacticalGameMode::PostLogin(APlayerController* NewPlayer)
{
	if (!NewPlayer)
	{
		return;
	}
	if (!HasAuthority())
	{
		Super::PostLogin(NewPlayer);
		return;
	}

	const FString PreparedIdentity = ResolveReconnectIdentity(NewPlayer);
	const FValidatedRosterReconnectAdmission* ValidatedAdmission =
		ValidatedReconnectAdmissions.Find(NewPlayer);
	const bool bValidatedRosterReconnect = !PreparedIdentity.IsEmpty()
		&& ValidatedAdmission
		&& ValidatedAdmission->IdentityKey == PreparedIdentity
		&& ValidatedAdmission->ParticipantId.IsValid();
	const FGuid ValidatedParticipantId = bValidatedRosterReconnect
		? ValidatedAdmission->ParticipantId
		: FGuid();
	const bool bPreparedRosterReconnect = bValidatedRosterReconnect
		&& PrepareRosterParticipantReconnect(
			NewPlayer, PreparedIdentity, ValidatedParticipantId);
	if (!IsValid(NewPlayer))
	{
		return;
	}
	Super::PostLogin(NewPlayer);
	if (!IsValid(NewPlayer))
	{
		// A GameSession/Blueprint login callback may reject and destroy the new
		// controller. Logout owns transaction rollback in that case.
		return;
	}
	if (bPreparedRosterReconnect)
	{
		if (APawn* BootstrapPawn = NewPlayer->GetPawn())
		{
			if (FPendingRosterReconnectTransaction* Transaction =
				PendingRosterReconnects.Find(NewPlayer))
			{
				Transaction->BootstrapPawn = BootstrapPawn;
			}
		}
	}

	ATacticalPlayerState* TPS = NewPlayer->GetPlayerState<ATacticalPlayerState>();
	if (!TPS)
	{
		if (bPreparedRosterReconnect)
		{
			// Keep the pending marker intact so Logout can roll the preserved
			// PlayerState/Pawn transaction back to its proxy.
			NewPlayer->Destroy();
		}
		return;
	}
	TPS->SetConnectionStatus(ETacticalPublicConnectionStatus::Connected);

	const FString PlayerID = PreparedIdentity.IsEmpty()
		? ResolveReconnectIdentity(NewPlayer)
		: PreparedIdentity;
	const bool bRosterReconnectRestored = bPreparedRosterReconnect
		&& TryRestoreRosterParticipant(NewPlayer, PlayerID);
	if (bRosterReconnectRestored)
	{
		if (FTacticalMatchParticipantRecord* Record = RosterSubsystem
			? RosterSubsystem->FindByIdentity(PlayerID)
			: nullptr)
		{
			SendReconnectAdmission(NewPlayer, *Record, true,
				ValidatedReconnectCredentials.FindRef(NewPlayer));
		}
		ValidatedReconnectAdmissions.Remove(NewPlayer);
		ValidatedReconnectCredentials.Remove(NewPlayer);
		ValidateTacticalState(TEXT("RosterParticipantReconnected"));
		TryStartMatch();
		return;
	}
	if (bPreparedRosterReconnect)
	{
		if (!IsValid(NewPlayer))
		{
			return;
		}
		TPS = NewPlayer->GetPlayerState<ATacticalPlayerState>();
		if (!IsValid(TPS))
		{
			// Do not half-commit an online controller without its preserved state.
			// Logout observes the still-present transaction marker and rolls back.
			NewPlayer->Destroy();
			return;
		}
		const FString ReconnectPlayerName = TPS->GetPlayerName();
		UE_LOG(LogTacticalFlow, Error,
			TEXT("Event=RosterParticipantReconnectFailed Player=\"%s\" Reason=CompletionRejected Recovery=Rollback"),
			*ReconnectPlayerName);
		// Keep the transaction marker and preserved object graph intact. Logout
		// restores the proxy/body atomically; a failed takeover must never convert
		// a living disconnected participant into a death or destroy its equipment.
		NewPlayer->Destroy();
		return;
	}
	const FTacticalMatchParticipantRecord* SnapshotAdmissionRecord =
		bValidatedRosterReconnect && RosterSubsystem
			? RosterSubsystem->FindByIdentity(PlayerID)
			: nullptr;
	const FPlayerDisconnectState* CredentialedSnapshot =
		DisconnectedPlayers.Find(PlayerID);
	const bool bHasCredentialedSnapshotFallback = SnapshotAdmissionRecord
		&& TacticalRules::IsSameParticipantGeneration(
			ValidatedParticipantId, SnapshotAdmissionRecord->ParticipantId)
		&& CredentialedSnapshot
		&& TacticalRules::IsSameParticipantGeneration(
			ValidatedParticipantId, CredentialedSnapshot->ParticipantId);
	if (bValidatedRosterReconnect && !bHasCredentialedSnapshotFallback)
	{
		// A credential was valid at admission, but the preserved object graph is
		// no longer recoverable. Never reinterpret this as a fresh connection.
		ValidatedReconnectAdmissions.Remove(NewPlayer);
		ValidatedReconnectCredentials.Remove(NewPlayer);
		TPS->SetUniqueId(FUniqueNetIdRepl());
		TPS->bIsActiveParticipant = false;
		TPS->SetConnectionStatus(ETacticalPublicConnectionStatus::Disconnected);
		const FText Reason = FText::FromString(
			TEXT("Reconnect state is no longer available."));
		const bool bKicked = GameSession && GameSession->KickPlayer(NewPlayer, Reason);
		if (!bKicked)
		{
			NewPlayer->ClientReturnToMainMenuWithTextReason(Reason);
			NewPlayer->Destroy();
		}
		return;
	}
	if (!PlayerID.IsEmpty() && RosterSubsystem)
	{
		if (const FTacticalMatchParticipantRecord* ExistingRecord =
			RosterSubsystem->FindByIdentity(PlayerID);
			ExistingRecord && ExistingRecord->PlayerController.IsValid())
		{
			// PreLogin is the primary rejection point. If a non-network/local path
			// bypasses it, never leave a second controller logged in as a successful
			// spectator with the same session identity: its later NotifyLogout could
			// unregister the real connection.
			TPS->SetUniqueId(FUniqueNetIdRepl());
			TPS->bIsActiveParticipant = false;
			TPS->SetConnectionStatus(ETacticalPublicConnectionStatus::Disconnected);
			TPS->ForceNetUpdate();
			UE_LOG(LogTacticalFlow, Error,
				TEXT("Event=DuplicateParticipantInvariantViolation Player=\"%s\" Participant=%s Action=Kick"),
				*TPS->GetPlayerName(), *ExistingRecord->ParticipantId.ToString());
			const FText Reason = FText::FromString(
				TEXT("This participant is already connected."));
			const bool bKicked = GameSession
				&& GameSession->KickPlayer(NewPlayer, Reason);
			if (!bKicked)
			{
				NewPlayer->ClientReturnToMainMenuWithTextReason(Reason);
				NewPlayer->Destroy();
			}
			return;
		}
	}

	// Restore persistent equipment/economy. A lobby reconnect keeps the team that
	// the player previously chose. This map is now only the fallback after a
	// pre-match grace record has expired or preservation could not be established.
	if (!PlayerID.IsEmpty())
	{
		FTacticalMatchParticipantRecord* AdmissionRecord = RosterSubsystem
			? RosterSubsystem->FindByIdentity(PlayerID)
			: nullptr;
		if (const FPlayerDisconnectState* SavedState =
			bValidatedRosterReconnect && AdmissionRecord
				? DisconnectedPlayers.Find(PlayerID)
				: nullptr)
		{
			if (!TacticalRules::IsSameParticipantGeneration(
				ValidatedParticipantId, SavedState->ParticipantId))
			{
				ValidatedReconnectAdmissions.Remove(NewPlayer);
				ValidatedReconnectCredentials.Remove(NewPlayer);
				NewPlayer->Destroy();
				return;
			}
			// Neither a TMap value pointer nor an actor pointer may cross the two
			// Blueprint-extensible restore delegates without a value/weak snapshot.
			const FPlayerDisconnectState SavedStateSnapshot = *SavedState;
			TWeakObjectPtr<APlayerController> NewPlayerWeak = NewPlayer;
			TWeakObjectPtr<ATacticalPlayerState> PlayerStateWeak = TPS;
			RestorePlayerState(TPS, SavedStateSnapshot);
			NewPlayer = NewPlayerWeak.Get();
			TPS = PlayerStateWeak.Get();
			FTacticalMatchParticipantRecord* ExistingRecord = RosterSubsystem
				? RosterSubsystem->FindByIdentity(PlayerID)
				: nullptr;
			const bool bGenerationStillAvailable = IsValid(NewPlayer)
				&& IsValid(TPS)
				&& NewPlayer->GetPlayerState<ATacticalPlayerState>() == TPS
				&& ExistingRecord
				&& TacticalRules::IsSameParticipantGeneration(
					ValidatedParticipantId, ExistingRecord->ParticipantId)
				&& !ExistingRecord->PlayerController.IsValid()
				&& (!ExistingRecord->PlayerState.IsValid()
					|| ExistingRecord->PlayerState.Get() == TPS);
			if (!bGenerationStillAvailable)
			{
				const bool bCurrentRecordOwnsConnection = ExistingRecord
					&& ExistingRecord->PlayerController.Get() == NewPlayer
					&& ExistingRecord->PlayerState.Get() == TPS;
				UE_LOG(LogTacticalFlow, Error,
					TEXT("Event=ReconnectSnapshotRestoreRejected Participant=%s Reason=GenerationOrOwnerChanged"),
					ValidatedParticipantId.IsValid()
						? *ValidatedParticipantId.ToString()
						: TEXT("Invalid"));
				ValidatedReconnectAdmissions.Remove(NewPlayerWeak);
				ValidatedReconnectCredentials.Remove(NewPlayerWeak);
				if (IsValid(NewPlayer) && !bCurrentRecordOwnsConnection)
				{
					RemoveFromTeam(NewPlayer);
					NewPlayer->Destroy();
				}
				return;
			}

			RemoveFromTeam(NewPlayer);
			uint8 RestoredTeam = SavedStateSnapshot.TeamByte <= 1
				? SavedStateSnapshot.TeamByte
				: uint8{255};
			if (ExistingRecord->bRosterSlotReserved
				&& ExistingRecord->TeamByte <= 1)
			{
				RestoredTeam = ExistingRecord->TeamByte;
			}
			else if (RestoredTeam <= 1
				&& CountReservedTeamSlots(RestoredTeam) >=
					TacticalEconomy::MaxPlayersPerTeam)
			{
				RestoredTeam = 255;
			}
			const bool bHasLobbySlot = !bMatchStarted
				&& CountLobbyCandidates() < TacticalEconomy::MaxPlayersPerTeam * 2;
			const bool bRestoredTeamSlot = ExistingRecord->bRosterSlotReserved
				&& RestoredTeam <= 1;
			TPS->TeamByte = bMatchStarted && !bRestoredTeamSlot
				? uint8{255}
				: RestoredTeam;
			TPS->bIsActiveParticipant = bHasLobbySlot;
			TPS->SetConnectionStatus(ETacticalPublicConnectionStatus::Connected);
			TPS->SetReady(false);
			if ((bMatchStarted && bRestoredTeamSlot) || bHasLobbySlot)
			{
				if (TPS->TeamByte <= 1)
				{
					(TPS->TeamByte == 0 ? TeamAPlayers : TeamBPlayers)
						.AddUnique(NewPlayer);
				}
			}

			// Publish canonical ownership before spawning/moving/spectator callbacks.
			ExistingRecord->PlayerController = NewPlayer;
			ExistingRecord->PlayerState = TPS;
			ExistingRecord->TeamByte = TPS->TeamByte;
			ExistingRecord->bRosterSlotReserved = bMatchStarted
				? bRestoredTeamSlot
				: TPS->TeamByte <= 1;
			ExistingRecord->DisconnectedController = nullptr;
			ExistingRecord->RetainedPawn = nullptr;
			ExistingRecord->ReconnectDeadlineServerTime = 0.0f;
			ExistingRecord->ConnectionState = bMatchStarted
				? ETacticalParticipantConnectionState::ReconnectedSpectating
				: ETacticalParticipantConnectionState::ConnectedPlaying;
			DisconnectedPlayers.Remove(PlayerID);
			TPS->ForceNetUpdate();

			if (!TPS->bIsActiveParticipant)
			{
				// A valid reconnect waits in recoverable spectator state until StartRound.
				MovePlayerToSpectator(NewPlayer, bMatchStarted && bRestoredTeamSlot);
			}
			else
			{
				if (ATacticalCharacter* TacticalCharacter =
					Cast<ATacticalCharacter>(NewPlayer->GetPawn()))
				{
					TacticalCharacter->SetTeam(TPS->TeamByte);
					TacticalCharacter->SetFrozen(false);
				}
				if (TPS->TeamByte <= 1)
				{
					MovePlayerToAssignedStart(NewPlayer, TPS->TeamByte);
				}
			}

			NewPlayer = NewPlayerWeak.Get();
			TPS = PlayerStateWeak.Get();
			FTacticalMatchParticipantRecord* CurrentRecord = RosterSubsystem
				? RosterSubsystem->FindByIdentity(PlayerID)
				: nullptr;
			if (IsValid(NewPlayer) && IsValid(TPS) && CurrentRecord
				&& TacticalRules::IsSameParticipantGeneration(
					ValidatedParticipantId, CurrentRecord->ParticipantId)
				&& CurrentRecord->PlayerController.Get() == NewPlayer
				&& CurrentRecord->PlayerState.Get() == TPS)
			{
				SendReconnectAdmission(NewPlayer, *CurrentRecord, true,
					ValidatedReconnectCredentials.FindRef(NewPlayer));
				ValidatedReconnectAdmissions.Remove(NewPlayer);
				ValidatedReconnectCredentials.Remove(NewPlayer);
				UE_LOG(LogTacticalFlow, Display,
					TEXT("Event=PlayerReconnected Player=\"%s\" Team=%s LobbyCandidate=%d Spectator=%d CanJoinNextRound=%d PendingSettlementRound=%d MatchStarted=%d"),
					*TPS->GetPlayerName(), TacticalDiagnostics::GetTeamName(TPS->TeamByte),
					TPS->bIsActiveParticipant, !TPS->bIsActiveParticipant,
					bMatchStarted && bRestoredTeamSlot,
					TPS->PendingSettlementRound, bMatchStarted);
				ValidateTacticalState(TEXT("PlayerReconnected"));
				TryStartMatch();
			}
			else
			{
				ValidatedReconnectAdmissions.Remove(NewPlayerWeak);
				ValidatedReconnectCredentials.Remove(NewPlayerWeak);
				UE_LOG(LogTacticalFlow, Error,
					TEXT("Event=ReconnectSnapshotCommitChanged Participant=%s Result=AdmissionCleared"),
					ValidatedParticipantId.IsValid()
						? *ValidatedParticipantId.ToString()
						: TEXT("Invalid"));
			}
			return;
		}
	}
	else
	{
		UE_LOG(LogTacticalFlow, Warning,
			TEXT("Event=ReconnectIdentityUnavailable Player=\"%s\" Result=FreshJoin"),
			*TPS->GetPlayerName());
	}

	TPS->InitializeForMatch();
	TPS->TeamByte = 255;
	TPS->SetReady(false);
	TPS->SetConnectionStatus(ETacticalPublicConnectionStatus::Connected);
	TPS->bIsActiveParticipant = !bMatchStarted
		&& CountLobbyCandidates() < TacticalEconomy::MaxPlayersPerTeam * 2;
	TPS->ForceNetUpdate();

	if (RosterSubsystem && !PlayerID.IsEmpty() && !bMatchStarted
		&& TPS->bIsActiveParticipant)
	{
		FTacticalMatchParticipantRecord& Record =
			RosterSubsystem->RegisterParticipant(PlayerID, NewPlayer, TPS);
		Record.ConnectionState = ETacticalParticipantConnectionState::ConnectedPlaying;
		SendReconnectAdmission(NewPlayer, Record, false);
	}

	if (!TPS->bIsActiveParticipant)
	{
		MovePlayerToSpectator(NewPlayer, false);
	}
	else if (ATacticalCharacter* TacticalCharacter = Cast<ATacticalCharacter>(NewPlayer->GetPawn()))
	{
		TacticalCharacter->SetTeam(255);
		TacticalCharacter->SetFrozen(false);
	}

	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=PlayerJoined Player=\"%s\" Team=%s LobbyCandidate=%d Spectator=%d MatchStarted=%d Candidates=%d TeamA=%d TeamB=%d"),
		*TPS->GetPlayerName(), TacticalDiagnostics::GetTeamName(TPS->TeamByte),
		TPS->bIsActiveParticipant, !TPS->bIsActiveParticipant, bMatchStarted, CountLobbyCandidates(),
		TeamAPlayers.Num(), TeamBPlayers.Num());
	ValidateTacticalState(TEXT("PlayerJoined"));

	TryStartMatch();
}

void ATacticalGameMode::HandleExplicitLeave(
	ATacticalPlayerController* PlayerController)
{
	if (!HasAuthority() || !IsValid(PlayerController)
		|| ExplicitlyLeavingPlayers.Contains(PlayerController))
	{
		return;
	}

	ExplicitlyLeavingPlayers.Add(PlayerController);
	ATacticalPlayerState* TPS =
		PlayerController->GetPlayerState<ATacticalPlayerState>();
	FTacticalMatchParticipantRecord* Record = RosterSubsystem && TPS
		? RosterSubsystem->FindByPlayerState(TPS)
		: nullptr;
	const FString IdentityKey = Record
		? Record->IdentityKey
		: ResolveReconnectIdentity(PlayerController);
	const FGuid ParticipantId = Record ? Record->ParticipantId : FGuid();
	const uint8 Team = TPS ? TPS->TeamByte : 255;
	const FString PlayerName = TPS ? TPS->GetPlayerName() : TEXT("Unknown");
	TWeakObjectPtr<ATacticalPlayerController> LeavingController(PlayerController);
	TWeakObjectPtr<ATacticalPlayerState> LeavingPlayerState(TPS);
	if (Record)
	{
		Record->ConnectionState = ETacticalParticipantConnectionState::Abandoned;
		Record->ReconnectDeadlineServerTime = 0.0f;
		Record->RetainedPawn = nullptr;
		Record->DisconnectedController = nullptr;
	}
	DisconnectedPlayers.Remove(IdentityKey);
	// Remove the quitter from every reward/roster path before the death callback
	// can synchronously resolve the round and settle economy.
	RemoveFromTeam(PlayerController);
	if (TPS)
	{
		TPS->bIsActiveParticipant = false;
		TPS->SetReady(false);
		TPS->PendingSettlementRound = 0;
		TPS->ClearEquipmentOnDeath();
		TPS->SetConnectionStatus(ETacticalPublicConnectionStatus::Abandoned);
		TPS->ForceNetUpdate();
	}

	ATacticalCharacter* Pawn =
		Cast<ATacticalCharacter>(PlayerController->GetPawn());
	TWeakObjectPtr<ATacticalCharacter> LeavingPawn(Pawn);
	if (IsValid(Pawn) && !Pawn->IsDead())
	{
		if (UHealthComponent* Health = Pawn->GetHealthComponent())
		{
			// Voluntary departure is an uncredited elimination. Clear any prior
			// tag so the last attacker cannot receive a stale reward.
			Health->LastDamageInstigator = nullptr;
			Health->TakeDamage(1000000.0f, nullptr, false);
		}
	}

	// Damage/death and round-resolution delegates are user-extensible and may
	// destroy actors or mutate the registry. Resolve every object again after
	// that callback boundary; never continue through the original raw pointers.
	PlayerController = LeavingController.Get();
	TPS = LeavingPlayerState.Get();
	Pawn = LeavingPawn.Get();
	Record = RosterSubsystem && !IdentityKey.IsEmpty()
		? RosterSubsystem->FindByIdentity(IdentityKey)
		: nullptr;
	if (Record && ParticipantId.IsValid()
		&& Record->ParticipantId != ParticipantId)
	{
		Record = nullptr;
	}
	if (Record)
	{
		Record->ConnectionState = ETacticalParticipantConnectionState::Abandoned;
		Record->ReconnectDeadlineServerTime = 0.0f;
		Record->RetainedPawn = nullptr;
		Record->DisconnectedController = nullptr;
		// Even a lobby record remains as a one-tick tombstone until Super::Logout
		// unregisters the old UniqueId from GameSession. Removing it now would let
		// the same identity register a new generation first, after which the old
		// NotifyLogout could unregister that new session entry.
	}
	if (PlayerController)
	{
		RemoveFromTeam(PlayerController);
		if (Pawn && PlayerController->GetPawn() == Pawn)
		{
			PlayerController->UnPossess();
		}
	}
	// APlayerController::Destroyed only unpossesses a still-connected controller;
	// it does not destroy the corpse. Explicit abandonment is terminal, so remove
	// the already-resolved pawn after its normal death/drop callbacks have run.
	if (Pawn)
	{
		Pawn->Destroy();
	}
	if (TPS)
	{
		TPS->bIsActiveParticipant = false;
		TPS->SetReady(false);
		TPS->PendingSettlementRound = 0;
		TPS->ClearEquipmentOnDeath();
		TPS->SetConnectionStatus(ETacticalPublicConnectionStatus::Abandoned);
		TPS->ForceNetUpdate();
	}

	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=ParticipantAbandoned Player=\"%s\" Participant=%s Team=%s GraceBypassed=1 Reward=0"),
		*PlayerName,
		ParticipantId.IsValid() ? *ParticipantId.ToString() : TEXT("Untracked"),
		TacticalDiagnostics::GetTeamName(Team));

	PlayerController = LeavingController.Get();
	if (!PlayerController)
	{
		return;
	}
	PlayerController->ClientCompleteExplicitLeave();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateWeakLambda(PlayerController,
				[LeavingController]()
				{
					if (ATacticalPlayerController* Controller =
						LeavingController.Get())
					{
						Controller->Destroy();
					}
				}));
	}
	else
	{
		PlayerController->Destroy();
	}
}

void ATacticalGameMode::HandleHostShutdown()
{
	if (!HasAuthority())
	{
		return;
	}
	if (bHostShutdownInProgress)
	{
		return;
	}
	bHostShutdownInProgress = true;
	GetWorldTimerManager().ClearTimer(MatchRestartTimer);
	if (!TacticalGS)
	{
		TacticalGS = GetGameState<ATacticalGameState>();
	}

	DeactivateTreasureForRound();
	ClearTeamEliminationRoundEnd();
	ResetHuntIntel();
	FreezeAllPlayers(true);
	if (TacticalGS)
	{
		TacticalGS->SetPhase(ERoundPhase::MatchEnd, 0.0f);
		TacticalGS->BroadcastMatchEnded(255, EMatchEndReason::HostLost);
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator();
		It; ++It)
	{
		ATacticalPlayerController* Controller =
			Cast<ATacticalPlayerController>(It->Get());
		if (Controller && !Controller->IsLocalPlayerController())
		{
			Controller->ClientHostSessionEnded();
		}
	}
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=HostSessionShutdown Result=HostLost ScoreAwarded=0"));
}

void ATacticalGameMode::Logout(AController* Exiting)
{
	LobbySlotAssignments.Remove(TWeakObjectPtr<AController>(Exiting));
	APlayerController* ExitingPC = Cast<APlayerController>(Exiting);
	const bool bExplicitLeave = ExplicitlyLeavingPlayers.Remove(ExitingPC) > 0;
	ValidatedReconnectAdmissions.Remove(ExitingPC);
	ValidatedReconnectCredentials.Remove(ExitingPC);
	FPendingRosterReconnectTransaction PendingReconnectTransaction;
	bool bHasPendingReconnectTransaction = false;
	FString PendingReconnectIdentity;
	if (const FPendingRosterReconnectTransaction* PendingTransaction =
		PendingRosterReconnects.Find(ExitingPC))
	{
		PendingReconnectTransaction = *PendingTransaction;
		PendingReconnectIdentity = PendingReconnectTransaction.IdentityKey;
		bHasPendingReconnectTransaction = true;
	}
	PendingRosterReconnects.Remove(ExitingPC);
	const TWeakObjectPtr<APawn> PendingReconnectBootstrapPawn =
		PendingReconnectTransaction.BootstrapPawn;
	auto DestroyPendingReconnectBootstrap =
		[ExitingPC, PendingReconnectBootstrapPawn](const APawn* RetainedPawn)
		{
			APawn* BootstrapPawn = PendingReconnectBootstrapPawn.Get();
			if (!IsValid(BootstrapPawn) || BootstrapPawn == RetainedPawn)
			{
				return;
			}
			AController* BootstrapController = BootstrapPawn->GetController();
			// A callback may already have transferred this pawn to another valid
			// controller. An old reconnect transaction must never destroy it.
			if (BootstrapController && BootstrapController != ExitingPC)
			{
				return;
			}
			if (IsValid(ExitingPC) && BootstrapController == ExitingPC)
			{
				ExitingPC->UnPossess();
			}
			BootstrapPawn->Destroy();
		};
	APlayerState* ExitingPlayerState = Exiting ? Exiting->GetPlayerState<APlayerState>() : nullptr;
	ATacticalPlayerState* ExitingTPS = Cast<ATacticalPlayerState>(ExitingPlayerState);
	const FString ExitingPlayerName = ExitingPlayerState
		? ExitingPlayerState->GetPlayerName()
		: GetNameSafe(Exiting);
	const uint8 PreviousTeam = ExitingTPS ? ExitingTPS->TeamByte : 255;
	const bool bWasActiveParticipant = ExitingTPS && ExitingTPS->bIsActiveParticipant;
	const ERoundPhase PhaseAtLogout = TacticalGS ? TacticalGS->CurrentPhase : ERoundPhase::PreRound;
	const UWorld* World = GetWorld();
	const bool bWorldTearingDown = !World || World->bIsTearingDown;
	const FString IdentityKey = ResolveReconnectIdentity(ExitingPC);
	GuestIdentityOverrides.Remove(ExitingPC);
	FTacticalMatchParticipantRecord* ParticipantRecord = RosterSubsystem && ExitingTPS
		? RosterSubsystem->FindByPlayerState(ExitingTPS)
		: nullptr;

	if (bExplicitLeave || (ParticipantRecord
		&& ParticipantRecord->ConnectionState ==
			ETacticalParticipantConnectionState::Abandoned))
	{
		const FGuid ParticipantId = ParticipantRecord
			? ParticipantRecord->ParticipantId
			: FGuid();
		const FString AbandonedIdentity = ParticipantRecord
			? ParticipantRecord->IdentityKey
			: IdentityKey;
		if (bHasPendingReconnectTransaction)
		{
			// Explicit leave can be invoked synchronously from a reconnect callback.
			// HandleExplicitLeave intentionally clears the canonical record, so use
			// the immutable transaction snapshot to retire the old proxy/body too.
			// Never touch an actor that a callback has transferred to a third party.
			ATacticalDisconnectedController* TransactionProxy =
				PendingReconnectTransaction.DisconnectedController.Get();
			ATacticalCharacter* TransactionRetainedPawn =
				PendingReconnectTransaction.RetainedPawn.Get();
			APawn* TransactionBootstrapPawn =
				PendingReconnectTransaction.BootstrapPawn.Get();

			auto DestroyTransactionPawn = [ExitingPC, TransactionProxy](APawn* Pawn)
			{
				if (!IsValid(Pawn))
				{
					return;
				}
				AController* Owner = Pawn->GetController();
				if (Owner && Owner != ExitingPC && Owner != TransactionProxy)
				{
					UE_LOG(LogTacticalFlow, Error,
						TEXT("Event=ReconnectAbandonActorCleanupSkipped Actor=\"%s\" Reason=ThirdPartyOwner Owner=\"%s\""),
						*GetNameSafe(Pawn), *GetNameSafe(Owner));
					return;
				}
				if (Owner == ExitingPC && IsValid(ExitingPC))
				{
					ExitingPC->UnPossess();
				}
				else if (Owner == TransactionProxy && IsValid(TransactionProxy))
				{
					TransactionProxy->UnPossess();
				}
				Pawn->Destroy();
			};

			if (TransactionBootstrapPawn != TransactionRetainedPawn)
			{
				DestroyTransactionPawn(TransactionBootstrapPawn);
			}
			DestroyTransactionPawn(TransactionRetainedPawn);
			if (IsValid(TransactionProxy))
			{
				APawn* ProxyPawn = TransactionProxy->GetPawn();
				if (!ProxyPawn || ProxyPawn == TransactionRetainedPawn)
				{
					if (ProxyPawn)
					{
						TransactionProxy->UnPossess();
					}
					TransactionProxy->DetachPreservedPlayerState();
					TransactionProxy->Destroy();
				}
				else
				{
					UE_LOG(LogTacticalFlow, Error,
						TEXT("Event=ReconnectAbandonProxyCleanupSkipped Proxy=\"%s\" Reason=UnexpectedPawn Pawn=\"%s\""),
						*GetNameSafe(TransactionProxy), *GetNameSafe(ProxyPawn));
				}
			}
		}
		RemoveFromTeam(Exiting);
		if (ExitingTPS)
		{
			ExitingTPS->bIsActiveParticipant = false;
			ExitingTPS->SetReady(false);
			ExitingTPS->PendingSettlementRound = 0;
			ExitingTPS->ClearEquipmentOnDeath();
			ExitingTPS->SetConnectionStatus(
				ETacticalPublicConnectionStatus::Abandoned);
		}
		// HandleExplicitLeave may already have removed a lobby record before this
		// deferred Logout runs. In that gap the same identity can establish a new
		// participant generation, so an untracked/invalid generation must never
		// delete state owned by the new connection.
		if (ParticipantId.IsValid())
		{
			DisconnectedPlayers.Remove(AbandonedIdentity);
		}
		Super::Logout(Exiting);
		bool bRetainAbandonedPlayerState = false;
		if (FTacticalMatchParticipantRecord* CurrentRecord =
			RosterSubsystem && !AbandonedIdentity.IsEmpty()
				? RosterSubsystem->FindByIdentity(AbandonedIdentity)
				: nullptr;
			CurrentRecord && TacticalRules::IsSameParticipantGeneration(
				ParticipantId, CurrentRecord->ParticipantId))
		{
			if (TacticalRules::ShouldRetainAbandonedReservation(bMatchStarted))
			{
				CurrentRecord->PlayerController = nullptr;
				CurrentRecord->PlayerState = ExitingTPS;
				CurrentRecord->RetainedPawn = nullptr;
				CurrentRecord->DisconnectedController = nullptr;
				CurrentRecord->ReconnectDeadlineServerTime = 0.0f;
				CurrentRecord->ConnectionState =
					ETacticalParticipantConnectionState::Abandoned;
				bRetainAbandonedPlayerState = IsValid(ExitingTPS);
			}
			else
			{
				// Lobby capacity is released only after the old online-session identity
				// has completed its engine logout, closing the identity-reuse window.
				RosterSubsystem->RemoveParticipant(AbandonedIdentity);
			}
		}
		if (bRetainAbandonedPlayerState && ExitingPC)
		{
			// APlayerController::Destroyed calls CleanupPlayerState after Logout.
			// Detach the mid-match tombstone first so the locked 3v3 roster keeps a
			// replicated LEFT row until the authoritative world/match is replaced.
			ExitingPC->SetPlayerState(nullptr);
			ExitingTPS->SetOwner(nullptr);
			ExitingTPS->SetLifeSpan(0.0f);
			ExitingTPS->SetIsInactive(false);
			if (TacticalGS)
			{
				TacticalGS->AddPlayerState(ExitingTPS);
			}
			ExitingTPS->ForceNetUpdate();
		}
#if WITH_EDITOR
		EditorReconnectIdentityOverrides.Remove(ExitingPC);
#endif
		if (!bWorldTearingDown && TacticalGS)
		{
			if (TacticalGS->CurrentPhase == ERoundPhase::Combat)
			{
				CheckVictoryConditions();
			}
			else if (!bMatchStarted
				&& TacticalGS->CurrentPhase == ERoundPhase::PreRound)
			{
				TryStartMatch();
			}
		}
		return;
	}

	if (!bMatchStarted && PendingReconnectIdentity.IsEmpty()
		&& ParticipantRecord
		&& ParticipantRecord->IdentityKey.StartsWith(TEXT("GUEST:"))
		&& !ParticipantRecord->bReconnectCredentialAcknowledged
		&& ParticipantRecord->PreviousReconnectCredential.IsEmpty())
	{
		const FGuid ParticipantId = ParticipantRecord->ParticipantId;
		RemoveFromTeam(Exiting);
		DisconnectedPlayers.Remove(IdentityKey);
		if (ExitingTPS)
		{
			ExitingTPS->bIsActiveParticipant = false;
			ExitingTPS->SetReady(false);
			ExitingTPS->SetConnectionStatus(
				ETacticalPublicConnectionStatus::Disconnected);
		}
		RosterSubsystem->RemoveParticipant(IdentityKey);
		Super::Logout(Exiting);
#if WITH_EDITOR
		EditorReconnectIdentityOverrides.Remove(ExitingPC);
#endif
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=UnacknowledgedGuestReleased Participant=%s LobbySlotReleased=1"),
			*ParticipantId.ToString());
		TryStartMatch();
		return;
	}

	// A GameSession or Blueprint PostLogin callback can reject a controller while
	// its preserved PlayerState is being transferred. Roll that transaction back
	// to the existing disconnect proxy instead of treating it as a second loss
	// and creating a duplicate proxy/Pawn.
	if (!bWorldTearingDown && bHasPendingReconnectTransaction
		&& !PendingReconnectIdentity.IsEmpty()
		&& RosterSubsystem && ExitingPC)
	{
		FTacticalMatchParticipantRecord* PendingRecord =
			RosterSubsystem->FindByIdentity(PendingReconnectIdentity);
		if (PendingRecord
			&& PendingRecord->ParticipantId ==
				PendingReconnectTransaction.ParticipantId
			&& PendingRecord->PlayerController.Get() == ExitingPC
			&& (!ExitingTPS || PendingRecord->PlayerState.Get() == ExitingTPS))
		{
			const FGuid ExpectedParticipantId =
				PendingReconnectTransaction.ParticipantId;
			const uint8 PendingTeam = PendingReconnectTransaction.TeamByte;
			const int32 PendingEnteredRound =
				PendingReconnectTransaction.EnteredRound;
			const int32 PendingLastSettledRound =
				PendingReconnectTransaction.LastSettledRound;
			TWeakObjectPtr<ATacticalPlayerState> TransactionStateWeak =
				PendingReconnectTransaction.PreservedState;
			TWeakObjectPtr<ATacticalDisconnectedController> ProxyWeak =
				PendingReconnectTransaction.DisconnectedController;
			TWeakObjectPtr<ATacticalCharacter> PawnWeak =
				PendingReconnectTransaction.RetainedPawn;
			const float PreservedDeadline =
				PendingReconnectTransaction.ReconnectDeadlineServerTime;

			RemoveFromTeam(ExitingPC);
			Super::Logout(Exiting);
			// The controller is normally already pending destruction here, but its
			// object is still alive until Logout returns. Detach the preserved state
			// so APlayerController::CleanupPlayerState cannot destroy it.
			ExitingPC->SetPlayerState(nullptr);
			ExitingTPS = TransactionStateWeak.Get();

			PendingRecord = RosterSubsystem->FindByIdentity(PendingReconnectIdentity);
			const bool bSameParticipantGeneration = PendingRecord
				&& TacticalRules::IsSameParticipantGeneration(
					ExpectedParticipantId, PendingRecord->ParticipantId);
			DestroyPendingReconnectBootstrap(PendingRecord
				? PendingRecord->RetainedPawn.Get()
				: PawnWeak.Get());
			if (bSameParticipantGeneration
				&& HasCoherentReplacementOwner(PendingRecord, ExitingPC))
			{
				UE_LOG(LogTacticalFlow, Warning,
					TEXT("Event=RosterParticipantReconnectRollbackSuperseded Participant=%s Result=CurrentOwnerPreserved"),
					*ExpectedParticipantId.ToString());
#if WITH_EDITOR
				EditorReconnectIdentityOverrides.Remove(ExitingPC);
#endif
				return;
			}
			if (bSameParticipantGeneration
				&& PendingRecord->PlayerState.IsValid()
				&& PendingRecord->PlayerState.Get() != TransactionStateWeak.Get())
			{
				UE_LOG(LogTacticalFlow, Error,
					TEXT("Event=RosterParticipantReconnectRollbackDeferred Participant=%s Reason=CanonicalStateChanged"),
					*ExpectedParticipantId.ToString());
#if WITH_EDITOR
				EditorReconnectIdentityOverrides.Remove(ExitingPC);
#endif
				return;
			}
			if (!bSameParticipantGeneration)
			{
				ATacticalDisconnectedController* OrphanProxy = ProxyWeak.Get();
				ATacticalCharacter* OrphanPawn = PawnWeak.Get();
				const bool bDifferentGenerationExists = PendingRecord
					&& !bSameParticipantGeneration;
				const bool bCurrentRecordClaimsProxy = bDifferentGenerationExists
					&& IsValid(OrphanProxy)
					&& PendingRecord->DisconnectedController.Get() == OrphanProxy;
				const bool bCurrentRecordClaimsPawn = bDifferentGenerationExists
					&& IsValid(OrphanPawn)
					&& (PendingRecord->RetainedPawn.Get() == OrphanPawn
						|| (PendingRecord->PlayerController.IsValid()
							&& PendingRecord->PlayerController->GetPawn() == OrphanPawn));
				const bool bCurrentRecordClaimsState = bDifferentGenerationExists
					&& IsValid(ExitingTPS)
					&& PendingRecord->PlayerState.Get() == ExitingTPS;
				if (!bCurrentRecordClaimsProxy && IsValid(OrphanProxy)
					&& OrphanProxy->GetPawn())
				{
					OrphanProxy->UnPossess();
				}
				if (!bCurrentRecordClaimsPawn && IsValid(OrphanPawn))
				{
					OrphanPawn->Destroy();
				}
				if (!bCurrentRecordClaimsProxy && IsValid(OrphanProxy))
				{
					OrphanProxy->DetachPreservedPlayerState();
					OrphanProxy->Destroy();
				}
				if (!bCurrentRecordClaimsState && IsValid(ExitingTPS))
				{
					if (bMatchStarted && PendingTeam <= 1
						&& PendingEnteredRound == CurrentRound
						&& PendingLastSettledRound < CurrentRound
						&& (PhaseAtLogout == ERoundPhase::Buy
							|| PhaseAtLogout == ERoundPhase::Combat))
					{
						ExitingTPS->PendingSettlementRound = CurrentRound;
						ExitingTPS->ClearEquipmentOnDeath();
					}
					SavePlayerStateForParticipantGeneration(
						ExitingTPS, PendingReconnectIdentity, ExpectedParticipantId);
					ExitingTPS->SetOwner(nullptr);
					ExitingTPS->SetUniqueId(FUniqueNetIdRepl());
					ExitingTPS->Destroy();
				}
				UE_LOG(LogTacticalFlow, Error,
					TEXT("Event=RosterParticipantReconnectRollbackFailed Participant=%s Reason=RecordInvalid Result=StateReleased"),
					*ExpectedParticipantId.ToString());
				if (PhaseAtLogout == ERoundPhase::Combat)
				{
					CheckVictoryConditions();
				}
				return;
			}

			ATacticalDisconnectedController* Proxy =
				PendingRecord->DisconnectedController.Get();
			if (!IsValid(Proxy))
			{
				Proxy = ProxyWeak.Get();
			}
			ATacticalCharacter* RetainedPawn =
				PendingRecord->RetainedPawn.Get();
			if (!IsValid(Proxy))
			{
				FActorSpawnParameters SpawnParameters;
				SpawnParameters.ObjectFlags |= RF_Transient;
				SpawnParameters.SpawnCollisionHandlingOverride =
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Proxy = GetWorld() ? GetWorld()->SpawnActor<ATacticalDisconnectedController>(
					ATacticalDisconnectedController::StaticClass(), FTransform::Identity,
					SpawnParameters) : nullptr;
			}

			if (IsValid(Proxy) && IsValid(ExitingTPS))
			{
				TWeakObjectPtr<ATacticalDisconnectedController> RollbackProxyWeak = Proxy;
				TWeakObjectPtr<ATacticalCharacter> RollbackPawnWeak = RetainedPawn;
				AController* RetainedPawnController = IsValid(RetainedPawn)
					? RetainedPawn->GetController()
					: nullptr;
				const bool bBodyOwnershipAvailable = !RetainedPawnController
					|| RetainedPawnController == ExitingPC
					|| RetainedPawnController == Proxy;
				const bool bRestoreLiveBody = IsValid(RetainedPawn)
					&& !RetainedPawn->IsDead()
					&& bBodyOwnershipAvailable
					&& PendingEnteredRound == CurrentRound
					&& (PhaseAtLogout == ERoundPhase::Buy
						|| PhaseAtLogout == ERoundPhase::Combat);
				const float RollbackTime = GetWorld()
					? GetWorld()->GetTimeSeconds()
					: 0.0f;
				const bool bReconnectStatusWindowActive = bRestoreLiveBody
					&& PreservedDeadline > RollbackTime;
				PendingRecord->PlayerController = nullptr;
				PendingRecord->PlayerState = ExitingTPS;
				PendingRecord->DisconnectedController = Proxy;
				// Keep a dead/current-round pawn referenced until the normal boundary
				// cleanup too; only a living one remains reclaimable.
				PendingRecord->RetainedPawn = RetainedPawn;
				PendingRecord->ReconnectDeadlineServerTime =
					(bReconnectStatusWindowActive || !bMatchStarted)
						? PreservedDeadline
						: 0.0f;
				PendingRecord->ConnectionState = bRestoreLiveBody
					? (bReconnectStatusWindowActive
						? ETacticalParticipantConnectionState::DisconnectedGrace
						: ETacticalParticipantConnectionState::DisconnectedBodyRetained)
					: ETacticalParticipantConnectionState::DisconnectedWaiting;
				const bool bRestorePublicGrace = !bMatchStarted
					|| bReconnectStatusWindowActive;
				ExitingTPS->SetConnectionStatus(
					bRestorePublicGrace
						? ETacticalPublicConnectionStatus::Reconnecting
						: ETacticalPublicConnectionStatus::Disconnected,
					bRestorePublicGrace ? PreservedDeadline : 0.0f);
				ExitingTPS->SetReady(false);
				ExitingTPS->bIsActiveParticipant = !bMatchStarted
					|| bRestoreLiveBody;

				auto FindRollbackRecord = [this, &PendingReconnectIdentity,
					ExpectedParticipantId, ExitingPC, &TransactionStateWeak,
					&RollbackProxyWeak, &RollbackPawnWeak](
						bool bRequireProxyStateOwnership)
					-> FTacticalMatchParticipantRecord*
				{
					FTacticalMatchParticipantRecord* CurrentRecord = RosterSubsystem
						? RosterSubsystem->FindByIdentity(PendingReconnectIdentity)
						: nullptr;
					ATacticalPlayerState* CurrentState = TransactionStateWeak.Get();
					ATacticalDisconnectedController* CurrentProxy = RollbackProxyWeak.Get();
					ATacticalCharacter* CurrentPawn = RollbackPawnWeak.Get();
					if (!CurrentRecord || !IsValid(CurrentState) || !IsValid(CurrentProxy)
						|| !TacticalRules::IsSameParticipantGeneration(
							ExpectedParticipantId, CurrentRecord->ParticipantId)
						|| CurrentRecord->PlayerController.IsValid()
						|| CurrentRecord->PlayerState.Get() != CurrentState
						|| CurrentRecord->DisconnectedController.Get() != CurrentProxy
						|| CurrentRecord->RetainedPawn.Get() != CurrentPawn)
					{
						return nullptr;
					}
					AActor* StateOwner = CurrentState->GetOwner();
					ATacticalPlayerState* ProxyState =
						CurrentProxy->GetPlayerState<ATacticalPlayerState>();
					if (bRequireProxyStateOwnership)
					{
						if (StateOwner != CurrentProxy || ProxyState != CurrentState)
						{
							return nullptr;
						}
					}
					else if ((StateOwner && StateOwner != ExitingPC
							&& StateOwner != CurrentProxy)
						|| (ProxyState && ProxyState != CurrentState))
					{
						return nullptr;
					}
					AController* PawnOwner = IsValid(CurrentPawn)
						? CurrentPawn->GetController()
						: nullptr;
					if (PawnOwner && PawnOwner != ExitingPC && PawnOwner != CurrentProxy)
					{
						return nullptr;
					}
					APawn* ProxyPawn = CurrentProxy->GetPawn();
					return (!ProxyPawn || ProxyPawn == CurrentPawn)
						? CurrentRecord
						: nullptr;
				};

				if (!bRestoreLiveBody && bMatchStarted && PendingTeam <= 1
					&& PendingEnteredRound == CurrentRound
					&& PendingLastSettledRound < CurrentRound)
				{
					ExitingTPS->PendingSettlementRound = CurrentRound;
					ExitingTPS->ClearEquipmentOnDeath();
					PendingRecord = FindRollbackRecord(false);
					ExitingTPS = TransactionStateWeak.Get();
					Proxy = RollbackProxyWeak.Get();
					RetainedPawn = RollbackPawnWeak.Get();
					if (!PendingRecord)
					{
						UE_LOG(LogTacticalFlow, Error,
							TEXT("Event=RosterParticipantReconnectRollbackDeferred Participant=%s Reason=GraphChangedDuringEquipmentCallback"),
							*ExpectedParticipantId.ToString());
						return;
					}
				}

				Proxy->AttachPreservedPlayerState(ExitingTPS);
				if (!(PendingRecord = FindRollbackRecord(true)))
				{
					return;
				}
				ExitingTPS = TransactionStateWeak.Get();
				Proxy = RollbackProxyWeak.Get();
				ExitingTPS->SetLifeSpan(0.0f);
				ExitingTPS->SetIsInactive(false);
				if (TacticalGS)
				{
					TacticalGS->AddPlayerState(ExitingTPS);
					if (!(PendingRecord = FindRollbackRecord(true)))
					{
						return;
					}
					ExitingTPS = TransactionStateWeak.Get();
					Proxy = RollbackProxyWeak.Get();
					RetainedPawn = RollbackPawnWeak.Get();
				}
				if (bRestoreLiveBody && Proxy->GetPawn() != RetainedPawn)
				{
					AController* CurrentPawnOwner = IsValid(RetainedPawn)
						? RetainedPawn->GetController()
						: nullptr;
					if (!FindRollbackRecord(true) || !IsValid(RetainedPawn)
						|| RetainedPawn->IsDead()
						|| (CurrentPawnOwner && CurrentPawnOwner != ExitingPC
							&& CurrentPawnOwner != Proxy))
					{
						return;
					}
					if (CurrentPawnOwner == ExitingPC && IsValid(ExitingPC))
					{
						ExitingPC->UnPossess();
						if (!FindRollbackRecord(true))
						{
							return;
						}
					}
					Proxy->Possess(RetainedPawn);
					if (!FindRollbackRecord(true) || Proxy->GetPawn() != RetainedPawn)
					{
						return;
					}
				}
				if (TransactionStateWeak.IsValid())
				{
					TransactionStateWeak->ForceNetUpdate();
				}
				UE_LOG(LogTacticalFlow, Warning,
					TEXT("Event=RosterParticipantReconnectRolledBack Player=\"%s\" Participant=%s"),
					*ExitingPlayerName, *ExpectedParticipantId.ToString());
				if (!bRestoreLiveBody && PhaseAtLogout == ERoundPhase::Combat)
				{
					CheckVictoryConditions();
				}
			}
			else if (!IsValid(Proxy) && IsValid(ExitingTPS))
			{
				// The original disconnect may already be using the proxyless
				// allocation-failure fallback. If this replacement connection then
				// leaves before takeover finishes and another proxy allocation fails,
				// keep the exact participant/body graph instead of manufacturing an
				// elimination and releasing the locked roster slot.
				TWeakObjectPtr<ATacticalPlayerState> ProxylessStateWeak = ExitingTPS;
				TWeakObjectPtr<ATacticalCharacter> ProxylessPawnWeak = RetainedPawn;
				AController* PawnOwner = IsValid(RetainedPawn)
					? RetainedPawn->GetController()
					: nullptr;
				AActor* StateOwner = ExitingTPS->GetOwner();
				const bool bExactProxylessGraph = PendingRecord
					&& TacticalRules::IsSameParticipantGeneration(
						ExpectedParticipantId, PendingRecord->ParticipantId)
					&& (!PendingRecord->PlayerController.IsValid()
						|| PendingRecord->PlayerController.Get() == ExitingPC)
					&& PendingRecord->PlayerState.Get() == ExitingTPS
					&& !PendingRecord->DisconnectedController.IsValid()
					&& PendingRecord->RetainedPawn.Get() == RetainedPawn
					&& (!StateOwner || StateOwner == ExitingPC)
					&& (!PawnOwner || PawnOwner == ExitingPC);
				if (!bExactProxylessGraph)
				{
					UE_LOG(LogTacticalFlow, Error,
						TEXT("Event=RosterParticipantReconnectRollbackDeferred Participant=%s Reason=ProxylessGraphChanged"),
						*ExpectedParticipantId.ToString());
					return;
				}

				const bool bBodyAlive = IsValid(RetainedPawn)
					&& !RetainedPawn->IsDead();
				const ERoundPhase CurrentRollbackPhase = TacticalGS
					? TacticalGS->CurrentPhase
					: ERoundPhase::PreRound;
				const bool bRestoreLiveBody =
					PendingReconnectTransaction.bRetainedBodyState
					&& bBodyAlive
					&& PendingEnteredRound == CurrentRound
					&& (CurrentRollbackPhase == ERoundPhase::Buy
						|| CurrentRollbackPhase == ERoundPhase::Combat);
				const float RollbackTime = GetWorld()
					? GetWorld()->GetTimeSeconds()
					: 0.0f;
				const bool bReconnectStatusWindowActive = bRestoreLiveBody
					&& PreservedDeadline > RollbackTime;
				const bool bRestoreLobbyGrace = !bMatchStarted;
				const bool bRestorePublicGrace = bRestoreLobbyGrace
					|| bReconnectStatusWindowActive;
				// A malformed zero lobby deadline must still become serviceable on
				// the next tick instead of reserving a slot forever.
				const float RestoredStatusDeadline = bRestoreLobbyGrace
					? FMath::Max(PreservedDeadline,
						FMath::Max(RollbackTime, KINDA_SMALL_NUMBER))
					: PreservedDeadline;
				const bool bNeedsDeathSettlement = !bBodyAlive
					&& bMatchStarted && PendingTeam <= 1
					&& PendingEnteredRound == CurrentRound
					&& PendingRecord->LastSettledRound < CurrentRound
					&& (CurrentRollbackPhase == ERoundPhase::Buy
						|| CurrentRollbackPhase == ERoundPhase::Combat);
				const bool bNeedsCombatVictoryRefresh = !bBodyAlive
					&& bMatchStarted && PendingTeam <= 1
					&& PendingEnteredRound == CurrentRound
					&& CurrentRollbackPhase == ERoundPhase::Combat;

				PendingRecord->PlayerController = nullptr;
				PendingRecord->PlayerState = ExitingTPS;
				PendingRecord->DisconnectedController = nullptr;
				PendingRecord->RetainedPawn = RetainedPawn;
				PendingRecord->ReconnectDeadlineServerTime =
					bRestorePublicGrace ? RestoredStatusDeadline : 0.0f;
				PendingRecord->ConnectionState = bRestoreLiveBody
					? (bReconnectStatusWindowActive
						? ETacticalParticipantConnectionState::DisconnectedGrace
						: ETacticalParticipantConnectionState::DisconnectedBodyRetained)
					: ETacticalParticipantConnectionState::DisconnectedWaiting;
				ExitingTPS->bIsActiveParticipant = bRestoreLobbyGrace
					|| bRestoreLiveBody;
				ExitingTPS->SetReady(false);
				ExitingTPS->SetConnectionStatus(
					bRestorePublicGrace
						? ETacticalPublicConnectionStatus::Reconnecting
						: ETacticalPublicConnectionStatus::Disconnected,
					bRestorePublicGrace ? RestoredStatusDeadline : 0.0f);
				if (bNeedsDeathSettlement)
				{
					ExitingTPS->PendingSettlementRound = CurrentRound;
				}

				auto FindProxylessRollbackRecord = [this, ExitingPC,
					&PendingReconnectIdentity, ExpectedParticipantId,
					&ProxylessStateWeak, &ProxylessPawnWeak](
						bool bRequireDetachedOwners,
						bool bRequirePawnStateAssociation)
					-> FTacticalMatchParticipantRecord*
				{
					FTacticalMatchParticipantRecord* CurrentRecord = RosterSubsystem
						? RosterSubsystem->FindByIdentity(PendingReconnectIdentity)
						: nullptr;
					ATacticalPlayerState* CurrentState = ProxylessStateWeak.Get();
					ATacticalCharacter* CurrentPawn = ProxylessPawnWeak.Get();
					if (!CurrentRecord || !IsValid(CurrentState)
						|| !TacticalRules::IsSameParticipantGeneration(
							ExpectedParticipantId, CurrentRecord->ParticipantId)
						|| CurrentRecord->PlayerController.IsValid()
						|| CurrentRecord->PlayerState.Get() != CurrentState
						|| CurrentRecord->DisconnectedController.IsValid()
						|| CurrentRecord->RetainedPawn.Get() != CurrentPawn)
					{
						return nullptr;
					}

					AActor* CurrentStateOwner = CurrentState->GetOwner();
					AController* CurrentPawnOwner = IsValid(CurrentPawn)
						? CurrentPawn->GetController()
						: nullptr;
					if (bRequireDetachedOwners)
					{
						if (CurrentStateOwner || CurrentPawnOwner)
						{
							return nullptr;
						}
					}
					else if ((CurrentStateOwner && CurrentStateOwner != ExitingPC)
						|| (CurrentPawnOwner && CurrentPawnOwner != ExitingPC))
					{
						return nullptr;
					}
					if (bRequirePawnStateAssociation && IsValid(CurrentPawn)
						&& CurrentPawn->GetPlayerState<ATacticalPlayerState>() != CurrentState)
					{
						return nullptr;
					}
					return CurrentRecord;
				};

				if (!(PendingRecord = FindProxylessRollbackRecord(false, false)))
				{
					UE_LOG(LogTacticalFlow, Error,
						TEXT("Event=RosterParticipantReconnectRollbackDeferred Participant=%s Reason=ProxylessGraphChangedDuringStatusUpdate"),
						*ExpectedParticipantId.ToString());
					return;
				}
				ExitingTPS = ProxylessStateWeak.Get();
				RetainedPawn = ProxylessPawnWeak.Get();
				if (bNeedsDeathSettlement)
				{
					ExitingTPS->ClearEquipmentOnDeath();
					if (!(PendingRecord = FindProxylessRollbackRecord(false, false)))
					{
						UE_LOG(LogTacticalFlow, Error,
							TEXT("Event=RosterParticipantReconnectRollbackDeferred Participant=%s Reason=ProxylessGraphChangedDuringEquipmentCallback"),
							*ExpectedParticipantId.ToString());
						return;
					}
					ExitingTPS = ProxylessStateWeak.Get();
					RetainedPawn = ProxylessPawnWeak.Get();
				}

				if (IsValid(RetainedPawn))
				{
					if (bRestoreLiveBody)
					{
						RetainedPawn->PrepareForDisconnectGrace();
						if (!(PendingRecord = FindProxylessRollbackRecord(false, false)))
						{
							UE_LOG(LogTacticalFlow, Error,
								TEXT("Event=RosterParticipantReconnectRollbackDeferred Participant=%s Reason=ProxylessGraphChangedDuringBodyPreparation"),
								*ExpectedParticipantId.ToString());
							return;
						}
						RetainedPawn = ProxylessPawnWeak.Get();
					}
					if (RetainedPawn->GetController() == ExitingPC)
					{
						ExitingPC->UnPossess();
						if (!(PendingRecord = FindProxylessRollbackRecord(false, false)))
						{
							UE_LOG(LogTacticalFlow, Error,
								TEXT("Event=RosterParticipantReconnectRollbackDeferred Participant=%s Reason=ProxylessGraphChangedDuringUnPossess"),
								*ExpectedParticipantId.ToString());
							return;
						}
						RetainedPawn = ProxylessPawnWeak.Get();
						ExitingTPS = ProxylessStateWeak.Get();
					}
					RetainedPawn->SetPlayerState(ExitingTPS);
					if (!(PendingRecord = FindProxylessRollbackRecord(false, true)))
					{
						UE_LOG(LogTacticalFlow, Error,
							TEXT("Event=RosterParticipantReconnectRollbackDeferred Participant=%s Reason=ProxylessGraphChangedDuringPawnStateCallback"),
							*ExpectedParticipantId.ToString());
						return;
					}
					ExitingTPS = ProxylessStateWeak.Get();
				}

				ExitingTPS->SetOwner(nullptr);
				ExitingTPS->SetLifeSpan(0.0f);
				ExitingTPS->SetIsInactive(false);
				if (!(PendingRecord = FindProxylessRollbackRecord(true,
					IsValid(ProxylessPawnWeak.Get()))))
				{
					UE_LOG(LogTacticalFlow, Error,
						TEXT("Event=RosterParticipantReconnectRollbackDeferred Participant=%s Reason=ProxylessGraphChangedDuringStateDetach"),
						*ExpectedParticipantId.ToString());
					return;
				}
				ExitingTPS = ProxylessStateWeak.Get();
				RetainedPawn = ProxylessPawnWeak.Get();
				if (TacticalGS)
				{
					TacticalGS->AddPlayerState(ExitingTPS);
					if (!(PendingRecord = FindProxylessRollbackRecord(true,
						IsValid(ProxylessPawnWeak.Get()))))
					{
						UE_LOG(LogTacticalFlow, Error,
							TEXT("Event=RosterParticipantReconnectRollbackDeferred Participant=%s Reason=ProxylessGraphChangedDuringGameStateCallback"),
							*ExpectedParticipantId.ToString());
						return;
					}
					ExitingTPS = ProxylessStateWeak.Get();
					RetainedPawn = ProxylessPawnWeak.Get();
				}
				ExitingTPS->ForceNetUpdate();
				if (IsValid(RetainedPawn))
				{
					RetainedPawn->ForceNetUpdate();
				}
				UE_LOG(LogTacticalFlow, Error,
					TEXT("Event=RosterParticipantReconnectRolledBackProxyless Player=\"%s\" Participant=%s BodyRetained=%d BodyAlive=%d"),
					*ExitingPlayerName, *ExpectedParticipantId.ToString(),
					bRestoreLiveBody, bBodyAlive);
#if WITH_EDITOR
				EditorReconnectIdentityOverrides.Remove(ExitingPC);
#endif
				ValidateTacticalState(TEXT("ReconnectRollbackProxyless"));
				if (bNeedsCombatVictoryRefresh)
				{
					CheckVictoryConditions();
				}
				return;
			}
			else
			{
				if (IsValid(Proxy) && Proxy->GetPawn())
				{
					Proxy->UnPossess();
				}
				if (IsValid(RetainedPawn))
				{
					RetainedPawn->Destroy();
				}
				if (IsValid(Proxy))
				{
					Proxy->DetachPreservedPlayerState();
					Proxy->Destroy();
				}
				if (IsValid(ExitingTPS))
				{
					if (bMatchStarted && PendingTeam <= 1
						&& PendingEnteredRound == CurrentRound
						&& PendingLastSettledRound < CurrentRound
						&& (PhaseAtLogout == ERoundPhase::Buy
							|| PhaseAtLogout == ERoundPhase::Combat))
					{
						ExitingTPS->PendingSettlementRound = CurrentRound;
						ExitingTPS->ClearEquipmentOnDeath();
					}
					SavePlayerStateForParticipantGeneration(
						ExitingTPS, PendingReconnectIdentity, ExpectedParticipantId);
					ExitingTPS->SetOwner(nullptr);
					ExitingTPS->SetUniqueId(FUniqueNetIdRepl());
					ExitingTPS->Destroy();
				}
				if (FTacticalMatchParticipantRecord* CurrentRecord =
					RosterSubsystem->FindByIdentity(PendingReconnectIdentity);
					CurrentRecord && TacticalRules::IsSameParticipantGeneration(
						ExpectedParticipantId, CurrentRecord->ParticipantId))
				{
					RosterSubsystem->RemoveParticipant(PendingReconnectIdentity);
				}
				UE_LOG(LogTacticalFlow, Error,
					TEXT("Event=RosterParticipantReconnectRollbackFailed Participant=%s Result=SlotReleased"),
					*ExpectedParticipantId.ToString());
				if (PhaseAtLogout == ERoundPhase::Combat)
				{
					CheckVictoryConditions();
				}
			}

#if WITH_EDITOR
			EditorReconnectIdentityOverrides.Remove(ExitingPC);
#endif
			return;
		}

		// A pending marker without its expected record/state graph is corruption,
		// not a second disconnect. Tear down every reachable transaction object and
		// call the engine logout path exactly once; never create another proxy.
		const FGuid CorruptParticipantId =
			PendingReconnectTransaction.ParticipantId;
		const uint8 CorruptTeam = PendingReconnectTransaction.TeamByte;
		const int32 CorruptEnteredRound =
			PendingReconnectTransaction.EnteredRound;
		const int32 CorruptLastSettledRound =
			PendingReconnectTransaction.LastSettledRound;
		TWeakObjectPtr<ATacticalPlayerState> CorruptPreservedState =
			PendingReconnectTransaction.PreservedState;
		TWeakObjectPtr<ATacticalDisconnectedController> CorruptProxy =
			PendingReconnectTransaction.DisconnectedController;
		TWeakObjectPtr<ATacticalCharacter> CorruptPawn =
			PendingReconnectTransaction.RetainedPawn;
		TWeakObjectPtr<ATacticalPlayerState> ControllerState = ExitingTPS;

		RemoveFromTeam(ExitingPC);
		Super::Logout(Exiting);
		ExitingPC->SetPlayerState(nullptr);
		DestroyPendingReconnectBootstrap(CorruptPawn.Get());
		if (ATacticalDisconnectedController* Proxy = CorruptProxy.Get())
		{
			if (Proxy->GetPawn())
			{
				Proxy->UnPossess();
			}
			Proxy->DetachPreservedPlayerState();
			Proxy->Destroy();
		}
		if (ATacticalCharacter* Pawn = CorruptPawn.Get())
		{
			Pawn->Destroy();
		}
		if (ATacticalPlayerState* State = CorruptPreservedState.Get())
		{
			if (bMatchStarted && CorruptTeam <= 1
				&& CorruptEnteredRound == CurrentRound
				&& CorruptLastSettledRound < CurrentRound)
			{
				State->PendingSettlementRound = CurrentRound;
				State->ClearEquipmentOnDeath();
			}
			SavePlayerStateForParticipantGeneration(
				State, PendingReconnectIdentity, CorruptParticipantId);
			State->SetOwner(nullptr);
			State->SetUniqueId(FUniqueNetIdRepl());
			State->Destroy();
		}
		if (ATacticalPlayerState* State = ControllerState.Get();
			IsValid(State) && State != CorruptPreservedState.Get())
		{
			State->SetIsInactive(true);
			State->SetUniqueId(FUniqueNetIdRepl());
			State->Destroy();
		}
		if (FTacticalMatchParticipantRecord* CurrentRecord =
			RosterSubsystem->FindByIdentity(PendingReconnectIdentity);
			CurrentRecord && TacticalRules::IsSameParticipantGeneration(
				CorruptParticipantId, CurrentRecord->ParticipantId))
		{
			RosterSubsystem->RemoveParticipant(PendingReconnectIdentity);
		}
		UE_LOG(LogTacticalFlow, Error,
			TEXT("Event=RosterParticipantReconnectRollbackFailed Participant=%s Reason=TransactionGraphInvalid Result=StateReleased"),
			CorruptParticipantId.IsValid()
				? *CorruptParticipantId.ToString()
				: TEXT("Invalid"));
		if (PhaseAtLogout == ERoundPhase::Combat)
		{
			CheckVictoryConditions();
		}
		return;
	}

	if (!ParticipantRecord && RosterSubsystem && ExitingPC && ExitingTPS
		&& !IdentityKey.IsEmpty() && bWasActiveParticipant
		&& (!bMatchStarted || PreviousTeam <= 1))
	{
		ParticipantRecord = &RosterSubsystem->RegisterParticipant(
			IdentityKey, ExitingPC, ExitingTPS);
		ParticipantRecord->TeamByte = PreviousTeam;
		ParticipantRecord->bRosterSlotReserved = PreviousTeam <= 1;
	}
	const FGuid ParticipantGenerationAtLogout = ParticipantRecord
		? ParticipantRecord->ParticipantId
		: FGuid();
	const bool bKnownParticipant = ParticipantRecord != nullptr || bWasActiveParticipant;

	const bool bLobbyCandidate = !bMatchStarted && bWasActiveParticipant;
	const bool bReservedMatchParticipant = bMatchStarted && ParticipantRecord
		&& ParticipantRecord->bRosterSlotReserved && PreviousTeam <= 1;
	const bool bShouldPreserveParticipant = !bWorldTearingDown && ExitingPC && ExitingTPS
		&& ParticipantRecord && (bLobbyCandidate || bReservedMatchParticipant);
	const bool bRetainLivePawn = bShouldPreserveParticipant
		&& ShouldRetainPawnOnDisconnect(
			Cast<ATacticalPlayerController>(ExitingPC), ParticipantRecord);
	bool bProxylessRetainedBody = false;
	FString ProxylessIdentity;
	FGuid ProxylessParticipantId;
	float ProxylessDeadline = 0.0f;
	TWeakObjectPtr<ATacticalPlayerState> ProxylessStateWeak;
	TWeakObjectPtr<ATacticalCharacter> ProxylessPawnWeak;

	if (bShouldPreserveParticipant)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ATacticalDisconnectedController* DisconnectedController =
			GetWorld()->SpawnActor<ATacticalDisconnectedController>(
				ATacticalDisconnectedController::StaticClass(), FTransform::Identity,
				SpawnParameters);
		if (DisconnectedController)
		{
			ATacticalCharacter* RetainedPawn = bRetainLivePawn
				? Cast<ATacticalCharacter>(ExitingPC->GetPawn())
				: nullptr;
			const float Deadline = GetWorld()->GetTimeSeconds() + ReconnectGraceDuration;
			const FString ParticipantIdentity = ParticipantRecord->IdentityKey;
			const FGuid ExpectedParticipantId = ParticipantRecord->ParticipantId;
			const int32 ParticipantEnteredRound = ParticipantRecord->EnteredRound;
			const int32 ParticipantLastSettledRound = ParticipantRecord->LastSettledRound;
			const FRotator DisconnectControlRotation = ExitingPC->GetControlRotation();
			TWeakObjectPtr<ATacticalPlayerState> PreservedStateWeak = ExitingTPS;
			TWeakObjectPtr<ATacticalCharacter> RetainedPawnWeak = RetainedPawn;
			TWeakObjectPtr<ATacticalDisconnectedController> ProxyWeak = DisconnectedController;

			RemoveFromTeam(Exiting);
			ExitingTPS->SetConnectionStatus(
				ETacticalPublicConnectionStatus::Reconnecting, Deadline);
			ExitingTPS->SetReady(false);
			if (bRetainLivePawn)
			{
				ExitingTPS->bIsActiveParticipant = true;
				ParticipantRecord->ConnectionState =
					ETacticalParticipantConnectionState::DisconnectedGrace;
				ParticipantRecord->EnteredRound = FMath::Max(
					ParticipantRecord->EnteredRound, CurrentRound);
			}
			else if (bLobbyCandidate)
			{
				// Keep this candidate in the all-ready gate until the lobby grace
				// expires. Reconnecting always resets readiness to false.
				ExitingTPS->bIsActiveParticipant = true;
				ParticipantRecord->ConnectionState =
					ETacticalParticipantConnectionState::DisconnectedWaiting;
			}
			else
			{
				ExitingTPS->bIsActiveParticipant = false;
				ExitingTPS->SetConnectionStatus(
					ETacticalPublicConnectionStatus::Disconnected);
				ParticipantRecord->ConnectionState =
					ETacticalParticipantConnectionState::DisconnectedWaiting;
				if ((PhaseAtLogout == ERoundPhase::Buy || PhaseAtLogout == ERoundPhase::Combat)
					&& ParticipantRecord->EnteredRound == CurrentRound)
				{
					ExitingTPS->PendingSettlementRound = CurrentRound;
				}
			}
			ExitingTPS->ForceNetUpdate();

			ParticipantRecord->PlayerController = nullptr;
			ParticipantRecord->PlayerState = ExitingTPS;
			ParticipantRecord->RetainedPawn = RetainedPawn;
			ParticipantRecord->DisconnectedController = DisconnectedController;
			ParticipantRecord->ReconnectDeadlineServerTime =
				(bRetainLivePawn || bLobbyCandidate) ? Deadline : 0.0f;
			ParticipantRecord->RetainedControlRotation = DisconnectControlRotation;

			UE_LOG(LogTacticalFlow, Display,
				TEXT("Event=DisconnectGraceStarted Player=\"%s\" Participant=%s Team=%s Round=%d Phase=%s RetainedPawn=%d Deadline=%.2f"),
				*ExitingPlayerName, *ParticipantRecord->ParticipantId.ToString(),
				TacticalDiagnostics::GetTeamName(PreviousTeam), CurrentRound,
				TacticalDiagnostics::GetPhaseName(PhaseAtLogout), bRetainLivePawn,
				ParticipantRecord->ReconnectDeadlineServerTime);

			// Notify the engine/session while the departing controller still owns
			// the original PlayerState, then transfer that state out of the normal
			// APlayerController destruction path.
			Super::Logout(Exiting);
			ExitingPC->SetPlayerState(nullptr);

			// Logout broadcasts user-extensible callbacks. Never trust Actor pointers
			// or a TMap value address captured across that boundary.
			ParticipantRecord = RosterSubsystem
				? RosterSubsystem->FindByIdentity(ParticipantIdentity)
				: nullptr;
			ExitingTPS = PreservedStateWeak.Get();
			RetainedPawn = RetainedPawnWeak.Get();
			DisconnectedController = ProxyWeak.Get();
			const bool bSameParticipantGeneration = ParticipantRecord
				&& TacticalRules::IsSameParticipantGeneration(
					ExpectedParticipantId, ParticipantRecord->ParticipantId);
			if (bSameParticipantGeneration
				&& HasCoherentReplacementOwner(ParticipantRecord, ExitingPC))
			{
				UE_LOG(LogTacticalFlow, Warning,
					TEXT("Event=DisconnectCommitSuperseded Participant=%s Result=CurrentOwnerPreserved"),
					*ExpectedParticipantId.ToString());
#if WITH_EDITOR
				EditorReconnectIdentityOverrides.Remove(ExitingPC);
#endif
				return;
			}
			if (bSameParticipantGeneration
				&& ParticipantRecord->PlayerState.IsValid()
				&& ParticipantRecord->PlayerState.Get() != ExitingTPS)
			{
				UE_LOG(LogTacticalFlow, Error,
					TEXT("Event=DisconnectCommitDeferred Participant=%s Reason=CanonicalStateChanged"),
					*ExpectedParticipantId.ToString());
#if WITH_EDITOR
				EditorReconnectIdentityOverrides.Remove(ExitingPC);
#endif
				return;
			}
			if (bSameParticipantGeneration)
			{
				ParticipantRecord->PlayerController = nullptr;
				ParticipantRecord->PlayerState = ExitingTPS;
				if (ATacticalDisconnectedController* CurrentProxy =
					ParticipantRecord->DisconnectedController.Get())
				{
					DisconnectedController = CurrentProxy;
				}
				else if (IsValid(DisconnectedController))
				{
					ParticipantRecord->DisconnectedController = DisconnectedController;
				}
				RetainedPawn = ParticipantRecord->RetainedPawn.Get();
			}
			if (!bSameParticipantGeneration
				|| !IsValid(ExitingTPS) || !IsValid(DisconnectedController))
			{
				const bool bDifferentGenerationExists = ParticipantRecord
					&& !bSameParticipantGeneration;
				const bool bCurrentRecordClaimsPawn = bDifferentGenerationExists
					&& IsValid(RetainedPawn)
					&& (ParticipantRecord->RetainedPawn.Get() == RetainedPawn
						|| (ParticipantRecord->PlayerController.IsValid()
							&& ParticipantRecord->PlayerController->GetPawn() == RetainedPawn));
				const bool bCurrentRecordClaimsProxy = bDifferentGenerationExists
					&& IsValid(DisconnectedController)
					&& ParticipantRecord->DisconnectedController.Get() ==
						DisconnectedController;
				const bool bCurrentRecordClaimsState = bDifferentGenerationExists
					&& IsValid(ExitingTPS)
					&& ParticipantRecord->PlayerState.Get() == ExitingTPS;
				if (!bCurrentRecordClaimsPawn && IsValid(RetainedPawn))
				{
					if (RetainedPawn->GetController() == ExitingPC)
					{
						ExitingPC->UnPossess();
					}
					RetainedPawn->Destroy();
				}
				if (!bCurrentRecordClaimsProxy && IsValid(DisconnectedController))
				{
					DisconnectedController->DetachPreservedPlayerState();
					DisconnectedController->Destroy();
				}
				if (!bCurrentRecordClaimsState && IsValid(ExitingTPS))
				{
					if (bMatchStarted && PreviousTeam <= 1
						&& (PhaseAtLogout == ERoundPhase::Buy
							|| PhaseAtLogout == ERoundPhase::Combat))
					{
						ExitingTPS->PendingSettlementRound = CurrentRound;
						ExitingTPS->ClearEquipmentOnDeath();
					}
					SavePlayerStateForParticipantGeneration(
						ExitingTPS, ParticipantIdentity, ExpectedParticipantId);
					ExitingTPS->SetOwner(nullptr);
					ExitingTPS->SetUniqueId(FUniqueNetIdRepl());
					ExitingTPS->Destroy();
				}
				if (RosterSubsystem && ParticipantRecord
					&& ParticipantRecord->ParticipantId == ExpectedParticipantId)
				{
					RosterSubsystem->RemoveParticipant(ParticipantIdentity);
				}
				UE_LOG(LogTacticalFlow, Error,
					TEXT("Event=DisconnectGraceCommitFailed Participant=%s Result=SlotReleased"),
					*ExpectedParticipantId.ToString());
				if (PhaseAtLogout == ERoundPhase::Combat)
				{
					CheckVictoryConditions();
				}
				return;
			}

			AController* RetainedPawnController = IsValid(RetainedPawn)
				? RetainedPawn->GetController()
				: nullptr;
			const bool bBodyOwnershipAvailable = !RetainedPawnController
				|| RetainedPawnController == ExitingPC
				|| RetainedPawnController == DisconnectedController;
			const bool bRetainedPawnStillAlive = IsValid(RetainedPawn)
				&& !RetainedPawn->IsDead() && bBodyOwnershipAvailable;
			if (!bBodyOwnershipAvailable)
			{
				ParticipantRecord->RetainedPawn = nullptr;
			}
			if (TacticalRules::ShouldDowngradeDisconnectedWithoutPawn(
				bLobbyCandidate, bRetainedPawnStillAlive))
			{
				ParticipantRecord->ReconnectDeadlineServerTime = 0.0f;
				ParticipantRecord->ConnectionState =
					ETacticalParticipantConnectionState::DisconnectedWaiting;
				ExitingTPS->bIsActiveParticipant = false;
				ExitingTPS->SetConnectionStatus(
					ETacticalPublicConnectionStatus::Disconnected);
				const bool bMissingBodyCountsAsCurrentRoundDeath =
					(PhaseAtLogout == ERoundPhase::Buy
						|| PhaseAtLogout == ERoundPhase::Combat)
					&& ParticipantEnteredRound == CurrentRound
					&& ParticipantLastSettledRound < CurrentRound;
				if (bMissingBodyCountsAsCurrentRoundDeath)
				{
					ExitingTPS->PendingSettlementRound = CurrentRound;
					ExitingTPS->ClearEquipmentOnDeath();
				}
			}
			if (bRetainedPawnStillAlive)
			{
				RetainedPawn->PrepareForDisconnectGrace();
				if (RetainedPawn->GetController() == ExitingPC)
				{
					ExitingPC->UnPossess();
				}
			}
			DisconnectedController->AttachPreservedPlayerState(ExitingTPS);
			ExitingTPS->SetLifeSpan(0.0f);
			ExitingTPS->SetIsInactive(false);
			if (TacticalGS)
			{
				TacticalGS->AddPlayerState(ExitingTPS);
			}
			if (bRetainedPawnStillAlive)
			{
				DisconnectedController->Possess(RetainedPawn);
				DisconnectedController->SetControlRotation(
					DisconnectControlRotation);
				RetainedPawn->ForceNetUpdate();
			}
			else if (IsValid(RetainedPawn) && RetainedPawn->GetController() == ExitingPC)
			{
				ExitingPC->UnPossess();
			}
			DisconnectedController->ForceNetUpdate();
			ExitingTPS->ForceNetUpdate();

#if WITH_EDITOR
			EditorReconnectIdentityOverrides.Remove(ExitingPC);
#endif
			ValidateTacticalState(TEXT("DisconnectGraceStarted"));
			if ((!bRetainLivePawn || !bRetainedPawnStillAlive)
				&& PhaseAtLogout == ERoundPhase::Combat)
			{
				CheckVictoryConditions();
			}
			return;
		}

		UE_LOG(LogTacticalFlow, Error,
			TEXT("Event=DisconnectGraceUnavailable Player=\"%s\" Reason=ProxySpawnFailed"),
			*ExitingPlayerName);
		ATacticalCharacter* FallbackPawn = bRetainLivePawn
			? Cast<ATacticalCharacter>(ExitingPC->GetPawn())
			: nullptr;
		if (IsValid(FallbackPawn) && !FallbackPawn->IsDead())
		{
			// Actor allocation failure must not turn a transport loss into an
			// elimination. Keep a controller-less but authoritative body as the
			// fail-safe; GameMode alive counting and the service loop both consult
			// the roster's RetainedPawn directly. A later real death still settles
			// the round, while a surviving body reaches the normal timeout draw.
			bProxylessRetainedBody = true;
			ProxylessIdentity = ParticipantRecord->IdentityKey;
			ProxylessParticipantId = ParticipantRecord->ParticipantId;
			ProxylessDeadline = GetWorld()->GetTimeSeconds() + ReconnectGraceDuration;
			ProxylessStateWeak = ExitingTPS;
			ProxylessPawnWeak = FallbackPawn;
			ParticipantRecord->PlayerController = nullptr;
			ParticipantRecord->PlayerState = ExitingTPS;
			ParticipantRecord->RetainedPawn = FallbackPawn;
			ParticipantRecord->DisconnectedController = nullptr;
			ParticipantRecord->ReconnectDeadlineServerTime = ProxylessDeadline;
			ParticipantRecord->ConnectionState =
				ETacticalParticipantConnectionState::DisconnectedGrace;
			ParticipantRecord->RetainedControlRotation = ExitingPC->GetControlRotation();
			FallbackPawn->PrepareForDisconnectGrace();
			UE_LOG(LogTacticalFlow, Error,
				TEXT("Event=DisconnectProxylessBodyRetained Player=\"%s\" Participant=%s Round=%d Phase=%s"),
				*ExitingPlayerName, *ProxylessParticipantId.ToString(), CurrentRound,
				TacticalDiagnostics::GetPhaseName(PhaseAtLogout));
		}
		else
		{
			ParticipantRecord->PlayerController = nullptr;
			ParticipantRecord->PlayerState = nullptr;
			ParticipantRecord->RetainedPawn = nullptr;
			ParticipantRecord->DisconnectedController = nullptr;
			ParticipantRecord->ReconnectDeadlineServerTime = 0.0f;
			ParticipantRecord->ConnectionState =
				ETacticalParticipantConnectionState::DisconnectedWaiting;
			if (!bMatchStarted)
			{
				// Keep only the private registry/token and bounded deadline. The
				// snapshot may be reclaimed with a validated credential, while a dead
				// PlayerState can no longer block the lobby's ready count.
				ParticipantRecord->ReconnectDeadlineServerTime =
					GetWorld()->GetTimeSeconds() + ReconnectGraceDuration;
			}
		}
	}

	const bool bCountsAsRoundDeath = !bProxylessRetainedBody
		&& (!ParticipantRecord
			|| ParticipantRecord->LastSettledRound < CurrentRound)
		&& TacticalRules::ShouldMarkDisconnectForSettlement(
			bWorldTearingDown, bMatchStarted, PhaseAtLogout,
			bWasActiveParticipant, PreviousTeam);

	// A real disconnect may reconnect later. A whole-world/PIE shutdown has no
	// future match state to restore and must not manufacture gameplay results.
	if (!bWorldTearingDown && ExitingTPS && bKnownParticipant)
	{
		if (bCountsAsRoundDeath)
		{
			ExitingTPS->PendingSettlementRound = CurrentRound;
			ExitingTPS->ClearEquipmentOnDeath();
			UE_LOG(LogTacticalFlow, Display,
				TEXT("Event=DisconnectEquipmentCleared Player=\"%s\" Team=%s Round=%d Phase=%s"),
				*ExitingPlayerName, TacticalDiagnostics::GetTeamName(PreviousTeam), CurrentRound,
				TacticalDiagnostics::GetPhaseName(PhaseAtLogout));
		}
		SavePlayerStateForParticipantGeneration(
			ExitingTPS, IdentityKey, ParticipantGenerationAtLogout);
	}

	RemoveFromTeam(Exiting);
	if (ExitingTPS)
	{
		// Retain TeamByte so the inactive PlayerState and saved reconnect state
		// still describe the player's chosen team, but remove it from all active
		// roster/counting paths immediately.
		ExitingTPS->bIsActiveParticipant = bProxylessRetainedBody;
		ExitingTPS->SetReady(false);
		if (!bWorldTearingDown && bKnownParticipant)
		{
			ExitingTPS->SetConnectionStatus(
				bProxylessRetainedBody
					? ETacticalPublicConnectionStatus::Reconnecting
					: ETacticalPublicConnectionStatus::Disconnected,
				bProxylessRetainedBody ? ProxylessDeadline : 0.0f);
		}
		ExitingTPS->ForceNetUpdate();
	}
	const int32 PendingSettlementRoundAtLogout = ExitingTPS ? ExitingTPS->PendingSettlementRound : 0;
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=PlayerLeft Player=\"%s\" PreviousTeam=%s WasActive=%d Phase=%s CountsAsRoundDeath=%d PendingSettlementRound=%d TeamA=%d TeamB=%d MatchStarted=%d WorldTearingDown=%d"),
		*ExitingPlayerName, TacticalDiagnostics::GetTeamName(PreviousTeam), bWasActiveParticipant,
		TacticalDiagnostics::GetPhaseName(PhaseAtLogout), bCountsAsRoundDeath,
		PendingSettlementRoundAtLogout, TeamAPlayers.Num(), TeamBPlayers.Num(),
		bMatchStarted, bWorldTearingDown);
	// Broadcast logout only after the authoritative roster state is coherent.
	Super::Logout(Exiting);
	if (bProxylessRetainedBody)
	{
		// Keep the PlayerState out of APlayerController::CleanupPlayerState and
		// leave the body unpossessed. Every object/record is re-resolved after the
		// extensible Logout callback before this fallback is committed.
		ExitingPC->SetPlayerState(nullptr);
		FTacticalMatchParticipantRecord* CurrentRecord = RosterSubsystem
			? RosterSubsystem->FindByIdentity(ProxylessIdentity)
			: nullptr;
		ATacticalPlayerState* PreservedState = ProxylessStateWeak.Get();
		ATacticalCharacter* RetainedPawn = ProxylessPawnWeak.Get();
		const bool bSameFallbackGeneration = CurrentRecord
			&& TacticalRules::IsSameParticipantGeneration(
				ProxylessParticipantId, CurrentRecord->ParticipantId);
		if (bSameFallbackGeneration
			&& HasCoherentReplacementOwner(CurrentRecord, ExitingPC))
		{
			UE_LOG(LogTacticalFlow, Warning,
				TEXT("Event=DisconnectProxylessCommitSuperseded Participant=%s Result=CurrentOwnerPreserved"),
				*ProxylessParticipantId.ToString());
			return;
		}
		AController* PawnOwner = IsValid(RetainedPawn)
			? RetainedPawn->GetController()
			: nullptr;
		if (bSameFallbackGeneration && IsValid(PreservedState)
			&& IsValid(RetainedPawn) && !RetainedPawn->IsDead()
			&& CurrentRecord->PlayerState.Get() == PreservedState
			&& CurrentRecord->RetainedPawn.Get() == RetainedPawn
			&& (!PreservedState->GetOwner()
				|| PreservedState->GetOwner() == ExitingPC)
			&& (!PawnOwner || PawnOwner == ExitingPC))
		{
			if (PawnOwner == ExitingPC)
			{
				ExitingPC->UnPossess();
			}
			CurrentRecord = RosterSubsystem
				? RosterSubsystem->FindByIdentity(ProxylessIdentity)
				: nullptr;
			PreservedState = ProxylessStateWeak.Get();
			RetainedPawn = ProxylessPawnWeak.Get();
			if (!CurrentRecord || !IsValid(PreservedState) || !IsValid(RetainedPawn)
				|| !TacticalRules::IsSameParticipantGeneration(
					ProxylessParticipantId, CurrentRecord->ParticipantId)
				|| CurrentRecord->PlayerController.IsValid()
				|| CurrentRecord->PlayerState.Get() != PreservedState
				|| CurrentRecord->RetainedPawn.Get() != RetainedPawn
				|| RetainedPawn->GetController()
				|| (PreservedState->GetOwner()
					&& PreservedState->GetOwner() != ExitingPC))
			{
				UE_LOG(LogTacticalFlow, Error,
					TEXT("Event=DisconnectProxylessCommitDeferred Participant=%s Reason=GraphChangedDuringUnPossess"),
					*ProxylessParticipantId.ToString());
				return;
			}
			// Preserve the replicated Pawn <-> PlayerState association even though
			// no controller could be allocated. This keeps teammate HUD/life counts,
			// armor persistence, and death presentation consistent with the normal
			// proxy path while the server roster remains authoritative.
			RetainedPawn->SetPlayerState(PreservedState);
			CurrentRecord = RosterSubsystem
				? RosterSubsystem->FindByIdentity(ProxylessIdentity)
				: nullptr;
			PreservedState = ProxylessStateWeak.Get();
			RetainedPawn = ProxylessPawnWeak.Get();
			if (!CurrentRecord || !IsValid(PreservedState) || !IsValid(RetainedPawn)
				|| !TacticalRules::IsSameParticipantGeneration(
					ProxylessParticipantId, CurrentRecord->ParticipantId)
				|| CurrentRecord->PlayerController.IsValid()
				|| CurrentRecord->PlayerState.Get() != PreservedState
				|| CurrentRecord->RetainedPawn.Get() != RetainedPawn
				|| RetainedPawn->GetController()
				|| (PreservedState->GetOwner()
					&& PreservedState->GetOwner() != ExitingPC)
				|| RetainedPawn->GetPlayerState<ATacticalPlayerState>() != PreservedState)
			{
				UE_LOG(LogTacticalFlow, Error,
					TEXT("Event=DisconnectProxylessCommitDeferred Participant=%s Reason=GraphChangedDuringPawnStateCallback"),
					*ProxylessParticipantId.ToString());
				return;
			}
			PreservedState->SetOwner(nullptr);
			PreservedState->SetLifeSpan(0.0f);
			PreservedState->SetIsInactive(false);
			if (TacticalGS)
			{
				TacticalGS->AddPlayerState(PreservedState);
				CurrentRecord = RosterSubsystem
					? RosterSubsystem->FindByIdentity(ProxylessIdentity)
					: nullptr;
				PreservedState = ProxylessStateWeak.Get();
				RetainedPawn = ProxylessPawnWeak.Get();
				if (!CurrentRecord || !IsValid(PreservedState)
					|| !IsValid(RetainedPawn)
					|| !TacticalRules::IsSameParticipantGeneration(
						ProxylessParticipantId, CurrentRecord->ParticipantId)
					|| CurrentRecord->PlayerController.IsValid()
					|| CurrentRecord->PlayerState.Get() != PreservedState
					|| CurrentRecord->RetainedPawn.Get() != RetainedPawn
					|| PreservedState->GetOwner()
					|| RetainedPawn->GetController()
					|| RetainedPawn->GetPlayerState<ATacticalPlayerState>() != PreservedState)
				{
					UE_LOG(LogTacticalFlow, Error,
						TEXT("Event=DisconnectProxylessCommitDeferred Participant=%s Reason=GraphChangedDuringGameStateCallback"),
						*ProxylessParticipantId.ToString());
					return;
				}
			}
			PreservedState->ForceNetUpdate();
			RetainedPawn->ForceNetUpdate();
#if WITH_EDITOR
			EditorReconnectIdentityOverrides.Remove(ExitingPC);
#endif
			ValidateTacticalState(TEXT("DisconnectProxylessBodyRetained"));
			return;
		}

		// The exact fallback graph was superseded or corrupted during Logout.
		// Clean up only actors still owned by this departing controller; never
		// mutate another participant generation or manufacture a damage event.
		if (IsValid(RetainedPawn)
			&& (!RetainedPawn->GetController()
				|| RetainedPawn->GetController() == ExitingPC))
		{
			if (RetainedPawn->GetController() == ExitingPC)
			{
				ExitingPC->UnPossess();
			}
			RetainedPawn->Destroy();
		}
		if (IsValid(PreservedState)
			&& (!CurrentRecord || CurrentRecord->PlayerState.Get() != PreservedState))
		{
			PreservedState->SetOwner(nullptr);
			PreservedState->SetUniqueId(FUniqueNetIdRepl());
			PreservedState->Destroy();
		}
		if (bSameFallbackGeneration)
		{
			RosterSubsystem->RemoveParticipant(ProxylessIdentity);
		}
		UE_LOG(LogTacticalFlow, Error,
			TEXT("Event=DisconnectProxylessCommitFailed Participant=%s Result=SlotReleased"),
			*ProxylessParticipantId.ToString());
		return;
	}

#if WITH_EDITOR
	EditorReconnectIdentityOverrides.Remove(ExitingPC);
#endif

	if (bWorldTearingDown)
	{
		ClearTeamEliminationRoundEnd();
		return;
	}

	ValidateTacticalState(TEXT("PlayerLeft"));

	if (HasAuthority() && TacticalGS)
	{
		if (TacticalRules::ShouldEvaluateDisconnectForElimination(
			bWorldTearingDown, PhaseAtLogout, bWasActiveParticipant, PreviousTeam))
		{
			CheckVictoryConditions();
		}
		else if (!bMatchStarted && TacticalGS->CurrentPhase == ERoundPhase::PreRound)
		{
			TryStartMatch();
		}
	}
}

// ---- Disconnect Preservation ----

FString ATacticalGameMode::ResolveReconnectIdentity(
	const APlayerController* PlayerController) const
{
	if (!PlayerController)
	{
		return FString();
	}

	if (const APlayerState* PlayerState = PlayerController->GetPlayerState<APlayerState>())
	{
		const FUniqueNetIdRepl& UniqueId = PlayerState->GetUniqueId();
		const FString ProviderIdentity = BuildProviderQualifiedIdentity(UniqueId);
		if (!ProviderIdentity.IsEmpty())
		{
			return ProviderIdentity;
		}
	}
	if (const FString* GuestIdentity = GuestIdentityOverrides.Find(PlayerController);
		GuestIdentity && !GuestIdentity->IsEmpty())
	{
		return *GuestIdentity;
	}

#if WITH_EDITOR
	if (const FString* PIEIdentity = EditorReconnectIdentityOverrides.Find(PlayerController);
		PIEIdentity && !PIEIdentity->IsEmpty())
	{
		return FString::Printf(TEXT("PIE:%s"), **PIEIdentity);
	}
#endif
	return FString();
}

FString ATacticalGameMode::ResolveReconnectIdentity(
	const FUniqueNetIdRepl& UniqueId,
	const FString& Options) const
{
	const FString ProviderIdentity = BuildProviderQualifiedIdentity(UniqueId);
	if (!ProviderIdentity.IsEmpty())
	{
		return ProviderIdentity;
	}

#if WITH_EDITOR
	const FString PIEIdentity = UGameplayStatics::ParseOption(
		Options, TEXT("TacticalPIEReconnectId"));
	if (!PIEIdentity.IsEmpty())
	{
		return FString::Printf(TEXT("PIE:%s"), *PIEIdentity);
	}
#endif
	return FString();
}

bool ATacticalGameMode::ValidateAdmissionOptions(
	const FString& Options,
	const FUniqueNetIdRepl& UniqueId,
	FString& OutErrorMessage,
	FString* OutIdentityKey,
	bool* bOutIsReconnect,
	FGuid* OutParticipantId) const
{
	OutErrorMessage.Reset();
	if (OutIdentityKey)
	{
		OutIdentityKey->Reset();
	}
	if (bOutIsReconnect)
	{
		*bOutIsReconnect = false;
	}
	if (OutParticipantId)
	{
		OutParticipantId->Invalidate();
	}
	if (bHostShutdownInProgress)
	{
		OutErrorMessage = TEXT("TACTICAL_HOST_SESSION_CLOSING");
		return false;
	}

	FString IdentityKey = ResolveReconnectIdentity(UniqueId, Options);

	const bool bHasMatchId = UGameplayStatics::HasOption(
		Options, TacticalNetworkAdmission::MatchIdOption);
	const bool bHasCredential = UGameplayStatics::HasOption(
		Options, TacticalNetworkAdmission::ReconnectCredentialOption);
	const bool bHasProtocolVersion = UGameplayStatics::HasOption(
		Options, TacticalNetworkAdmission::ProtocolVersionOption);
	const TacticalNetworkAdmission::EReconnectClaimShape ClaimShape =
		TacticalNetworkAdmission::ClassifyClaim(
			bHasMatchId, bHasCredential, bHasProtocolVersion);

#if UE_BUILD_SHIPPING
	// Steam Auth's packet-handler handshake runs before gameplay admission.
	// Shipping accepts only the authenticated Steam provider identity; PIE/Null
	// and generated guest identities remain development-only.
	if (!IsSteamProviderIdentity(IdentityKey))
	{
		OutErrorMessage = TEXT("TACTICAL_STEAM_AUTHENTICATION_REQUIRED");
		return false;
	}
#endif

	const bool bHasAuthenticatedIdentity = !IdentityKey.IsEmpty();
	const FTacticalMatchParticipantRecord* ExistingRecord =
		RosterSubsystem && !IdentityKey.IsEmpty()
			? RosterSubsystem->FindByIdentity(IdentityKey)
			: nullptr;
	if (ClaimShape == TacticalNetworkAdmission::EReconnectClaimShape::FreshJoin)
	{
		if (ExistingRecord)
		{
			if (ExistingRecord->PlayerController.IsValid())
			{
				OutErrorMessage = TEXT("TACTICAL_DUPLICATE_ACTIVE_PARTICIPANT");
				return false;
			}
			if (ExistingRecord->ConnectionState ==
				ETacticalParticipantConnectionState::Abandoned)
			{
				OutErrorMessage = TEXT("TACTICAL_RECONNECT_PARTICIPANT_UNAVAILABLE");
				return false;
			}
			if (IsSteamProviderIdentity(IdentityKey))
			{
				// Session-resolved Steam travel deliberately carries no bearer in
				// its URL. The authenticated provider identity can reclaim only its
				// own inactive roster record; PostLogin rotates the private token.
				if (OutIdentityKey)
				{
					*OutIdentityKey = IdentityKey;
				}
				if (bOutIsReconnect)
				{
					*bOutIsReconnect = true;
				}
				if (OutParticipantId)
				{
					*OutParticipantId = ExistingRecord->ParticipantId;
				}
				return true;
			}
			if (TacticalNetworkAdmission::CanUseUnacknowledgedFirstIssueRecovery(
				IdentityKey.StartsWith(TEXT("UID:")),
				ExistingRecord->PlayerController.IsValid(),
				ExistingRecord->bReconnectCredentialAcknowledged,
				!ExistingRecord->PreviousReconnectCredential.IsEmpty(),
				ExistingRecord->ConnectionState ==
					ETacticalParticipantConnectionState::Abandoned))
			{
				// A first-issue Client RPC may be lost with the connection that
				// carried it. The same authenticated provider identity receives one
				// controlled recovery until it acknowledges a credential.
				if (OutIdentityKey)
				{
					*OutIdentityKey = IdentityKey;
				}
				if (bOutIsReconnect)
				{
					*bOutIsReconnect = true;
				}
				if (OutParticipantId)
				{
					*OutParticipantId = ExistingRecord->ParticipantId;
				}
				return true;
			}
			OutErrorMessage = TEXT("TACTICAL_RECONNECT_CREDENTIAL_REQUIRED");
			return false;
		}
		if (bMatchStarted)
		{
			OutErrorMessage = TEXT("TACTICAL_MATCH_ALREADY_IN_PROGRESS");
			return false;
		}
		if (OutIdentityKey)
		{
			*OutIdentityKey = IdentityKey;
		}
		return true;
	}

	if (ClaimShape ==
		TacticalNetworkAdmission::EReconnectClaimShape::MalformedPartialClaim)
	{
		OutErrorMessage = TEXT("TACTICAL_RECONNECT_CLAIM_MALFORMED");
		return false;
	}
	const FString VersionText = UGameplayStatics::ParseOption(
		Options, TacticalNetworkAdmission::ProtocolVersionOption);
	int32 CandidateVersion = 0;
	if (!LexTryParseString(CandidateVersion, *VersionText)
		|| !TacticalNetworkAdmission::IsCompatibleProtocolVersion(CandidateVersion))
	{
		OutErrorMessage = TEXT("TACTICAL_RECONNECT_VERSION_MISMATCH");
		return false;
	}
	const FString CandidateMatchId = UGameplayStatics::ParseOption(
		Options, TacticalNetworkAdmission::MatchIdOption);
	const FString CandidateCredential = UGameplayStatics::ParseOption(
		Options, TacticalNetworkAdmission::ReconnectCredentialOption);
	if (!RosterSubsystem)
	{
		OutErrorMessage = TEXT("TACTICAL_RECONNECT_PARTICIPANT_UNAVAILABLE");
		return false;
	}

	if (bHasAuthenticatedIdentity)
	{
		// Once an online provider authenticates an account, a bearer token may
		// only reclaim that exact UID-bound record.
		if (!ExistingRecord || !RosterSubsystem->ValidateReconnectCredential(
			IdentityKey, CandidateMatchId, CandidateCredential))
		{
			OutErrorMessage = TEXT("TACTICAL_RECONNECT_ADMISSION_REJECTED");
			return false;
		}
	}
	else
	{
		// Provider-neutral builds cannot manufacture a platform identity. A
		// server-generated guest key is recovered only by its 128-bit bearer.
		ExistingRecord = RosterSubsystem->FindByReconnectCredential(
			CandidateMatchId, CandidateCredential);
		if (!ExistingRecord || !ExistingRecord->IdentityKey.StartsWith(TEXT("GUEST:")))
		{
			OutErrorMessage = TEXT("TACTICAL_RECONNECT_ADMISSION_REJECTED");
			return false;
		}
		IdentityKey = ExistingRecord->IdentityKey;
	}
	if (ExistingRecord->ConnectionState ==
		ETacticalParticipantConnectionState::Abandoned)
	{
		OutErrorMessage = TEXT("TACTICAL_RECONNECT_PARTICIPANT_UNAVAILABLE");
		return false;
	}
	if (ExistingRecord->PlayerController.IsValid())
	{
		OutErrorMessage = TEXT("TACTICAL_DUPLICATE_ACTIVE_PARTICIPANT");
		return false;
	}

	if (OutIdentityKey)
	{
		*OutIdentityKey = IdentityKey;
	}
	if (bOutIsReconnect)
	{
		*bOutIsReconnect = true;
	}
	if (OutParticipantId)
	{
		*OutParticipantId = ExistingRecord->ParticipantId;
	}
	return true;
}

void ATacticalGameMode::SendReconnectAdmission(
	APlayerController* PlayerController,
	FTacticalMatchParticipantRecord& Record,
	bool bRotateCredential,
	const FString& CredentialKnownByClient)
{
	ATacticalPlayerController* TacticalController =
		Cast<ATacticalPlayerController>(PlayerController);
	if (!HasAuthority() || !RosterSubsystem || !TacticalController)
	{
		return;
	}

	const FString& Credential = bRotateCredential
		? RosterSubsystem->RotateReconnectCredential(
			Record, CredentialKnownByClient)
		: RosterSubsystem->EnsureReconnectCredential(Record);
	TacticalController->ClientReceiveReconnectAdmission(
		RosterSubsystem->GetMatchId(), Credential,
		TacticalNetworkAdmission::ProtocolVersion);
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=ReconnectAdmissionIssued Participant=%s Protocol=%d Rotated=%d"),
		*Record.ParticipantId.ToString(),
		TacticalNetworkAdmission::ProtocolVersion, bRotateCredential);
}

void ATacticalGameMode::HandleReconnectAdmissionAcknowledgement(
	ATacticalPlayerController* PlayerController,
	const FString& MatchId,
	const FString& ReconnectCredential)
{
	if (!HasAuthority() || !RosterSubsystem || !IsValid(PlayerController))
	{
		return;
	}

	ATacticalPlayerState* TPS =
		PlayerController->GetPlayerState<ATacticalPlayerState>();
	FTacticalMatchParticipantRecord* Record = TPS
		? RosterSubsystem->FindByPlayerState(TPS)
		: nullptr;
	if (!Record || Record->PlayerController.Get() != PlayerController
		|| Record->ConnectionState ==
			ETacticalParticipantConnectionState::Abandoned)
	{
		return;
	}

	const bool bAcknowledged = RosterSubsystem->AcknowledgeReconnectCredential(
		*Record, MatchId, ReconnectCredential);
	if (bAcknowledged)
	{
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=ReconnectAdmissionAcknowledged Participant=%s Accepted=1"),
			*Record->ParticipantId.ToString());
	}
	else
	{
		UE_LOG(LogTacticalFlow, Warning,
			TEXT("Event=ReconnectAdmissionAcknowledged Participant=%s Accepted=0"),
			*Record->ParticipantId.ToString());
	}
}

bool ATacticalGameMode::ShouldRetainPawnOnDisconnect(
	const ATacticalPlayerController* PlayerController,
	const FTacticalMatchParticipantRecord* KnownRecord) const
{
	const UWorld* World = GetWorld();
	if (!HasAuthority() || !World || World->bIsTearingDown || !bMatchStarted
		|| !TacticalGS || !PlayerController || !RosterSubsystem)
	{
		return false;
	}

	const ERoundPhase Phase = TacticalGS->CurrentPhase;
	if (Phase != ERoundPhase::Buy && Phase != ERoundPhase::Combat)
	{
		return false;
	}

	const ATacticalPlayerState* TPS =
		PlayerController->GetPlayerState<ATacticalPlayerState>();
	const ATacticalCharacter* Pawn =
		Cast<ATacticalCharacter>(PlayerController->GetPawn());
	if (const FPendingRosterReconnectTransaction* PendingTransaction =
		PendingRosterReconnects.Find(PlayerController))
	{
		// During takeover, only the original retained body may bypass the
		// engine's PawnLeavingGame destruction. A temporary/bootstrap pawn must
		// be destroyed normally even if RestartPlayer is interrupted re-entrantly.
		if (Pawn != PendingTransaction->RetainedPawn.Get())
		{
			return false;
		}
	}
	const FTacticalMatchParticipantRecord* Record = KnownRecord;
	if (!Record && TPS)
	{
		Record = RosterSubsystem->FindByIdentity(
			ResolveReconnectIdentity(PlayerController));
	}
	return TPS && TPS->bIsActiveParticipant && TPS->TeamByte <= 1
		&& Pawn && !Pawn->IsDead() && Record && Record->bRosterSlotReserved;
}

bool ATacticalGameMode::PrepareRosterParticipantReconnect(
	APlayerController* NewPlayer,
	const FString& IdentityKey,
	const FGuid& ExpectedParticipantId)
{
	if (!HasAuthority() || !RosterSubsystem || !NewPlayer || IdentityKey.IsEmpty())
	{
		return false;
	}

	FTacticalMatchParticipantRecord* Record =
		RosterSubsystem->FindByIdentity(IdentityKey);
	auto IsExpectedOfflineRecord = [ExpectedParticipantId](
		const FTacticalMatchParticipantRecord* Candidate)
	{
		return Candidate
			&& TacticalRules::IsSameParticipantGeneration(
				ExpectedParticipantId, Candidate->ParticipantId)
			&& !Candidate->PlayerController.IsValid()
			&& Candidate->ConnectionState !=
				ETacticalParticipantConnectionState::ConnectedPlaying
			&& Candidate->ConnectionState !=
				ETacticalParticipantConnectionState::ReconnectedPlaying;
	};
	if (!IsExpectedOfflineRecord(Record))
	{
		return false;
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	if (!bMatchStarted && Record->ReconnectDeadlineServerTime > 0.0f
		&& Now >= Record->ReconnectDeadlineServerTime)
	{
		ReleaseExpiredLobbyParticipant(IdentityKey, ExpectedParticipantId);
		return false;
	}
	if (bMatchStarted
		&& Record->ConnectionState == ETacticalParticipantConnectionState::DisconnectedGrace
		&& Record->ReconnectDeadlineServerTime > 0.0f
		&& Now >= Record->ReconnectDeadlineServerTime)
	{
		ServiceDisconnectedParticipants();
		Record = RosterSubsystem->FindByIdentity(IdentityKey);
		if (!IsExpectedOfflineRecord(Record))
		{
			return false;
		}
	}

	ATacticalPlayerState* PreservedState = Record->PlayerState.Get();
	ATacticalPlayerState* TemporaryState =
		NewPlayer->GetPlayerState<ATacticalPlayerState>();
	if (!IsValid(PreservedState) || !IsValid(TemporaryState)
		|| PreservedState == TemporaryState)
	{
		return false;
	}
	TWeakObjectPtr<ATacticalPlayerState> PreservedStateWeak = PreservedState;
	TWeakObjectPtr<ATacticalPlayerState> TemporaryStateWeak = TemporaryState;

	if (ATacticalDisconnectedController* Proxy =
		Record->DisconnectedController.Get())
	{
		// Leave the Pawn on the proxy until Super::PostLogin has finished. The
		// proxy no longer owns the PlayerState, so it cannot destroy that state if
		// an unrelated cleanup occurs during login callbacks.
		Proxy->DetachPreservedPlayerState();
	}

	NewPlayer->SetPlayerState(PreservedState);
	PreservedState->SetOwner(NewPlayer);
	PreservedState->SetReplicates(true);
	PreservedState->SetLifeSpan(0.0f);
	PreservedState->SetIsInactive(false);
	// Claim the record and publish the transaction marker before invoking any
	// Blueprint-extensible PlayerState callback below. A callback-triggered
	// Logout can then restore the old proxy instead of creating a second one.
	Record->PlayerController = NewPlayer;
	FPendingRosterReconnectTransaction Transaction;
	Transaction.IdentityKey = IdentityKey;
	Transaction.ParticipantId = ExpectedParticipantId;
	Transaction.TeamByte = Record->TeamByte;
	Transaction.bRosterSlotReserved = Record->bRosterSlotReserved;
	Transaction.bRetainedBodyState = Record->ConnectionState ==
			ETacticalParticipantConnectionState::DisconnectedGrace
		|| Record->ConnectionState ==
			ETacticalParticipantConnectionState::DisconnectedBodyRetained;
	Transaction.EnteredRound = Record->EnteredRound;
	Transaction.LastSettledRound = Record->LastSettledRound;
	Transaction.ReconnectDeadlineServerTime =
		Record->ReconnectDeadlineServerTime;
	Transaction.RetainedControlRotation = Record->RetainedControlRotation;
	Transaction.PreservedState = PreservedState;
	Transaction.DisconnectedController = Record->DisconnectedController;
	Transaction.RetainedPawn = Record->RetainedPawn;
	PendingRosterReconnects.Add(NewPlayer, MoveTemp(Transaction));
	PreservedState->DispatchOverrideWith(TemporaryState);

	auto IsTransactionValid = [this, NewPlayer, &IdentityKey,
		ExpectedParticipantId, &PreservedStateWeak]()
	{
		const FTacticalMatchParticipantRecord* CurrentRecord = RosterSubsystem
			? RosterSubsystem->FindByIdentity(IdentityKey)
			: nullptr;
		const FPendingRosterReconnectTransaction* PendingTransaction =
			PendingRosterReconnects.Find(NewPlayer);
		return IsValid(NewPlayer) && PreservedStateWeak.IsValid()
			&& PendingTransaction
			&& PendingTransaction->IdentityKey == IdentityKey
			&& PendingTransaction->ParticipantId == ExpectedParticipantId
			&& CurrentRecord
			&& CurrentRecord->ParticipantId == ExpectedParticipantId
			&& CurrentRecord->PlayerController.Get() == NewPlayer
			&& CurrentRecord->PlayerState.Get() == PreservedStateWeak.Get()
			&& NewPlayer->GetPlayerState<ATacticalPlayerState>() == PreservedStateWeak.Get();
	};
	auto AbortTransaction = [NewPlayer, &TemporaryStateWeak]()
	{
		if (ATacticalPlayerState* StateToDestroy = TemporaryStateWeak.Get())
		{
			StateToDestroy->SetIsInactive(true);
			StateToDestroy->SetUniqueId(FUniqueNetIdRepl());
			StateToDestroy->Destroy();
		}
		if (IsValid(NewPlayer))
		{
			NewPlayer->Destroy();
		}
	};

	if (!IsTransactionValid())
	{
		AbortTransaction();
		return false;
	}
	PreservedState = PreservedStateWeak.Get();
	PreservedState->SetConnectionStatus(ETacticalPublicConnectionStatus::Connected);
	PreservedState->SetReady(false);
	if (GameState)
	{
		GameState->AddPlayerState(PreservedState);
	}
	if (!IsTransactionValid())
	{
		AbortTransaction();
		return false;
	}

	if (ATacticalPlayerState* StateToDestroy = TemporaryStateWeak.Get())
	{
		StateToDestroy->SetIsInactive(true);
		StateToDestroy->SetUniqueId(FUniqueNetIdRepl());
		StateToDestroy->Destroy();
	}
	if (!IsTransactionValid())
	{
		AbortTransaction();
		return false;
	}
	PreservedState = PreservedStateWeak.Get();
	PreservedState->OnReactivated();
	if (!IsTransactionValid())
	{
		AbortTransaction();
		return false;
	}
	PreservedState = PreservedStateWeak.Get();
	PreservedState->ForceNetUpdate();
	NewPlayer->ForceNetUpdate();
	Record = RosterSubsystem->FindByIdentity(IdentityKey);

	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=RosterParticipantStatePrepared Player=\"%s\" Participant=%s BeforeSuperPostLogin=1"),
		*PreservedState->GetPlayerName(),
		Record ? *Record->ParticipantId.ToString() : TEXT("Invalid"));
	return true;
}

bool ATacticalGameMode::TryRestoreRosterParticipant(
	APlayerController* NewPlayer,
	const FString& IdentityKey)
{
	if (!HasAuthority() || !RosterSubsystem || !NewPlayer || IdentityKey.IsEmpty())
	{
		return false;
	}
	const FPendingRosterReconnectTransaction* PendingTransaction =
		PendingRosterReconnects.Find(NewPlayer);
	if (!PendingTransaction || PendingTransaction->IdentityKey != IdentityKey
		|| !PendingTransaction->ParticipantId.IsValid())
	{
		return false;
	}
	const FPendingRosterReconnectTransaction TransactionSnapshot =
		*PendingTransaction;

	FTacticalMatchParticipantRecord* Record =
		RosterSubsystem->FindByIdentity(IdentityKey);
	ATacticalPlayerState* PreservedState =
		TransactionSnapshot.PreservedState.Get();
	if (!Record
		|| !TacticalRules::IsSameParticipantGeneration(
			TransactionSnapshot.ParticipantId, Record->ParticipantId)
		|| Record->PlayerController.Get() != NewPlayer
		|| !IsValid(PreservedState)
		|| Record->PlayerState.Get() != PreservedState
		|| Record->DisconnectedController.Get() !=
			TransactionSnapshot.DisconnectedController.Get()
		|| Record->RetainedPawn.Get() != TransactionSnapshot.RetainedPawn.Get()
		|| NewPlayer->GetPlayerState<ATacticalPlayerState>() != PreservedState)
	{
		return false;
	}

	// PrepareRosterParticipantReconnect admitted this connection immediately
	// before Super::PostLogin. Treat that admission as atomic: a grace deadline
	// crossing during login callbacks must not destroy the PlayerState now owned
	// by NewPlayer.
	// Copy every decision input before the first possession/destruction callback.
	// The registry is a TMap, so a value pointer must never survive a callback
	// that can add/remove records.
	const FGuid ExpectedParticipantId = TransactionSnapshot.ParticipantId;
	const uint8 ReservedTeam = TransactionSnapshot.TeamByte;
	const bool bSlotReserved = TransactionSnapshot.bRosterSlotReserved;
	const int32 EnteredRound = TransactionSnapshot.EnteredRound;
	const int32 LastSettledRound = TransactionSnapshot.LastSettledRound;
	const FRotator RetainedRotation =
		TransactionSnapshot.RetainedControlRotation;
	const int32 ReconnectRound = CurrentRound;
	const bool bMatchStartedAtReconnect = bMatchStarted;
	const ERoundPhase ReconnectPhase = TacticalGS
		? TacticalGS->CurrentPhase
		: ERoundPhase::PreRound;
	TWeakObjectPtr<ATacticalPlayerState> PreservedStateWeak = PreservedState;
	TWeakObjectPtr<APawn> BootstrapPawnWeak = TransactionSnapshot.BootstrapPawn;
	TWeakObjectPtr<ATacticalCharacter> RetainedPawnWeak =
		TransactionSnapshot.RetainedPawn;
	TWeakObjectPtr<ATacticalDisconnectedController> DisconnectedControllerWeak =
		TransactionSnapshot.DisconnectedController;

	auto FindTransactionRecord = [this, NewPlayer, &IdentityKey,
		ExpectedParticipantId, ReconnectRound, ReconnectPhase,
		bMatchStartedAtReconnect, &PreservedStateWeak]() -> FTacticalMatchParticipantRecord*
	{
		FTacticalMatchParticipantRecord* CurrentRecord = RosterSubsystem
			? RosterSubsystem->FindByIdentity(IdentityKey)
			: nullptr;
		const FPendingRosterReconnectTransaction* CurrentTransaction =
			PendingRosterReconnects.Find(NewPlayer);
		if (!IsValid(NewPlayer) || !PreservedStateWeak.IsValid()
			|| CurrentRound != ReconnectRound
			|| bMatchStarted != bMatchStartedAtReconnect
			|| !TacticalGS || TacticalGS->CurrentPhase != ReconnectPhase
			|| !CurrentTransaction
			|| CurrentTransaction->IdentityKey != IdentityKey
			|| CurrentTransaction->ParticipantId != ExpectedParticipantId
			|| !CurrentRecord
			|| CurrentRecord->ParticipantId != ExpectedParticipantId
			|| CurrentRecord->PlayerController.Get() != NewPlayer
			|| CurrentRecord->PlayerState.Get() != PreservedStateWeak.Get()
			|| CurrentTransaction->PreservedState.Get() != PreservedStateWeak.Get()
			|| CurrentRecord->DisconnectedController.Get() !=
				CurrentTransaction->DisconnectedController.Get()
			|| CurrentRecord->RetainedPawn.Get() !=
				CurrentTransaction->RetainedPawn.Get()
			|| NewPlayer->GetPlayerState<ATacticalPlayerState>() != PreservedStateWeak.Get())
		{
			return nullptr;
		}
		return CurrentRecord;
	};

	ATacticalCharacter* RetainedPawn = RetainedPawnWeak.Get();
	const bool bCanReclaimPawn = TacticalRules::CanReclaimRetainedPawn(
		TransactionSnapshot.bRetainedBodyState,
		IsValid(RetainedPawn) && !RetainedPawn->IsDead(),
		EnteredRound == ReconnectRound,
		ReconnectPhase);
	const bool bLobbyReturn = !bMatchStartedAtReconnect
		&& ReconnectPhase == ERoundPhase::PreRound;
	const bool bCanEnterCurrentBuy = bMatchStartedAtReconnect
		&& ReconnectPhase == ERoundPhase::Buy
		&& bSlotReserved && ReservedTeam <= 1
		&& EnteredRound != ReconnectRound;
	const bool bRetainedBodyLostThisRound =
		TransactionSnapshot.bRetainedBodyState
		&& !bCanReclaimPawn
		&& EnteredRound == ReconnectRound
		&& LastSettledRound < ReconnectRound
		&& (ReconnectPhase == ERoundPhase::Buy
			|| ReconnectPhase == ERoundPhase::Combat);

	if (APawn* BootstrapPawn = BootstrapPawnWeak.Get())
	{
		if (!FindTransactionRecord()
			|| (BootstrapPawn->GetController()
				&& BootstrapPawn->GetController() != NewPlayer)
			|| (NewPlayer->GetPawn() && NewPlayer->GetPawn() != BootstrapPawn))
		{
			return false;
		}
		if (BootstrapPawn->GetController() == NewPlayer)
		{
			NewPlayer->UnPossess();
			if (!FindTransactionRecord())
			{
				return false;
			}
		}
	}

	ATacticalDisconnectedController* DisconnectedController =
		DisconnectedControllerWeak.Get();

	PreservedState = PreservedStateWeak.Get();
	PreservedState->SetOwner(NewPlayer);
	PreservedState->SetReplicates(true);
	PreservedState->SetLifeSpan(0.0f);
	PreservedState->SetIsInactive(false);
	PreservedState->SetConnectionStatus(ETacticalPublicConnectionStatus::Connected);
	PreservedState->SetReady(false);
	if (GameState)
	{
		GameState->AddPlayerState(PreservedState);
		if (!FindTransactionRecord())
		{
			return false;
		}
	}

	Record = FindTransactionRecord();
	if (!Record)
	{
		return false;
	}
	Record->PlayerController = NewPlayer;
	Record->PlayerState = PreservedState;

	if (ReservedTeam <= 1 && bSlotReserved)
	{
		PreservedState->TeamByte = ReservedTeam;
	}

	if (bCanReclaimPawn)
	{
		Record = FindTransactionRecord();
		AController* RetainedPawnOwner = IsValid(RetainedPawn)
			? RetainedPawn->GetController()
			: nullptr;
		if (!Record || !IsValid(RetainedPawn) || RetainedPawn->IsDead()
			|| (RetainedPawnOwner
				&& RetainedPawnOwner != DisconnectedController
				&& RetainedPawnOwner != NewPlayer)
			|| NewPlayer->GetPawn())
		{
			return false;
		}
		PreservedState = PreservedStateWeak.Get();
		PreservedState->SetIsOnlyASpectator(false);
		PreservedState->SetIsSpectator(false);
		PreservedState->bIsActiveParticipant = true;
		NewPlayer->Possess(RetainedPawn);
		if (!(Record = FindTransactionRecord()))
		{
			return false;
		}
		if (NewPlayer->GetPawn() != RetainedPawn)
		{
			return false;
		}
		NewPlayer->SetControlRotation(RetainedRotation);
		if (bSlotReserved && ReservedTeam <= 1)
		{
			(ReservedTeam == 0 ? TeamAPlayers : TeamBPlayers).AddUnique(NewPlayer);
		}
		Record->ConnectionState = ETacticalParticipantConnectionState::ReconnectedPlaying;
	}
	else if (bLobbyReturn || bCanEnterCurrentBuy)
	{
		PreservedState = PreservedStateWeak.Get();
		PreservedState->SetIsOnlyASpectator(false);
		PreservedState->SetIsSpectator(false);
		PreservedState->bIsActiveParticipant = true;
		if (APawn* BootstrapPawn = BootstrapPawnWeak.Get())
		{
			if (!FindTransactionRecord()
				|| (BootstrapPawn->GetController()
					&& BootstrapPawn->GetController() != NewPlayer)
				|| (NewPlayer->GetPawn() && NewPlayer->GetPawn() != BootstrapPawn))
			{
				return false;
			}
			NewPlayer->Possess(BootstrapPawn);
		}
		else
		{
			RestartPlayer(NewPlayer);
			if (!FindTransactionRecord())
			{
				return false;
			}
			APawn* SpawnedBootstrapPawn = NewPlayer->GetPawn();
			if (!IsValid(SpawnedBootstrapPawn)
				|| SpawnedBootstrapPawn->GetController() != NewPlayer)
			{
				return false;
			}
			if (FPendingRosterReconnectTransaction* CurrentTransaction =
				PendingRosterReconnects.Find(NewPlayer);
				CurrentTransaction
				&& CurrentTransaction->IdentityKey == IdentityKey
				&& CurrentTransaction->ParticipantId == ExpectedParticipantId)
			{
				CurrentTransaction->BootstrapPawn = SpawnedBootstrapPawn;
				BootstrapPawnWeak = SpawnedBootstrapPawn;
			}
		}
		if (!(Record = FindTransactionRecord()))
		{
			return false;
		}
		if (!Cast<ATacticalCharacter>(NewPlayer->GetPawn()))
		{
			return false;
		}
		InitializeParticipantPawn(NewPlayer, ReservedTeam, bCanEnterCurrentBuy);
		if (!(Record = FindTransactionRecord()))
		{
			return false;
		}
		if (ReservedTeam <= 1)
		{
			MovePlayerToAssignedStart(NewPlayer, ReservedTeam);
			if (!(Record = FindTransactionRecord()))
			{
				return false;
			}
		}
		if (bSlotReserved && ReservedTeam <= 1)
		{
			(ReservedTeam == 0 ? TeamAPlayers : TeamBPlayers).AddUnique(NewPlayer);
		}
		Record->ConnectionState = ETacticalParticipantConnectionState::ReconnectedPlaying;
	}
	else
	{
		if (APawn* BootstrapPawn = BootstrapPawnWeak.Get())
		{
			if (!FindTransactionRecord()
				|| (BootstrapPawn->GetController()
					&& BootstrapPawn->GetController() != NewPlayer)
				|| (NewPlayer->GetPawn() && NewPlayer->GetPawn() != BootstrapPawn))
			{
				return false;
			}
			if (BootstrapPawn->GetController() == NewPlayer)
			{
				NewPlayer->UnPossess();
				if (!FindTransactionRecord())
				{
					return false;
				}
			}
			BootstrapPawn->Destroy();
			if (!FindTransactionRecord())
			{
				return false;
			}
		}
		PreservedState = PreservedStateWeak.Get();
		if (bRetainedBodyLostThisRound)
		{
			// The retained body can die/be destroyed between admission and this
			// takeover. Commit that already-observed body loss before attaching the
			// controller as a spectator, otherwise ServiceDisconnectedParticipants
			// will skip the now-connected record and the round can stall.
			PreservedState->PendingSettlementRound = ReconnectRound;
			PreservedState->ClearEquipmentOnDeath();
			if (!(Record = FindTransactionRecord()))
			{
				return false;
			}
			PreservedState = PreservedStateWeak.Get();
		}
		PreservedState->bIsActiveParticipant = false;
		MovePlayerToSpectator(NewPlayer, bMatchStartedAtReconnect && bSlotReserved);
		if (!(Record = FindTransactionRecord()))
		{
			return false;
		}
		if (!NewPlayer->IsInState(NAME_Spectating) || NewPlayer->GetPawn())
		{
			return false;
		}
		if (bSlotReserved && ReservedTeam <= 1)
		{
			(ReservedTeam == 0 ? TeamAPlayers : TeamBPlayers).AddUnique(NewPlayer);
		}
		Record->ConnectionState = ETacticalParticipantConnectionState::ReconnectedSpectating;
	}

	Record = FindTransactionRecord();
	if (!Record)
	{
		return false;
	}
	// Only retire the old proxy/body after the new possession or spectator state
	// is proven valid. Until here, Logout can roll the entire takeover back.
	if (bCanReclaimPawn)
	{
		if (APawn* BootstrapPawn = BootstrapPawnWeak.Get();
			IsValid(BootstrapPawn) && BootstrapPawn != RetainedPawn)
		{
			if (!FindTransactionRecord()
				|| (BootstrapPawn->GetController()
					&& BootstrapPawn->GetController() != NewPlayer)
				|| (NewPlayer->GetPawn() && NewPlayer->GetPawn() == BootstrapPawn))
			{
				return false;
			}
			BootstrapPawn->Destroy();
			if (!(Record = FindTransactionRecord()))
			{
				return false;
			}
		}
	}
	else if (IsValid(RetainedPawn))
	{
		if (!FindTransactionRecord())
		{
			return false;
		}
		AController* RetainedPawnOwner = RetainedPawn->GetController();
		if (RetainedPawnOwner && RetainedPawnOwner != DisconnectedController)
		{
			return false;
		}
		if (IsValid(DisconnectedController)
			&& RetainedPawnOwner == DisconnectedController)
		{
			DisconnectedController->UnPossess();
			if (!FindTransactionRecord())
			{
				return false;
			}
		}
		RetainedPawn->Destroy();
		if (!(Record = FindTransactionRecord()))
		{
			return false;
		}
	}
	if (IsValid(DisconnectedController))
	{
		if (!FindTransactionRecord())
		{
			return false;
		}
		APawn* ProxyPawn = DisconnectedController->GetPawn();
		if (ProxyPawn && ProxyPawn != RetainedPawn)
		{
			return false;
		}
		if (ProxyPawn)
		{
			DisconnectedController->UnPossess();
			if (!FindTransactionRecord())
			{
				return false;
			}
		}
		DisconnectedController->DetachPreservedPlayerState();
		DisconnectedController->Destroy();
		if (!(Record = FindTransactionRecord()))
		{
			return false;
		}
	}
	Record->DisconnectedController = nullptr;
	Record->RetainedPawn = nullptr;
	Record->ReconnectDeadlineServerTime = 0.0f;
	if (bCanEnterCurrentBuy)
	{
		// Commit round admission only after every spawn/movement/proxy callback
		// has succeeded. A failed takeover can then retry this same Buy phase.
		Record->EnteredRound = ReconnectRound;
	}
	PendingRosterReconnects.Remove(NewPlayer);
	PreservedState = PreservedStateWeak.Get();
	PreservedState->ForceNetUpdate();
	NewPlayer->ForceNetUpdate();
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=RosterParticipantReclaimed Player=\"%s\" Participant=%s Team=%s Round=%d Phase=%s SamePawn=%d EnteredBuy=%d Spectating=%d"),
		*PreservedState->GetPlayerName(), *ExpectedParticipantId.ToString(),
		TacticalDiagnostics::GetTeamName(PreservedState->TeamByte), ReconnectRound,
		TacticalDiagnostics::GetPhaseName(ReconnectPhase),
		bCanReclaimPawn, bCanEnterCurrentBuy, !PreservedState->bIsActiveParticipant);
	if (bRetainedBodyLostThisRound && ReconnectPhase == ERoundPhase::Combat)
	{
		CheckVictoryConditions();
	}
	return true;
}

void ATacticalGameMode::ServiceDisconnectedParticipants()
{
	if (!HasAuthority() || !RosterSubsystem || !GetWorld())
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	TArray<TPair<FString, FGuid>> Candidates;
	Candidates.Reserve(RosterSubsystem->GetMutableRecords().Num());
	for (const TPair<FString, FTacticalMatchParticipantRecord>& Pair :
		RosterSubsystem->GetMutableRecords())
	{
		if (!Pair.Value.PlayerController.IsValid())
		{
			Candidates.Emplace(Pair.Key, Pair.Value.ParticipantId);
		}
	}

	bool bNeedsCombatVictoryRefresh = false;
	for (const TPair<FString, FGuid>& Candidate : Candidates)
	{
		const FString& IdentityKey = Candidate.Key;
		FTacticalMatchParticipantRecord* Record =
			RosterSubsystem->FindByIdentity(IdentityKey);
		if (!Record || !TacticalRules::IsSameParticipantGeneration(
				Candidate.Value, Record->ParticipantId)
			|| Record->PlayerController.IsValid())
		{
			continue;
		}
		if (!bMatchStarted && Record->ReconnectDeadlineServerTime > 0.0f
			&& Now >= Record->ReconnectDeadlineServerTime)
		{
			ReleaseExpiredLobbyParticipant(IdentityKey, Candidate.Value);
			continue;
		}

		ATacticalPlayerState* TPS = Record->PlayerState.Get();
		if (!IsValid(TPS))
		{
			continue;
		}
		const bool bHasRetainedBodyState = Record->ConnectionState ==
				ETacticalParticipantConnectionState::DisconnectedGrace
			|| Record->ConnectionState ==
				ETacticalParticipantConnectionState::DisconnectedBodyRetained;
		if (!bHasRetainedBodyState)
		{
			continue;
		}

		ATacticalCharacter* Pawn = Record->RetainedPawn.Get();
		if (!IsValid(Pawn) || Pawn->IsDead())
		{
			const bool bNeedsSettlement = Record->EnteredRound == CurrentRound
				&& Record->LastSettledRound < CurrentRound;
			TWeakObjectPtr<ATacticalPlayerState> StateWeak = TPS;
			// Commit registry state before the armor delegate in
			// ClearEquipmentOnDeath can enter user-authored code.
			Record->ConnectionState =
				ETacticalParticipantConnectionState::DisconnectedWaiting;
			Record->ReconnectDeadlineServerTime = 0.0f;
			TPS->bIsActiveParticipant = false;
			TPS->SetConnectionStatus(ETacticalPublicConnectionStatus::Disconnected);
			if (bNeedsSettlement)
			{
				TPS->PendingSettlementRound = CurrentRound;
			}
			TPS->ClearEquipmentOnDeath();
			if (ATacticalPlayerState* CurrentState = StateWeak.Get())
			{
				CurrentState->ForceNetUpdate();
			}
			bNeedsCombatVictoryRefresh = true;
			continue;
		}
		if (Record->ConnectionState ==
			ETacticalParticipantConnectionState::DisconnectedBodyRetained
			|| Record->ReconnectDeadlineServerTime <= 0.0f
			|| Now < Record->ReconnectDeadlineServerTime)
		{
			continue;
		}

		const FGuid ParticipantId = Record->ParticipantId;
		const uint8 Team = Record->TeamByte;
		const FString PlayerName = TPS->GetPlayerName();
		Record->ReconnectDeadlineServerTime = 0.0f;
		Record->ConnectionState =
			ETacticalParticipantConnectionState::DisconnectedBodyRetained;
		TPS->bIsActiveParticipant = true;
		TPS->SetConnectionStatus(ETacticalPublicConnectionStatus::Disconnected);
		TPS->ForceNetUpdate();
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=DisconnectStatusWindowExpired Player=\"%s\" Participant=%s Team=%s Round=%d Phase=%s BodyRetained=1"),
			*PlayerName, *ParticipantId.ToString(),
			TacticalDiagnostics::GetTeamName(Team), CurrentRound,
			TacticalGS ? TacticalDiagnostics::GetPhaseName(TacticalGS->CurrentPhase) : TEXT("Unknown"));
	}

	if (bNeedsCombatVictoryRefresh && TacticalGS
		&& TacticalGS->CurrentPhase == ERoundPhase::Combat)
	{
		CheckVictoryConditions();
	}
}

void ATacticalGameMode::MarkDisconnectedParticipantDead(AController* VictimController)
{
	if (!RosterSubsystem || !VictimController)
	{
		return;
	}

	ATacticalPlayerState* TPS = GetTacticalPS(VictimController);
	FTacticalMatchParticipantRecord* Record = TPS
		? RosterSubsystem->FindByPlayerState(TPS)
		: nullptr;
	if (!Record || Record->DisconnectedController.Get() != VictimController)
	{
		return;
	}

	Record->ConnectionState = ETacticalParticipantConnectionState::DisconnectedWaiting;
	Record->ReconnectDeadlineServerTime = 0.0f;
	TPS->bIsActiveParticipant = false;
	TPS->SetConnectionStatus(ETacticalPublicConnectionStatus::Disconnected);
	if (Record->EnteredRound == CurrentRound && Record->LastSettledRound < CurrentRound)
	{
		TPS->PendingSettlementRound = CurrentRound;
	}
	TPS->ForceNetUpdate();
}

void ATacticalGameMode::FinalizeDisconnectedParticipantsAtRoundBoundary()
{
	if (!RosterSubsystem)
	{
		return;
	}

	TArray<TPair<FString, FGuid>> OfflineParticipants;
	OfflineParticipants.Reserve(RosterSubsystem->GetMutableRecords().Num());
	for (const TPair<FString, FTacticalMatchParticipantRecord>& Pair :
		RosterSubsystem->GetMutableRecords())
	{
		if (!Pair.Value.PlayerController.IsValid()
			&& Pair.Value.ConnectionState !=
				ETacticalParticipantConnectionState::Abandoned)
		{
			OfflineParticipants.Emplace(Pair.Key, Pair.Value.ParticipantId);
		}
	}

	for (const TPair<FString, FGuid>& OfflineParticipant : OfflineParticipants)
	{
		const FString& IdentityKey = OfflineParticipant.Key;
		FTacticalMatchParticipantRecord* Record =
			RosterSubsystem->FindByIdentity(IdentityKey);
		if (!Record || !TacticalRules::IsSameParticipantGeneration(
				OfflineParticipant.Value, Record->ParticipantId)
			|| Record->PlayerController.IsValid()
			|| Record->ConnectionState ==
				ETacticalParticipantConnectionState::Abandoned)
		{
			continue;
		}

		ATacticalPlayerState* TPS = Record->PlayerState.Get();
		ATacticalCharacter* Pawn = Record->RetainedPawn.Get();
		ATacticalDisconnectedController* Proxy =
			Record->DisconnectedController.Get();
		const bool bRetainedPawnAlive = IsValid(Pawn) && !Pawn->IsDead();
		const EWeaponTier PrimaryTier = bRetainedPawnAlive
			? Pawn->GetPrimaryTier()
			: EWeaponTier::DefaultPistol;
		const EWeaponTier PistolTier = bRetainedPawnAlive
			? Pawn->GetPistolTier()
			: EWeaponTier::DefaultPistol;

		// Commit the record before UnPossess/Destroy/PlayerState callbacks. The
		// identity list remains valid even if any callback mutates the roster TMap.
		Record->RetainedPawn = nullptr;
		Record->ReconnectDeadlineServerTime = 0.0f;
		Record->ConnectionState =
			ETacticalParticipantConnectionState::DisconnectedWaiting;
		if (Record->EnteredRound == CurrentRound)
		{
			Record->LastSettledRound = CurrentRound;
		}
		if (IsValid(TPS))
		{
			if (bRetainedPawnAlive)
			{
				TPS->SetSavedLoadout(PrimaryTier, PistolTier);
			}
			TPS->bIsActiveParticipant = false;
			TPS->SetConnectionStatus(ETacticalPublicConnectionStatus::Disconnected);
			TPS->PendingSettlementRound = 0;
			TPS->ForceNetUpdate();
		}
		if (IsValid(Proxy) && Proxy->GetPawn())
		{
			Proxy->UnPossess();
		}
		if (IsValid(Pawn))
		{
			Pawn->Destroy();
		}
	}
}

void ATacticalGameMode::ReleaseExpiredLobbyParticipant(
	const FString& IdentityKey,
	const FGuid& ExpectedParticipantId)
{
	if (!RosterSubsystem)
	{
		return;
	}

	FTacticalMatchParticipantRecord* Record =
		RosterSubsystem->FindByIdentity(IdentityKey);
	if (!Record
		|| (ExpectedParticipantId.IsValid()
			&& !TacticalRules::IsSameParticipantGeneration(
				ExpectedParticipantId, Record->ParticipantId))
		|| Record->PlayerController.IsValid())
	{
		return;
	}

	ATacticalPlayerState* TPS = Record->PlayerState.Get();
	ATacticalCharacter* Pawn = Record->RetainedPawn.Get();
	ATacticalDisconnectedController* Proxy = Record->DisconnectedController.Get();
	const FGuid ParticipantId = Record->ParticipantId;
	const FString PlayerName = TPS ? TPS->GetPlayerName() : TEXT("Unknown");
	const uint8 Team = Record->TeamByte;
	// Remove the exact expired generation before invoking actor callbacks. This
	// prevents a callback-created replacement from being erased by identity.
	DisconnectedPlayers.Remove(IdentityKey);
	RosterSubsystem->RemoveParticipant(IdentityKey);
	if (TPS)
	{
		TPS->bIsActiveParticipant = false;
		TPS->SetConnectionStatus(ETacticalPublicConnectionStatus::Disconnected);
		TPS->ForceNetUpdate();
	}
	if (IsValid(Proxy) && Proxy->GetPawn())
	{
		Proxy->UnPossess();
	}
	if (IsValid(Pawn))
	{
		Pawn->Destroy();
	}
	if (IsValid(Proxy))
	{
		Proxy->DetachPreservedPlayerState();
		Proxy->Destroy();
	}
	if (IsValid(TPS))
	{
		TPS->SetOwner(nullptr);
		TPS->SetUniqueId(FUniqueNetIdRepl());
		TPS->Destroy();
	}
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=LobbyReconnectGraceExpired Player=\"%s\" Participant=%s Team=%s SlotReleased=1"),
		*PlayerName, *ParticipantId.ToString(), TacticalDiagnostics::GetTeamName(Team));
	TryStartMatch();
}

void ATacticalGameMode::SavePlayerState(
	ATacticalPlayerState* TPS,
	const FString& IdentityOverride)
{
	if (!TPS)
	{
		return;
	}

	APlayerState* PS = Cast<APlayerState>(TPS);
	if (!PS)
	{
		return;
	}

	FString PlayerID = IdentityOverride;
	if (PlayerID.IsEmpty())
	{
		if (const APlayerController* OwnerPC =
			Cast<APlayerController>(TPS->GetOwningController()))
		{
			PlayerID = ResolveReconnectIdentity(OwnerPC);
		}
		else
		{
			const FUniqueNetIdRepl& UniqueIdRepl = PS->GetUniqueId();
			PlayerID = BuildProviderQualifiedIdentity(UniqueIdRepl);
		}
	}
	if (PlayerID.IsEmpty())
	{
		UE_LOG(LogTacticalFlow, Warning,
			TEXT("Event=DisconnectStateNotSaved Player=\"%s\" Reason=MissingUniqueNetId"),
			*TPS->GetPlayerName());
		return;
	}

	FPlayerDisconnectState State;
	State.Money = TPS->Money;
	State.ArmorType = TPS->ArmorType;
	State.ArmorDurability = TPS->ArmorDurability;
	State.LossStreak = TPS->LossStreak;
	State.RoundsWon = TPS->RoundsWon;
	State.TeamByte = TPS->TeamByte;
	State.PrimaryTier = TPS->SavedPrimaryTier;
	State.PistolTier = TPS->SavedPistolTier;
	State.PendingSettlementRound = TPS->PendingSettlementRound;
	State.PlayerName = TPS->GetPlayerName();

	// A round-death snapshot must not be overwritten by the still-alive pawn
	// that exists for a few more frames during disconnect cleanup.
	if (State.PendingSettlementRound == 0)
	{
		if (ATacticalCharacter* TacChar = Cast<ATacticalCharacter>(TPS->GetPawn());
			TacChar && !TacChar->IsDead())
		{
			State.PrimaryTier = TacChar->GetPrimaryTier();
			State.PistolTier = TacChar->GetPistolTier();
		}
	}

	DisconnectedPlayers.Add(PlayerID, State);
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=DisconnectStateSaved Player=\"%s\" Team=%s Money=%d PendingSettlementRound=%d Primary=%s Pistol=%s Armor=%s Durability=%.1f"),
		*State.PlayerName, TacticalDiagnostics::GetTeamName(State.TeamByte), State.Money,
		State.PendingSettlementRound, TacticalDiagnostics::GetWeaponTierName(State.PrimaryTier),
		TacticalDiagnostics::GetWeaponTierName(State.PistolTier),
		TacticalDiagnostics::GetArmorTypeName(State.ArmorType), State.ArmorDurability);
}

bool ATacticalGameMode::SavePlayerStateForParticipantGeneration(
	ATacticalPlayerState* TPS,
	const FString& IdentityKey,
	const FGuid& ExpectedParticipantId)
{
	if (!IsValid(TPS) || IdentityKey.IsEmpty())
	{
		return false;
	}

	const FTacticalMatchParticipantRecord* CurrentRecord = RosterSubsystem
		? RosterSubsystem->FindByIdentity(IdentityKey)
		: nullptr;
	if (!CurrentRecord || !TacticalRules::IsSameParticipantGeneration(
		ExpectedParticipantId, CurrentRecord->ParticipantId))
	{
		UE_LOG(LogTacticalFlow, Warning,
			TEXT("Event=DisconnectStateSaveSkipped Identity=Redacted ExpectedParticipant=%s CurrentParticipant=%s Reason=GenerationUnavailable"),
			ExpectedParticipantId.IsValid()
				? *ExpectedParticipantId.ToString()
				: TEXT("Invalid"),
			CurrentRecord
				? *CurrentRecord->ParticipantId.ToString()
				: TEXT("Missing"));
		return false;
	}

	SavePlayerState(TPS, IdentityKey);
	if (FPlayerDisconnectState* SavedState = DisconnectedPlayers.Find(IdentityKey))
	{
		SavedState->ParticipantId = ExpectedParticipantId;
		return true;
	}
	return false;
}

void ATacticalGameMode::RestorePlayerState(ATacticalPlayerState* TPS, const FPlayerDisconnectState& State)
{
	if (!TPS)
	{
		return;
	}

	TPS->Money = FMath::Clamp(State.Money, 0, TacticalEconomy::MaxMoney);
	TPS->ArmorType = State.ArmorDurability > 0.0f
		? State.ArmorType
		: EArmorType::None;
	TPS->ArmorDurability = FMath::Max(0.0f, State.ArmorDurability);
	++TPS->ArmorStateSerial;
	TPS->LossStreak = FMath::Clamp(State.LossStreak, 0, TacticalEconomy::MaxLossStreak);
	TPS->RoundsWon = FMath::Max(0, State.RoundsWon);
	TPS->SavedPrimaryTier = State.PrimaryTier;
	TPS->SavedPistolTier = State.PistolTier;
	TPS->PendingSettlementRound = State.PendingSettlementRound;

	// Team capacity and membership are committed by the generation-validated
	// PostLogin transaction after presentation delegates return.
	TPS->TeamByte = State.TeamByte <= 1 ? State.TeamByte : uint8{255};

	// All authoritative fields and roster membership are committed before either
	// Blueprint-extensible presentation delegate runs. Never dereference TPS after
	// a delegate unless the weak reference still resolves.
	TWeakObjectPtr<ATacticalPlayerState> StateWeak = TPS;
	const EArmorType RestoredArmorType = TPS->ArmorType;
	const float RestoredArmorPercent = TPS->GetArmorDurabilityPercent();
	const int32 RestoredMoney = TPS->Money;
	TPS->OnArmorUpdated.Broadcast(RestoredArmorType, RestoredArmorPercent);
	if (ATacticalPlayerState* CurrentState = StateWeak.Get())
	{
		CurrentState->OnMoneyUpdated.Broadcast(RestoredMoney);
	}
}

bool ATacticalGameMode::ReadyToStartMatch_Implementation()
{
	if (!TacticalGS || TacticalGS->CurrentPhase != ERoundPhase::PreRound || bMatchStarted)
	{
		return false;
	}
	if (bUsePlacedSpawnPoints && !bPlacedSpawnLayoutValid)
	{
		return false;
	}
	const int32 RequiredPlayers = FMath::Clamp(MinPlayersToStart, 2, TacticalEconomy::MaxPlayersPerTeam * 2);
	return TacticalRules::CanStartMatch(
		TeamAPlayers.Num(), TeamBPlayers.Num(), CountLobbyCandidates(), RequiredPlayers,
		AreAllLobbyPlayersSelectedAndReady());
}

AActor* ATacticalGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (bMatchStarted || !Player || !GetWorld())
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	if (PlacedSpawnPoints.IsEmpty())
	{
		CacheAndValidatePlacedSpawnPoints();
	}
	if (!bUsePlacedSpawnPoints)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	const TWeakObjectPtr<AController> ControllerKey(Player);
	if (const int32* AssignedSlot = LobbySlotAssignments.Find(ControllerKey))
	{
		if (ATacticalSpawnPoint* Marker = FindPlacedSpawnPoint(
			ETacticalSpawnGroup::Lobby, *AssignedSlot))
		{
			return Marker;
		}
	}

	for (int32 Slot = 1; Slot <= TacticalEconomy::MaxPlayersPerTeam * 2; ++Slot)
	{
		bool bOccupied = false;
		for (const TPair<TWeakObjectPtr<AController>, int32>& Assignment : LobbySlotAssignments)
		{
			if (Assignment.Key.IsValid() && Assignment.Value == Slot)
			{
				bOccupied = true;
				break;
			}
		}
		if (!bOccupied)
		{
			if (ATacticalSpawnPoint* Marker = FindPlacedSpawnPoint(
				ETacticalSpawnGroup::Lobby, Slot))
			{
				LobbySlotAssignments.Add(ControllerKey, Slot);
				UE_LOG(LogTacticalFlow, Display,
					TEXT("Event=LobbySpawnAssigned Controller=\"%s\" Slot=%d Marker=\"%s\""),
					*GetNameSafe(Player), Slot, *GetNameSafe(Marker));
				return Marker;
			}
		}
	}

	UE_LOG(LogTacticalFlow, Error,
		TEXT("Event=LobbySpawnUnavailable Controller=\"%s\" Reason=NoDistinctConfiguredSlot"),
		*GetNameSafe(Player));
	return Super::ChoosePlayerStart_Implementation(Player);
}

void ATacticalGameMode::HandleStartingNewPlayer_Implementation(
	APlayerController* NewPlayer)
{
	// A reconnect prepared before Super::PostLogin already owns its preserved
	// PlayerState. Its preserved Pawn (if any) is still held by the disconnect
	// proxy until PostLogin completes, so the normal startup path must not spawn
	// a second, temporary Pawn in between.
	if (NewPlayer && PendingRosterReconnects.Contains(NewPlayer))
	{
		UE_LOG(LogTacticalFlow, Verbose,
			TEXT("Event=ReconnectAutoSpawnSuppressed Player=\"%s\""),
			*GetNameSafe(NewPlayer->GetPlayerState<APlayerState>()));
		return;
	}

	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

bool ATacticalGameMode::PlayerCanRestart_Implementation(APlayerController* Player)
{
	// After MatchStarted, every spawn is issued explicitly by StartRound.
	// This also prevents a recoverable reconnect spectator from using the
	// engine's ServerRestartPlayer RPC to enter Combat before that boundary.
	return TacticalRules::AllowsClientRequestedRestart(bMatchStarted)
		&& Super::PlayerCanRestart_Implementation(Player);
}

void ATacticalGameMode::NotifyPlayerReadyStateChanged()
{
	if (HasAuthority() && !bMatchStarted && TacticalGS && TacticalGS->CurrentPhase == ERoundPhase::PreRound)
	{
		LogFlowSnapshot(TEXT("ReadyStateChanged"));
		TryStartMatch();
	}
}

bool ATacticalGameMode::TrySelectTeamAndReady(APlayerController* PlayerController, uint8 DesiredTeam)
{
	if (!HasAuthority() || !PlayerController || DesiredTeam > 1 || bMatchStarted)
	{
		return false;
	}

	if (!TacticalGS)
	{
		TacticalGS = GetGameState<ATacticalGameState>();
	}
	ATacticalPlayerState* TPS = PlayerController->GetPlayerState<ATacticalPlayerState>();
	if (!TacticalGS || TacticalGS->CurrentPhase != ERoundPhase::PreRound
		|| !TPS || !TPS->bIsActiveParticipant)
	{
		return false;
	}

	TeamAPlayers.RemoveAll([](const TObjectPtr<APlayerController>& PC) { return !IsValid(PC); });
	TeamBPlayers.RemoveAll([](const TObjectPtr<APlayerController>& PC) { return !IsValid(PC); });
	TArray<TObjectPtr<APlayerController>>& DesiredRoster = DesiredTeam == 0 ? TeamAPlayers : TeamBPlayers;
	FTacticalMatchParticipantRecord* ParticipantRecord = RosterSubsystem
		? RosterSubsystem->FindByController(PlayerController)
		: nullptr;
	if (!ParticipantRecord || !ParticipantRecord->bReconnectCredentialAcknowledged)
	{
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=TeamSelectionRejected Player=\"%s\" DesiredTeam=%s Reason=AdmissionNotAcknowledged"),
			*TPS->GetPlayerName(), TacticalDiagnostics::GetTeamName(DesiredTeam));
		return false;
	}
	if (!DesiredRoster.Contains(PlayerController)
		&& (!ParticipantRecord || ParticipantRecord->TeamByte != DesiredTeam)
		&& CountReservedTeamSlots(DesiredTeam) >= TacticalEconomy::MaxPlayersPerTeam)
	{
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=TeamSelectionRejected Player=\"%s\" DesiredTeam=%s Reason=TeamFull TeamA=%d TeamB=%d"),
			*TPS->GetPlayerName(), TacticalDiagnostics::GetTeamName(DesiredTeam),
			TeamAPlayers.Num(), TeamBPlayers.Num());
		return false;
	}

	const uint8 PreviousTeam = TPS->TeamByte;
	const bool bAlreadySelectedAndReady = PreviousTeam == DesiredTeam
		&& DesiredRoster.Contains(PlayerController) && TPS->bIsReady;
	if (bAlreadySelectedAndReady)
	{
		return true;
	}

	TeamAPlayers.Remove(PlayerController);
	TeamBPlayers.Remove(PlayerController);
	DesiredRoster.AddUnique(PlayerController);
	TPS->TeamByte = DesiredTeam;
	TPS->SetReady(true);
	TPS->ForceNetUpdate();
	if (ParticipantRecord)
	{
		ParticipantRecord->TeamByte = DesiredTeam;
		ParticipantRecord->bRosterSlotReserved = true;
		ParticipantRecord->ConnectionState =
			ETacticalParticipantConnectionState::ConnectedPlaying;
	}

	if (ATacticalCharacter* TacticalCharacter = Cast<ATacticalCharacter>(PlayerController->GetPawn()))
	{
		TacticalCharacter->SetTeam(DesiredTeam);
		TacticalCharacter->SetFrozen(false);
	}
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=TeamSelected Player=\"%s\" PreviousTeam=%s Team=%s Ready=1 TeamA=%d TeamB=%d Candidates=%d"),
		*TPS->GetPlayerName(), TacticalDiagnostics::GetTeamName(PreviousTeam),
		TacticalDiagnostics::GetTeamName(DesiredTeam), TeamAPlayers.Num(), TeamBPlayers.Num(),
		CountLobbyCandidates());
	ValidateTacticalState(TEXT("TeamSelected"));
	NotifyPlayerReadyStateChanged();
	return true;
}

// ---- Round Flow ----

void ATacticalGameMode::StartMatch()
{
	if (bMatchStarted)
	{
		return;
	}
	if (bUsePlacedSpawnPoints && !bPlacedSpawnLayoutValid)
	{
		UE_LOG(LogTacticalFlow, Error,
			TEXT("Event=MatchStartBlocked Reason=InvalidPlacedSpawnLayout"));
		return;
	}

	if (!TacticalGS)
	{
		TacticalGS = GetGameState<ATacticalGameState>();
	}
	if (!TacticalGS)
	{
		return;
	}

	bMatchStarted = true;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UTacticalSessionSubsystem* SessionSubsystem =
			GameInstance->GetSubsystem<UTacticalSessionSubsystem>())
		{
			SessionSubsystem->SetGameplayMatchInProgress(true);
		}
	}
	CurrentRound = 0;
	bTeamQualifiedForMatch[0] = TeamAPlayers.Num() > 0;
	bTeamQualifiedForMatch[1] = TeamBPlayers.Num() > 0;
	InitializeMatchEconomy();
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=MatchStarted TeamA=%d TeamB=%d StartingMoney=%d WinTarget=%d MaxRounds=%d"),
		TeamAPlayers.Num(), TeamBPlayers.Num(), TacticalEconomy::StartingMoney,
		TacticalEconomy::RoundsToWin, TacticalEconomy::MaxRounds);
	StartRound(1);
}

void ATacticalGameMode::TryStartMatch()
{
	if (!HasAuthority() || !bAutoStartMatch || bMatchStarted)
	{
		return;
	}

	if (!TacticalGS)
	{
		TacticalGS = GetGameState<ATacticalGameState>();
	}
	if (!TacticalGS)
	{
		UE_LOG(LogTacticalFlow, Warning, TEXT("Event=ReadyGateDelayed Reason=GameStateNotReady"));
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimerForNextTick(this, &ATacticalGameMode::TryStartMatch);
		}
		return;
	}
	if (TacticalGS->CurrentPhase != ERoundPhase::PreRound)
	{
		return;
	}

	const int32 AssignedPlayers = TeamAPlayers.Num() + TeamBPlayers.Num();
	const int32 LobbyPlayers = CountLobbyCandidates();
	const int32 RequiredPlayers = FMath::Clamp(MinPlayersToStart, 2, TacticalEconomy::MaxPlayersPerTeam * 2);
	const bool bAllPlayersReady = AreAllLobbyPlayersSelectedAndReady();
	int32 ReadyPlayers = 0;
	FString WaitingPlayers;
	if (GameState)
	{
		for (const APlayerState* PlayerState : GameState->PlayerArray)
		{
			const ATacticalPlayerState* TPS = Cast<ATacticalPlayerState>(PlayerState);
			if (!TPS || !TPS->bIsActiveParticipant)
			{
				continue;
			}
			if (TPS->TeamByte <= 1 && TPS->bIsReady)
			{
				++ReadyPlayers;
			}
			else
			{
				if (!WaitingPlayers.IsEmpty())
				{
					WaitingPlayers += TEXT(",");
				}
				WaitingPlayers += TPS->GetPlayerName();
				if (TPS->TeamByte > 1)
				{
					WaitingPlayers += TEXT("(NoTeam)");
				}
			}
		}
	}

	const bool bCanStart = (!bUsePlacedSpawnPoints || bPlacedSpawnLayoutValid)
		&& TacticalRules::CanStartMatch(
			TeamAPlayers.Num(), TeamBPlayers.Num(), LobbyPlayers, RequiredPlayers, bAllPlayersReady);
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=ReadyGateEvaluated Phase=%s Candidates=%d Selected=%d Ready=%d TeamA=%d TeamB=%d Connected=%d Required=%d AllSelectedReady=%d CanStart=%d Waiting=\"%s\""),
		TacticalDiagnostics::GetPhaseName(TacticalGS->CurrentPhase), LobbyPlayers, AssignedPlayers, ReadyPlayers,
		TeamAPlayers.Num(), TeamBPlayers.Num(), GameState ? GameState->PlayerArray.Num() : 0,
		RequiredPlayers, bAllPlayersReady, bCanStart, WaitingPlayers.IsEmpty() ? TEXT("None") : *WaitingPlayers);

	if (bCanStart)
	{
		StartMatch();
	}
}

void ATacticalGameMode::StartRound(int32 RoundNumber)
{
	ClearTeamEliminationRoundEnd();
	ResetHuntIntel();
	if (CurrentRound > 0)
	{
		FinalizeDisconnectedParticipantsAtRoundBoundary();
	}

	CurrentRound = RoundNumber;

	if (TacticalGS)
	{
		TacticalGS->ResetRoundState(RoundNumber);
	}
	TeamAPlayers.RemoveAll([](const TObjectPtr<APlayerController>& PC) { return !IsValid(PC); });
	TeamBPlayers.RemoveAll([](const TObjectPtr<APlayerController>& PC) { return !IsValid(PC); });

	for (APlayerController* PC : TeamAPlayers)
	{
		if (ATacticalPlayerState* TPS = PC ? PC->GetPlayerState<ATacticalPlayerState>() : nullptr)
		{
			TPS->SetIsOnlyASpectator(false);
			TPS->bIsActiveParticipant = true;
			TPS->SetConnectionStatus(ETacticalPublicConnectionStatus::Connected);
			TPS->ForceNetUpdate();
			if (RosterSubsystem)
			{
				if (FTacticalMatchParticipantRecord* Record =
					RosterSubsystem->FindByPlayerState(TPS))
				{
					if (Record->ConnectionState ==
						ETacticalParticipantConnectionState::ReconnectedSpectating)
					{
						Record->ConnectionState =
							ETacticalParticipantConnectionState::EligibleNextRound;
						UE_LOG(LogTacticalFlow, Display,
							TEXT("Event=ReconnectEligibleNextRound Player=\"%s\" Participant=%s Round=%d"),
							*TPS->GetPlayerName(), *Record->ParticipantId.ToString(), RoundNumber);
					}
					Record->EnteredRound = RoundNumber;
					Record->ConnectionState =
						ETacticalParticipantConnectionState::ConnectedPlaying;
				}
			}
		}
	}
	for (APlayerController* PC : TeamBPlayers)
	{
		if (ATacticalPlayerState* TPS = PC ? PC->GetPlayerState<ATacticalPlayerState>() : nullptr)
		{
			TPS->SetIsOnlyASpectator(false);
			TPS->bIsActiveParticipant = true;
			TPS->SetConnectionStatus(ETacticalPublicConnectionStatus::Connected);
			TPS->ForceNetUpdate();
			if (RosterSubsystem)
			{
				if (FTacticalMatchParticipantRecord* Record =
					RosterSubsystem->FindByPlayerState(TPS))
				{
					if (Record->ConnectionState ==
						ETacticalParticipantConnectionState::ReconnectedSpectating)
					{
						Record->ConnectionState =
							ETacticalParticipantConnectionState::EligibleNextRound;
						UE_LOG(LogTacticalFlow, Display,
							TEXT("Event=ReconnectEligibleNextRound Player=\"%s\" Participant=%s Round=%d"),
							*TPS->GetPlayerName(), *Record->ParticipantId.ToString(), RoundNumber);
					}
					Record->EnteredRound = RoundNumber;
					Record->ConnectionState =
						ETacticalParticipantConnectionState::ConnectedPlaying;
				}
			}
		}
	}

	RespawnAllPlayers();
	SelectTreasureForRound();
	SetPhase(ERoundPhase::Buy);
	LogFlowSnapshot(TEXT("RoundStarted"));
	ValidateTacticalState(TEXT("RoundStarted"));
}

void ATacticalGameMode::SetPhase(ERoundPhase NewPhase)
{
	if (!TacticalGS)
	{
		return;
	}

	const ERoundPhase PreviousPhase = TacticalGS->CurrentPhase;
#if DO_CHECK
	ensureMsgf(NewPhase != ERoundPhase::Overtime,
		TEXT("Tactical flow attempted to enter the removed Overtime phase."));
	ensureMsgf(IsAllowedPhaseTransition(PreviousPhase, NewPhase),
		TEXT("Unexpected tactical phase transition: %s -> %s"),
		TacticalDiagnostics::GetPhaseName(PreviousPhase), TacticalDiagnostics::GetPhaseName(NewPhase));
#endif

	if (PreviousPhase == ERoundPhase::Combat && NewPhase != ERoundPhase::Combat)
	{
		DeactivateTreasureForRound();
	}

	if (NewPhase != ERoundPhase::Combat)
	{
		ClearTeamEliminationRoundEnd();
		ResetHuntIntel();
	}

	float Duration = 0.0f;
	switch (NewPhase)
	{
	case ERoundPhase::PreRound:
		FreezeAllPlayers(false);
		break;
	case ERoundPhase::Buy:
		Duration = BuyPhaseDuration;
		FreezeAllPlayers(true);
		break;
	case ERoundPhase::Combat:
		Duration = CombatPhaseDuration;
		FreezeAllPlayers(false);
		ResetHuntIntel();
		break;
	case ERoundPhase::RoundEnd:
		Duration = RoundEndDuration;
		FreezeAllPlayers(true);
		break;
	default:
		break;
	}

	TacticalGS->SetPhase(NewPhase, Duration);
	if (NewPhase == ERoundPhase::Combat)
	{
		// Also evaluate once on entry so shortened test durations cannot skip a
		// threshold before the first 50 ms GameMode tick.
		TickTreasureTimeline();
	}
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=PhaseChanged Round=%d From=%s To=%s Duration=%.2f EndServerTime=%.2f ScoreA=%d ScoreB=%d AliveA=%d AliveB=%d"),
		CurrentRound, TacticalDiagnostics::GetPhaseName(PreviousPhase), TacticalDiagnostics::GetPhaseName(NewPhase),
		Duration, TacticalGS->PhaseEndServerTime, TacticalGS->TeamScores[0], TacticalGS->TeamScores[1],
		CountPlayersAlive(0), CountPlayersAlive(1));
}

void ATacticalGameMode::OnPhaseTimerExpired()
{
	if (!TacticalGS)
	{
		return;
	}

	switch (TacticalGS->CurrentPhase)
	{
	case ERoundPhase::Buy:
		SetPhase(ERoundPhase::Combat);
		// Retained disconnect bodies remain alive here. This immediate evaluation
		// only resolves a team that truly has no living round participant (for
		// example, an explicit abandonment or an already-dead reconnect record).
		CheckVictoryConditions();
		break;

	case ERoundPhase::Combat:
	{
		ClearTeamEliminationRoundEnd();
		const bool bTeamADead = IsTeamEliminatedForRound(0);
		const bool bTeamBDead = IsTeamEliminatedForRound(1);
		const TacticalRules::FRoundResolution Resolution =
			TacticalRules::ResolveCombatTimeout(bTeamADead, bTeamBDead);
		if (Resolution.WinningTeam <= 1)
		{
			EndRound(Resolution.WinningTeam, Resolution.Reason);
		}
		else
		{
			EndRoundDraw(Resolution.Reason);
		}
		break;
	}

	case ERoundPhase::RoundEnd:
	{
		TeamAPlayers.RemoveAll([](const TObjectPtr<APlayerController>& PC)
		{
			return !IsValid(PC);
		});
		TeamBPlayers.RemoveAll([](const TObjectPtr<APlayerController>& PC)
		{
			return !IsValid(PC);
		});
		const bool bScoreResultLocked = TacticalGS->IsMatchOver();
		const uint8 ScoreWinner = bScoreResultLocked
			? TacticalGS->GetMatchWinner()
			: uint8{255};
		const TacticalRules::FMatchForfeitResolution MatchResolution =
			TacticalRules::ResolveRoundEndMatchTermination(
				bScoreResultLocked, ScoreWinner,
				bTeamQualifiedForMatch[0], TeamAPlayers.Num(),
				bTeamQualifiedForMatch[1], TeamBPlayers.Num());
		if (MatchResolution.bShouldEndMatch)
		{
			if (MatchResolution.Reason == EMatchEndReason::TeamDisconnectForfeit
				|| MatchResolution.Reason == EMatchEndReason::AllTeamsDisconnectedAbort)
			{
				UE_LOG(LogTacticalFlow, Display,
					TEXT("Event=DisconnectMatchTermination Round=%d Winner=%s Reason=%s ConnectedA=%d ConnectedB=%d ScoreA=%d ScoreB=%d RoundScoreAdded=0"),
					CurrentRound,
					TacticalDiagnostics::GetMatchOutcomeName(
						MatchResolution.WinningTeam, MatchResolution.Reason),
					TacticalDiagnostics::GetMatchEndReasonName(MatchResolution.Reason),
					TeamAPlayers.Num(), TeamBPlayers.Num(),
					TacticalGS->TeamScores[0], TacticalGS->TeamScores[1]);
			}
			EndMatch(MatchResolution.WinningTeam, MatchResolution.Reason);
		}
		else
		{
			StartRound(CurrentRound + 1);
		}
		break;
	}

	default:
		break;
	}
}

void ATacticalGameMode::EndRound(uint8 WinningTeam, ERoundEndReason Reason)
{
	const int32 PreviousScoreA = TacticalGS ? TacticalGS->TeamScores[0] : 0;
	const int32 PreviousScoreB = TacticalGS ? TacticalGS->TeamScores[1] : 0;
	ClearTeamEliminationRoundEnd();
	// Settle while Combat is still authoritative. Every player/record is marked
	// settled before its money delegate runs, so a synchronous Logout cannot turn
	// a completed payout back into a pending (and later duplicated) settlement.
	AwardRoundEconomy(WinningTeam);
	SetPhase(ERoundPhase::RoundEnd);
	TacticalGS->AwardRoundWin(WinningTeam, Reason);
	FinalizeDisconnectedParticipantsAtRoundBoundary();
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=RoundEnded Round=%d Winner=%s Reason=%s ScoreA=%d->%d ScoreB=%d->%d AliveA=%d AliveB=%d"),
		CurrentRound, TacticalDiagnostics::GetTeamName(WinningTeam),
		TacticalDiagnostics::GetRoundEndReasonName(Reason),
		PreviousScoreA, TacticalGS->TeamScores[0], PreviousScoreB, TacticalGS->TeamScores[1],
		CountPlayersAlive(0), CountPlayersAlive(1));
	ValidateTacticalState(TEXT("RoundEnded"));
}

void ATacticalGameMode::EndRoundDraw(ERoundEndReason Reason)
{
	if (!TacticalGS)
	{
		return;
	}

	ClearTeamEliminationRoundEnd();
	AwardDrawEconomy();
	SetPhase(ERoundPhase::RoundEnd);
	TacticalGS->BroadcastRoundDraw(Reason);
	FinalizeDisconnectedParticipantsAtRoundBoundary();
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=RoundEnded Round=%d Winner=DRAW Reason=%s ScoreA=%d ScoreB=%d AliveA=%d AliveB=%d"),
		CurrentRound, TacticalDiagnostics::GetRoundEndReasonName(Reason),
		TacticalGS->TeamScores[0], TacticalGS->TeamScores[1], CountPlayersAlive(0), CountPlayersAlive(1));
	ValidateTacticalState(TEXT("RoundDrawEnded"));
}

void ATacticalGameMode::ScheduleTeamEliminationRoundEnd()
{
	if (!HasAuthority() || !TacticalGS)
	{
		return;
	}

	if (TacticalGS->CurrentPhase != ERoundPhase::Combat)
	{
		return;
	}

	if (bTeamEliminationRoundEndPending)
	{
		return;
	}

	bTeamEliminationRoundEndPending = true;
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=EliminationResolutionScheduled Round=%d Window=%.2f AliveA=%d AliveB=%d"),
		CurrentRound, TacticalEconomy::EliminationResolutionWindow,
		CountPlayersAlive(0), CountPlayersAlive(1));

	GetWorld()->GetTimerManager().SetTimer(
		TeamEliminationRoundEndTimer,
		this,
		&ATacticalGameMode::FinishTeamEliminationRoundEnd,
		TacticalEconomy::EliminationResolutionWindow,
		false);
}

void ATacticalGameMode::FinishTeamEliminationRoundEnd()
{
	if (!HasAuthority() || !TacticalGS)
	{
		ClearTeamEliminationRoundEnd();
		return;
	}

	if (TacticalGS->CurrentPhase != ERoundPhase::Combat)
	{
		ClearTeamEliminationRoundEnd();
		return;
	}

	const bool bTeamADead = IsTeamEliminatedForRound(0);
	const bool bTeamBDead = IsTeamEliminatedForRound(1);
	const TacticalRules::FRoundResolution Resolution = TacticalRules::ResolveElimination(bTeamADead, bTeamBDead);
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=EliminationResolutionResolved Round=%d TeamAEliminated=%d TeamBEliminated=%d Winner=%s Reason=%s"),
		CurrentRound, bTeamADead, bTeamBDead,
		TacticalDiagnostics::GetOutcomeName(Resolution.WinningTeam),
		TacticalDiagnostics::GetRoundEndReasonName(Resolution.Reason));
	if (Resolution.bShouldEndRound && Resolution.WinningTeam <= 1)
	{
		EndRound(Resolution.WinningTeam, Resolution.Reason);
		return;
	}
	if (Resolution.bShouldEndRound)
	{
		EndRoundDraw(Resolution.Reason);
		return;
	}

	ClearTeamEliminationRoundEnd();
}

void ATacticalGameMode::ClearTeamEliminationRoundEnd()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TeamEliminationRoundEndTimer);
	}

	bTeamEliminationRoundEndPending = false;
}

void ATacticalGameMode::TickHuntIntel(float DeltaSeconds)
{
	if (!TacticalGS || TacticalGS->CurrentPhase != ERoundPhase::Combat)
	{
		return;
	}

	const float TimeRemaining = TacticalGS->GetPhaseTimeRemaining();
	if (TimeRemaining > TacticalEconomy::HuntIntelFirstThreshold || TimeRemaining <= 0.0f)
	{
		return;
	}

	const bool bEscalated = TimeRemaining <= TacticalEconomy::HuntIntelEscalatedThreshold;
	if (!bHuntIntelStarted || bHuntIntelEscalated != bEscalated)
	{
		bHuntIntelStarted = true;
		bHuntIntelEscalated = bEscalated;
		HuntPulseCountdown = 0.0f;
		TacticalGS->SetHuntIntelState(true, bEscalated);
	}

	HuntPulseCountdown -= DeltaSeconds;
	if (HuntPulseCountdown <= 0.0f)
	{
		BroadcastHuntIntelPulse(bEscalated);
		HuntPulseCountdown = bEscalated
			? TacticalEconomy::HuntIntelEscalatedInterval
			: TacticalEconomy::HuntIntelFirstInterval;
	}
}

void ATacticalGameMode::ResetHuntIntel()
{
	bHuntIntelStarted = false;
	bHuntIntelEscalated = false;
	HuntPulseCountdown = 0.0f;
	HuntPulseSequence = 0;
	if (TacticalGS)
	{
		TacticalGS->SetHuntIntelState(false, false);
	}
}

void ATacticalGameMode::BroadcastHuntIntelPulse(bool bEscalated)
{
	++HuntPulseSequence;
	if (HuntPulseSequence == 0)
	{
		HuntPulseSequence = 1;
	}

	TArray<FVector> TeamIntel[2];
	for (APlayerState* PS : GameState->PlayerArray)
	{
		ATacticalPlayerState* TargetPS = Cast<ATacticalPlayerState>(PS);
		if (!TargetPS || !TargetPS->bIsActiveParticipant || TargetPS->TeamByte > 1)
		{
			continue;
		}

		ATacticalCharacter* Target = Cast<ATacticalCharacter>(TargetPS->GetPawn());
		if (!Target || Target->IsDead())
		{
			continue;
		}

		const float Radius = bEscalated
			? FMath::FRandRange(100.0f, 250.0f)
			: FMath::FRandRange(300.0f, 600.0f);
		const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
		const FVector Offset(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f);
		const uint8 ViewingTeam = TargetPS->TeamByte == 0 ? 1 : 0;
		TeamIntel[ViewingTeam].Add(Target->GetActorLocation() + Offset);
	}

	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ATacticalPlayerController* Viewer = Cast<ATacticalPlayerController>(It->Get());
		uint8 ViewingTeam = 255;
		if (!Viewer || !Viewer->TryGetHuntIntelViewingTeam(ViewingTeam))
		{
			continue;
		}

		Viewer->ClientReceiveHuntIntelPulse(
			TeamIntel[ViewingTeam], bEscalated, CurrentRound, HuntPulseSequence);
	}

	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=HuntIntelPulse Round=%d Sequence=%u Escalated=%d RedReceives=%d BlueReceives=%d"),
		CurrentRound, HuntPulseSequence, bEscalated,
		TeamIntel[0].Num(), TeamIntel[1].Num());
}

void ATacticalGameMode::SelectTreasureForRound()
{
	if (ATacticalGuardArena* Arena = ATacticalGuardArena::Find(GetWorld()))
	{
		Arena->SetArenaActive(false);
	}
	TArray<AActor*> ChestActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATreasureChest::StaticClass(), ChestActors);
	for (AActor* Actor : ChestActors)
	{
		if (ATreasureChest* Chest = Cast<ATreasureChest>(Actor))
		{
			Chest->DeactivateForRound();
		}
	}

	SelectedTreasureChest = nullptr;
	SelectedTreasureTransform = FTransform::Identity;
	SelectedTreasureSiteIndex = 255;
	TreasureActivationServerTime = 0.0f;
	bTreasureAnnounced = false;
	bTreasureActivated = false;
	TreasureCommittedRound = INDEX_NONE;
	TreasureRewardedParticipantIds.Reset();
	TreasureRewardedFallbackStates.Reset();

	if (!bTreasureEnabled)
	{
		if (TacticalGS)
		{
			TacticalGS->ResetTreasureState(CurrentRound);
		}
		return;
	}

	if (ChestActors.Num() != 1)
	{
		UE_LOG(LogTacticalFlow, Error,
			TEXT("Event=TreasureRoundDisabled Round=%d Reason=InvalidChestCount Actual=%d Expected=1"),
			CurrentRound, ChestActors.Num());
		if (TacticalGS)
		{
			TacticalGS->ResetTreasureState(CurrentRound);
		}
		return;
	}

	ATreasureChest* ChestTemplate = Cast<ATreasureChest>(ChestActors[0]);
	ATacticalGuardArena* Arena = ATacticalGuardArena::Find(GetWorld());
	if (!ChestTemplate || !Arena || !Arena->IsPointInside(ChestTemplate->GetActorLocation())
		|| ChestTemplate->GetActorTransform().ContainsNaN())
	{
		UE_LOG(LogTacticalFlow, Error,
			TEXT("Event=TreasureRoundDisabled Round=%d Reason=ChestOrArenaInvalid Chest=%s Arena=%s"),
			CurrentRound, *GetNameSafe(ChestTemplate), *GetNameSafe(Arena));
		if (TacticalGS)
		{
			TacticalGS->ResetTreasureState(CurrentRound);
		}
		return;
	}
	UNavigationSystemV1* NavigationSystem =
		UNavigationSystemV1::GetCurrent(GetWorld());
	const ANavigationData* NavigationData = NavigationSystem
		? NavigationSystem->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)
		: nullptr;
	if (!ChestTemplate || !NavigationSystem || !NavigationData)
	{
		UE_LOG(LogTacticalFlow, Error,
			TEXT("Event=TreasureRoundDisabled Round=%d Reason=NavigationUnavailable ChestValid=%d NavSystem=%d NavData=%d"),
			CurrentRound, IsValid(ChestTemplate), NavigationSystem != nullptr,
			NavigationData != nullptr);
		if (TacticalGS)
		{
			TacticalGS->ResetTreasureState(CurrentRound);
		}
		return;
	}

	TArray<AActor*> GuardMarkerActors;
	UGameplayStatics::GetAllActorsOfClass(
		GetWorld(), ATacticalGuardSpawnPoint::StaticClass(), GuardMarkerActors);
	bool bSeenGuardPost[TacticalEconomy::TreasureGuardCount] = {};
	if (GuardMarkerActors.Num() != TacticalEconomy::TreasureGuardCount)
	{
		UE_LOG(LogTacticalFlow, Error,
			TEXT("Event=TreasureRoundDisabled Round=%d Reason=GuardMarkerCount Actual=%d Expected=%d"),
			CurrentRound, GuardMarkerActors.Num(), TacticalEconomy::TreasureGuardCount);
		if (TacticalGS)
		{
			TacticalGS->ResetTreasureState(CurrentRound);
		}
		return;
	}
	for (AActor* Actor : GuardMarkerActors)
	{
		const ATacticalGuardSpawnPoint* Marker = Cast<ATacticalGuardSpawnPoint>(Actor);
		if (!Marker || Marker->GuardIndex < 0
			|| Marker->GuardIndex >= TacticalEconomy::TreasureGuardCount
			|| bSeenGuardPost[Marker->GuardIndex])
		{
			UE_LOG(LogTacticalFlow, Error,
				TEXT("Event=TreasureRoundDisabled Round=%d Reason=InvalidGuardIndex Marker=%s"),
				CurrentRound, *GetNameSafe(Marker));
			if (TacticalGS)
			{
				TacticalGS->ResetTreasureState(CurrentRound);
			}
			return;
		}
		bSeenGuardPost[Marker->GuardIndex] = true;
		FNavLocation ProjectedLocation;
		const bool bProjected = NavigationSystem->ProjectPointToNavigation(
			Marker->GetActorLocation(), ProjectedLocation,
			FVector(180.0f, 180.0f, 250.0f), NavigationData);
		if (!bProjected || !Arena->IsPointInsideWithInset(ProjectedLocation.Location, 100.0f)
			|| FVector::DistSquared2D(ProjectedLocation.Location, Marker->GetActorLocation())
				> FMath::Square(100.0f)
			|| FMath::Abs(ProjectedLocation.Location.Z - Marker->GetActorLocation().Z) > 120.0f
			|| FVector::DistSquared2D(ProjectedLocation.Location, ChestTemplate->GetActorLocation())
				< FMath::Square(400.0f))
		{
			UE_LOG(LogTacticalFlow, Error,
				TEXT("Event=TreasureRoundDisabled Round=%d Reason=GuardMarkerOffGroundOrArena Index=%d Location=%s Projected=%d ProjectedLocation=%s XYDistance=%.1f ZDistance=%.1f Inside=%d"),
				CurrentRound, Marker->GuardIndex, *Marker->GetActorLocation().ToCompactString(),
				bProjected, bProjected ? *ProjectedLocation.Location.ToCompactString() : TEXT("None"),
				bProjected ? FVector::Dist2D(ProjectedLocation.Location, Marker->GetActorLocation()) : -1.0f,
				bProjected ? FMath::Abs(ProjectedLocation.Location.Z - Marker->GetActorLocation().Z) : -1.0f,
				bProjected && Arena->IsPointInsideWithInset(ProjectedLocation.Location, 100.0f));
			if (TacticalGS)
			{
				TacticalGS->ResetTreasureState(CurrentRound);
			}
			return;
		}
	}

	SelectedTreasureChest = ChestTemplate;
	SelectedTreasureSiteIndex = 0;
	SelectedTreasureTransform = ChestTemplate->GetActorTransform();

	SelectedTreasureChest->PrepareForRound(CurrentRound, SelectedTreasureSiteIndex);
	PublishTreasureState(ETreasureRoundState::WaitingForAnnouncement);
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=TreasureSiteSelected Round=%d Site=%d PublicBeforeAnnouncement=0 Source=PlacedChest"),
		CurrentRound, SelectedTreasureSiteIndex);
}

void ATacticalGameMode::TickTreasureTimeline()
{
	if (!HasAuthority() || !TacticalGS
		|| TacticalGS->CurrentPhase != ERoundPhase::Combat
		|| !IsValid(SelectedTreasureChest))
	{
		return;
	}

	const float Remaining = TacticalGS->GetPhaseTimeRemaining();
	if (TacticalRules::ShouldAnnounceTreasure(
		bTreasureEnabled, IsValid(SelectedTreasureChest), bTreasureAnnounced,
		TacticalGS->CurrentPhase, Remaining))
	{
		bTreasureAnnounced = true;
		TreasureActivationServerTime = TacticalGS->PhaseEndServerTime
			- TacticalEconomy::TreasureActivationTimeRemaining;
		TreasureActivationServerTime = FMath::Max(
			TacticalGS->GetServerWorldTimeSeconds(), TreasureActivationServerTime);
		// Chest position is authored on the central platform; announcement never
		// relocates it to the old three candidate sites.
		PublishTreasureState(ETreasureRoundState::Announced);
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=TreasureSiteAnnounced Round=%d Site=%d Location=%s Remaining=%.2f ActivationServerTime=%.2f"),
			CurrentRound, SelectedTreasureSiteIndex,
			*SelectedTreasureTransform.GetLocation().ToCompactString(), Remaining,
			TreasureActivationServerTime);
	}

	if (TacticalRules::ShouldActivateTreasure(
		bTreasureEnabled, IsValid(SelectedTreasureChest), bTreasureAnnounced,
		bTreasureActivated, TacticalGS->CurrentPhase, Remaining))
	{
		// Set the fence first because guard spawning and Blueprint callbacks can
		// synchronously re-enter authority code.
		bTreasureActivated = true;
		if (ATacticalGuardArena* Arena = ATacticalGuardArena::Find(GetWorld()))
		{
			Arena->SetArenaActive(true);
		}
		if (!SelectedTreasureChest->ActivateForRound())
		{
			if (ATacticalGuardArena* Arena = ATacticalGuardArena::Find(GetWorld()))
			{
				Arena->SetArenaActive(false);
			}
			SelectedTreasureChest->DeactivateForRound();
			SelectedTreasureChest = nullptr;
			PublishTreasureState(ETreasureRoundState::Disabled);
		}
	}
}

void ATacticalGameMode::DeactivateTreasureForRound()
{
	if (ATacticalGuardArena* Arena = ATacticalGuardArena::Find(GetWorld()))
	{
		Arena->SetArenaActive(false);
	}
	if (IsValid(SelectedTreasureChest))
	{
		SelectedTreasureChest->DeactivateForRound();
	}
	SelectedTreasureChest = nullptr;
	SelectedTreasureSiteIndex = 255;
	TreasureActivationServerTime = 0.0f;
	bTreasureAnnounced = false;
	bTreasureActivated = false;
	if (TacticalGS)
	{
		TacticalGS->ResetTreasureState(CurrentRound);
	}
}

void ATacticalGameMode::NotifyTreasureChestStateChanged(ATreasureChest* Chest)
{
	if (!HasAuthority() || !TacticalGS || Chest != SelectedTreasureChest
		|| !bTreasureAnnounced || Chest->GetPreparedRound() != CurrentRound)
	{
		return;
	}

	if (Chest->IsLooted())
	{
		if (ATacticalGuardArena* Arena = ATacticalGuardArena::Find(GetWorld()))
		{
			Arena->SetArenaActive(false);
		}
		PublishTreasureState(
			ETreasureRoundState::Claimed, 0, nullptr, Chest->GetClaimedTeam());
	}
	else if (const ATacticalPlayerState* ClaimingPlayer = Chest->GetClaimingPlayerState())
	{
		PublishTreasureState(
			ETreasureRoundState::Claiming,
			static_cast<uint8>(Chest->GetLivingGuardCount()), ClaimingPlayer);
	}
	else if (Chest->IsOpenable())
	{
		PublishTreasureState(ETreasureRoundState::Unlocked);
	}
	else if (Chest->IsActiveForRound())
	{
		PublishTreasureState(
			ETreasureRoundState::Guarded,
			static_cast<uint8>(Chest->GetLivingGuardCount()));
	}
}

void ATacticalGameMode::PublishTreasureState(
	ETreasureRoundState State,
	uint8 RemainingGuards,
	const ATacticalPlayerState* ClaimingPlayer,
	uint8 ClaimedTeam)
{
	if (!TacticalGS)
	{
		return;
	}

	FTreasureRoundSnapshot Snapshot;
	Snapshot.State = State;
	Snapshot.RoundNumber = CurrentRound;
	if (State != ETreasureRoundState::Disabled
		&& State != ETreasureRoundState::WaitingForAnnouncement)
	{
		Snapshot.SiteIndex = SelectedTreasureSiteIndex;
		Snapshot.SiteLocation = SelectedTreasureTransform.GetLocation();
		Snapshot.ActivationServerTime = TreasureActivationServerTime;
	}
	Snapshot.RemainingGuards = RemainingGuards;
	if (ClaimingPlayer)
	{
		Snapshot.ClaimingTeam = ClaimingPlayer->TeamByte;
		Snapshot.ClaimingPlayerName = ClaimingPlayer->GetPlayerName();
		Snapshot.ClaimEndServerTime = IsValid(SelectedTreasureChest)
			? SelectedTreasureChest->GetClaimEndServerTime()
			: 0.0f;
	}
	Snapshot.ClaimedTeam = ClaimedTeam;
	TacticalGS->SetTreasureState(Snapshot);
}

int32 ATacticalGameMode::CommitTreasureTeamReward(uint8 Team)
{
	if (!HasAuthority() || Team > 1 || CurrentRound <= 0
		|| TreasureCommittedRound == CurrentRound)
	{
		return 0;
	}

	struct FTreasureRewardCandidate
	{
		FGuid ParticipantId;
		FString IdentityKey;
		TWeakObjectPtr<ATacticalPlayerState> FallbackPlayerState;
	};
	TArray<FTreasureRewardCandidate> Candidates;
	if (RosterSubsystem)
	{
		for (const TPair<FString, FTacticalMatchParticipantRecord>& Pair :
			RosterSubsystem->GetRecords())
		{
			const FTacticalMatchParticipantRecord& Record = Pair.Value;
			if (Record.ParticipantId.IsValid()
				&& TacticalRules::IsTreasureRewardEligible(
					Record.bRosterSlotReserved, Record.TeamByte, Team,
					Record.EnteredRound, CurrentRound,
					Record.ConnectionState == ETacticalParticipantConnectionState::Abandoned))
			{
				Candidates.Add({ Record.ParticipantId, Pair.Key, Record.PlayerState });
			}
		}
	}

	if (GameState)
	{
		for (APlayerState* PlayerState : GameState->PlayerArray)
		{
			ATacticalPlayerState* TPS = Cast<ATacticalPlayerState>(PlayerState);
			if (!TPS || !TPS->bIsActiveParticipant || TPS->TeamByte != Team)
			{
				continue;
			}
			// The stable roster is authoritative whenever it has a record for this
			// PlayerState, including when that record is deliberately ineligible.
			// The fallback is only for the short bootstrap case with no record.
			if (RosterSubsystem && RosterSubsystem->FindByPlayerState(TPS))
			{
				continue;
			}
			const bool bAlreadyRepresented = Candidates.ContainsByPredicate(
				[TPS](const FTreasureRewardCandidate& Candidate)
				{
					return Candidate.FallbackPlayerState.Get() == TPS;
				});
			if (!bAlreadyRepresented)
			{
				Candidates.Add({ FGuid(), FString(), TPS });
			}
		}
	}

	// Commit the round and every candidate identity before the first money
	// delegate. A synchronous disconnect cannot add the same participant again.
	TreasureCommittedRound = CurrentRound;
	TArray<FTreasureRewardCandidate> CommittedCandidates;
	for (const FTreasureRewardCandidate& Candidate : Candidates)
	{
		if (Candidate.ParticipantId.IsValid())
		{
			if (TreasureRewardedParticipantIds.Contains(Candidate.ParticipantId))
			{
				continue;
			}
			TreasureRewardedParticipantIds.Add(Candidate.ParticipantId);
		}
		else
		{
			if (!Candidate.FallbackPlayerState.IsValid()
				|| TreasureRewardedFallbackStates.Contains(Candidate.FallbackPlayerState))
			{
				continue;
			}
			TreasureRewardedFallbackStates.Add(Candidate.FallbackPlayerState);
		}
		CommittedCandidates.Add(Candidate);
	}

	int32 RewardedPlayers = 0;
	for (const FTreasureRewardCandidate& Candidate : CommittedCandidates)
	{
		ATacticalPlayerState* TPS = Candidate.FallbackPlayerState.Get();
		FPlayerDisconnectState* SavedState = nullptr;
		if (!Candidate.IdentityKey.IsEmpty())
		{
			FTacticalMatchParticipantRecord* CurrentRecord = RosterSubsystem
				? RosterSubsystem->FindByIdentity(Candidate.IdentityKey)
				: nullptr;
			if (!CurrentRecord || !TacticalRules::IsSameParticipantGeneration(
				Candidate.ParticipantId, CurrentRecord->ParticipantId))
			{
				continue;
			}
			TPS = CurrentRecord->PlayerState.Get();
			SavedState = DisconnectedPlayers.Find(Candidate.IdentityKey);
			if (SavedState && !TacticalRules::IsSameParticipantGeneration(
				Candidate.ParticipantId, SavedState->ParticipantId))
			{
				SavedState = nullptr;
			}
		}

		const int32 MoneyBefore = IsValid(TPS)
			? TPS->Money
			: SavedState ? SavedState->Money : 0;
		const int32 TargetMoney = TacticalRules::ApplyTreasureMoneyReward(MoneyBefore);
		FString PlayerName = IsValid(TPS)
			? TPS->GetPlayerName()
			: SavedState ? SavedState->PlayerName : TEXT("Unknown");
		if (IsValid(TPS))
		{
			TPS->AddMoney(TacticalEconomy::ChestTeamReward);
		}
		else if (SavedState)
		{
			SavedState->Money = TargetMoney;
		}
		else
		{
			continue;
		}

		// If AddMoney triggered a disconnect, keep its compatibility snapshot on
		// the same committed value without touching the possibly destroyed TPS.
		if (!Candidate.IdentityKey.IsEmpty())
		{
			if (FPlayerDisconnectState* UpdatedState =
				DisconnectedPlayers.Find(Candidate.IdentityKey);
				UpdatedState && TacticalRules::IsSameParticipantGeneration(
					Candidate.ParticipantId, UpdatedState->ParticipantId))
			{
				UpdatedState->Money = TargetMoney;
			}
		}

		++RewardedPlayers;
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=TreasureEconomyAward Round=%d Player=\"%s\" Team=%s Money=%d->%d Participant=%s"),
			CurrentRound, *PlayerName, TacticalDiagnostics::GetTeamName(Team),
			MoneyBefore, TargetMoney,
			Candidate.ParticipantId.IsValid()
				? *Candidate.ParticipantId.ToString() : TEXT("Fallback"));
	}

	return RewardedPlayers;
}

void ATacticalGameMode::EndMatch(uint8 WinningTeam, EMatchEndReason Reason)
{
	SetPhase(ERoundPhase::MatchEnd);
	FinalizeDisconnectedParticipantsAtRoundBoundary();
	TacticalGS->BroadcastMatchEnded(WinningTeam, Reason);
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=MatchEnded Winner=%s Reason=%s CompletedRounds=%d ScoreA=%d ScoreB=%d RestartDelay=%.2f"),
		TacticalDiagnostics::GetMatchOutcomeName(WinningTeam, Reason),
		TacticalDiagnostics::GetMatchEndReasonName(Reason), CurrentRound,
		TacticalGS->TeamScores[0], TacticalGS->TeamScores[1], MatchEndDuration);
	ValidateTacticalState(TEXT("MatchEnded"));

	GetWorld()->GetTimerManager().SetTimer(MatchRestartTimer, [this]()
	{
		if (GetWorld() && !bHostShutdownInProgress)
		{
			if (UTacticalSessionSubsystem* SessionSubsystem =
				GetGameInstance()->GetSubsystem<UTacticalSessionSubsystem>())
			{
				SessionSubsystem->NotifyHostRestartTravelStarted();
			}
			GetWorld()->ServerTravel(TEXT("?Restart"), true);
		}
	}, MatchEndDuration, false);
}

// ---- Victory Conditions ----

void ATacticalGameMode::CheckVictoryConditions()
{
	if (!TacticalGS || !HasAuthority())
	{
		return;
	}

	if (TacticalGS->CurrentPhase != ERoundPhase::Combat)
	{
		return;
	}

	uint8 WinningTeam = 0;
	if (EvaluateTeamEliminated(WinningTeam))
	{
		ScheduleTeamEliminationRoundEnd();
		return;
	}

	if (IsTeamEliminatedForRound(0) && IsTeamEliminatedForRound(1))
	{
		ScheduleTeamEliminationRoundEnd();
		return;
	}

	ClearTeamEliminationRoundEnd();
}

bool ATacticalGameMode::EvaluateTeamEliminated(uint8& OutWinningTeam) const
{
	if (!TacticalGS)
	{
		return false;
	}

	const TacticalRules::FRoundResolution Resolution = TacticalRules::ResolveElimination(
		IsTeamEliminatedForRound(0), IsTeamEliminatedForRound(1));
	if (!Resolution.bShouldEndRound || Resolution.WinningTeam > 1)
	{
		return false;
	}

	OutWinningTeam = Resolution.WinningTeam;
	return true;
}

bool ATacticalGameMode::IsTeamEliminatedForRound(uint8 Team) const
{
	// Qualification is fixed at match start. If an entire qualified team remains
	// offline in a later round, Combat resolves it as eliminated instead of
	// waiting 105 seconds because the current roster happens to be empty.
	return Team <= 1 && TacticalRules::IsQualifiedTeamEliminated(
		bTeamQualifiedForMatch[Team], CountPlayersAlive(Team));
}

// ---- Kills ----

void ATacticalGameMode::OnPlayerKilled(AController* Killer, AController* Victim)
{
	if (!HasAuthority())
	{
		return;
	}

	ATacticalPlayerState* KillerPS = GetTacticalPS(Killer);
	ATacticalPlayerState* VictimPS = GetTacticalPS(Victim);
	FTacticalMatchParticipantRecord* ProxylessVictimRecord = nullptr;
	if (!VictimPS && !Victim && Killer && RosterSubsystem)
	{
		// A retained body normally has an ATacticalDisconnectedController, which
		// gives the death event its original PlayerState. If proxy allocation had
		// failed during disconnect, identify the one just-killed roster body by
		// its authoritative last instigator so the real kill still receives the
		// normal reward and settlement instead of becoming an anonymous death.
		for (TPair<FString, FTacticalMatchParticipantRecord>& Pair :
			RosterSubsystem->GetMutableRecords())
		{
			FTacticalMatchParticipantRecord& Candidate = Pair.Value;
			const bool bRetainedBodyState = Candidate.ConnectionState ==
					ETacticalParticipantConnectionState::DisconnectedGrace
				|| Candidate.ConnectionState ==
					ETacticalParticipantConnectionState::DisconnectedBodyRetained;
			ATacticalCharacter* CandidatePawn = Candidate.RetainedPawn.Get();
			ATacticalPlayerState* CandidateState = Candidate.PlayerState.Get();
			const UHealthComponent* CandidateHealth = IsValid(CandidatePawn)
				? CandidatePawn->GetHealthComponent()
				: nullptr;
			if (bRetainedBodyState && !Candidate.DisconnectedController.IsValid()
				&& IsValid(CandidatePawn) && CandidatePawn->IsDead()
				&& IsValid(CandidateState) && CandidateState->bIsActiveParticipant
				&& CandidateHealth
				&& CandidateHealth->LastDamageInstigator.Get() == Killer)
			{
				ProxylessVictimRecord = &Candidate;
				VictimPS = CandidateState;
				break;
			}
		}
	}
	const bool bValidEnemyKill = KillerPS && VictimPS
		&& Killer != Victim
		&& KillerPS->bIsActiveParticipant
		&& VictimPS->bIsActiveParticipant
		&& KillerPS->TeamByte <= 1
		&& VictimPS->TeamByte <= 1
		&& KillerPS->TeamByte != VictimPS->TeamByte;
	const int32 KillerMoneyBefore = KillerPS ? KillerPS->Money : 0;
	if (bValidEnemyKill)
	{
		KillerPS->AddMoney(TacticalEconomy::KillRewardPlayer);
	}
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=PlayerKilled Round=%d Killer=\"%s\" KillerTeam=%s Victim=\"%s\" VictimTeam=%s ValidEnemyKill=%d Reward=%d KillerMoney=%d->%d AliveA=%d AliveB=%d"),
		CurrentRound,
		KillerPS ? *KillerPS->GetPlayerName() : TEXT("None"),
		TacticalDiagnostics::GetTeamName(KillerPS ? KillerPS->TeamByte : 255),
		VictimPS ? *VictimPS->GetPlayerName() : TEXT("None"),
		TacticalDiagnostics::GetTeamName(VictimPS ? VictimPS->TeamByte : 255),
		bValidEnemyKill, bValidEnemyKill ? TacticalEconomy::KillRewardPlayer : 0,
		KillerMoneyBefore, KillerPS ? KillerPS->Money : 0,
		CountPlayersAlive(0), CountPlayersAlive(1));

	if (bHuntIntelStarted)
	{
		// A pulse contains deliberately anonymous positions, so there is no target
		// identifier to remove selectively. Clear the small marker set for everyone
		// and fence every pulse authored before this death; the next scheduled pulse
		// immediately excludes the dead target.
		for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (ATacticalPlayerController* Viewer =
				Cast<ATacticalPlayerController>(It->Get()))
			{
				Viewer->ClientClearHuntIntelMarkers(CurrentRound, HuntPulseSequence);
			}
		}
	}

	// A retained Pawn keeps a proxy controller specifically so this path can
	// still resolve its original PlayerState and grant the enemy kill normally.
	if (ProxylessVictimRecord)
	{
		const FGuid VictimParticipantId = ProxylessVictimRecord->ParticipantId;
		const FString VictimIdentity = ProxylessVictimRecord->IdentityKey;
		TWeakObjectPtr<ATacticalPlayerState> VictimStateWeak = VictimPS;
		ProxylessVictimRecord->ConnectionState =
			ETacticalParticipantConnectionState::DisconnectedWaiting;
		ProxylessVictimRecord->ReconnectDeadlineServerTime = 0.0f;
		VictimPS->bIsActiveParticipant = false;
		VictimPS->SetConnectionStatus(ETacticalPublicConnectionStatus::Disconnected);
		if (ProxylessVictimRecord->EnteredRound == CurrentRound
			&& ProxylessVictimRecord->LastSettledRound < CurrentRound)
		{
			VictimPS->PendingSettlementRound = CurrentRound;
		}
		VictimPS->ClearEquipmentOnDeath();
		if (ATacticalPlayerState* CurrentVictimState = VictimStateWeak.Get())
		{
			CurrentVictimState->ForceNetUpdate();
		}
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=ProxylessRetainedBodyKilled Participant=%s Identity=Redacted Round=%d"),
			*VictimParticipantId.ToString(), CurrentRound);
		// Do not retain a TMap value pointer across the equipment delegate.
		ProxylessVictimRecord = RosterSubsystem->FindByIdentity(VictimIdentity);
		if (ProxylessVictimRecord
			&& !TacticalRules::IsSameParticipantGeneration(
				VictimParticipantId, ProxylessVictimRecord->ParticipantId))
		{
			ProxylessVictimRecord = nullptr;
		}
	}
	else
	{
		MarkDisconnectedParticipantDead(Victim);
	}

	CheckVictoryConditions();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &ATacticalGameMode::CheckVictoryConditions);
	}
}

void ATacticalGameMode::OnNPCKilled(AController* Killer, AShooterNPC* Victim)
{
	if (!HasAuthority())
	{
		return;
	}

	if (TacticalEconomy::KillRewardAI > 0 && Killer)
	{
		if (ATacticalPlayerState* KillerPS = GetTacticalPS(Killer))
		{
			KillerPS->AddMoney(TacticalEconomy::KillRewardAI);
		}
	}

	// Note: GuardedChest->NotifyGuardKilled() is called by AShooterNPC::OnDeath() —
	// we don't call it again here to avoid double-counting.
}

// ---- Economy ----

void ATacticalGameMode::AwardRoundEconomy(uint8 WinningTeam)
{
	TArray<TWeakObjectPtr<ATacticalPlayerState>> ConnectedCandidates;
	if (GameState)
	{
		for (APlayerState* PS : GameState->PlayerArray)
		{
			ATacticalPlayerState* TPS = Cast<ATacticalPlayerState>(PS);
			if (TPS && TPS->TeamByte <= 1
				&& (TPS->bIsActiveParticipant
					|| TPS->PendingSettlementRound == CurrentRound))
			{
				ConnectedCandidates.AddUnique(TPS);
			}
		}
	}

	for (const TWeakObjectPtr<ATacticalPlayerState>& CandidateWeak : ConnectedCandidates)
	{
		ATacticalPlayerState* TPS = CandidateWeak.Get();
		if (!IsValid(TPS) || TPS->TeamByte > 1
			|| (!TPS->bIsActiveParticipant
				&& TPS->PendingSettlementRound != CurrentRound))
		{
			continue;
		}
		FTacticalMatchParticipantRecord* Record = RosterSubsystem
			? RosterSubsystem->FindByPlayerState(TPS)
			: nullptr;
		if (Record && Record->LastSettledRound >= CurrentRound)
		{
			TPS->PendingSettlementRound = 0;
			continue;
		}

		const FString PlayerName = TPS->GetPlayerName();
		const uint8 Team = TPS->TeamByte;
		const int32 MoneyBefore = TPS->Money;
		const int32 LossStreakBefore = TPS->LossStreak;
		const bool bWonRound = Team == WinningTeam;
		// Commit the idempotency markers before the Blueprint-assignable money
		// callback can synchronously disconnect or destroy this PlayerState.
		TPS->PendingSettlementRound = 0;
		if (Record)
		{
			Record->LastSettledRound = CurrentRound;
		}
		if (bWonRound)
		{
			TPS->AwardRoundWin();
		}
		else
		{
			TPS->AwardRoundLoss();
		}
		TPS = CandidateWeak.Get();
		const int32 MoneyAfter = IsValid(TPS) ? TPS->Money : MoneyBefore;
		const int32 LossStreakAfter = IsValid(TPS)
			? TPS->LossStreak
			: LossStreakBefore;
		const EWeaponTier PrimaryTier = IsValid(TPS)
			? TPS->SavedPrimaryTier
			: EWeaponTier::DefaultPistol;
		const EWeaponTier PistolTier = IsValid(TPS)
			? TPS->SavedPistolTier
			: EWeaponTier::DefaultPistol;
		const EArmorType ArmorType = IsValid(TPS)
			? TPS->ArmorType
			: EArmorType::None;
		const float ArmorDurability = IsValid(TPS) ? TPS->ArmorDurability : 0.0f;
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=EconomySettlement Round=%d Presence=Connected Player=\"%s\" Team=%s Outcome=%s Money=%d->%d LossStreak=%d->%d Primary=%s Pistol=%s Armor=%s Durability=%.1f"),
			CurrentRound, *PlayerName, TacticalDiagnostics::GetTeamName(Team),
			bWonRound ? TEXT("Win") : TEXT("Loss"), MoneyBefore, MoneyAfter,
			LossStreakBefore, LossStreakAfter,
			TacticalDiagnostics::GetWeaponTierName(PrimaryTier),
			TacticalDiagnostics::GetWeaponTierName(PistolTier),
			TacticalDiagnostics::GetArmorTypeName(ArmorType), ArmorDurability);
	}

	for (TPair<FString, FPlayerDisconnectState>& Pair : DisconnectedPlayers)
	{
		FPlayerDisconnectState& State = Pair.Value;
		if (State.TeamByte > 1 || State.PendingSettlementRound != CurrentRound)
		{
			continue;
		}
		FTacticalMatchParticipantRecord* Record = RosterSubsystem
			? RosterSubsystem->FindByIdentity(Pair.Key)
			: nullptr;
		if (Record && TacticalRules::IsSameParticipantGeneration(
				State.ParticipantId, Record->ParticipantId)
			&& Record->LastSettledRound >= CurrentRound)
		{
			State.PendingSettlementRound = 0;
			continue;
		}

		const int32 MoneyBefore = State.Money;
		const int32 LossStreakBefore = State.LossStreak;
		const bool bWonRound = State.TeamByte == WinningTeam;
		TacticalRules::ApplyRoundEconomy(
			bWonRound ? TacticalRules::ERoundEconomyOutcome::Win : TacticalRules::ERoundEconomyOutcome::Loss,
			State.Money, State.LossStreak, State.RoundsWon);
		State.PendingSettlementRound = 0;
		if (Record && TacticalRules::IsSameParticipantGeneration(
			State.ParticipantId, Record->ParticipantId))
		{
			Record->LastSettledRound = CurrentRound;
		}
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=EconomySettlement Round=%d Presence=Disconnected Player=\"%s\" Team=%s Outcome=%s Money=%d->%d LossStreak=%d->%d Primary=%s Pistol=%s Armor=%s Durability=%.1f"),
			CurrentRound, State.PlayerName.IsEmpty() ? TEXT("Unknown") : *State.PlayerName,
			TacticalDiagnostics::GetTeamName(State.TeamByte), bWonRound ? TEXT("Win") : TEXT("Loss"),
			MoneyBefore, State.Money, LossStreakBefore, State.LossStreak,
			TacticalDiagnostics::GetWeaponTierName(State.PrimaryTier),
			TacticalDiagnostics::GetWeaponTierName(State.PistolTier),
			TacticalDiagnostics::GetArmorTypeName(State.ArmorType), State.ArmorDurability);
	}
}

void ATacticalGameMode::AwardDrawEconomy()
{
	TArray<TWeakObjectPtr<ATacticalPlayerState>> ConnectedCandidates;
	if (GameState)
	{
		for (APlayerState* PS : GameState->PlayerArray)
		{
			ATacticalPlayerState* TPS = Cast<ATacticalPlayerState>(PS);
			if (TPS && TPS->TeamByte <= 1
				&& (TPS->bIsActiveParticipant || TPS->PendingSettlementRound == CurrentRound))
			{
				ConnectedCandidates.AddUnique(TPS);
			}
		}
	}

	for (const TWeakObjectPtr<ATacticalPlayerState>& CandidateWeak : ConnectedCandidates)
	{
		ATacticalPlayerState* TPS = CandidateWeak.Get();
		if (!IsValid(TPS) || TPS->TeamByte > 1
			|| (!TPS->bIsActiveParticipant
				&& TPS->PendingSettlementRound != CurrentRound))
		{
			continue;
		}
		FTacticalMatchParticipantRecord* Record = RosterSubsystem
			? RosterSubsystem->FindByPlayerState(TPS)
			: nullptr;
		if (Record && Record->LastSettledRound >= CurrentRound)
		{
			TPS->PendingSettlementRound = 0;
			continue;
		}

		const FString PlayerName = TPS->GetPlayerName();
		const uint8 Team = TPS->TeamByte;
		const int32 MoneyBefore = TPS->Money;
		const int32 LossStreakBefore = TPS->LossStreak;
		TPS->PendingSettlementRound = 0;
		if (Record)
		{
			Record->LastSettledRound = CurrentRound;
		}
		TPS->AwardRoundDraw();
		TPS = CandidateWeak.Get();
		const int32 MoneyAfter = IsValid(TPS) ? TPS->Money : MoneyBefore;
		const int32 LossStreakAfter = IsValid(TPS)
			? TPS->LossStreak
			: LossStreakBefore;
		const EWeaponTier PrimaryTier = IsValid(TPS)
			? TPS->SavedPrimaryTier
			: EWeaponTier::DefaultPistol;
		const EWeaponTier PistolTier = IsValid(TPS)
			? TPS->SavedPistolTier
			: EWeaponTier::DefaultPistol;
		const EArmorType ArmorType = IsValid(TPS)
			? TPS->ArmorType
			: EArmorType::None;
		const float ArmorDurability = IsValid(TPS) ? TPS->ArmorDurability : 0.0f;
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=EconomySettlement Round=%d Presence=Connected Player=\"%s\" Team=%s Outcome=Draw Money=%d->%d LossStreak=%d->%d Primary=%s Pistol=%s Armor=%s Durability=%.1f"),
			CurrentRound, *PlayerName, TacticalDiagnostics::GetTeamName(Team),
			MoneyBefore, MoneyAfter, LossStreakBefore, LossStreakAfter,
			TacticalDiagnostics::GetWeaponTierName(PrimaryTier),
			TacticalDiagnostics::GetWeaponTierName(PistolTier),
			TacticalDiagnostics::GetArmorTypeName(ArmorType), ArmorDurability);
	}

	for (TPair<FString, FPlayerDisconnectState>& Pair : DisconnectedPlayers)
	{
		FPlayerDisconnectState& State = Pair.Value;
		if (State.TeamByte > 1 || State.PendingSettlementRound != CurrentRound)
		{
			continue;
		}

		FTacticalMatchParticipantRecord* Record = RosterSubsystem
			? RosterSubsystem->FindByIdentity(Pair.Key)
			: nullptr;
		if (Record && TacticalRules::IsSameParticipantGeneration(
				State.ParticipantId, Record->ParticipantId)
			&& Record->LastSettledRound >= CurrentRound)
		{
			State.PendingSettlementRound = 0;
			continue;
		}

		const int32 MoneyBefore = State.Money;
		const int32 LossStreakBefore = State.LossStreak;
		TacticalRules::ApplyRoundEconomy(TacticalRules::ERoundEconomyOutcome::Draw,
			State.Money, State.LossStreak, State.RoundsWon);
		State.PendingSettlementRound = 0;
		if (Record && TacticalRules::IsSameParticipantGeneration(
			State.ParticipantId, Record->ParticipantId))
		{
			Record->LastSettledRound = CurrentRound;
		}
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=EconomySettlement Round=%d Presence=Disconnected Player=\"%s\" Team=%s Outcome=Draw Money=%d->%d LossStreak=%d->%d Primary=%s Pistol=%s Armor=%s Durability=%.1f"),
			CurrentRound, State.PlayerName.IsEmpty() ? TEXT("Unknown") : *State.PlayerName,
			TacticalDiagnostics::GetTeamName(State.TeamByte), MoneyBefore, State.Money,
			LossStreakBefore, State.LossStreak,
			TacticalDiagnostics::GetWeaponTierName(State.PrimaryTier),
			TacticalDiagnostics::GetWeaponTierName(State.PistolTier),
			TacticalDiagnostics::GetArmorTypeName(State.ArmorType), State.ArmorDurability);
	}
}

void ATacticalGameMode::InitializeMatchEconomy()
{
	if (!GameState)
	{
		return;
	}

	for (APlayerState* PS : GameState->PlayerArray)
	{
		if (ATacticalPlayerState* TPS = Cast<ATacticalPlayerState>(PS))
		{
			if (TPS->TeamByte <= 1)
			{
				TPS->InitializeForMatch();
				UE_LOG(LogTacticalFlow, Display,
					TEXT("Event=EconomyInitialized Player=\"%s\" Team=%s Money=%d"),
					*TPS->GetPlayerName(), TacticalDiagnostics::GetTeamName(TPS->TeamByte), TPS->Money);
			}
		}
	}
}

// ---- Teams ----

void ATacticalGameMode::RemoveFromTeam(AController* Exiting)
{
	if (APlayerController* PC = Cast<APlayerController>(Exiting))
	{
		TeamAPlayers.Remove(PC);
		TeamBPlayers.Remove(PC);
	}
}

ATacticalPlayerState* ATacticalGameMode::GetTacticalPS(AController* Controller) const
{
	if (!Controller)
	{
		return nullptr;
	}
	return Controller->GetPlayerState<ATacticalPlayerState>();
}

// ---- Player State ----

void ATacticalGameMode::FreezeAllPlayers(bool bFreeze)
{
	TSet<ATacticalCharacter*> UpdatedCharacters;
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (PC)
		{
			if (ATacticalCharacter* Char = Cast<ATacticalCharacter>(PC->GetPawn()))
			{
				Char->SetFrozen(bFreeze);
				UpdatedCharacters.Add(Char);
			}
		}
	}

	// Disconnected proxy controllers are not part of the PlayerController
	// iterator. Their retained Pawns must still obey Buy/Combat/RoundEnd gates.
	if (RosterSubsystem)
	{
		for (const TPair<FString, FTacticalMatchParticipantRecord>& Pair :
			RosterSubsystem->GetRecords())
		{
			ATacticalCharacter* RetainedPawn = Pair.Value.RetainedPawn.Get();
			if (IsValid(RetainedPawn) && !UpdatedCharacters.Contains(RetainedPawn))
			{
				RetainedPawn->SetFrozen(bFreeze);
			}
		}
	}
}

void ATacticalGameMode::RespawnAllPlayers()
{
	if (bUsePlacedSpawnPoints && !bPlacedSpawnLayoutValid)
	{
		UE_LOG(LogTacticalFlow, Error,
			TEXT("Event=RoundSpawnBlocked Reason=InvalidPlacedSpawnLayout"));
		return;
	}

	TArray<AActor*> StartActors;
	if (!bUsePlacedSpawnPoints)
	{
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), StartActors);
	}
	TArray<APlayerStart*> AllStarts;
	TArray<APlayerStart*> TeamStarts[2];

	auto HasAnyTag = [](const APlayerStart* Start, const TArray<FName>& AcceptedTags)
	{
		if (!Start)
		{
			return false;
		}
		for (const FName Tag : AcceptedTags)
		{
			if (Start->PlayerStartTag.IsEqual(Tag, ENameCase::IgnoreCase) || Start->ActorHasTag(Tag))
			{
				return true;
			}
		}
		return false;
	};

	const TArray<FName> TeamATags = { TEXT("RED"), TEXT("TeamA"), TEXT("Team0") };
	const TArray<FName> TeamBTags = { TEXT("BLUE"), TEXT("TeamB"), TEXT("Team1") };
	for (AActor* Actor : StartActors)
	{
		if (APlayerStart* Start = Cast<APlayerStart>(Actor))
		{
			AllStarts.Add(Start);
			if (HasAnyTag(Start, TeamATags))
			{
				TeamStarts[0].Add(Start);
			}
			else if (HasAnyTag(Start, TeamBTags))
			{
				TeamStarts[1].Add(Start);
			}
		}
	}

	auto RespawnPlayer = [&](APlayerController* PC, uint8 Team, int32 TeamIndex, int32 TeamCount)
	{
		if (!PC)
		{
			return;
		}
		ATacticalPlayerState* TPS = PC->GetPlayerState<ATacticalPlayerState>();
		if (!TPS || !TPS->bIsActiveParticipant)
		{
			return;
		}

		if (PC->GetPawn())
		{
			PC->GetPawn()->Destroy();
		}

		APlayerStart* SelectedStart = nullptr;
		ATacticalSpawnPoint* SelectedMarker = nullptr;
		if (bUsePlacedSpawnPoints)
		{
			// With one teammate use the middle; with two use both flanks.
			const int32 Slot = TeamCount == 1 ? 2
				: TeamCount == 2 ? (TeamIndex == 0 ? 1 : 3)
				: TeamIndex + 1;
			SelectedMarker = FindPlacedSpawnPoint(
				Team == 0 ? ETacticalSpawnGroup::Red : ETacticalSpawnGroup::Blue, Slot);
			if (!SelectedMarker)
			{
				UE_LOG(LogTacticalFlow, Error,
					TEXT("Event=PlayerRespawnFailed Controller=\"%s\" Team=%s Reason=MissingPlacedMarker Slot=%d"),
					*GetNameSafe(PC), TacticalDiagnostics::GetTeamName(Team), Slot);
				return;
			}
		}
		else if (Team <= 1 && TeamStarts[Team].Num() > 0)
		{
			SelectedStart = TeamStarts[Team][TeamIndex % TeamStarts[Team].Num()];
		}
		else if (AllStarts.Num() > 0)
		{
			SelectedStart = AllStarts[TeamIndex % AllStarts.Num()];
		}

		if (SelectedMarker)
		{
			RestartPlayerAtTransform(PC, SelectedMarker->GetActorTransform());
		}
		else if (SelectedStart)
		{
			RestartPlayerAtPlayerStart(PC, SelectedStart);
		}
		else
		{
			RestartPlayer(PC);
		}

		if (APawn* NewPawn = PC->GetPawn())
		{
			if (ATacticalCharacter* Char = Cast<ATacticalCharacter>(NewPawn))
			{
				Char->SetTeam(Team);
				if (UHealthComponent* HealthComp = Char->GetHealthComponent()) { HealthComp->ResetToFull(); }

				if (UArmorComponent* Armor = Char->GetArmorComponent())
				{
					Armor->RestoreArmor(TPS->ArmorType, TPS->ArmorDurability);
				}

				if (ATacticalPlayerController* TPC = Cast<ATacticalPlayerController>(PC))
				{
					if (TSubclassOf<AShooterWeapon> PrimaryClass = TPC->GetWeaponClassForTier(TPS->SavedPrimaryTier))
					{
						Char->EquipPurchasedWeapon(PrimaryClass, TPS->SavedPrimaryTier);
					}
					if (TSubclassOf<AShooterWeapon> PistolClass = TPC->GetWeaponClassForTier(TPS->SavedPistolTier))
					{
						Char->EquipPurchasedWeapon(PistolClass, TPS->SavedPistolTier);
					}
				}

				UE_LOG(LogTacticalFlow, Display,
					TEXT("Event=PlayerRespawned Round=%d Player=\"%s\" Team=%s Money=%d Primary=%s Pistol=%s Armor=%s Durability=%.1f Start=\"%s\""),
					CurrentRound, *TPS->GetPlayerName(), TacticalDiagnostics::GetTeamName(Team), TPS->Money,
					TacticalDiagnostics::GetWeaponTierName(TPS->SavedPrimaryTier),
					TacticalDiagnostics::GetWeaponTierName(TPS->SavedPistolTier),
					TacticalDiagnostics::GetArmorTypeName(TPS->ArmorType), TPS->ArmorDurability,
					*GetNameSafe(SelectedMarker ? static_cast<AActor*>(SelectedMarker) : SelectedStart));
			}
		}
		else
		{
			UE_LOG(LogTacticalFlow, Error, TEXT("Event=PlayerRespawnFailed Controller=\"%s\" Team=%s"),
				*GetNameSafe(PC), TacticalDiagnostics::GetTeamName(Team));
		}
	};

	for (uint8 Team = 0; Team < 2; ++Team)
	{
		const TArray<TObjectPtr<APlayerController>>& Roster = Team == 0
			? TeamAPlayers : TeamBPlayers;
		TArray<APlayerController*> Eligible;
		for (APlayerController* PC : Roster)
		{
			const ATacticalPlayerState* TPS = PC
				? PC->GetPlayerState<ATacticalPlayerState>() : nullptr;
			if (IsValid(PC) && TPS && TPS->bIsActiveParticipant)
			{
				Eligible.Add(PC);
			}
		}
		if (bUsePlacedSpawnPoints && Eligible.Num() > TacticalEconomy::MaxPlayersPerTeam)
		{
			UE_LOG(LogTacticalFlow, Error,
				TEXT("Event=RoundSpawnBlocked Team=%s Reason=TeamExceedsThree Count=%d"),
				TacticalDiagnostics::GetTeamName(Team), Eligible.Num());
			continue;
		}
		for (int32 Index = 0; Index < Eligible.Num(); ++Index)
		{
			RespawnPlayer(Eligible[Index], Team, Index, Eligible.Num());
		}
	}
}

void ATacticalGameMode::InitializeParticipantPawn(
	APlayerController* PlayerController,
	uint8 Team,
	bool bResetVitals)
{
	ATacticalPlayerState* TPS = PlayerController
		? PlayerController->GetPlayerState<ATacticalPlayerState>()
		: nullptr;
	ATacticalCharacter* Character = PlayerController
		? Cast<ATacticalCharacter>(PlayerController->GetPawn())
		: nullptr;
	if (!TPS || !Character)
	{
		return;
	}

	Character->SetTeam(Team);
	if (bResetVitals)
	{
		if (UHealthComponent* Health = Character->GetHealthComponent())
		{
			Health->ResetToFull();
		}
		if (UArmorComponent* Armor = Character->GetArmorComponent())
		{
			Armor->RestoreArmor(TPS->ArmorType, TPS->ArmorDurability);
		}
		if (ATacticalPlayerController* TacticalController =
			Cast<ATacticalPlayerController>(PlayerController))
		{
			if (TSubclassOf<AShooterWeapon> PrimaryClass =
				TacticalController->GetWeaponClassForTier(TPS->SavedPrimaryTier))
			{
				Character->EquipPurchasedWeapon(PrimaryClass, TPS->SavedPrimaryTier);
			}
			if (TSubclassOf<AShooterWeapon> PistolClass =
				TacticalController->GetWeaponClassForTier(TPS->SavedPistolTier))
			{
				Character->EquipPurchasedWeapon(PistolClass, TPS->SavedPistolTier);
			}
		}
	}

	const ERoundPhase Phase = TacticalGS
		? TacticalGS->CurrentPhase
		: ERoundPhase::PreRound;
	Character->SetFrozen(Phase == ERoundPhase::Buy
		|| Phase == ERoundPhase::RoundEnd
		|| Phase == ERoundPhase::MatchEnd);
	Character->ForceNetUpdate();
}

// ---- Helpers ----

int32 ATacticalGameMode::CountPlayersAlive(uint8 Team) const
{
	if (Team > 1)
	{
		return 0;
	}

	int32 Count = 0;
	TSet<const APawn*> CountedPawns;
	if (RosterSubsystem)
	{
		for (const TPair<FString, FTacticalMatchParticipantRecord>& Pair :
			RosterSubsystem->GetRecords())
		{
			const FTacticalMatchParticipantRecord& Record = Pair.Value;
			if (!Record.bRosterSlotReserved || Record.TeamByte != Team
				|| Record.ConnectionState ==
					ETacticalParticipantConnectionState::DisconnectedWaiting
				|| Record.ConnectionState ==
					ETacticalParticipantConnectionState::ReconnectedSpectating
				|| Record.ConnectionState ==
					ETacticalParticipantConnectionState::Abandoned)
			{
				continue;
			}

			const ATacticalPlayerState* TPS = Record.PlayerState.Get();
			const bool bRetainedBodyState = Record.ConnectionState ==
					ETacticalParticipantConnectionState::DisconnectedGrace
				|| Record.ConnectionState ==
					ETacticalParticipantConnectionState::DisconnectedBodyRetained;
			if (!bRetainedBodyState
				&& (!IsValid(TPS) || !TPS->bIsActiveParticipant))
			{
				continue;
			}

			const APawn* Pawn = Record.PlayerController.IsValid()
				? Record.PlayerController->GetPawn()
				: Record.RetainedPawn.Get();
			const ATacticalCharacter* Character =
				Cast<ATacticalCharacter>(Pawn);
			if (IsValid(Character) && !Character->IsDead()
				&& !CountedPawns.Contains(Character))
			{
				CountedPawns.Add(Character);
				++Count;
			}
		}
	}

	// Include any legitimate active PlayerState not yet present in the private
	// registry (for example, a narrow fresh-login/bootstrap window), while the
	// set prevents a roster-owned pawn from being counted twice.
	if (TacticalGS)
	{
		for (APlayerState* PlayerState : TacticalGS->PlayerArray)
		{
			const ATacticalPlayerState* TPS =
				Cast<ATacticalPlayerState>(PlayerState);
			if (!TPS || !TPS->bIsActiveParticipant || TPS->TeamByte != Team)
			{
				continue;
			}
			const ATacticalCharacter* Character =
				Cast<ATacticalCharacter>(TPS->GetPawn());
			if (IsValid(Character) && !Character->IsDead()
				&& !CountedPawns.Contains(Character))
			{
				CountedPawns.Add(Character);
				++Count;
			}
		}
	}
	return Count;
}

int32 ATacticalGameMode::CountReservedTeamSlots(uint8 Team) const
{
	if (Team > 1)
	{
		return 0;
	}

	int32 Count = RosterSubsystem
		? RosterSubsystem->CountReservedTeamSlots(Team)
		: 0;
	const TArray<TObjectPtr<APlayerController>>& ConnectedRoster =
		Team == 0 ? TeamAPlayers : TeamBPlayers;
	for (const APlayerController* PlayerController : ConnectedRoster)
	{
		const FTacticalMatchParticipantRecord* Record = RosterSubsystem
			? RosterSubsystem->FindByIdentity(
				ResolveReconnectIdentity(PlayerController))
			: nullptr;
		if (!Record || !Record->bRosterSlotReserved || Record->TeamByte != Team)
		{
			++Count;
		}
	}
	return Count;
}

int32 ATacticalGameMode::CountLobbyCandidates() const
{
	if (!GameState)
	{
		return 0;
	}

	int32 LobbyPlayers = 0;
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		const ATacticalPlayerState* TPS = Cast<ATacticalPlayerState>(PlayerState);
		LobbyPlayers += TPS && TPS->bIsActiveParticipant ? 1 : 0;
	}
	return LobbyPlayers;
}

bool ATacticalGameMode::AreAllLobbyPlayersSelectedAndReady() const
{
	if (!GameState)
	{
		return false;
	}

	int32 LobbyPlayers = 0;
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		const ATacticalPlayerState* TPS = Cast<ATacticalPlayerState>(PlayerState);
		if (!TPS || !TPS->bIsActiveParticipant)
		{
			continue;
		}

		++LobbyPlayers;
		if (TPS->TeamByte > 1 || !TPS->bIsReady)
		{
			return false;
		}
	}

	const int32 RequiredPlayers = FMath::Clamp(MinPlayersToStart, 2, TacticalEconomy::MaxPlayersPerTeam * 2);
	return LobbyPlayers >= RequiredPlayers;
}

void ATacticalGameMode::LogFlowSnapshot(const TCHAR* Event) const
{
	if (!TacticalGS || !GameState)
	{
		UE_LOG(LogTacticalFlow, Warning, TEXT("Event=%s SnapshotUnavailable GameStateValid=%d"),
			Event, TacticalGS != nullptr);
		return;
	}

	int32 ActiveTeamA = 0;
	int32 ActiveTeamB = 0;
	int32 LobbyPlayers = 0;
	int32 AssignedPlayers = 0;
	int32 ReadyPlayers = 0;
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		const ATacticalPlayerState* TPS = Cast<ATacticalPlayerState>(PlayerState);
		if (!TPS || !TPS->bIsActiveParticipant)
		{
			continue;
		}

		++LobbyPlayers;
		if (TPS->TeamByte > 1)
		{
			continue;
		}
		++AssignedPlayers;
		ReadyPlayers += TPS->bIsReady ? 1 : 0;
		ActiveTeamA += TPS->TeamByte == 0 ? 1 : 0;
		ActiveTeamB += TPS->TeamByte == 1 ? 1 : 0;
	}

	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=%s Phase=%s Round=%d/%d Remaining=%.2f ScoreA=%d ScoreB=%d RosterA=%d RosterB=%d ActiveA=%d ActiveB=%d AliveA=%d AliveB=%d Candidates=%d Selected=%d Ready=%d Connected=%d MatchStarted=%d EliminationPending=%d"),
		Event, TacticalDiagnostics::GetPhaseName(TacticalGS->CurrentPhase), CurrentRound,
		TacticalGS->MaxRounds, TacticalGS->GetPhaseTimeRemaining(),
		TacticalGS->TeamScores[0], TacticalGS->TeamScores[1], TeamAPlayers.Num(), TeamBPlayers.Num(),
		ActiveTeamA, ActiveTeamB, CountPlayersAlive(0), CountPlayersAlive(1),
		LobbyPlayers, AssignedPlayers, ReadyPlayers, GameState->PlayerArray.Num(), bMatchStarted,
		bTeamEliminationRoundEndPending);
}

void ATacticalGameMode::ValidateTacticalState(const TCHAR* Context) const
{
#if DO_CHECK
	if (!HasAuthority() || !TacticalGS || !GameState)
	{
		return;
	}

	ensureMsgf(TacticalGS->CurrentPhase != ERoundPhase::Overtime,
		TEXT("[%s] Removed Overtime phase became active."), Context);
	ensureMsgf(TeamAPlayers.Num() <= TacticalEconomy::MaxPlayersPerTeam,
		TEXT("[%s] RED roster exceeds the three-player limit: %d"), Context, TeamAPlayers.Num());
	ensureMsgf(TeamBPlayers.Num() <= TacticalEconomy::MaxPlayersPerTeam,
		TEXT("[%s] BLUE roster exceeds the three-player limit: %d"), Context, TeamBPlayers.Num());
	ensureMsgf(CountReservedTeamSlots(0) <= TacticalEconomy::MaxPlayersPerTeam,
		TEXT("[%s] RED stable reservations exceed the three-player limit: %d"),
		Context, CountReservedTeamSlots(0));
	ensureMsgf(CountReservedTeamSlots(1) <= TacticalEconomy::MaxPlayersPerTeam,
		TEXT("[%s] BLUE stable reservations exceed the three-player limit: %d"),
		Context, CountReservedTeamSlots(1));

	if (RosterSubsystem)
	{
		TSet<const ATacticalPlayerState*> UniqueParticipantStates;
		TSet<const ATacticalCharacter*> UniqueRetainedPawns;
		for (const TPair<FString, FTacticalMatchParticipantRecord>& Pair :
			RosterSubsystem->GetRecords())
		{
			const FTacticalMatchParticipantRecord& Record = Pair.Value;
			ensureMsgf(!Pair.Key.IsEmpty() && Pair.Key == Record.IdentityKey,
				TEXT("[%s] Stable roster identity key is empty or mismatched."), Context);
			ensureMsgf(Record.ParticipantId.IsValid(),
				TEXT("[%s] Stable roster record has no participant id."), Context);
			ensureMsgf(!Record.bRosterSlotReserved || Record.TeamByte <= 1,
				TEXT("[%s] Reserved participant %s has invalid team %d."),
				Context, *Record.ParticipantId.ToString(), Record.TeamByte);

			if (const ATacticalPlayerState* RecordState = Record.PlayerState.Get())
			{
				ensureMsgf(!UniqueParticipantStates.Contains(RecordState),
					TEXT("[%s] One PlayerState belongs to multiple participant records."), Context);
				UniqueParticipantStates.Add(RecordState);
			}
			if (const ATacticalCharacter* RecordPawn = Record.RetainedPawn.Get())
			{
				ensureMsgf(!UniqueRetainedPawns.Contains(RecordPawn),
					TEXT("[%s] One retained Pawn belongs to multiple participant records."), Context);
				UniqueRetainedPawns.Add(RecordPawn);
			}

			if (Record.ConnectionState ==
					ETacticalParticipantConnectionState::DisconnectedGrace
				|| Record.ConnectionState ==
					ETacticalParticipantConnectionState::DisconnectedBodyRetained)
			{
				const ATacticalPlayerState* RecordState = Record.PlayerState.Get();
				const ATacticalCharacter* RecordPawn = Record.RetainedPawn.Get();
				const bool bProxylessGraph = !Record.DisconnectedController.IsValid()
					&& IsValid(RecordState) && IsValid(RecordPawn)
					&& !RecordPawn->GetController()
					&& RecordPawn->GetPlayerState<ATacticalPlayerState>() == RecordState;
				ensureMsgf(!Record.PlayerController.IsValid()
					&& IsValid(RecordState)
					&& IsValid(RecordPawn)
					&& (Record.DisconnectedController.IsValid() || bProxylessGraph),
					TEXT("[%s] Participant %s has an incomplete retained-body ownership graph."),
					Context, *Record.ParticipantId.ToString());
			}
		}
	}

	TSet<const APlayerController*> UniqueControllers;
	for (const APlayerController* PC : TeamAPlayers)
	{
		ensureMsgf(PC != nullptr, TEXT("[%s] RED roster contains a null controller."), Context);
		if (PC)
		{
			ensureMsgf(!UniqueControllers.Contains(PC),
				TEXT("[%s] A controller appears more than once in the tactical rosters."), Context);
			UniqueControllers.Add(PC);
			const ATacticalPlayerState* TPS = PC->GetPlayerState<ATacticalPlayerState>();
			ensureMsgf(TPS && TPS->TeamByte == 0,
				TEXT("[%s] RED roster controller %s does not report RED."), Context, *GetNameSafe(PC));
		}
	}
	for (const APlayerController* PC : TeamBPlayers)
	{
		ensureMsgf(PC != nullptr, TEXT("[%s] BLUE roster contains a null controller."), Context);
		if (PC)
		{
			ensureMsgf(!UniqueControllers.Contains(PC),
				TEXT("[%s] A controller belongs to both tactical teams."), Context);
			UniqueControllers.Add(PC);
			const ATacticalPlayerState* TPS = PC->GetPlayerState<ATacticalPlayerState>();
			ensureMsgf(TPS && TPS->TeamByte == 1,
				TEXT("[%s] BLUE roster controller %s does not report BLUE."), Context, *GetNameSafe(PC));
		}
	}

	int32 LobbyPlayers = 0;
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		const ATacticalPlayerState* TPS = Cast<ATacticalPlayerState>(PlayerState);
		if (!TPS)
		{
			continue;
		}

		LobbyPlayers += TPS->bIsActiveParticipant ? 1 : 0;
		const bool bUnassignedLobbyCandidate = !bMatchStarted
			&& TacticalGS->CurrentPhase == ERoundPhase::PreRound
			&& TPS->bIsActiveParticipant && TPS->TeamByte > 1;
		ensureMsgf(!TPS->bIsActiveParticipant || TPS->TeamByte <= 1 || bUnassignedLobbyCandidate,
			TEXT("[%s] Active player %s has invalid team %d outside PreRound."),
			Context, *TPS->GetPlayerName(), TPS->TeamByte);
		ensureMsgf(!TPS->bIsReady || TPS->TeamByte <= 1,
			TEXT("[%s] Ready player %s has not selected a valid team."), Context, *TPS->GetPlayerName());
		if (const APlayerController* PC = Cast<APlayerController>(TPS->GetOwningController());
			PC && TacticalRules::RequiresActiveRosterMembership(TPS->bIsActiveParticipant, TPS->TeamByte))
		{
			const bool bInExpectedRoster = TPS->TeamByte == 0
				? TeamAPlayers.Contains(PC)
				: TeamBPlayers.Contains(PC);
			ensureMsgf(bInExpectedRoster,
				TEXT("[%s] Player %s reports team %d but is missing from that roster."),
				Context, *TPS->GetPlayerName(), TPS->TeamByte);
		}
		ensureMsgf(TPS->Money >= 0 && TPS->Money <= TacticalEconomy::MaxMoney,
			TEXT("[%s] Player %s money is outside 0..%d: %d"),
			Context, *TPS->GetPlayerName(), TacticalEconomy::MaxMoney, TPS->Money);
		ensureMsgf(TPS->LossStreak >= 0 && TPS->LossStreak <= TacticalEconomy::MaxLossStreak,
			TEXT("[%s] Player %s loss streak is invalid: %d"), Context, *TPS->GetPlayerName(), TPS->LossStreak);
		ensureMsgf(TPS->ArmorDurability >= 0.0f,
			TEXT("[%s] Player %s has negative armor durability: %.2f"),
			Context, *TPS->GetPlayerName(), TPS->ArmorDurability);
	}
	ensureMsgf(LobbyPlayers <= TacticalEconomy::MaxPlayersPerTeam * 2,
		TEXT("[%s] Lobby candidate count exceeds six: %d"), Context, LobbyPlayers);

	if (bMatchStarted)
	{
		ensureMsgf(CurrentRound >= 1 && CurrentRound <= TacticalGS->MaxRounds,
			TEXT("[%s] Current round is invalid: %d/%d"), Context, CurrentRound, TacticalGS->MaxRounds);
		ensureMsgf(CurrentRound == TacticalGS->RoundNumber,
			TEXT("[%s] GameMode round %d differs from GameState round %d."),
			Context, CurrentRound, TacticalGS->RoundNumber);
	}

	const bool bTimedRoundPhase = TacticalGS->CurrentPhase == ERoundPhase::Buy
		|| TacticalGS->CurrentPhase == ERoundPhase::Combat
		|| TacticalGS->CurrentPhase == ERoundPhase::RoundEnd;
	ensureMsgf(!bTimedRoundPhase || TacticalGS->PhaseEndServerTime > 0.0f,
		TEXT("[%s] Timed phase %s has no valid end timestamp."),
		Context, TacticalDiagnostics::GetPhaseName(TacticalGS->CurrentPhase));
	ensureMsgf(!bTeamEliminationRoundEndPending || TacticalGS->CurrentPhase == ERoundPhase::Combat,
		TEXT("[%s] Elimination resolution is pending outside Combat."), Context);
	ensureMsgf(TacticalGS->TeamScores[0] >= 0 && TacticalGS->TeamScores[1] >= 0,
		TEXT("[%s] A tactical team has a negative score."), Context);
	ensureMsgf(!bMatchStarted || TacticalGS->TeamScores[0] + TacticalGS->TeamScores[1] <= CurrentRound,
		TEXT("[%s] Awarded wins exceed completed/current rounds."), Context);
#endif
}

void ATacticalGameMode::EnsureReadyTargets()
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	ReadyTargets.RemoveAll([](const TObjectPtr<ATacticalReadyTarget>& Target) { return !IsValid(Target); });
	TArray<AActor*> ExistingTargetActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATacticalReadyTarget::StaticClass(), ExistingTargetActors);
	bool bHasTargetForTeam[2] = { false, false };
	for (AActor* Actor : ExistingTargetActors)
	{
		if (ATacticalReadyTarget* Target = Cast<ATacticalReadyTarget>(Actor))
		{
			ReadyTargets.AddUnique(Target);
			if (Target->GetTeamByte() <= 1)
			{
				bHasTargetForTeam[Target->GetTeamByte()] = true;
			}
		}
	}
	if (bHasTargetForTeam[0] && bHasTargetForTeam[1])
	{
		return;
	}

	TArray<AActor*> StartActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), StartActors);
	APlayerStart* AnchorStart = StartActors.Num() > 0 ? Cast<APlayerStart>(StartActors[0]) : nullptr;

	const FVector TraceStart = AnchorStart
		? AnchorStart->GetActorLocation() + FVector(0.0f, 0.0f, 50.0f)
		: FVector(0.0f, 0.0f, 350.0f);
	const FVector Forward = AnchorStart ? AnchorStart->GetActorForwardVector() : FVector::ForwardVector;
	const FVector TraceEnd = TraceStart + Forward * 3000.0f;

	FHitResult WallHit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TacticalReadyTargetWallTrace), false, AnchorStart);
	const bool bHitWall = GetWorld()->LineTraceSingleByChannel(
		WallHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);

	const FVector SurfaceNormal = bHitWall ? WallHit.ImpactNormal : -Forward;
	const FVector BaseLocation = bHitWall
		? WallHit.ImpactPoint + SurfaceNormal * 10.0f
		: TraceStart + Forward * 700.0f;
	FVector SurfaceRight = FVector::CrossProduct(FVector::UpVector, SurfaceNormal).GetSafeNormal();
	if (SurfaceRight.IsNearlyZero())
	{
		SurfaceRight = AnchorStart ? AnchorStart->GetActorRightVector() : FVector::RightVector;
	}
	const FRotator TargetRotation = SurfaceNormal.Rotation();

	for (uint8 Team = 0; Team < 2; ++Team)
	{
		if (bHasTargetForTeam[Team])
		{
			continue;
		}
		const float SideOffset = Team == 0 ? -65.0f : 65.0f;
		const FTransform SpawnTransform(TargetRotation, BaseLocation + SurfaceRight * SideOffset);

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ATacticalReadyTarget* Target = GetWorld()->SpawnActor<ATacticalReadyTarget>(
			ATacticalReadyTarget::StaticClass(), SpawnTransform, SpawnParams))
		{
			Target->InitializeForTeam(Team);
			ReadyTargets.Add(Target);
			UE_LOG(LogTacticalFlow, Display,
				TEXT("Event=ReadyTargetSpawned Team=%s Location=%s WallTraceHit=%d"),
				TacticalDiagnostics::GetTeamName(Team), *Target->GetActorLocation().ToCompactString(), bHitWall);
		}
		else
		{
			UE_LOG(LogTacticalFlow, Error, TEXT("Event=ReadyTargetSpawnFailed Team=%s"),
				TacticalDiagnostics::GetTeamName(Team));
		}
	}
}

void ATacticalGameMode::CacheAndValidatePlacedSpawnPoints()
{
	PlacedSpawnPoints.Reset();
	bUsePlacedSpawnPoints = false;
	bPlacedSpawnLayoutValid = false;
	if (!GetWorld())
	{
		return;
	}

	TArray<AActor*> MarkerActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATacticalSpawnPoint::StaticClass(), MarkerActors);
	for (AActor* Actor : MarkerActors)
	{
		if (ATacticalSpawnPoint* Marker = Cast<ATacticalSpawnPoint>(Actor))
		{
			PlacedSpawnPoints.Add(Marker);
		}
	}
	bUsePlacedSpawnPoints = !PlacedSpawnPoints.IsEmpty();
	if (!bUsePlacedSpawnPoints)
	{
		UE_LOG(LogTacticalFlow, Warning,
			TEXT("Event=SpawnLayoutLegacy Reason=NoTacticalSpawnPointActors"));
		return;
	}

	ATacticalSpawnPoint* SlotMarkers[3][6] = {};
	bool bValid = true;
	for (ATacticalSpawnPoint* Marker : PlacedSpawnPoints)
	{
		const int32 GroupIndex = static_cast<int32>(Marker->Group);
		const int32 MaxSlot = GroupIndex == 0
			? TacticalEconomy::MaxPlayersPerTeam * 2
			: TacticalEconomy::MaxPlayersPerTeam;
		if (GroupIndex < 0 || GroupIndex > 2
			|| Marker->SlotIndex < 1 || Marker->SlotIndex > MaxSlot)
		{
			bValid = false;
			UE_LOG(LogTacticalFlow, Error,
				TEXT("Event=SpawnLayoutInvalid Reason=InvalidGroupOrSlot Marker=\"%s\" Group=%d Slot=%d"),
				*GetNameSafe(Marker), GroupIndex, Marker->SlotIndex);
			continue;
		}
		ATacticalSpawnPoint*& Existing = SlotMarkers[GroupIndex][Marker->SlotIndex - 1];
		if (Existing)
		{
			bValid = false;
			UE_LOG(LogTacticalFlow, Error,
				TEXT("Event=SpawnLayoutInvalid Reason=DuplicateSlot Group=%d Slot=%d First=\"%s\" Second=\"%s\""),
				GroupIndex, Marker->SlotIndex, *GetNameSafe(Existing), *GetNameSafe(Marker));
			continue;
		}
		Existing = Marker;
	}

	for (int32 GroupIndex = 0; GroupIndex < 3; ++GroupIndex)
	{
		const int32 MaxSlot = GroupIndex == 0
			? TacticalEconomy::MaxPlayersPerTeam * 2
			: TacticalEconomy::MaxPlayersPerTeam;
		for (int32 SlotIndex = 0; SlotIndex < MaxSlot; ++SlotIndex)
		{
			ATacticalSpawnPoint* Marker = SlotMarkers[GroupIndex][SlotIndex];
			if (!Marker)
			{
				bValid = false;
				UE_LOG(LogTacticalFlow, Error,
					TEXT("Event=SpawnLayoutInvalid Reason=MissingSlot Group=%d Slot=%d"),
					GroupIndex, SlotIndex + 1);
				continue;
			}
			for (int32 OtherSlot = SlotIndex + 1; OtherSlot < MaxSlot; ++OtherSlot)
			{
				const ATacticalSpawnPoint* Other = SlotMarkers[GroupIndex][OtherSlot];
				if (Other
					&& FVector::DistSquared2D(Marker->GetActorLocation(), Other->GetActorLocation()) < FMath::Square(100.0f)
					&& FMath::Abs(Marker->GetActorLocation().Z - Other->GetActorLocation().Z) < 192.0f)
				{
					bValid = false;
					UE_LOG(LogTacticalFlow, Error,
						TEXT("Event=SpawnLayoutInvalid Reason=OverlappingCapsules Group=%d SlotA=%d SlotB=%d"),
						GroupIndex, SlotIndex + 1, OtherSlot + 1);
				}
			}
		}
	}
	bPlacedSpawnLayoutValid = bValid;
	if (bValid)
	{
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=SpawnLayoutValidated Markers=%d Valid=1 LobbySlots=6 RedSlots=3 BlueSlots=3"),
			PlacedSpawnPoints.Num());
	}
	else
	{
		UE_LOG(LogTacticalFlow, Error,
			TEXT("Event=SpawnLayoutValidated Markers=%d Valid=0 MatchStartBlocked=1"),
			PlacedSpawnPoints.Num());
	}
}

ATacticalSpawnPoint* ATacticalGameMode::FindPlacedSpawnPoint(
	ETacticalSpawnGroup Group, int32 SlotIndex) const
{
	for (ATacticalSpawnPoint* Marker : PlacedSpawnPoints)
	{
		if (IsValid(Marker) && Marker->Group == Group && Marker->SlotIndex == SlotIndex)
		{
			return Marker;
		}
	}
	return nullptr;
}

void ATacticalGameMode::EnsureTeamPlayerStarts()
{
	if (!HasAuthority() || !GetWorld() || bUsePlacedSpawnPoints || RuntimeTeamStarts.Num() > 0)
	{
		return;
	}

	TArray<AActor*> StartActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), StartActors);
	APlayerStart* AnchorStart = StartActors.Num() > 0 ? Cast<APlayerStart>(StartActors[0]) : nullptr;

	auto IsTeamStart = [](const APlayerStart* Start, uint8 Team)
	{
		if (!Start)
		{
			return false;
		}
		const TArray<FName> AcceptedTags = Team == 0
			? TArray<FName>{ TEXT("RED"), TEXT("TeamA"), TEXT("Team0") }
			: TArray<FName>{ TEXT("BLUE"), TEXT("TeamB"), TEXT("Team1") };
		for (const FName Tag : AcceptedTags)
		{
			if (Start->PlayerStartTag.IsEqual(Tag, ENameCase::IgnoreCase) || Start->ActorHasTag(Tag))
			{
				return true;
			}
		}
		return false;
	};

	bool bHasTeamStarts[2] = { false, false };
	for (AActor* Actor : StartActors)
	{
		if (const APlayerStart* Start = Cast<APlayerStart>(Actor))
		{
			bHasTeamStarts[0] |= IsTeamStart(Start, 0);
			bHasTeamStarts[1] |= IsTeamStart(Start, 1);
		}
	}

	const FVector AnchorLocation = AnchorStart ? AnchorStart->GetActorLocation() : FVector(0.0f, 0.0f, 302.0f);
	const FRotator AnchorRotation = AnchorStart ? AnchorStart->GetActorRotation() : FRotator::ZeroRotator;
	const FVector Forward = AnchorRotation.Vector();
	const FVector Right = FRotationMatrix(AnchorRotation).GetUnitAxis(EAxis::Y);

	for (uint8 Team = 0; Team < 2; ++Team)
	{
		if (bHasTeamStarts[Team])
		{
			continue;
		}

		const FName TeamTag = Team == 0 ? TEXT("RED") : TEXT("BLUE");
		const float SideOffset = Team == 0 ? -180.0f : 180.0f;
		for (int32 Slot = 0; Slot < TacticalEconomy::MaxPlayersPerTeam; ++Slot)
		{
			const float ForwardOffset = static_cast<float>(Slot - 1) * 110.0f;
			const FVector SpawnLocation = AnchorLocation + Right * SideOffset + Forward * ForwardOffset;

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (APlayerStart* Start = GetWorld()->SpawnActor<APlayerStart>(
				APlayerStart::StaticClass(), SpawnLocation, AnchorRotation, SpawnParams))
			{
				Start->PlayerStartTag = TeamTag;
				Start->Tags.AddUnique(TeamTag);
				RuntimeTeamStarts.Add(Start);
			}
		}
	}
}

void ATacticalGameMode::MovePlayerToAssignedStart(APlayerController* PlayerController, uint8 Team)
{
	if (!HasAuthority() || !GetWorld() || !PlayerController || Team > 1 || !PlayerController->GetPawn())
	{
		return;
	}
	// Team selection occurs in the common preparation lobby. Only a round
	// transition (or an eligible reconnect during an active match) may move a
	// player to the team half of the arena.
	if (!bMatchStarted)
	{
		return;
	}

	if (bUsePlacedSpawnPoints)
	{
		if (!bPlacedSpawnLayoutValid)
		{
			return;
		}
		const TArray<TObjectPtr<APlayerController>>& Roster = Team == 0
			? TeamAPlayers : TeamBPlayers;
		TArray<APlayerController*> Eligible;
		for (APlayerController* PC : Roster)
		{
			const ATacticalPlayerState* TPS = PC
				? PC->GetPlayerState<ATacticalPlayerState>() : nullptr;
			if (IsValid(PC) && TPS && TPS->bIsActiveParticipant)
			{
				Eligible.Add(PC);
			}
		}
		// During a buy-phase reconnect the replacement controller is inserted
		// into the roster after this teleport, so count it explicitly here.
		if (!Eligible.Contains(PlayerController))
		{
			Eligible.Add(PlayerController);
		}
		const int32 Index = Eligible.IndexOfByKey(PlayerController);
		if (Index == INDEX_NONE || Eligible.Num() > TacticalEconomy::MaxPlayersPerTeam)
		{
			return;
		}
		const int32 PreferredSlot = Eligible.Num() == 1 ? 2
			: Eligible.Num() == 2 ? (Index == 0 ? 1 : 3)
			: Index + 1;
		const ETacticalSpawnGroup Group = Team == 0
			? ETacticalSpawnGroup::Red : ETacticalSpawnGroup::Blue;
		TArray<int32, TInlineAllocator<3>> CandidateSlots;
		CandidateSlots.Add(PreferredSlot);
		for (int32 OtherSlot = 1; OtherSlot <= TacticalEconomy::MaxPlayersPerTeam; ++OtherSlot)
		{
			if (OtherSlot != PreferredSlot)
			{
				CandidateSlots.Add(OtherSlot);
			}
		}
		ATacticalSpawnPoint* Marker = nullptr;
		for (int32 CandidateSlot : CandidateSlots)
		{
			ATacticalSpawnPoint* Candidate = FindPlacedSpawnPoint(Group, CandidateSlot);
			if (!Candidate)
			{
				continue;
			}
			bool bOccupied = false;
			for (APlayerController* Teammate : Eligible)
			{
				const APawn* TeammatePawn = Teammate && Teammate != PlayerController
					? Teammate->GetPawn() : nullptr;
				if (IsValid(TeammatePawn)
					&& FVector::DistSquared2D(
						TeammatePawn->GetActorLocation(), Candidate->GetActorLocation()) < FMath::Square(100.0f)
					&& FMath::Abs(TeammatePawn->GetActorLocation().Z
						- Candidate->GetActorLocation().Z) < 192.0f)
				{
					bOccupied = true;
					break;
				}
			}
			if (!bOccupied)
			{
				Marker = Candidate;
				break;
			}
		}
		if (!Marker)
		{
			UE_LOG(LogTacticalFlow, Warning,
				TEXT("Event=TeamSpawnTeleportFailed Player=\"%s\" Reason=NoUnoccupiedMarker"),
				*GetNameSafe(PlayerController));
			return;
		}
		APawn* Pawn = PlayerController->GetPawn();
		if (Pawn->TeleportTo(Marker->GetActorLocation(), Marker->GetActorRotation(), false, false))
		{
			PlayerController->SetControlRotation(Marker->GetActorRotation());
			if (ATacticalCharacter* Character = Cast<ATacticalCharacter>(Pawn))
			{
				Character->GetCharacterMovement()->StopMovementImmediately();
			}
		}
		else
		{
			UE_LOG(LogTacticalFlow, Warning,
				TEXT("Event=TeamSpawnTeleportFailed Player=\"%s\" Marker=\"%s\""),
				*GetNameSafe(PlayerController), *GetNameSafe(Marker));
		}
		return;
	}

	TArray<AActor*> StartActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), StartActors);
	TArray<APlayerStart*> TeamStarts;
	const TArray<FName> AcceptedTags = Team == 0
		? TArray<FName>{ TEXT("RED"), TEXT("TeamA"), TEXT("Team0") }
		: TArray<FName>{ TEXT("BLUE"), TEXT("TeamB"), TEXT("Team1") };
	for (AActor* Actor : StartActors)
	{
		APlayerStart* Start = Cast<APlayerStart>(Actor);
		if (!Start)
		{
			continue;
		}
		for (const FName Tag : AcceptedTags)
		{
			if (Start->PlayerStartTag.IsEqual(Tag, ENameCase::IgnoreCase) || Start->ActorHasTag(Tag))
			{
				TeamStarts.Add(Start);
				break;
			}
		}
	}

	if (TeamStarts.Num() == 0)
	{
		return;
	}

	const int32 TeamIndex = Team == 0
		? TeamAPlayers.IndexOfByKey(PlayerController)
		: TeamBPlayers.IndexOfByKey(PlayerController);
	APlayerStart* SelectedStart = TeamStarts[FMath::Max(TeamIndex, 0) % TeamStarts.Num()];
	APawn* Pawn = PlayerController->GetPawn();
	FVector SafeSpawnLocation = SelectedStart->GetActorLocation();
	if (const ATacticalCharacter* TacticalCharacter = Cast<ATacticalCharacter>(Pawn))
	{
		const UCapsuleComponent* CharacterCapsule = TacticalCharacter->GetCapsuleComponent();
		const UCapsuleComponent* StartCapsule = SelectedStart->GetCapsuleComponent();
		if (CharacterCapsule && StartCapsule)
		{
			// PlayerStart's navigation capsule can be shorter than the actual
			// character capsule. Compensate before teleporting so the pawn is not
			// embedded in the floor (which causes repeated depenetration/correction).
			const float CapsuleHeightDifference = CharacterCapsule->GetScaledCapsuleHalfHeight()
				- StartCapsule->GetScaledCapsuleHalfHeight();
			SafeSpawnLocation.Z += FMath::Max(0.0f, CapsuleHeightDifference) + 2.0f;
		}
	}

	if (!Pawn->TeleportTo(SafeSpawnLocation, SelectedStart->GetActorRotation(), false, false))
	{
		UE_LOG(LogTacticalFlow, Warning,
			TEXT("Event=LobbyTeleportFailed Controller=\"%s\" Team=%s Start=\"%s\""),
			*GetNameSafe(PlayerController), TacticalDiagnostics::GetTeamName(Team), *GetNameSafe(SelectedStart));
		return;
	}
	PlayerController->SetControlRotation(SelectedStart->GetActorRotation());
	if (ATacticalCharacter* TacticalCharacter = Cast<ATacticalCharacter>(Pawn))
	{
		TacticalCharacter->GetCharacterMovement()->StopMovementImmediately();
	}
}

void ATacticalGameMode::MovePlayerToSpectator(APlayerController* PlayerController, bool bCanJoinNextRound)
{
	APlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<APlayerState>() : nullptr;
	if (!HasAuthority() || !PlayerController || !PlayerState)
	{
		return;
	}
	const FString PlayerName = PlayerState->GetPlayerName();

	// Capture any pawn supplied by the normal or an alternate spawn path before
	// changing state. BeginSpectatingState unpossesses it; destroying it prevents
	// an unowned collision/damage actor from remaining in the live round.
	APawn* SpawnedPawn = PlayerController->GetPawn();
	if (bCanJoinNextRound)
	{
		PlayerState->SetIsOnlyASpectator(false);
		PlayerState->SetIsSpectator(true);
		PlayerController->ChangeState(NAME_Spectating);
		PlayerController->bPlayerIsWaiting = false;
	}
	else
	{
		PlayerController->StartSpectatingOnly();
	}
	if (ATacticalPlayerController* TacticalController = Cast<ATacticalPlayerController>(PlayerController))
	{
		TacticalController->ClientStartRestrictedSpectating();
	}

	const bool bHadSpawnedPawn = IsValid(SpawnedPawn);
	if (bHadSpawnedPawn)
	{
		SpawnedPawn->Destroy();
	}

	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=SpectatorPolicyApplied Player=\"%s\" Policy=%s SpawnedPawnDestroyed=%d"),
		*PlayerName,
		bCanJoinNextRound ? TEXT("WaitForNextRound") : TEXT("MatchSpectatorOnly"), bHadSpawnedPawn);
}

ERoundPhase ATacticalGameMode::GetCurrentPhase() const
{
	return TacticalGS ? TacticalGS->CurrentPhase : ERoundPhase::PreRound;
}
