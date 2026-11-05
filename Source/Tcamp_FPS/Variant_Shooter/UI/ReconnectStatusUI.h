// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ReconnectStatusUI.generated.h"

class UButton;
class UHorizontalBox;
class UTextBlock;
class UTacticalGameInstance;

/**
 * Native, full-screen reconnect presentation owned by UTacticalGameInstance.
 *
 * The root consumes hit tests across the viewport. Gameplay input is locked by
 * the game instance as a separate safety boundary, so losing widget focus can
 * never resume movement or weapon input during recovery.
 */
UCLASS()
class TCAMP_FPS_API UReconnectStatusUI final : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeReconnectOwner(UTacticalGameInstance* InOwner);

	/** Show a queued automatic attempt, including its bounded-backoff delay. */
	void ShowAutomaticRetry(int32 NextAttempt, int32 MaxAttempts, float DelaySeconds);

	/** Show an attempt whose ClientTravel request has been issued. */
	void ShowAttemptInProgress(int32 CurrentAttempt, int32 MaxAttempts);

	/** Transport connected; keep blocking until gameplay admission is confirmed. */
	void ShowAwaitingServerConfirmation(int32 CurrentAttempt, int32 MaxAttempts);

	/** Automatic recovery stopped. Expose deliberate manual retry/leave actions. */
	void ShowReconnectFailure(const FText& FailureReason, int32 AttemptsMade, int32 MaxAttempts);

	void HideReconnectOverlay();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

private:
	void BuildNativeWidgetTree();
	void SetOverlayVisible(bool bVisible);
	void SetActionsVisible(bool bVisible);

	UFUNCTION()
	void HandleManualRetryClicked();

	UFUNCTION()
	void HandleLeaveMatchClicked();

	TWeakObjectPtr<UTacticalGameInstance> ReconnectOwner;

	UPROPERTY()
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY()
	TObjectPtr<UTextBlock> ServerStatusText;

	UPROPERTY()
	TObjectPtr<UTextBlock> AttemptText;

	UPROPERTY()
	TObjectPtr<UTextBlock> DetailText;

	UPROPERTY()
	TObjectPtr<UHorizontalBox> ActionRow;

	UPROPERTY()
	TObjectPtr<UButton> ManualRetryButton;

	UPROPERTY()
	TObjectPtr<UButton> LeaveMatchButton;
};

