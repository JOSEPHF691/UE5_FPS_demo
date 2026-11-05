// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tcamp_FPSPlayerController.h"
#include "ShooterPlayerController.generated.h"

class AShooterCharacter;
class UHealthComponent;
class UShooterBulletCounterUI;
class UShooterCombatFeedbackUI;
class UShooterUI;

/**
 *  PlayerController for the shooter game. Respawns pawn on death. Manages UI.
 */
UCLASS(abstract, config="Game")
class TCAMP_FPS_API AShooterPlayerController : public ATcamp_FPSPlayerController
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category = "Shooter|Respawn")
	TSubclassOf<AShooterCharacter> CharacterClass;

	UPROPERTY(EditAnywhere, Category = "Shooter|UI")
	TSubclassOf<UShooterBulletCounterUI> BulletCounterUIClass;

	UPROPERTY(EditAnywhere, Category = "Shooter|UI")
	TSubclassOf<UShooterUI> ShooterUIClass;

	UPROPERTY()
	TObjectPtr<UShooterUI> ShooterUI;

	UPROPERTY(EditAnywhere, Category = "Shooter|Player")
	FName PlayerPawnTag = FName("Player");

	UPROPERTY()
	TObjectPtr<UShooterBulletCounterUI> BulletCounterUI;

	/** Native open crosshair, confirmed-hit marker, and gradient damage vignette. */
	UPROPERTY()
	TObjectPtr<UShooterCombatFeedbackUI> CombatFeedbackUI;

	/** Health source currently allowed to drive the reused local HUD. */
	TWeakObjectPtr<UHealthComponent> BoundHUDHealthComponent;

protected:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void AcknowledgePossession(APawn* InPawn) override;

	void BindHUDDelegates(APawn* InPawn);
	void EnsureCombatFeedbackUI();

	UFUNCTION()
	void OnPawnDestroyed(AActor* DestroyedActor);

	/** Tactical round modes respawn centrally from GameMode, not per-controller. */
	virtual bool ShouldAutoRespawnPawn() const { return true; }

	UFUNCTION()
	void OnHealthDamaged(float HealthPercent);

	UFUNCTION()
	void OnHealthHealed(float HealthPercent);

	void SynchronizeHealthHUD(float HealthPercent);

	/** Local state gate that prevents delayed cosmetic cues after camera/state changes. */
	virtual bool CanPresentDamageFeedback() const;

	UFUNCTION()
	void OnAmmoUpdated(int32 CurrentAmmo, int32 MagazineSize, int32 ReserveAmmo);

	UFUNCTION()
	void OnEnemyHit();

public:
	/** Authority-only bridge shared by hitscan and projectile damage paths. */
	void NotifyEnemyHitConfirmed();

	/** Authority-only bridge for transient damage-direction feedback. */
	void NotifyDamageReceived(const FVector& WorldDirectionToSource, float Strength = 1.0f);

protected:
	UFUNCTION(Client, Unreliable)
	void ClientNotifyEnemyHitConfirmed();

	UFUNCTION(Client, Unreliable)
	void ClientNotifyDamageReceived(
		FVector_NetQuantizeNormal WorldDirectionToSource,
		float Strength);

	void PresentDamageReceived(const FVector& WorldDirectionToSource, float Strength);

	UFUNCTION()
	void OnWeaponChanged(AShooterWeapon* NewWeapon, AShooterWeapon* OldWeapon);

	UFUNCTION()
	void OnKillsUpdated(int32 Kills, int32 KillGoal);

	UFUNCTION()
	void OnMatchOver();
};
