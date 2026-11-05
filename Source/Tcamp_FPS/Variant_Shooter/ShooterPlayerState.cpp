// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterPlayerState.h"
#include "Net/UnrealNetwork.h"

void AShooterPlayerState::AddKill()
{
	Kills++;
	SetScore(Kills);
	// Force the OnRep to fire on the server too (so server's UI updates)
	OnRep_Kills();
}

void AShooterPlayerState::MarkAsWinner()
{
	bIsWinner = true;
	bMatchOver = true;
	OnRep_MatchOver();
}

void AShooterPlayerState::MarkMatchOver()
{
	bIsWinner = false;
	bMatchOver = true;
	OnRep_MatchOver();
}

void AShooterPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShooterPlayerState, Kills);
	DOREPLIFETIME(AShooterPlayerState, KillGoal);
	DOREPLIFETIME(AShooterPlayerState, bMatchOver);
	DOREPLIFETIME(AShooterPlayerState, bIsWinner);
}

void AShooterPlayerState::OnRep_Kills()
{
	// Called on ALL clients when Kills replicates
	// The owning client's PlayerController handles updating the local UI
	OnKillsUpdated.Broadcast(Kills, KillGoal);
}

void AShooterPlayerState::OnRep_MatchOver()
{
	// Called on ALL clients when the match ends
	OnMatchOver.Broadcast();
}
