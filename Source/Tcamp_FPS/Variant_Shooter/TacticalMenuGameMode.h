// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TacticalMenuGameMode.generated.h"

/** Lightweight native game mode for the frontend world. */
UCLASS()
class TCAMP_FPS_API ATacticalMenuGameMode final : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATacticalMenuGameMode();

protected:
	virtual void BeginPlay() override;
};

