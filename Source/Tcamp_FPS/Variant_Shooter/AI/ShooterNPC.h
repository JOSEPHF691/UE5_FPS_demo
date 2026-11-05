// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tcamp_FPSCharacter.h"
#include "ShooterWeaponHolder.h"
#include "ShooterNPC.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPawnDeathDelegate);

class AShooterWeapon;
class ATreasureChest;
class UHealthComponent;
class UAnimMontage;
class UAnimSequenceBase;
class UAnimationAsset;

/**
 *  AI-controlled NPC. Uses HealthComponent for damage/death.
 *  Holds a single weapon. Behavior driven by StateTree in AIController.
 */
UCLASS(abstract)
class TCAMP_FPS_API AShooterNPC : public ATcamp_FPSCharacter, public IShooterWeaponHolder
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UHealthComponent* HealthComponent;

protected:
	UPROPERTY(EditAnywhere, Category = "Damage")
	FName RagdollCollisionProfile = FName("Ragdoll");

	UPROPERTY(EditAnywhere, Category = "Damage")
	FName HeadBoneName = FName("head");

	UPROPERTY(EditAnywhere, Category = "Damage")
	float DeferredDestructionTime = 5.0f;

	UPROPERTY(EditAnywhere, Category = "Team")
	uint8 TeamByte = 1;

	UPROPERTY(EditAnywhere, Category = "Weapon")
	TSubclassOf<AShooterWeapon> WeaponClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons")
	FName FirstPersonWeaponSocket = FName("HandGrip_R");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons")
	FName ThirdPersonWeaponSocket = FName("HandGrip_R");

	/** Use a socket on the character mesh as the authoritative shot origin. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapons|Integrated Visual")
	bool bUseCharacterMuzzle = false;

	/** Socket on the character's animated mesh used by integrated weapon visuals. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapons|Integrated Visual",
		meta = (EditCondition = "bUseCharacterMuzzle"))
	FName CharacterMuzzleSocket = FName("Muzzle");

	/** Keep the weapon actor for gameplay but hide both of its rendered meshes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapons|Integrated Visual")
	bool bHideAttachedWeaponMeshes = false;

	/** Character-skeleton montage used instead of the weapon's player montage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> NPCFiringMontage;

	/** Optional character-skeleton reload montage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> NPCReloadMontage;

	/** Optional reload sequence played as a dynamic montage when no authored montage exists. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequenceBase> NPCReloadAnimation;

	/** Slot used for NPCReloadAnimation. Must exist in the NPC AnimBP. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	FName NPCAnimationSlotName = FName("UpperBody");

	/** Whether death should enter physics simulation. Disable for incomplete PhysicsAssets. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Death")
	bool bRagdollOnDeath = true;

	/** Non-looping animation used when ragdoll is disabled. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Death",
		meta = (EditCondition = "!bRagdollOnDeath"))
	TObjectPtr<UAnimSequenceBase> NPCDeathAnimation;

	UPROPERTY(EditAnywhere, Category = "Aim")
	float AimRange = 10000.0f;

	UPROPERTY(EditAnywhere, Category = "Aim")
	float AimVarianceHalfAngle = 10.0f;

	UPROPERTY(EditAnywhere, Category = "Aim")
	float MinAimOffsetZ = -35.0f;

	UPROPERTY(EditAnywhere, Category = "Aim")
	float MaxAimOffsetZ = -60.0f;

	TWeakObjectPtr<AActor> CurrentAimTarget;
	bool bIsShooting = false;

	FTimerHandle DeathTimer;
	mutable bool bReportedMissingCharacterMuzzle = false;

	/** Equipped weapon */
	UPROPERTY()
	TObjectPtr<AShooterWeapon> Weapon;

public:
	AShooterNPC();
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Cosmetic-only impact after server HP resolution. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastResolvedBodyHit(uint8 EffectFlags, FVector_NetQuantize ImpactPoint,
		FVector_NetQuantizeNormal ImpactNormal, FName BoneName);

	FPawnDeathDelegate OnPawnDeath;

	/** Chest this NPC is guarding. Set by TreasureChest at spawn, read by TacticalGameMode on death. */
	UPROPERTY()
	TObjectPtr<ATreasureChest> GuardedChest;

	/** Individual navigable guard post used when this guard returns home. */
	FVector GuardHomeLocation = FVector::ZeroVector;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void OnDeath();

	void DeferredDestruction();

public:
	//~Begin IShooterWeaponHolder interface
	virtual void AttachWeaponMeshes(AShooterWeapon* InWeapon) override;
	virtual void PlayFiringMontage(UAnimMontage* Montage) override;
	virtual void PlayReloadMontage(UAnimMontage* Montage) override;
	virtual void AddWeaponRecoil(float Recoil) override;
	virtual void UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize, int32 ReserveAmmo) override;
	virtual FVector GetWeaponTargetLocation() override;
	virtual bool TryGetWeaponMuzzleTransform(FTransform& OutTransform) const override;
	virtual void AddWeaponClass(const TSubclassOf<AShooterWeapon>& InWeaponClass) override;
	virtual void OnWeaponActivated(AShooterWeapon* InWeapon) override;
	virtual void OnWeaponDeactivated(AShooterWeapon* InWeapon) override;
	virtual void OnSemiWeaponRefire() override;
	//~End IShooterWeaponHolder interface

	void StartShooting(AActor* ActorToShoot);
	void StopShooting();
	bool IsWeaponMagazineEmpty() const;
	bool CanReloadWeapon() const;
	bool IsWeaponReloading() const;
	bool RequestWeaponReload();

	/** Access the health component */
	UFUNCTION(BlueprintPure, Category = "Health")
	UHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "Team")
	uint8 GetTeamByte() const { return TeamByte; }

private:
	void ApplyWeaponVisualPolicy(AShooterWeapon* InWeapon) const;
	bool IsAnimationCompatible(const UAnimationAsset* Animation) const;
};
