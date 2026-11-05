// Copyright Epic Games, Inc. All Rights Reserved.

#include "TacticalSpectatorAnimationController.h"

#include "GameFramework/Pawn.h"

ATacticalSpectatorAnimationController::ATacticalSpectatorAnimationController()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
}

void ATacticalSpectatorAnimationController::SetAnimationSourcePawn(APawn* InPawn)
{
	AnimationSourcePawn = InPawn;
}

FRotator ATacticalSpectatorAnimationController::GetControlRotation() const
{
	if (const APawn* SourcePawn = AnimationSourcePawn.Get())
	{
		FRotator Result = SourcePawn->GetActorRotation();
		Result.Pitch = FRotator::NormalizeAxis(
			FRotator::DecompressAxisFromShort(SourcePawn->GetRemoteViewPitch()));
		Result.Roll = 0.0f;
		return Result;
	}

	return Super::GetControlRotation();
}

