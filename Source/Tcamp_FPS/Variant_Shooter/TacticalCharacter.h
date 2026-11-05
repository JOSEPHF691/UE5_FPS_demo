// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterCharacter.h"
#include "Economy/EconomyTypes.h"
#include "TacticalCharacter.generated.h"

class UArmorComponent;
class ATacticalWeapon;
class UInputAction;
class AShooterPickup;
class ATreasureChest;
class USkeletalMeshComponent;
class UTacticalSniperFPGripAnimInstance;

/**
 *  Tactical FPS character. Extends AShooterCharacter with:
 *  - Armor damage reduction via HealthComponent::OnProcessDamage callback
 *  - Weapon slot enforcement via WeaponInventoryComponent
 *  - Scope/zoom FOV control via weapon delegates
 *  - Buy phase freeze
	 *  - Death weapon drops
 */
UCLASS(abstract)
class TCAMP_FPS_API ATacticalCharacter : public AShooterCharacter
{
	GENERATED_BODY()

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UArmorComponent* ArmorComponent;

	UPROPERTY(EditAnywhere, Category = "Damage")
	FName HeadBoneName = FName("head");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons")
	EWeaponTier PrimaryTier = EWeaponTier::DefaultPistol;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapons")
	EWeaponTier PistolTier = EWeaponTier::DefaultPistol;

	// ---- Scope Input ----

	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* AimAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* HoldBreathAction;

	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* InteractAction;

	// ---- Scope State ----

	/** Normal FOV before aiming — cached on first aim */
	float DefaultFOV = 90.0f;

	/** Normal FirstPersonFieldOfView — cached on first aim, restored when not aiming */
	float DefaultFirstPersonFOV = 70.0f;
	float TargetFOV = 90.0f;
	float TargetFirstPersonFOV = 70.0f;
	FVector HipFirstPersonMeshRelativeLocation = FVector::ZeroVector;
	FQuat HipFirstPersonMeshRelativeRotation = FQuat::Identity;
	bool bHasHipFirstPersonMeshRelativeLocation = false;
	bool bFirstPersonIronSightReady = false;
	/** Last gun sight line from a frame without arms/gun action montages. */
	TWeakObjectPtr<ATacticalWeapon> CachedVisualAimWeapon;
	FVector CachedVisualRearInArms = FVector::ZeroVector;
	FVector CachedVisualFrontInArms = FVector::ZeroVector;
	/** Static gun-component references follow the animated hand socket without following recoil bones. */
	FVector CachedVisualRearInGun = FVector::ZeroVector;
	FVector CachedVisualFrontInGun = FVector::ZeroVector;
	bool bHasCachedVisualAimLine = false;
	/** Owner-only sniper walking sway is applied to the shared arms/gun root. */
	float SniperMoveBobPhase = 0.0f;
	float SniperMoveBobWeight = 0.0f;

	/** Owner-only Manny overlay: copies the existing weapon pose, then places the
	 * sniper support hand on its authored foregrip without changing other guns. */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> SniperFirstPersonGripOverlay;

	void UpdateSniperFirstPersonGripOverlay(AShooterWeapon* ActiveWeapon);

	/** Remote-view Manny copies the finished rifle pose and places its left palm
	 * on the active rifle/sniper foregrip. The source body remains the weapon's
	 * right-hand attachment and animation/montage driver. */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> ThirdPersonGripOverlay;
	bool bThirdPersonGripSourceStateCached = false;
	bool bCachedThirdPersonBodyMainPass = true;
	bool bCachedThirdPersonBodyDepthPass = true;
	bool bCachedThirdPersonBodyCastShadow = true;
	uint8 CachedThirdPersonBodyTickOption = 0;
	bool bThirdPersonGripOverlaySuppressedForSpectator = false;

	void UpdateThirdPersonGripOverlay(AShooterWeapon* ActiveWeapon);

	UPROPERTY(ReplicatedUsing = OnRep_Frozen)
	bool bFrozen = false;

	/** Authority-accepted treasure interaction; replicated to enforce the same fire gate everywhere. */
	UPROPERTY(ReplicatedUsing = OnRep_TreasureClaimActive)
	bool bTreasureClaimActive = false;

	/** Owning-client prediction closes the input-to-RPC firing window. */
	bool bLocalTreasureInteractHeld = false;

	/** Prevents phase-derived gates from reading health before components begin play. */
	bool bGameplayGatesInitialized = false;

	// ---- Pickup / Interact ----

	/** Pickup overlapping the player, waiting for interact key to swap */
	UPROPERTY()
	AShooterPickup* PendingPickup = nullptr;

	/** Openable chest currently inside interaction range. */
	UPROPERTY()
	TObjectPtr<ATreasureChest> NearbyTreasureChest;

public:
	ATacticalCharacter();

	virtual void BeginPlay() override;
	virtual void PawnClientRestart() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;
	virtual void CalcCamera(float DeltaTime, struct FMinimalViewInfo& OutResult) override;

	/** Returns the owning player's look pitch, including the replicated remote-pawn fallback. */
	float GetSpectatorViewPitch() const;

	/** Recompute the stable observer eye transform after a view-specific mesh swap. */
	void RefreshStableSpectatorCameraTransform()
	{
		CacheStableSpectatorCameraTransform();
	}
	/** Reattach after spectator-only mesh swaps change the available grip socket. */
	void RefreshFirstPersonWeaponAttachment(AShooterWeapon* Weapon)
	{
		EnsureFirstPersonWeaponAttachment(Weapon);
	}

	/** Local spectator camera temporarily hides the remote-view body overlay. */
	void SetThirdPersonGripOverlaySuppressedForSpectator(bool bSuppress);

	/** Detect headshots, apply multiplier, then delegate to HealthComponent */
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Cosmetic-only impact after server HP/armor resolution. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastResolvedBodyHit(uint8 EffectFlags, FVector_NetQuantize ImpactPoint,
		FVector_NetQuantizeNormal ImpactNormal, FName BoneName);

	// ---- Armor (processed via HealthComponent callback) ----

	UFUNCTION(BlueprintPure, Category = "Armor")
	UArmorComponent* GetArmorComponent() const { return ArmorComponent; }

	// ---- Death (overrides to add weapon drops before super) ----

	virtual void OnDeath() override;

	// ---- Weapon slot enforcement ----

	virtual void AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass) override;

	/** Adds a weapon only when its tactical slot is free. */
	bool TryAddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass);

	/** Adds a tier-aware tactical weapon when its slot is free. */
	bool TryAddWeaponForTier(const TSubclassOf<AShooterWeapon>& WeaponClass, EWeaponTier Tier);

	/** Equips a buy-menu weapon without creating a free world drop from the replaced slot. */
	bool EquipPurchasedWeapon(const TSubclassOf<AShooterWeapon>& WeaponClass, EWeaponTier Tier);

	// ---- Montage playback ----

	virtual void PlayFiringMontage(UAnimMontage* Montage) override;
	virtual void PlayReloadMontage(UAnimMontage* Montage) override;
	/** Plays only owner-view arm/gun fire montages for local presentation prediction. */
	bool PlayPredictedFirstPersonFire(AShooterWeapon* Weapon);
	/** True only after a valid gun sight has converged on the owner's view axis. */
	bool HasUsableFirstPersonIronSight() const { return bFirstPersonIronSightReady; }

	// ---- Freeze (buy phase) ----

	void SetFrozen(bool bInFrozen);
	void ApplyFrozenLocally(bool bInFrozen);
	bool IsFrozen() const { return bFrozen; }

	/** Cancel held actions and velocity before this Pawn enters reconnect grace. */
	void PrepareForDisconnectGrace();

	/** Authority entry used by TreasureChest; never writes bCanFire directly. */
	void SetTreasureClaimActive(bool bActive);
	bool IsTreasureClaimBlockingFire() const
	{
		return bTreasureClaimActive || bLocalTreasureInteractHeld;
	}

	// ---- Slot queries ----

	UFUNCTION(BlueprintPure, Category = "Weapons")
	bool HasFreeSlot(EWeaponCategory Category) const;

	UFUNCTION(BlueprintPure, Category = "Economy")
	bool CanAfford(int32 Cost) const;

	EWeaponTier GetPrimaryTier() const { return PrimaryTier; }
	EWeaponTier GetPistolTier() const { return PistolTier; }
	void SetPrimaryTier(EWeaponTier Tier) { PrimaryTier = Tier; }
	void SetPistolTier(EWeaponTier Tier) { PistolTier = Tier; }

	/** Drop a weapon of the given tier (for death drops and slot swaps) */
	void DropWeapon(EWeaponTier Tier);

	// ---- Scope input ----

	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoStartAiming();

	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoStopAiming();

	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoCycleZoom();

	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoStartHoldBreath();

	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoStopHoldBreath();

	// ---- Pickup / Interact ----

	/** Called by pickups when slot is occupied — stores pickup for interact */
	void OfferPickupSwap(AShooterPickup* Pickup);

	/** Called by pickups when overlap ends — clears pending state */
	void ClearPendingPickup(AShooterPickup* Pickup);

	void SetNearbyTreasureChest(ATreasureChest* Chest);
	void ClearNearbyTreasureChest(ATreasureChest* Chest);

	/** Interact key — swaps weapon with the pending pickup */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoInteract();

	UFUNCTION(BlueprintCallable, Category = "Input")
	void DoStopInteract();

	/** Server-side RPC for DoInteract — performs the actual swap with authority */
	UFUNCTION(Server, Reliable)
	void ServerDoInteract();

	/** Keeps the owning client's local interaction target in sync with authority. */
	UFUNCTION(Client, Reliable)
	void ClientSetPickupOffer(AShooterPickup* Pickup, bool bHasOffer);

	UFUNCTION(Server, Reliable)
	void ServerBeginTreasureClaim(ATreasureChest* Chest);

	UFUNCTION(Server, Reliable)
	void ServerCancelTreasureClaim(ATreasureChest* Chest);

	/** Immediate owner correction for accepted, rejected, completed, or cancelled claims. */
	UFUNCTION(Client, Reliable)
	void ClientSetTreasureClaimActive(bool bActive);

	/** Blueprint event for showing/hiding the pickup prompt */
	UFUNCTION(BlueprintImplementableEvent, Category = "Pickup")
	void BP_OnPickupOfferChanged(bool bHasOffer);

	UFUNCTION(BlueprintImplementableEvent, Category = "Treasure")
	void BP_OnTreasureOfferChanged(bool bHasOffer, float HoldDuration);

	// ---- Weapon activation ----

	/** Keep both weapon meshes attached directly to the authoritative Manny meshes. */
	virtual void AttachWeaponMeshes(AShooterWeapon* Weapon) override;

	virtual void OnWeaponActivated(AShooterWeapon* Weapon) override;

	// ---- Input (gated by freeze) ----

	virtual void DoStartFiring() override;
	virtual void DoMove(float Right, float Forward) override;
	virtual void DoJumpStart() override;
	virtual void DoJumpEnd() override;
	virtual void DoAim(float Yaw, float Pitch) override;

protected:
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;

	/**
	 * Cache the authored first-person eye transform relative to the first-person
	 * mesh component root after the runtime eye-pivot migration. Spectator
	 * cameras use this stable offset independent of animated head motion.
	 */
	void CacheStableSpectatorCameraTransform();

	FTransform StableSpectatorCameraRelativeToMesh = FTransform::Identity;
	bool bHasStableSpectatorCameraTransform = false;

	/** Cache movement so a held direction is applied every character tick. */
	void CaptureMoveInput(const FInputActionValue& Value);
	void ClearMoveInput(const FInputActionValue& Value);
	FVector2D CachedMoveInput = FVector2D::ZeroVector;

	/** Apply the client/server corpse presentation once for this actor instance. */
	void ApplyDeathPresentation();

	void EnterDeathRagdoll();

	/** Resolve the single authoritative first-person Manny grip mesh. */
	USkeletalMeshComponent* ResolveFirstPersonWeaponAttachmentMesh() const;

	/** Idempotently repair a weapon created/replicated before its Manny mesh was ready. */
	void EnsureFirstPersonWeaponAttachment(AShooterWeapon* Weapon);
	void UpdateFirstPersonIronSight(ATacticalWeapon* Weapon, float DeltaTime);

	/** Prevent replicated-state catch-up from replaying local presentation work. */
	bool bDeathPresentationApplied = false;

	/** Prevent any duplicate authority callback from settling death twice. */
	bool bAuthoritativeDeathHandled = false;

	bool GetMostValuableDroppableWeaponTier(EWeaponTier& OutTier) const;

	void SaveCurrentLoadoutToPlayerState() const;
	void RefreshWeaponFireGate();

	bool TryReplaceWeaponInSlot(const TSubclassOf<AShooterWeapon>& WeaponClass, EWeaponTier Tier, bool bDropReplacedWeapon);

	/** Bind/unbind scope delegates when weapon changes */
	UFUNCTION()
	void OnWeaponChanged_Scope(AShooterWeapon* NewWeapon, AShooterWeapon* OldWeapon);

	/** Converge replicated weapon changes onto the authoritative Manny grip. */
	UFUNCTION()
	void OnWeaponChanged_Attachment(AShooterWeapon* NewWeapon, AShooterWeapon* OldWeapon);

	UFUNCTION()
	void OnAimFOVChanged(bool bAiming, float FOV);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	/** Armor callback bound to HealthComponent::OnProcessDamage */
	float ProcessArmorDamage(float IncomingDamage, bool bHeadshot, float& OutAbsorbed);

	AActor* PendingDamageCauser = nullptr;
	/** Actual absorption from this damage call, distinct from armor cleared by death cleanup. */
	float PendingDamageArmorAbsorbed = 0.0f;

	UFUNCTION()
	void OnRep_Frozen();

	UFUNCTION()
	void OnRep_TreasureClaimActive();

#if WITH_DEV_AUTOMATION_TESTS
	friend class FTacticalPlayerPresentationConfigurationTest;
#endif
};
