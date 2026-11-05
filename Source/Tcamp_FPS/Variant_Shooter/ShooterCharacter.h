// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tcamp_FPSCharacter.h"
#include "ShooterWeaponHolder.h"
#include "ShooterCharacter.generated.h"

class UHealthComponent;
class UWeaponInventoryComponent;
class AShooterWeapon;
class UInputAction;
class UInputComponent;
class UPawnNoiseEmitterComponent;
class UAnimInstance;
class USkeletalMesh;

/**
 *  First-person shooter character built with components.
 *  HealthComponent handles HP/damage/death. WeaponInventoryComponent handles weapons.
 *  Still implements IShooterWeaponHolder for visual callbacks (mesh attach, montage, recoil, HUD).
 */
UCLASS(abstract)
class TCAMP_FPS_API AShooterCharacter : public ATcamp_FPSCharacter, public IShooterWeaponHolder
{
	GENERATED_BODY()

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UPawnNoiseEmitterComponent* PawnNoiseEmitter;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UHealthComponent* HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UWeaponInventoryComponent* WeaponInventory;

	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* FireAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* SwitchWeaponAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* SelectPrimaryAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* SelectSecondaryAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* ReloadAction;

	UPROPERTY(EditAnywhere, Category = "Weapons")
	FName FirstPersonWeaponSocket = FName("HandGrip_R");

	UPROPERTY(EditAnywhere, Category = "Weapons")
	FName ThirdPersonWeaponSocket = FName("HandGrip_R");

	/**
	 * Animation used while the replicated weapon reference has not arrived yet.
	 * Weapon-specific animation Blueprints read CurrentWeapon every update, so
	 * leaving one active during a legitimate no-weapon window is unsafe.
	 */
	UPROPERTY()
	TSubclassOf<UAnimInstance> SafeUnarmedThirdPersonAnimClass;

	/** Blueprint-authored first-person mesh to restore after a weapon-specific mesh. */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMesh> DefaultFirstPersonCharacterMesh;
	bool bCapturedDefaultFirstPersonCharacterMesh = false;

	UPROPERTY(EditAnywhere, Category = "Aim", meta = (ClampMin = 0, ClampMax = 100000, Units = "cm"))
	float MaxAimDistance = 10000.0f;

	UPROPERTY(EditAnywhere, Category = "Health")
	float MaxHP = 500.0f;

	UPROPERTY(EditAnywhere, Category = "Team")
	uint8 TeamByte = 0;

	UPROPERTY(EditAnywhere, Category = "Destruction", meta = (ClampMin = 0, ClampMax = 10, Units = "s"))
	float RespawnTime = 5.0f;

	FTimerHandle RespawnTimer;

public:
	AShooterCharacter();

	/** Access the health component */
	UFUNCTION(BlueprintPure, Category = "Health")
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }

	/** Access the weapon inventory */
	UFUNCTION(BlueprintPure, Category = "Weapons")
	UWeaponInventoryComponent* GetWeaponInventory() const { return WeaponInventory; }

protected:
	virtual void BeginPlay() override;
	virtual bool ShouldApplyFirstPersonViewPitch() const override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;

	/** Called when HealthComponent health changes — updates HUD */
	UFUNCTION()
	void OnHealthDamaged(float HealthPercent);

	/** Called when HealthComponent hits 0 — handles death + respawn */
	UFUNCTION()
	virtual void OnDeath();

	/** Called when weapon changes — updates anim instances */
	UFUNCTION()
	void OnWeaponChanged(AShooterWeapon* NewWeapon, AShooterWeapon* OldWeapon);

	/** Called when ammo changes — broadcasts to UI */
	UFUNCTION()
	void OnAmmoChanged(int32 CurrentAmmo, int32 MagazineSize, int32 ReserveAmmo);

	/** Route engine damage to HealthComponent */
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/**
	 * Sends one transient, owner-only damage-direction cue after authoritative
	 * damage has actually been applied. Persistent health replication remains
	 * independent so reconnecting clients never replay an old hit flash.
	 */
	void NotifyOwningPlayerOfDamage(
		const FDamageEvent& DamageEvent,
		AController* EventInstigator,
		AActor* DamageCauser,
		float AppliedDamage) const;

public:
	//~ Input handlers (gated by IsDead)
	virtual void DoAim(float Yaw, float Pitch) override;
	virtual void DoMove(float Right, float Forward) override;
	virtual void DoJumpStart() override;
	virtual void DoJumpEnd() override;

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoStartFiring();

	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoStopFiring();

	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoSwitchWeapon();

	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoSelectPrimary();

	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoSelectSecondary();

	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoReload();

	//~Begin IShooterWeaponHolder interface
	virtual void AttachWeaponMeshes(AShooterWeapon* Weapon) override;
	virtual void PlayFiringMontage(UAnimMontage* Montage) override;
	virtual void PlayReloadMontage(UAnimMontage* Montage) override;
	virtual void AddWeaponRecoil(float Recoil) override;
	virtual void UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize, int32 ReserveAmmo) override;
	virtual FVector GetWeaponTargetLocation() override;
	virtual void AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass) override;

	UFUNCTION(BlueprintCallable, Category = "Weapons", meta = (DisplayName = "Grant Weapon Class"))
	void GrantWeaponClass(TSubclassOf<AShooterWeapon> WeaponClass);

	virtual void OnWeaponActivated(AShooterWeapon* Weapon) override;
	virtual void OnWeaponDeactivated(AShooterWeapon* Weapon) override;
	virtual void OnSemiWeaponRefire() override;
	//~End IShooterWeaponHolder interface

	/** Returns true if health depleted */
	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const;

	/** Returns team byte */
	UFUNCTION(BlueprintPure, Category = "Team")
	uint8 GetTeam() const { return TeamByte; }

	/** Updates the authoritative pawn/component team after lobby selection or respawn. */
	virtual void SetTeam(uint8 NewTeam);

protected:
	void OnRespawn();
};
