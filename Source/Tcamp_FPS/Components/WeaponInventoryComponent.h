// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "Variant_Shooter/Economy/EconomyTypes.h"
#include "WeaponInventoryComponent.generated.h"

class AShooterWeapon;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnWeaponChanged, AShooterWeapon*, NewWeapon, AShooterWeapon*, OldWeapon);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnAmmoUpdated, int32, CurrentAmmo, int32, MagazineSize, int32, ReserveAmmo);

/**
 *  Manages weapon ownership and switching. Optional slot system for tactical modes.
 *  Decoupled from visual/feedback logic — the owning character handles that via delegates.
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class TCAMP_FPS_API UWeaponInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWeaponInventoryComponent();

	// ---- Slot Configuration (optional, for tactical mode) ----

	/** If true, enforces slot categories (1 per slot). False = free cycling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	bool bUseSlotSystem = false;

	/** Max weapon count when not using slots */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory", meta = (ClampMin = 1, ClampMax = 10))
	int32 MaxWeapons = 5;

	// ---- State ----

	UPROPERTY(ReplicatedUsing = OnRep_OwnedWeapons, BlueprintReadOnly, Category = "Inventory")
	TArray<AShooterWeapon*> OwnedWeapons;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeapon, BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<AShooterWeapon> CurrentWeapon;

	/** Gate set by owner character — cleared on death or freeze */
	bool bCanFire = true;

	// ---- Delegates ----

	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FOnWeaponChanged OnWeaponChanged;

	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FOnAmmoUpdated OnAmmoUpdated;

	// ---- API ----

	/** Add a weapon of the given class. Returns the spawned weapon. */
	AShooterWeapon* AddWeapon(TSubclassOf<AShooterWeapon> WeaponClass);

	/**
	 * Spawn a tactical weapon with an explicit economy tier. This deliberately
	 * permits two tiers to share one Blueprint class while a slot is replaced.
	 */
	AShooterWeapon* AddWeaponForTier(TSubclassOf<AShooterWeapon> WeaponClass, EWeaponTier Tier);

	/** Cycle to the next weapon */
	void SwitchWeapon();

	/** Switch to the first weapon matching the given category (0=pistol, 1=primary) */
	void SwitchToCategory(int32 Category);

	/** Start firing the current weapon — auto-routes to server if needed */
	void StartFiring();

	/** Stop firing the current weapon — auto-routes to server if needed */
	void StopFiring();

	/** Reload the current weapon */
	void Reload();

	/**
	 * Called by the fire-effects multicast on the owning client. A locally
	 * predicted first-person shot must not be played again when its server echo
	 * arrives. This never suppresses effects on other viewers.
	 */
	bool ShouldSuppressOwnerFireEcho(AShooterWeapon* Weapon,
		uint16 PredictionBurstId, uint16 BurstShotIndex);

	/** Align the local reload presentation lock with the server's accepted reload. */
	void NotifyAuthoritativeReloadStarted(AShooterWeapon* Weapon);

	/** Cancel active local fire immediately; retain short-lived echo tombstones. */
	void CancelLocalFirePrediction();

	/** Remove a weapon by tier (used by tactical mode drop system) */
	AShooterWeapon* FindWeaponByTier(int32 Tier);

	/** Check if there's room for a weapon of the given category (slot mode only) */
	bool HasFreeSlot(int32 Category) const;

	/** Return which tier is equipped for a category (slot mode only), or -1 */
	int32 GetEquippedTier(int32 Category) const;

	/** Remove and return a weapon by tier (for drop system) */
	AShooterWeapon* RemoveWeaponByTier(int32 Tier);

	/** Check if already owns a weapon of this class */
	AShooterWeapon* FindWeaponOfClass(TSubclassOf<AShooterWeapon> WeaponClass) const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION(Server, Reliable)
	void ServerStartFiring(AShooterWeapon* ExpectedWeapon, uint16 PredictionBurstId);

	UFUNCTION(Client, Reliable)
	void ClientFireBurstRejected(uint16 PredictionBurstId);

	UFUNCTION(Server, Reliable)
	void ServerStopFiring(AShooterWeapon* ExpectedWeapon);

	UFUNCTION(Server, Reliable)
	void ServerReload(AShooterWeapon* ExpectedWeapon);

	UFUNCTION(Server, Reliable)
	void ServerSwitchWeapon();

	UFUNCTION(Server, Reliable)
	void ServerSwitchToCategory(int32 Category);
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void SetCurrentWeapon(AShooterWeapon* NewWeapon);

	UFUNCTION()
	void OnRep_OwnedWeapons();

	UFUNCTION()
	void OnRep_CurrentWeapon();

	/**
	 * A replicated actor can be destroyed on a client before the component's
	 * CurrentWeapon update arrives. Track the current actor so that window is
	 * converted into a clean null transition instead of a pending-kill pointer.
	 */
	UFUNCTION()
	void OnTrackedCurrentWeaponDestroyed(AActor* DestroyedActor);

	void TrackCurrentWeaponLifetime(AShooterWeapon* Weapon);
	void ClearInvalidCurrentWeapon();
	AShooterWeapon* FindFirstValidWeapon() const;

	/** Only the owning autonomous client runs this non-gameplay timer. */
	void TryPlayNextPredictedFire();
	void StopLocalFirePrediction();
	void PrunePredictedFireEchoes(double Now);
	void ResetLocalFirePrediction();
	bool IsLocalSwitchPending(double Now);
	bool IsAutonomousLocalOwner() const;
	float GetLocalReloadPresentationDuration(const AShooterWeapon* Weapon) const;

	struct FPendingPredictedFireEcho
	{
		TWeakObjectPtr<AShooterWeapon> Weapon;
		uint16 BurstId = 0;
		uint16 ShotIndex = 0;
		double ExpiresAt = 0.0;
	};

	struct FLocalWeaponFireTiming
	{
		TWeakObjectPtr<AShooterWeapon> Weapon;
		double NextAllowedFireTime = 0.0;
	};

	/** Purely transient presentation bookkeeping; never replicated or used for damage. */
	TArray<FPendingPredictedFireEcho> PendingPredictedFireEchoes;
	TArray<FPendingPredictedFireEcho> ObservedAuthoritativeFireEchoes;
	TArray<FLocalWeaponFireTiming> LocalWeaponFireTimings;
	TWeakObjectPtr<AShooterWeapon> LocalPredictedWeapon;
	FTimerHandle LocalPredictedFireTimer;
	uint16 NextPredictionBurstId = 0;
	uint16 ActivePredictionBurstId = 0;
	uint16 NextPredictionShotIndex = 0;
	int32 LocalAmmoAtBurstStart = 0;
	int32 LocalAttemptedShotsInBurst = 0;
	double LocalReloadBlockUntil = 0.0;
	double LocalSwitchBlockUntil = 0.0;
	bool bLocalSwitchPending = false;
	bool bLocalTriggerHeld = false;

	/** Cached previous weapon for client-side OnRep tracking */
	UPROPERTY(Transient)
	TObjectPtr<AShooterWeapon> CachedCurrentWeapon;

	/** Current actor whose OnDestroyed callback protects replicated consumers. */
	UPROPERTY(Transient)
	TObjectPtr<AShooterWeapon> TrackedCurrentWeapon;
};
