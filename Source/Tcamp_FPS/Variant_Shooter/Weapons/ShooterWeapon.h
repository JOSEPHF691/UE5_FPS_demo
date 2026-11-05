// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TacticalBallisticRules.h"
#include "GameFramework/Actor.h"
#include "ShooterWeaponHolder.h"
#include "Animation/AnimInstance.h"
#include "ShooterWeapon.generated.h"

/** Lightweight replicated hit data for hitscan weapons. No actor/component pointers. */
USTRUCT(BlueprintType)
struct FHitscanHitData
{
	GENERATED_BODY()

	/** Impact point in world space */
	UPROPERTY(BlueprintReadOnly)
	FVector_NetQuantize ImpactPoint;

	/** Surface normal at impact point */
	UPROPERTY(BlueprintReadOnly)
	FVector_NetQuantizeNormal ImpactNormal;

	/** 0 = world geometry, 1 = character */
	UPROPERTY(BlueprintReadOnly)
	uint8 HitType = 0;

	/** Bone name for headshot detection on characters (valid when HitType == 1) */
	UPROPERTY(BlueprintReadOnly)
	FName BoneName;
};

/** Broadcast on the owning client when a hit lands on an enemy character */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEnemyHit);

class IShooterWeaponHolder;
class AShooterProjectile;
class USkeletalMeshComponent;
class USkeletalMesh;
class UStaticMesh;
class UAnimMontage;
class UAnimInstance;

/**
 *  Base class for a simple first person shooter weapon
 *  Provides both first person and third person perspective meshes
 *  Handles ammo and firing logic
 *  Interacts with the weapon owner through the ShooterWeaponHolder interface
 */
UCLASS(abstract)
class TCAMP_FPS_API AShooterWeapon : public AActor
{
	GENERATED_BODY()
	
	/** First person perspective mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

	/** Third person perspective mesh */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* ThirdPersonMesh;

protected:

	/** Cast pointer to the weapon owner */
	IShooterWeaponHolder* WeaponOwner;

	/** Type of projectiles this weapon will shoot */
	UPROPERTY(EditAnywhere, Category="Ammo")
	TSubclassOf<AShooterProjectile> ProjectileClass;

	/** Number of bullets in a magazine */
	UPROPERTY(EditAnywhere, Category="Ammo", meta = (ClampMin = 0, ClampMax = 100))
	int32 MagazineSize = 10;

	/** Number of bullets in the current magazine */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentBullets)
	int32 CurrentBullets = 0;

	/** Total bullets available for reloading */
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_ReserveBullets, Category="Ammo", meta = (ClampMin = 0, ClampMax = 1000))
	int32 ReserveBullets = 30;

	/** Maximum bullets this weapon can hold in reserve */
	UPROPERTY(EditAnywhere, Category="Ammo", meta = (ClampMin = 0, ClampMax = 1000))
	int32 MaxReserveBullets = 30;
	
	/** Animation montage to play when firing this weapon */
	UPROPERTY(EditAnywhere, Category="Animation")
	UAnimMontage* FiringMontage;

	/** Animation montage to play when reloading this weapon */
	UPROPERTY(EditAnywhere, Category="Animation")
	UAnimMontage* ReloadMontage;

	/** AnimInstance class to set for the first person character mesh when this weapon is active */
	UPROPERTY(EditAnywhere, Category="Animation")
	TSubclassOf<UAnimInstance> FirstPersonAnimInstanceClass;

	/** AnimInstance class to set for the third person character mesh when this weapon is active */
	UPROPERTY(EditAnywhere, Category="Animation")
	TSubclassOf<UAnimInstance> ThirdPersonAnimInstanceClass;

	/** Optional first-person character mesh for a weapon-authored animation set. Null keeps the character's default mesh. */
	UPROPERTY(EditDefaultsOnly, Category="Animation|First Person")
	TObjectPtr<USkeletalMesh> FirstPersonCharacterMeshOverride = nullptr;

	/** Optional first-person gun mesh, independent of the world-space presentation. */
	UPROPERTY(EditDefaultsOnly, Category="Animation|First Person")
	TObjectPtr<USkeletalMesh> FirstPersonWeaponMeshOverride = nullptr;

	/** Optional world-space gun mesh; can match the first-person gun while using its own mount and projection. */
	UPROPERTY(EditDefaultsOnly, Category="Animation|Third Person")
	TObjectPtr<USkeletalMesh> ThirdPersonWeaponMeshOverride = nullptr;

	/** First-person character montages. Null retains the original shared montage. */
	UPROPERTY(EditDefaultsOnly, Category="Animation|First Person")
	TObjectPtr<UAnimMontage> FirstPersonFiringMontage = nullptr;

	UPROPERTY(EditDefaultsOnly, Category="Animation|First Person")
	TObjectPtr<UAnimMontage> FirstPersonReloadMontage = nullptr;

	/** Third-person character montages. Null retains the original shared montage. */
	UPROPERTY(EditDefaultsOnly, Category="Animation|Third Person")
	TObjectPtr<UAnimMontage> ThirdPersonFiringMontage = nullptr;

	UPROPERTY(EditDefaultsOnly, Category="Animation|Third Person")
	TObjectPtr<UAnimMontage> ThirdPersonReloadMontage = nullptr;

	/** Optional gun-body animation graph and matching first-person action montages. */
	UPROPERTY(EditDefaultsOnly, Category="Animation|First Person Weapon")
	TSubclassOf<UAnimInstance> FirstPersonWeaponAnimInstanceClass;

	UPROPERTY(EditDefaultsOnly, Category="Animation|First Person Weapon")
	TObjectPtr<UAnimMontage> FirstPersonWeaponFiringMontage = nullptr;

	UPROPERTY(EditDefaultsOnly, Category="Animation|First Person Weapon")
	TObjectPtr<UAnimMontage> FirstPersonWeaponReloadMontage = nullptr;

	/** Cone half-angle for variance while aiming */
	UPROPERTY(EditAnywhere, Category="Aim", meta = (ClampMin = 0, ClampMax = 90, Units = "Degrees"))
	float AimVariance = 0.0f;

	/** Amount of firing recoil to apply to the owner */
	UPROPERTY(EditAnywhere, Category="Aim", meta = (ClampMin = 0, ClampMax = 100))
	float FiringRecoil = 0.0f;

	/** Cosmetic muzzle socket used by first-/third-person effects and tracers. */
	UPROPERTY(EditAnywhere, Category="Aim")
	FName MuzzleSocketName = FName(TEXT("Muzzle"));

	/** Stable gameplay muzzle relative to the server player viewpoint (X forward,
	 *  Y right, Z up). This does not depend on viewmodel or world-mesh bone ticking. */
	UPROPERTY(EditDefaultsOnly, Category="Aim|Gameplay Muzzle")
	FVector GameplayMuzzleViewOffset = FVector(35.0f, 10.0f, -18.0f);

	/** Optional correction applied after the first-person weapon is snapped to the hand socket. */
	UPROPERTY(EditAnywhere, Category="Attachment")
	FTransform FirstPersonAttachmentOffset = FTransform::Identity;

	/** Optional holder socket for an authored first-person mesh/weapon pair. */
	UPROPERTY(EditDefaultsOnly, Category="Attachment|First Person")
	FName FirstPersonHolderSocketOverride = NAME_None;

	/** Enables a weapon-authored first-person support-hand target. */
	UPROPERTY(EditAnywhere, Category="Attachment|First Person Grip")
	bool bUseFirstPersonLeftHandGrip = false;

	/**
	 * Optional weapon-mesh socket used as the support-hand anchor. When set,
	 * FirstPersonLeftHandGrip is interpreted as an offset from this socket.
	 */
	UPROPERTY(EditAnywhere, Category="Attachment|First Person Grip",
		meta = (EditCondition = "bUseFirstPersonLeftHandGrip"))
	FName FirstPersonLeftHandGripSocketName = NAME_None;

	/**
	 * Support-hand transform. It is weapon-component local when no socket is
	 * selected, otherwise it is relative to FirstPersonLeftHandGripSocketName.
	 */
	UPROPERTY(EditAnywhere, Category="Attachment|First Person Grip",
		meta = (EditCondition = "bUseFirstPersonLeftHandGrip"))
	FTransform FirstPersonLeftHandGrip = FTransform::Identity;

	/** Optional correction applied after the third-person weapon is snapped to the hand socket. */
	UPROPERTY(EditAnywhere, Category="Attachment")
	FTransform ThirdPersonAttachmentOffset = FTransform::Identity;

	/** Distance ahead of the gameplay muzzle that bullets spawn at. Collision is
	 *  checked before this offset is applied so it cannot skip a thin wall. */
	UPROPERTY(EditAnywhere, Category="Aim", meta = (ClampMin = 0, ClampMax = 1000, Units = "cm"))
	float MuzzleOffset = 10.0f;

	/** If true, this weapon will automatically fire at the refire rate */
	UPROPERTY(EditAnywhere, Category="Refire")
	bool bFullAuto = false;

	/** Time between shots for this weapon. Affects both full auto and semi auto modes */
	UPROPERTY(EditAnywhere, Category="Refire", meta = (ClampMin = 0.01, ClampMax = 5, Units = "s"))
	float RefireRate = 0.5f;

	/** Game time of last shot fired, used to enforce refire rate on semi auto */
	float TimeOfLastShot = 0.0f;

	/** If true, the weapon is currently firing */
	bool bIsFiring = false;

	/** Timer to handle full auto refiring */
	FTimerHandle RefireTimer;

	/** Cast pawn pointer to the owner for AI perception system interactions */
	TObjectPtr<APawn> PawnOwner;

	/** Loudness of the shot for AI perception system interactions */
	UPROPERTY(EditAnywhere, Category="Perception", meta = (ClampMin = 0, ClampMax = 100))
	float ShotLoudness = 1.0f;

	/** Max range of shot AI perception noise */
	UPROPERTY(EditAnywhere, Category="Perception", meta = (ClampMin = 0, ClampMax = 100000, Units = "cm"))
	float ShotNoiseRange = 3000.0f;

	/** Tag to apply to noise generated by shooting this weapon */
	UPROPERTY(EditAnywhere, Category="Perception")
	FName ShotNoiseTag = FName("Shot");

	/** Static mesh used when this weapon is dropped as a world pickup. */
	UPROPERTY(EditAnywhere, Category="Pickup")
	TObjectPtr<UStaticMesh> DropPickupMesh = nullptr;

	// ---- Hitscan ----

	/** If true, this weapon fires instant line traces instead of spawning projectiles */
	UPROPERTY(EditAnywhere, Category="Hitscan")
	bool bUseHitscan = false;

	/** Physics impulse applied to simulated bodies on hitscan impact */
	UPROPERTY(EditAnywhere, Category="Hitscan", meta = (ClampMin = 0, ClampMax = 50000))
	float HitscanPhysicsForce = 100.0f;

	/** Cosmetic frequency; 1 shows each hit-scan shot, 2 shows every other shot. */
	UPROPERTY(EditDefaultsOnly, Category="Hitscan|Tracer", meta = (ClampMin = 1, ClampMax = 10))
	int32 TracerEveryNthShot = 1;

public:	

	/** Constructor */
	AShooterWeapon();

protected:
	
	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Apply authored gun meshes before any subclass inspects sockets or attaches them. */
	void ApplyPresentationMeshOverrides();

	/** Gameplay Cleanup */
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

protected:

	/** Called when the weapon's owner is destroyed */
	UFUNCTION()
	void OnOwnerDestroyed(AActor* DestroyedActor);

public:

	/** Activates this weapon and gets it ready to fire */
	void ActivateWeapon();

	/** Deactivates this weapon */
	void DeactivateWeapon();

	/** Start firing this weapon */
	void StartFiring(uint16 PredictionBurstId = 0);

	/** Stop firing this weapon */
	void StopFiring();

	/** Reload from reserve ammo into the current magazine */
	UFUNCTION(BlueprintCallable, Category="Ammo")
	void Reload();

	/** Effective first-person actions used by existing presentation readers. */
	UAnimMontage* GetReloadMontage() const { return GetFirstPersonReloadMontage(); }
	UAnimMontage* GetFiringMontage() const { return GetFirstPersonFiringMontage(); }

	/** Native reload entry point for holders whose visible animation is longer than the weapon montage. */
	void ReloadWithMinimumDuration(float MinimumDuration);

protected:

	/** Fire the weapon */
	virtual void Fire();

	/** Fire once using a supplied target location. */
	virtual void FireAtTarget(const FVector& TargetLocation);

	/** Called when the refire rate time has passed while shooting semi auto weapons */
	void FireCooldownExpired();

	/** Fire a projectile towards the target location */
	virtual void FireProjectile(const FVector& TargetLocation);

	/** For hitscan weapons: perform an instant line trace with penetration */
	virtual void PerformHitscan(const FVector& TargetLocation);

	/** Configure a freshly-spawned projectile — override to set damage/penetration */
	virtual void ConfigureProjectile(class AShooterProjectile* Projectile) {}

	/** Populate hitscan damage/penetration values — override in subclass for DataTable-driven stats */
	virtual void ConfigureHitscanParams(float& OutDamage, int32& OutPenetrations, float& OutFalloff) {}

	/** Gameplay modes may provide virtual reserve ammunition without mutating replicated ammo state. */
	virtual bool HasInfiniteReserveAmmo() const { return false; }

	/** Multicast one accepted shot; prediction IDs only suppress the owner's cosmetic echo. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFireEffects(uint16 PredictionBurstId, uint16 BurstShotIndex);

	/** Multicast reload montage to all clients */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastReloadEffects();

	/** One transient multicast carries both impact FX and the bounded visual path. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastHitscanResult(const FVector_NetQuantize& TraceStart,
		const FVector_NetQuantize& VisualEnd, uint16 ShotId,
		const TArray<FHitscanHitData>& Hits);

	/** Notify the owning client that a hit landed on an enemy (crosshair feedback) */
	UFUNCTION(Client, Unreliable)
	void ClientNotifyEnemyHit();

	/** Called when CurrentBullets replicates */
	UFUNCTION()
	void OnRep_CurrentBullets();

	/** Called when ReserveBullets replicates */
	UFUNCTION()
	void OnRep_ReserveBullets();

	/** Pushes the current ammo state to the weapon owner HUD */
	void NotifyAmmoChanged() const;

	/** Calculates the spawn transform for projectiles shot by this weapon */
	FTransform CalculateProjectileSpawnTransform(const FVector& TargetLocation,
		bool& bOutNearWallBlocked) const;

	/** Returns the canonical gameplay muzzle, independent of the shooter's local view. */
	FVector GetMuzzleLocation() const;
	void GetGameplayViewPoint(FVector& OutLocation, FRotator& OutRotation) const;

	/** Tests eye-to-muzzle and muzzle-to-offset segments before selecting the
	 *  authoritative hitscan or projectile launch path. */
	TacticalBallisticRules::FShotPath ResolveGameplayShotPath(
		const FVector& TargetLocation, FHitResult& OutObstruction) const;

public:
	/** Stable server gameplay muzzle for arena damage-boundary validation. */
	FVector GetGameplayMuzzleLocation() const { return GetMuzzleLocation(); }

	/** Returns the first person mesh */
	UFUNCTION(BlueprintPure, Category="Weapon")
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; };

	/** Returns the third person mesh */
	UFUNCTION(BlueprintPure, Category="Weapon")
	USkeletalMeshComponent* GetThirdPersonMesh() const { return ThirdPersonMesh; };

	/** Returns the first person anim instance class */
	const TSubclassOf<UAnimInstance>& GetFirstPersonAnimInstanceClass() const;

	/** Returns the third person anim instance class */
	const TSubclassOf<UAnimInstance>& GetThirdPersonAnimInstanceClass() const;

	/** Weapon-specific authored assets are optional so existing weapons keep their current presentation. */
	USkeletalMesh* GetFirstPersonCharacterMeshOverride() const
	{
		return FirstPersonCharacterMeshOverride.Get();
	}
	USkeletalMesh* GetFirstPersonWeaponMeshOverride() const
	{
		return FirstPersonWeaponMeshOverride.Get();
	}
	USkeletalMesh* GetThirdPersonWeaponMeshOverride() const
	{
		return ThirdPersonWeaponMeshOverride.Get();
	}
	FName GetFirstPersonHolderSocketOverride() const
	{
		return FirstPersonHolderSocketOverride;
	}
	UAnimMontage* GetFirstPersonFiringMontage() const
	{
		return FirstPersonFiringMontage ? FirstPersonFiringMontage.Get() : FiringMontage;
	}
	UAnimMontage* GetFirstPersonReloadMontage() const
	{
		return FirstPersonReloadMontage ? FirstPersonReloadMontage.Get() : ReloadMontage;
	}
	UAnimMontage* GetThirdPersonFiringMontage() const
	{
		return ThirdPersonFiringMontage ? ThirdPersonFiringMontage.Get() : FiringMontage;
	}
	UAnimMontage* GetThirdPersonReloadMontage() const
	{
		return ThirdPersonReloadMontage ? ThirdPersonReloadMontage.Get() : ReloadMontage;
	}
	UAnimMontage* GetFirstPersonWeaponFiringMontage() const
	{
		return FirstPersonWeaponFiringMontage.Get();
	}
	UAnimMontage* GetFirstPersonWeaponReloadMontage() const
	{
		return FirstPersonWeaponReloadMontage.Get();
	}
	TSubclassOf<UAnimInstance> GetFirstPersonWeaponAnimInstanceClass() const;

	/** Name of the first-person muzzle socket used by both gameplay and presentation. */
	FName GetMuzzleSocketName() const
	{
		return MuzzleSocketName.IsNone() ? FName(TEXT("Muzzle")) : MuzzleSocketName;
	}

	/**
	 * Resolves the first-person left palm contact in weapon-component space.
	 * The pose solver aligns Manny's HandGrip_L socket to this transform.
	 */
	virtual bool TryGetFirstPersonLeftHandGripTransform(
		FTransform& OutWeaponSpaceTransform) const;

	/** Returns the magazine size */
	int32 GetMagazineSize() const { return MagazineSize; };

	/** Returns the effective aim variance (may be reduced when ADSing) */
	virtual float GetEffectiveAimVariance() const { return AimVariance; }

	/** Returns the current bullet count */
	int32 GetBulletCount() const { return CurrentBullets; }
	bool IsFullAuto() const { return bFullAuto; }
	float GetRefireInterval() const;

	/** Blueprint hook for hitscan tracer/impact effects */
	UFUNCTION(BlueprintImplementableEvent, Category = "Hitscan", meta = (DisplayName = "On Hitscan Hit"))
	void BP_OnHitscanHit(const FHitscanHitData& HitData);

	/** Broadcast on the owning client when a hit lands on an enemy — bind in HUD for crosshair feedback */
	UPROPERTY(BlueprintAssignable, Category = "Combat")
	FOnEnemyHit OnEnemyHit;

	/** Returns the current reserve bullet count */
	int32 GetReserveBulletCount() const { return ReserveBullets; }

	/** Returns the maximum reserve bullet count */
	int32 GetMaxReserveBulletCount() const { return MaxReserveBullets; }

	/** True while the authoritative reload action is waiting to complete. */
	bool IsReloadInProgress() const { return bReloadInProgress; }

	/** Returns weapon tier (overridden by ATacticalWeapon). 0 = default. */
	virtual int32 GetWeaponTier() const { return 0; }

	/** Returns weapon category (overridden by ATacticalWeapon). 0 = pistol. */
	virtual int32 GetWeaponCategory() const { return 0; }

	/** Returns the static mesh used by dropped weapon pickups. */
	UStaticMesh* GetDropPickupMesh() const { return DropPickupMesh; }

	/** Shared helper: attaches a weapon's dual meshes to a holder character */
	static void AttachWeaponToHolder(AShooterWeapon* Weapon, AActor* Holder,
		USkeletalMeshComponent* HolderMesh1P, USkeletalMeshComponent* HolderMesh3P,
		FName Socket1P, FName Socket3P);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	/** Server-owned action state. */
	UPROPERTY(Transient)
	bool bReloadInProgress = false;

	/** Dedicated timer so fire cooldown and reload completion cannot cancel each other. */
	FTimerHandle ReloadCompletionTimer;

	/** Prevent a bad weapon asset from logging a missing muzzle warning every shot. */

	/** Server-only identity for deterministic cosmetic frequency and future owner prediction. */
	uint16 ServerHitscanShotId = 0;

	/** Current server trigger burst; never used to authorize shots or choose aim. */
	uint16 ActivePredictionBurstId = 0;
	uint16 ServerBurstShotIndex = 0;

	/** Original component scales captured before the first holder attachment. */
	FVector AttachmentBaseScale1P = FVector::OneVector;
	FVector AttachmentBaseScale3P = FVector::OneVector;
	bool bAttachmentBaseScalesCached = false;

	/** Preserve the Blueprint-authored gun AnimBP when remote meshes stop animating until observed. */
	UPROPERTY(Transient)
	TSubclassOf<UAnimInstance> CachedFirstPersonWeaponAnimInstanceClass;

	/** Transfer ammunition only after the reload montage duration has elapsed. */
	void CompleteReload();

	/** Abort an unfinished reload without granting ammunition. */
	void CancelReload();

	/** Choose the gun mesh actually rendered to this machine, including first-person spectating. */
	bool TryGetLocalTracerMuzzle(FVector& OutMuzzle, bool& bOutFirstPerson) const;

	/** Spawn a transient, non-replicated visual. Never affects hit detection. */
	void SpawnLocalHitscanTracer(const FVector& TraceStart, const FVector& VisualEnd,
		uint16 ShotId);
};
