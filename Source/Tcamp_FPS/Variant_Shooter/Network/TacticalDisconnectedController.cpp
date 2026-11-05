// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/TacticalDisconnectedController.h"
#include "TacticalPlayerState.h"

ATacticalDisconnectedController::ATacticalDisconnectedController()
{
	PrimaryActorTick.bCanEverTick = false;
	// The proxy exists only to keep server ownership/death attribution coherent.
	// Replicating it would expose a controller that has no owning connection and
	// make remote camera code read its non-replicated ControlRotation as zero.
	bReplicates = false;
}

void ATacticalDisconnectedController::AttachPreservedPlayerState(
	ATacticalPlayerState* InPlayerState)
{
	SetPlayerState(InPlayerState);
	if (InPlayerState)
	{
		InPlayerState->SetOwner(this);
	}
}

ATacticalPlayerState* ATacticalDisconnectedController::DetachPreservedPlayerState()
{
	ATacticalPlayerState* PreservedState = GetPlayerState<ATacticalPlayerState>();
	SetPlayerState(nullptr);
	return PreservedState;
}
