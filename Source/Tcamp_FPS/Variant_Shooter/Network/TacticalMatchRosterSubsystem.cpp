// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/TacticalMatchRosterSubsystem.h"
#include "Network/TacticalNetworkAdmission.h"
#include "Network/TacticalDisconnectedController.h"
#include "TacticalCharacter.h"
#include "TacticalPlayerState.h"
#include "GameFramework/PlayerController.h"

void UTacticalMatchRosterSubsystem::ResetForNewMatch()
{
	ParticipantRecords.Reset();
	MatchId = GenerateOpaqueId();
}

FTacticalMatchParticipantRecord& UTacticalMatchRosterSubsystem::RegisterParticipant(
	const FString& IdentityKey,
	APlayerController* PlayerController,
	ATacticalPlayerState* PlayerState)
{
	if (MatchId.IsEmpty())
	{
		MatchId = GenerateOpaqueId();
	}
	FTacticalMatchParticipantRecord& Record = ParticipantRecords.FindOrAdd(IdentityKey);
	if (!Record.ParticipantId.IsValid())
	{
		Record.ParticipantId = FGuid::NewGuid();
	}
	EnsureReconnectCredential(Record);
	Record.IdentityKey = IdentityKey;
	Record.PlayerController = PlayerController;
	Record.PlayerState = PlayerState;
	return Record;
}

FTacticalMatchParticipantRecord* UTacticalMatchRosterSubsystem::FindByIdentity(
	const FString& IdentityKey)
{
	return IdentityKey.IsEmpty() ? nullptr : ParticipantRecords.Find(IdentityKey);
}

const FTacticalMatchParticipantRecord* UTacticalMatchRosterSubsystem::FindByIdentity(
	const FString& IdentityKey) const
{
	return IdentityKey.IsEmpty() ? nullptr : ParticipantRecords.Find(IdentityKey);
}

FTacticalMatchParticipantRecord* UTacticalMatchRosterSubsystem::FindByController(
	const APlayerController* PlayerController)
{
	for (TPair<FString, FTacticalMatchParticipantRecord>& Pair : ParticipantRecords)
	{
		if (Pair.Value.PlayerController.Get() == PlayerController)
		{
			return &Pair.Value;
		}
	}
	return nullptr;
}

FTacticalMatchParticipantRecord* UTacticalMatchRosterSubsystem::FindByPlayerState(
	const ATacticalPlayerState* PlayerState)
{
	for (TPair<FString, FTacticalMatchParticipantRecord>& Pair : ParticipantRecords)
	{
		if (Pair.Value.PlayerState.Get() == PlayerState)
		{
			return &Pair.Value;
		}
	}
	return nullptr;
}

FTacticalMatchParticipantRecord* UTacticalMatchRosterSubsystem::FindByReconnectCredential(
	const FString& CandidateMatchId,
	const FString& CandidateCredential)
{
	return const_cast<FTacticalMatchParticipantRecord*>(
		static_cast<const UTacticalMatchRosterSubsystem*>(this)
			->FindByReconnectCredential(CandidateMatchId, CandidateCredential));
}

const FTacticalMatchParticipantRecord*
UTacticalMatchRosterSubsystem::FindByReconnectCredential(
	const FString& CandidateMatchId,
	const FString& CandidateCredential) const
{
	if (CandidateCredential.IsEmpty()
		|| !TacticalNetworkAdmission::ConstantTimeCredentialEquals(
			CandidateMatchId, MatchId))
	{
		return nullptr;
	}

	const FTacticalMatchParticipantRecord* Match = nullptr;
	for (const TPair<FString, FTacticalMatchParticipantRecord>& Pair : ParticipantRecords)
	{
		const bool bMatchesCurrent =
			TacticalNetworkAdmission::ConstantTimeCredentialEquals(
				CandidateCredential, Pair.Value.ReconnectCredential);
		const bool bMatchesPrevious = !Pair.Value.PreviousReconnectCredential.IsEmpty()
			&& TacticalNetworkAdmission::ConstantTimeCredentialEquals(
				CandidateCredential, Pair.Value.PreviousReconnectCredential);
		if (bMatchesCurrent || bMatchesPrevious)
		{
			// Generation prevents collisions. Fail closed if registry corruption
			// ever makes one bearer credential identify more than one record.
			if (Match)
			{
				return nullptr;
			}
			Match = &Pair.Value;
		}
	}
	return Match;
}

FString UTacticalMatchRosterSubsystem::CreateGuestIdentityKey() const
{
	FString IdentityKey;
	do
	{
		IdentityKey = FString::Printf(TEXT("GUEST:%s"), *GenerateOpaqueId());
	}
	while (ParticipantRecords.Contains(IdentityKey));
	return IdentityKey;
}

int32 UTacticalMatchRosterSubsystem::CountReservedTeamSlots(uint8 TeamByte) const
{
	int32 Count = 0;
	for (const TPair<FString, FTacticalMatchParticipantRecord>& Pair : ParticipantRecords)
	{
		const FTacticalMatchParticipantRecord& Record = Pair.Value;
		Count += Record.bRosterSlotReserved
			&& Record.TeamByte == TeamByte
			? 1 : 0;
	}
	return Count;
}

const FString& UTacticalMatchRosterSubsystem::EnsureReconnectCredential(
	FTacticalMatchParticipantRecord& Record)
{
	if (Record.ReconnectCredential.IsEmpty())
	{
		FString NewCredential;
		do
		{
			NewCredential = GenerateOpaqueId();
		}
		while (IsCredentialInUse(NewCredential));
		Record.ReconnectCredential = MoveTemp(NewCredential);
	}
	return Record.ReconnectCredential;
}

const FString& UTacticalMatchRosterSubsystem::RotateReconnectCredential(
	FTacticalMatchParticipantRecord& Record,
	const FString& CredentialKnownByClient)
{
	FString Replacement;
	do
	{
		Replacement = GenerateOpaqueId();
	}
	while (Replacement == Record.ReconnectCredential || IsCredentialInUse(Replacement));
	// The overlap follows the credential this particular client proved it
	// possesses. If delivery of a prior rotation was interrupted, preserving
	// that older bearer avoids locking the legitimate client out again.
	Record.PreviousReconnectCredential = CredentialKnownByClient.IsEmpty()
		? Record.ReconnectCredential
		: CredentialKnownByClient;
	Record.ReconnectCredential = MoveTemp(Replacement);
	Record.bReconnectCredentialAcknowledged = false;
	return Record.ReconnectCredential;
}

bool UTacticalMatchRosterSubsystem::AcknowledgeReconnectCredential(
	FTacticalMatchParticipantRecord& Record,
	const FString& CandidateMatchId,
	const FString& CandidateCredential)
{
	if (!TacticalNetworkAdmission::ConstantTimeCredentialEquals(
		CandidateMatchId, MatchId)
		|| !TacticalNetworkAdmission::ConstantTimeCredentialEquals(
			CandidateCredential, Record.ReconnectCredential))
	{
		return false;
	}

	Record.PreviousReconnectCredential.Reset();
	Record.bReconnectCredentialAcknowledged = true;
	return true;
}

bool UTacticalMatchRosterSubsystem::ValidateReconnectCredential(
	const FString& IdentityKey,
	const FString& CandidateMatchId,
	const FString& CandidateCredential) const
{
	const FTacticalMatchParticipantRecord* Record = FindByIdentity(IdentityKey);
	return Record
		&& TacticalNetworkAdmission::ConstantTimeCredentialEquals(CandidateMatchId, MatchId)
		&& (TacticalNetworkAdmission::ConstantTimeCredentialEquals(
			CandidateCredential, Record->ReconnectCredential)
			|| (!Record->PreviousReconnectCredential.IsEmpty()
				&& TacticalNetworkAdmission::ConstantTimeCredentialEquals(
					CandidateCredential, Record->PreviousReconnectCredential)));
}

FString UTacticalMatchRosterSubsystem::GenerateOpaqueId() const
{
	// Platform GUID creation is backed by the operating system on supported
	// desktop targets and produces a URL-safe, opaque 128-bit value.
	return FGuid::NewGuid().ToString(EGuidFormats::Digits);
}

bool UTacticalMatchRosterSubsystem::IsCredentialInUse(const FString& Credential) const
{
	for (const TPair<FString, FTacticalMatchParticipantRecord>& Pair : ParticipantRecords)
	{
		if (Pair.Value.ReconnectCredential == Credential
			|| Pair.Value.PreviousReconnectCredential == Credential)
		{
			return true;
		}
	}
	return false;
}

void UTacticalMatchRosterSubsystem::RemoveParticipant(const FString& IdentityKey)
{
	ParticipantRecords.Remove(IdentityKey);
}
