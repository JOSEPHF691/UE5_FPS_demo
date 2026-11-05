// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TacticalMatchRosterSubsystem.generated.h"

class APlayerController;
class ATacticalCharacter;
class ATacticalDisconnectedController;
class ATacticalPlayerState;

/** Server-side connection state for one authenticated match participant. */
enum class ETacticalParticipantConnectionState : uint8
{
	ConnectedPlaying,
	DisconnectedGrace,
	/** Public countdown expired, but the living pawn remains part of this round. */
	DisconnectedBodyRetained,
	DisconnectedWaiting,
	ReconnectedPlaying,
	ReconnectedSpectating,
	EligibleNextRound,
	Abandoned
};

/**
 * Stable participant data whose lifetime is independent of a net connection's
 * short-lived PlayerController. This is intentionally server-only; clients see
 * the relevant state through PlayerState and normal gameplay replication.
 */
struct FTacticalMatchParticipantRecord
{
	FGuid ParticipantId;
	FString IdentityKey;
	/** Server-only bearer credential. Never copy it into replicated state or project logs. */
	FString ReconnectCredential;
	/** Previous client-known bearer retained only until the replacement is acknowledged. */
	FString PreviousReconnectCredential;
	/** True only after the owner confirms durable receipt of the current bearer. */
	bool bReconnectCredentialAcknowledged = false;
	uint8 TeamByte = 255;
	bool bRosterSlotReserved = false;
	int32 EnteredRound = 0;
	int32 LastSettledRound = 0;
	float ReconnectDeadlineServerTime = 0.0f;
	FRotator RetainedControlRotation = FRotator::ZeroRotator;
	ETacticalParticipantConnectionState ConnectionState =
		ETacticalParticipantConnectionState::ConnectedPlaying;

	TWeakObjectPtr<APlayerController> PlayerController;
	TWeakObjectPtr<ATacticalPlayerState> PlayerState;
	TWeakObjectPtr<ATacticalCharacter> RetainedPawn;
	TWeakObjectPtr<ATacticalDisconnectedController> DisconnectedController;
};

/** Owns the authoritative match roster independently from PlayerController lifetime. */
UCLASS()
class TCAMP_FPS_API UTacticalMatchRosterSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	void ResetForNewMatch();
	const FString& GetMatchId() const { return MatchId; }

	FTacticalMatchParticipantRecord& RegisterParticipant(
		const FString& IdentityKey,
		APlayerController* PlayerController,
		ATacticalPlayerState* PlayerState);

	FTacticalMatchParticipantRecord* FindByIdentity(const FString& IdentityKey);
	const FTacticalMatchParticipantRecord* FindByIdentity(const FString& IdentityKey) const;
	FTacticalMatchParticipantRecord* FindByController(const APlayerController* PlayerController);
	FTacticalMatchParticipantRecord* FindByPlayerState(const ATacticalPlayerState* PlayerState);
	FTacticalMatchParticipantRecord* FindByReconnectCredential(
		const FString& CandidateMatchId,
		const FString& CandidateCredential);
	const FTacticalMatchParticipantRecord* FindByReconnectCredential(
		const FString& CandidateMatchId,
		const FString& CandidateCredential) const;

	/** Creates a server-originated identity for sessions with no online provider. */
	FString CreateGuestIdentityKey() const;

	int32 CountReservedTeamSlots(uint8 TeamByte) const;
	const FString& EnsureReconnectCredential(FTacticalMatchParticipantRecord& Record);
	const FString& RotateReconnectCredential(
		FTacticalMatchParticipantRecord& Record,
		const FString& CredentialKnownByClient);
	bool AcknowledgeReconnectCredential(
		FTacticalMatchParticipantRecord& Record,
		const FString& CandidateMatchId,
		const FString& CandidateCredential);
	bool ValidateReconnectCredential(
		const FString& IdentityKey,
		const FString& CandidateMatchId,
		const FString& CandidateCredential) const;
	void RemoveParticipant(const FString& IdentityKey);

	TMap<FString, FTacticalMatchParticipantRecord>& GetMutableRecords() { return ParticipantRecords; }
	const TMap<FString, FTacticalMatchParticipantRecord>& GetRecords() const { return ParticipantRecords; }

private:
	FString GenerateOpaqueId() const;
	bool IsCredentialInUse(const FString& Credential) const;

	/** Server-only match instance identifier. It is sent only to each owning client. */
	FString MatchId;
	TMap<FString, FTacticalMatchParticipantRecord> ParticipantRecords;
};
