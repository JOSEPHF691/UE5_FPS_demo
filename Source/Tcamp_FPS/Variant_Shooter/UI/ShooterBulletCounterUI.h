	// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShooterBulletCounterUI.generated.h"

class UWidgetAnimation;

/**
 *  Simple bullet counter UI widget for a first person shooter game
 */
UCLASS(abstract)
class TCAMP_FPS_API UShooterBulletCounterUI : public UUserWidget
{
	GENERATED_BODY()
	
public:
	/** Update only the life bar value; never play the Blueprint damage animation. */
	void SetHealthPercentSilently(float LifePercent);

	/**
	 * Cancel transient hit feedback and restore its authored hidden state.
	 *
	 * The HUD is reused across tactical respawns. A lethal hit can start the
	 * damage animation immediately before spectator mode collapses this widget;
	 * without an explicit reset that animation resumes when the HUD is restored
	 * for the next round.
	 */
	void ResetTransientFeedback();

	/** Allows Blueprint to update sub-widgets with the new bullet count */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta=(DisplayName = "UpdateBulletCounter"))
	void BP_UpdateBulletCounter(int32 MagazineSize, int32 BulletCount);

	/** Allows Blueprint to update sub-widgets with magazine and reserve ammo */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta=(DisplayName = "Update Ammo Counter"))
	void BP_UpdateAmmoCounter(int32 MagazineSize, int32 BulletCount, int32 ReserveCount);

	/** Allows Blueprint to update sub-widgets with the new life total and play a damage effect on the HUD */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta=(DisplayName = "Damaged"))
	void BP_Damaged(float LifePercent);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Optional binding for the Damage animation authored by the existing HUD. */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Shooter|Animation",
		meta=(BindWidgetAnimOptional))
	TObjectPtr<UWidgetAnimation> Damage;

private:
	void HandleVisibilityChanged(ESlateVisibility NewVisibility);
};
