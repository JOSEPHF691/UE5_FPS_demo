// Copyright Epic Games, Inc. All Rights Reserved.

#include "TacticalMenuGameMode.h"

#include "TacticalGameInstance.h"

ATacticalMenuGameMode::ATacticalMenuGameMode()
{
	DefaultPawnClass = nullptr;
	bStartPlayersAsSpectators = true;
	bUseSeamlessTravel = false;
}

void ATacticalMenuGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (UTacticalGameInstance* GameInstance = Cast<UTacticalGameInstance>(GetGameInstance()))
	{
		GameInstance->ShowMultiplayerMenu(false);
	}
}
