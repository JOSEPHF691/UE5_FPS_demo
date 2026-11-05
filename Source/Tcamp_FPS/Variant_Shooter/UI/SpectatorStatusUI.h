// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/SpectatorStatusTypes.h"
#include "SpectatorStatusUI.generated.h"

class UBorder;
class UProgressBar;
class UTextBlock;
class UVerticalBox;

/** Native-only bottom-center panel describing the teammate currently observed. */
UCLASS()
class TCAMP_FPS_API USpectatorStatusUI : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Show the complete, authoritative state for the current teammate. */
	void SetSpectatorDisplay(const FSpectatorHudSnapshot& Snapshot);

	/** Show immediately after a view-target switch while waiting for fresh data. */
	void ShowPending(const FString& TargetName, uint8 TeamByte, int32 TargetIndex, int32 TargetCount);

	/** Keep an explicit spectator message visible when no living teammate exists. */
	void ShowNoTarget();

	/** Hide the whole panel when normal local-player HUD presentation resumes. */
	void ClearSpectatorDisplay();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	void BuildNativeWidgetTree();
	void ApplyTargetIdentity(const FString& TargetName, int32 TargetPlayerId, uint8 TeamByte,
		int32 TargetIndex, int32 TargetCount);

	UPROPERTY()
	TObjectPtr<UBorder> SpectatorPanel;

	UPROPERTY()
	TObjectPtr<UTextBlock> SpectatingTitleText;

	UPROPERTY()
	TObjectPtr<UBorder> TeamBadge;

	UPROPERTY()
	TObjectPtr<UTextBlock> TeamBadgeText;

	UPROPERTY()
	TObjectPtr<UTextBlock> TargetNameText;

	UPROPERTY()
	TObjectPtr<UTextBlock> TargetCountText;

	UPROPERTY()
	TObjectPtr<UVerticalBox> DetailPanel;

	UPROPERTY()
	TObjectPtr<UProgressBar> HealthBar;

	UPROPERTY()
	TObjectPtr<UTextBlock> HealthText;

	UPROPERTY()
	TObjectPtr<UProgressBar> ArmorBar;

	UPROPERTY()
	TObjectPtr<UTextBlock> ArmorText;

	UPROPERTY()
	TObjectPtr<UTextBlock> WeaponText;

	UPROPERTY()
	TObjectPtr<UTextBlock> AmmoText;
};

