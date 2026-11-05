// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Controller.h"
#include "TacticalDisconnectedController.generated.h"

class ATacticalPlayerState;

/**
 * Server-only stand-in that keeps a disconnected participant's PlayerState and,
 * for the rest of its current round, Pawn connected to the normal damage/death pipeline.
 */
UCLASS(NotBlueprintable, Transient)
class TCAMP_FPS_API ATacticalDisconnectedController : public AController
{
	GENERATED_BODY()

public:
	ATacticalDisconnectedController();

	void AttachPreservedPlayerState(ATacticalPlayerState* InPlayerState);
	ATacticalPlayerState* DetachPreservedPlayerState();
};
