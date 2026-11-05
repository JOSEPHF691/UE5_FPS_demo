// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "ShooterGameState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnShooterKillEvent, AController*, Killer, AController*, Victim);

/**
 *  Lightweight GameState for the shooter variant.
 *  Serves as the event hub — characters fire kill events here, GameMode subscribes.
 *  Removes the need for characters to Cast<ShooterGameMode>.
 */
UCLASS()
class TCAMP_FPS_API AShooterGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	/** Fired when any character dies. GameMode subscribes for scoring. */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnShooterKillEvent OnKillEvent;
};
