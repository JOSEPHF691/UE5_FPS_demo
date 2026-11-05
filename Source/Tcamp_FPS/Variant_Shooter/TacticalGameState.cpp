// Copyright Epic Games, Inc. All Rights Reserved.

#include "TacticalGameState.h"
#include "TacticalRules.h"
#include "Net/UnrealNetwork.h"
#include "TacticalPlayerState.h"
#include "GameFramework/PlayerState.h"
#include "ShooterCharacter.h"
#include "TacticalCharacter.h"

ATacticalGameState::ATacticalGameState()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ATacticalGameState::ResetRoundState(int32 InRoundNumber)
{
	if (!HasAuthority())
	{
		return;
	}

	RoundNumber = InRoundNumber;
	LastRoundWinningTeam = 255;
	LastRoundEndReason = ERoundEndReason::Unknown;
	SetHuntIntelState(false, false);
	ResetTreasureState(InRoundNumber);
}

bool ATacticalGameState::TickPhaseTimer(float DeltaSeconds)
{
	if (!HasAuthority() || PhaseEndServerTime <= 0.0f)
	{
		return false;
	}

	PhaseTimeRemaining = GetPhaseTimeRemaining();
	return PhaseTimeRemaining <= 0.0f;
}

float ATacticalGameState::GetPhaseTimeRemaining() const
{
	if (PhaseEndServerTime <= 0.0f)
	{
		return 0.0f;
	}
	return FMath::Max(0.0f, PhaseEndServerTime - GetServerWorldTimeSeconds());
}

void ATacticalGameState::SetPhase(ERoundPhase NewPhase, float Duration)
{
	if (!HasAuthority())
	{
		return;
	}

	CurrentPhase = NewPhase;
	PhaseTimeRemaining = Duration;
	PhaseEndServerTime = Duration > 0.0f ? GetServerWorldTimeSeconds() + Duration : 0.0f;

	if (NewPhase != ERoundPhase::Combat)
	{
		SetHuntIntelState(false, false);
	}

	OnPhaseChanged.Broadcast(NewPhase, Duration);
}

void ATacticalGameState::SetHuntIntelState(bool bActive, bool bEscalated)
{
	if (!HasAuthority())
	{
		return;
	}

	const bool bChanged = bHuntIntelActive != bActive || bHuntIntelEscalated != bEscalated;
	bHuntIntelActive = bActive;
	bHuntIntelEscalated = bActive && bEscalated;
	if (bChanged)
	{
		++HuntIntelStateSerial;
		OnHuntIntelStateChanged.Broadcast(bHuntIntelActive, bHuntIntelEscalated);
	}
}

void ATacticalGameState::SetTreasureState(const FTreasureRoundSnapshot& NewState)
{
	if (!HasAuthority())
	{
		return;
	}

	const uint8 NextRevision = static_cast<uint8>(TreasureState.Revision + 1);
	TreasureState = NewState;
	TreasureState.Revision = NextRevision;
	OnTreasureStateChanged.Broadcast();
	ForceNetUpdate();
}

void ATacticalGameState::ResetTreasureState(int32 InRoundNumber)
{
	if (!HasAuthority())
	{
		return;
	}

	FTreasureRoundSnapshot ResetState;
	ResetState.RoundNumber = InRoundNumber;
	SetTreasureState(ResetState);
}

float ATacticalGameState::GetTreasureActivationTimeRemaining() const
{
	return TreasureState.ActivationServerTime > 0.0f
		? FMath::Max(0.0f, TreasureState.ActivationServerTime - GetServerWorldTimeSeconds())
		: 0.0f;
}

float ATacticalGameState::GetTreasureClaimTimeRemaining() const
{
	return TreasureState.ClaimEndServerTime > 0.0f
		? FMath::Max(0.0f, TreasureState.ClaimEndServerTime - GetServerWorldTimeSeconds())
		: 0.0f;
}

void ATacticalGameState::AwardRoundWin(uint8 WinningTeam, ERoundEndReason Reason)
{
	if (!HasAuthority() || WinningTeam > 1)
	{
		return;
	}

	TeamScores[WinningTeam]++;
	BroadcastRoundEnded(WinningTeam, Reason);

}

void ATacticalGameState::BroadcastRoundDraw(ERoundEndReason Reason)
{
	if (!HasAuthority())
	{
		return;
	}

	BroadcastRoundEnded(255, Reason);
}

bool ATacticalGameState::IsMatchOver() const
{
	return TacticalRules::IsMatchOver(TeamScores[0], TeamScores[1], RoundNumber, MaxRounds);
}

uint8 ATacticalGameState::GetMatchWinner() const
{
	return TacticalRules::GetMatchWinner(TeamScores[0], TeamScores[1]);
}

void ATacticalGameState::BroadcastMatchEnded(uint8 WinningTeam, EMatchEndReason Reason)
{
	if (!HasAuthority())
	{
		return;
	}

	LastMatchWinningTeam = WinningTeam;
	LastMatchEndReason = Reason;
	++MatchEndSerial;
	OnMatchEnded.Broadcast(WinningTeam);
}

int32 ATacticalGameState::CountPlayersAlive(uint8 Team) const
{
	int32 Count = 0;
	for (APlayerState* PS : PlayerArray)
	{
		ATacticalPlayerState* TPS = Cast<ATacticalPlayerState>(PS);
		if (!TPS || !TPS->bIsActiveParticipant || TPS->TeamByte != Team)
		{
			continue;
		}

		APawn* Pawn = PS->GetPawn();
		if (!Pawn)
		{
			continue;
		}

		if (AShooterCharacter* Char = Cast<AShooterCharacter>(Pawn))
		{
			if (!Char->IsDead())
			{
				Count++;
			}
		}
	}
	return Count;
}

bool ATacticalGameState::IsTeamEliminated(uint8 Team) const
{
	int32 TotalOnTeam = 0;
	for (const APlayerState* PS : PlayerArray)
	{
		if (const ATacticalPlayerState* TPS = Cast<ATacticalPlayerState>(PS))
		{
			if (TPS->bIsActiveParticipant && TPS->TeamByte == Team)
			{
				++TotalOnTeam;
			}
		}
	}

	// An empty team is not eliminated — only return true if there are registered players, all dead
	return TotalOnTeam > 0 && CountPlayersAlive(Team) <= 0;
}

void ATacticalGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATacticalGameState, RoundNumber);
	DOREPLIFETIME(ATacticalGameState, CurrentPhase);
	DOREPLIFETIME(ATacticalGameState, PhaseEndServerTime);
	DOREPLIFETIME(ATacticalGameState, TeamScores);
	DOREPLIFETIME(ATacticalGameState, MaxRounds);
	DOREPLIFETIME(ATacticalGameState, LastRoundWinningTeam);
	DOREPLIFETIME(ATacticalGameState, LastRoundEndReason);
	DOREPLIFETIME(ATacticalGameState, RoundEndSerial);
	DOREPLIFETIME(ATacticalGameState, bHuntIntelActive);
	DOREPLIFETIME(ATacticalGameState, bHuntIntelEscalated);
	DOREPLIFETIME(ATacticalGameState, HuntIntelStateSerial);
	DOREPLIFETIME(ATacticalGameState, TreasureState);
	DOREPLIFETIME(ATacticalGameState, LastMatchWinningTeam);
	DOREPLIFETIME(ATacticalGameState, LastMatchEndReason);
	DOREPLIFETIME(ATacticalGameState, MatchEndSerial);
}

void ATacticalGameState::OnRep_RoundNumber()
{
	// UI updates on round change — clients bind to this if needed
}

void ATacticalGameState::OnRep_Phase()
{
	PhaseTimeRemaining = GetPhaseTimeRemaining();
	OnPhaseChanged.Broadcast(CurrentPhase, PhaseTimeRemaining);
}

void ATacticalGameState::OnRep_PhaseTiming()
{
	PhaseTimeRemaining = GetPhaseTimeRemaining();
}

void ATacticalGameState::OnRep_RoundEnd()
{
	OnRoundEnded.Broadcast(LastRoundWinningTeam, TeamScores[0], TeamScores[1], LastRoundEndReason);
}

void ATacticalGameState::OnRep_HuntIntelState()
{
	OnHuntIntelStateChanged.Broadcast(bHuntIntelActive, bHuntIntelEscalated);
}

void ATacticalGameState::OnRep_TreasureState()
{
	OnTreasureStateChanged.Broadcast();
}

void ATacticalGameState::OnRep_MatchEnd()
{
	OnMatchEnded.Broadcast(LastMatchWinningTeam);
}

void ATacticalGameState::BroadcastRoundEnded(uint8 WinningTeam, ERoundEndReason Reason)
{
	LastRoundWinningTeam = WinningTeam;
	LastRoundEndReason = Reason;
	++RoundEndSerial;
	OnRoundEnded.Broadcast(WinningTeam, TeamScores[0], TeamScores[1], Reason);
}
