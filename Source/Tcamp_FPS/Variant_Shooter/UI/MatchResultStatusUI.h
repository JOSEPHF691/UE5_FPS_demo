// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Economy/EconomyTypes.h"
#include "MatchResultStatusUI.generated.h"

class UBorder;
class UTextBlock;

/** Native additive banner for special match endings that the legacy HUD cannot label. */
UCLASS(NotBlueprintable)
class TCAMP_FPS_API UMatchResultStatusUI final : public UUserWidget
{
	GENERATED_BODY()

public:
	void ShowSpecialResult(uint8 WinningTeam, EMatchEndReason Reason,
		int32 TeamAScore, int32 TeamBScore);
	void HideResult();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	void BuildNativeWidgetTree();

	UPROPERTY()
	TObjectPtr<UBorder> ResultPanel;

	UPROPERTY()
	TObjectPtr<UTextBlock> ResultTitleText;

	UPROPERTY()
	TObjectPtr<UTextBlock> ResultDetailText;
};
