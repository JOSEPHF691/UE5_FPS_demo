// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variant_Shooter/ShooterGameMode.h"
#include "ShooterGameState.h"
#include "ShooterPlayerState.h"
#include "ShooterCharacter.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

AShooterGameMode::AShooterGameMode()
{
	GameStateClass = AShooterGameState::StaticClass();
	PlayerStateClass = AShooterPlayerState::StaticClass();
}

void AShooterGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Subscribe to GameState kill events instead of expecting direct calls from characters
	if (AShooterGameState* SGS = GetGameState<AShooterGameState>())
	{
		SGS->OnKillEvent.AddDynamic(this, &AShooterGameMode::OnKillEvent);
	}
}

void AShooterGameMode::OnKillEvent(AController* Killer, AController* Victim)
{
	if (bGameOver || !HasAuthority())
	{
		return;
	}

	// Increment kills on the killer's PlayerState
	if (Killer)
	{
		if (AShooterPlayerState* KillerPS = Killer->GetPlayerState<AShooterPlayerState>())
		{
			KillerPS->AddKill();
			KillerPS->KillGoal = KillGoal;

			if (KillerPS->GetKills() >= KillGoal)
			{
				TriggerVictory(KillerPS);
			}
		}
	}
}

void AShooterGameMode::TriggerVictory(AShooterPlayerState* Winner)
{
	bGameOver = true;
	Winner->MarkAsWinner();

	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}

		AShooterPlayerState* PS = PC->GetPlayerState<AShooterPlayerState>();
		if (PS && PS != Winner)
		{
			PS->MarkMatchOver();
		}

		if (AShooterCharacter* Char = Cast<AShooterCharacter>(PC->GetPawn()))
		{
			Char->DisableInput(PC);
		}
	}

	FTimerHandle RestartTimer;
	GetWorld()->GetTimerManager().SetTimer(RestartTimer, this, &AShooterGameMode::EndMatchForAll, 10.0f, false);
}

void AShooterGameMode::EndMatchForAll()
{
	GetWorld()->ServerTravel(TEXT("?Restart"), true);
}
