// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterWeapon.h"
#include "Economy/EconomyTypes.h"
#include "Engine/DataTable.h"
#include "TacticalWeapon.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UAnimSequence;
class USkeletalMesh;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAimStateChanged, bool, bAiming, float, CurrentFOV);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBreathChanged, float, StabilityPercent);

/** Tuning for Manny's support palm on a first-person weapon. */
USTRUCT(BlueprintType)
struct FFirstPersonGripProfile
{
	GENERATED_BODY()

	/** A full palm contact transform on the first-person weapon mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grip")
	FName LeftPalmSocket = TEXT("FP_LeftHandGrip");

	/** Keep authored wrist orientation while moving the palm onto the weapon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grip", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WristRotationAlpha = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grip", meta = (ClampMin = "0.0", ClampMax = "90.0", Units = "Degrees"))
	float MaxWristCorrectionDegrees = 40.0f;

	/** Direction weights for the elbow pole; their ratio determines its direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grip", meta = (ClampMin = "0.0"))
	float ElbowOutwardCm = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grip", meta = (ClampMin = "0.0"))
	float ElbowDownCm = 80.0f;

	/** How strongly the downward pole steers away from the animation's elbow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grip", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ElbowPoleBiasAlpha = 0.65f;

	/** First-person-only shoulder offset, so Manny's sleeve stays below the near-camera grip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grip", meta = (ClampMin = "0.0", ClampMax = "25.0", Units = "cm"))
	float ShoulderForwardCm = 12.0f;

	/** Grip fades out during reload and in when the weapon is ready. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grip", meta = (ClampMin = "1.0", ClampMax = "40.0"))
	float BlendSpeed = 16.0f;
};

/** C++ visual-only ADS calibration; gameplay shots continue to use the server muzzle. */
struct FTacticalVisualAimProfile
{
	FName RearReference = NAME_None;
	FName FrontReference = NAME_None;
	/** Offsets are in the weapon mesh's component axes, not the socket's rotation axes. */
	FVector RearMeshOffset = FVector::ZeroVector;
	FVector FrontMeshOffset = FVector::ZeroVector;
	float SightDistanceCm = 40.0f;
	float SightHeightCm = 0.0f;
	float MaxRotationCorrectionDegrees = 45.0f;
};

/**
 *  Tactical weapon — tier/category + DataTable-driven damage + scope/zoom.
 */
UCLASS(abstract)
class TCAMP_FPS_API ATacticalWeapon : public AShooterWeapon
{
	GENERATED_BODY()

protected:
	// ---- Identity ----

	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_WeaponTier, Category = "Tactical")
	EWeaponTier WeaponTier = EWeaponTier::DefaultPistol;

	UPROPERTY(EditAnywhere, Category = "Tactical")
	EWeaponCategory WeaponCategory = EWeaponCategory::Pistol;

	UPROPERTY(EditDefaultsOnly, Category = "Tactical|First Person Grip")
	bool bOverrideFirstPersonGripProfile = false;

	UPROPERTY(EditDefaultsOnly, Category = "Tactical|First Person Grip",
		meta = (EditCondition = "bOverrideFirstPersonGripProfile"))
	FFirstPersonGripProfile FirstPersonGripProfile;

	/** Assemble the RAR rifle's default magazine, handguard and sight for both views. */
	UPROPERTY(EditDefaultsOnly, Category = "Tactical|RAR Presentation")
	bool bUseRARDefaultFirstPersonParts = false;

	UPROPERTY()
	TObjectPtr<UStaticMesh> RARDefaultMagazineMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> RARDefaultForestockMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> RARDefaultFrontSightMesh;
	/** Cook-safe aimed reload source used by the first-person RAR graph. */
	UPROPERTY()
	TObjectPtr<UAnimSequence> RARAimedReloadSequence;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> RARFirstPersonParts;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> RARThirdPersonParts;

	/** Cook-safe fallback for the known pistol/rifle montage mismatch in the template asset. */
	UPROPERTY()
	TObjectPtr<UAnimMontage> PistolFireMontageFallback = nullptr;

	/** Cook-safe one-handed pose used when a weapon cannot satisfy the rifle left-hand IK contract. */
	UPROPERTY()
	TSubclassOf<UAnimInstance> OneHandedThirdPersonAnimFallback;

	/** Project-owned, socket-calibrated copy of the imported sniper mesh. */
	UPROPERTY()
	TObjectPtr<USkeletalMesh> SniperPresentationMesh;

	/** The imported sniper has no reload clip; Manny's rifle reload fits both character AnimBPs. */
	UPROPERTY()
	TObjectPtr<UAnimMontage> SniperCharacterReloadMontage;

	// ---- DataTable ----

	UPROPERTY(EditAnywhere, Category = "Tactical|Data")
	TSoftObjectPtr<UDataTable> WeaponDataTable;

	float CachedBodyDamage = 18.0f;
	float CachedHeadshotMultiplier = 3.0f;

	// ---- Penetration ----

	/** Penetration count for this weapon (0 = no penetration) */
	UPROPERTY(EditAnywhere, Category = "Tactical|Penetration", meta = (ClampMin = 0, ClampMax = 10))
	int32 PenetrationCount = 0;

	/** Damage falloff per penetration */
	UPROPERTY(EditAnywhere, Category = "Tactical|Penetration", meta = (ClampMin = 0.0f, ClampMax = 1.0f))
	float PenetrationFalloff = 0.7f;

	// ---- Scope / Zoom ----

	/** If true, this weapon supports aiming down sights (scope/zoom) */
	UPROPERTY(EditAnywhere, Category = "Tactical|Scope")
	bool bHasScope = false;

	/** Zoom levels (FOV values). Low = more zoom. e.g. {20, 40} for sniper, {55} for rifle */
	UPROPERTY(EditAnywhere, Category = "Tactical|Scope", meta = (ClampMin = "5.0", ClampMax = "120.0"))
	TArray<float> ZoomLevels;

	/** If true, this weapon has hold-breath stability mechanic while scoped */
	UPROPERTY(EditAnywhere, Category = "Tactical|Scope")
	bool bHasBreathControl = false;

	/** How long the player can hold breath (seconds) before losing stability */
	UPROPERTY(EditAnywhere, Category = "Tactical|Scope", meta = (ClampMin = "1.0", ClampMax = "20.0"))
	float BreathDuration = 3.0f;

	/** How long to recover breath (seconds) */
	UPROPERTY(EditAnywhere, Category = "Tactical|Scope", meta = (ClampMin = "1.0", ClampMax = "20.0"))
	float BreathRecoveryTime = 5.0f;

	/** Tick interval for breath meter */
	UPROPERTY(EditAnywhere, Category = "Tactical|Scope", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float BreathTickInterval = 0.033f;

	/** Spread multiplier while ADSing (0.0 = laser, 1.0 = unchanged) */
	UPROPERTY(EditAnywhere, Category = "Tactical|Scope", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AdsSpreadMultiplier = 0.3f;

	/** Additional spread multiplier while successfully holding breath. */
	UPROPERTY(EditAnywhere, Category = "Tactical|Scope", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HoldBreathSpreadMultiplier = 0.1f;

	/** Speed of hip→ADS animation blend (higher = snappier) */
	UPROPERTY(EditAnywhere, Category = "Tactical|Scope", meta = (ClampMin = "1.0", ClampMax = "50.0"))
	float AimBlendSpeed = 12.0f;

	// ---- Runtime scope state ----

	UPROPERTY(ReplicatedUsing = OnRep_AimingState)
	bool bIsAiming = false;
	int32 CurrentZoomIndex = 0;

	float AimBlendAlpha = 0.0f;

	UPROPERTY(Replicated)
	bool bIsHoldingBreath = false;
	float BreathMeter = 0.0f;
	bool bBreathDepleted = false;
	FTimerHandle BreathTimer;

public:
	ATacticalWeapon();
	void SetRARFirstPersonPartsOnlyOwnerSee(bool bOnlyOwnerSee);

	/**
	 * Sets the runtime tier before a shared weapon class is equipped.
	 * Multiple economy tiers may intentionally use the same Blueprint weapon class.
	 */
	void InitializeWeaponTier(EWeaponTier InWeaponTier);

	virtual int32 GetWeaponTier() const override { return static_cast<int32>(WeaponTier); }
	virtual int32 GetWeaponCategory() const override { return static_cast<int32>(WeaponCategory); }
	virtual bool TryGetFirstPersonLeftHandGripTransform(
		FTransform& OutWeaponSpaceTransform) const override;
	FFirstPersonGripProfile GetFirstPersonGripProfile() const;
	FTacticalVisualAimProfile GetFirstPersonVisualAimProfile() const;

	UFUNCTION(BlueprintPure, Category = "Tactical")
	EWeaponTier GetWeaponTierEnum() const { return WeaponTier; }

	UFUNCTION(BlueprintPure, Category = "Tactical")
	EWeaponCategory GetWeaponCategoryEnum() const { return WeaponCategory; }

	UFUNCTION(BlueprintPure, Category = "Damage")
	float GetBodyDamage() const { return CachedBodyDamage; }

	UFUNCTION(BlueprintPure, Category = "Damage")
	float GetHeadshotMultiplier() const { return CachedHeadshotMultiplier; }

	// ---- Scope API ----

	UPROPERTY(BlueprintAssignable, Category = "Scope")
	FOnAimStateChanged OnAimStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Scope")
	FOnBreathChanged OnBreathChanged;

	UFUNCTION(BlueprintCallable, Category = "Scope")
	void StartAiming();

	UFUNCTION(BlueprintCallable, Category = "Scope")
	void StopAiming();

	/** Toggle to next zoom level. Only valid while aiming. */
	UFUNCTION(BlueprintCallable, Category = "Scope")
	void CycleZoom();

	UFUNCTION(BlueprintCallable, Category = "Scope")
	void StartHoldBreath();

	UFUNCTION(BlueprintCallable, Category = "Scope")
	void StopHoldBreath();

	UFUNCTION(BlueprintPure, Category = "Scope")
	bool IsAiming() const { return bIsAiming; }

	UFUNCTION(BlueprintPure, Category = "Scope")
	float GetAimBlendAlpha() const { return AimBlendAlpha; }
	/** The owning pawn drives its viewmodel blend, independent of weapon BeginPlay/owner replication order. */
	void AdvanceLocalAimBlend(float DeltaTime, bool bShouldAim);
	/** A hidden/retired weapon must not retain a previous owner's ADS pose. */
	void ResetLocalAimBlend() { AimBlendAlpha = 0.0f; }

	UFUNCTION(BlueprintPure, Category = "Scope")
	float GetCurrentAimFOV() const;
	/** ADS FOV even while AimBlendAlpha is returning to zero after release. */
	float GetConfiguredAimFOV() const;

	UFUNCTION(BlueprintPure, Category = "Scope")
	float GetBreathPercent() const;

	UFUNCTION(BlueprintPure, Category = "Scope")
	bool HasScope() const { return bHasScope; }

	/** Extra authoritative/client reload lock when the aimed RAR clip is longer. */
	float GetAdditionalReloadPresentationDuration() const;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	/** Repairs inherited template presentation assets before the base BeginPlay attaches the weapon. */
	void SanitizePresentationConfiguration();
	void ApplySniperPresentation();

	void CacheWeaponDataFromDataTable();
	void PushAimStateToAnimInstance();
	virtual void ConfigureProjectile(AShooterProjectile* Projectile) override;
	virtual void ConfigureHitscanParams(float& OutDamage, int32& OutPenetrations, float& OutFalloff) override;
	virtual float GetEffectiveAimVariance() const override;
	virtual bool HasInfiniteReserveAmmo() const override;

	void BreathTick();
	void ApplyScopeFOV();

	UFUNCTION(Server, Reliable)
	void ServerSetAiming(bool bNewAiming);

	UFUNCTION(Server, Reliable)
	void ServerSetHoldingBreath(bool bNewHoldingBreath);

	UFUNCTION()
	void OnRep_AimingState();

	UFUNCTION()
	void OnRep_WeaponTier();
};
