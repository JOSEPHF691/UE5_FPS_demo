// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ReadyLobbyUI.generated.h"

class UTextBlock;
class UBorder;

/** Small native lobby panel used to ready up before the tactical match starts. */
UCLASS()
class TCAMP_FPS_API UReadyLobbyUI : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetReadyDisplay(bool bLocalPlayerReady, int32 ReadyPlayers, int32 SelectedPlayers,
		int32 LobbyPlayers, int32 RedPlayers, int32 BluePlayers, uint8 TeamByte,
		bool bLobbyCandidate, bool bShowReadyPrompt);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	void BuildNativeWidgetTree();

	UPROPERTY()
	TObjectPtr<UTextBlock> ReadyStatusText;

	UPROPERTY()
	TObjectPtr<UBorder> TeamBadge;

	UPROPERTY()
	TObjectPtr<UTextBlock> TeamBadgeText;
};
