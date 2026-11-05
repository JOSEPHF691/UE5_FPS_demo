// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Economy/EconomyTypes.h"
#include "ArmorStatusUI.generated.h"

class UBorder;
class UProgressBar;
class UTextBlock;

/** Compact native armor bar aligned above the existing health bar. */
UCLASS()
class TCAMP_FPS_API UArmorStatusUI : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetArmorDisplay(EArmorType Type, float CurrentDurability, float MaxDurability);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	void BuildNativeWidgetTree();

	UPROPERTY()
	TObjectPtr<UBorder> ArmorPanel;

	UPROPERTY()
	TObjectPtr<UTextBlock> ArmorText;

	UPROPERTY()
	TObjectPtr<UProgressBar> ArmorBar;
};
