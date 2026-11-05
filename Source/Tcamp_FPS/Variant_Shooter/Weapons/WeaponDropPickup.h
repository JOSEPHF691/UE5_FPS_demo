// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterPickup.h"
#include "Economy/EconomyTypes.h"
#include "WeaponDropPickup.generated.h"

class AShooterWeapon;

/**
 *  Weapon drop pickup from dead players.
 *  One-time use — does not respawn. Auto-destroys after lifetime.
 */
UCLASS()
class TCAMP_FPS_API AWeaponDropPickup : public AShooterPickup
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Drop", meta = (Units = "cm"))
	FVector DropMeshRelativeLocation = FVector(0.0f, 0.0f, -84.0f);

	UPROPERTY(EditDefaultsOnly, Category = "Drop")
	FRotator DropMeshRelativeRotation = FRotator(0.0f, 90.0f, 0.0f);

	UPROPERTY(EditDefaultsOnly, Category = "Drop")
	FVector DropMeshRelativeScale = FVector(1.0f, 1.0f, 1.0f);

	/** Weapon class to grant on pickup */
	UPROPERTY(ReplicatedUsing = OnRep_DroppedWeaponData)
	TSubclassOf<AShooterWeapon> DroppedWeaponClass;

	/** Weapon tier for slot management */
	UPROPERTY(ReplicatedUsing = OnRep_DroppedWeaponData)
	EWeaponTier DroppedTier;

	FTimerHandle LifetimeTimer;

public:
	AWeaponDropPickup();

	/** Initializes this pickup from a dropped weapon */
	void InitializeFromWeapon(TSubclassOf<AShooterWeapon> InWeaponClass, EWeaponTier Tier);

	/** Returns the weapon class this drop grants */
	TSubclassOf<AShooterWeapon> GetDroppedWeaponClass() const { return DroppedWeaponClass; }

	/** Returns the tier of the dropped weapon */
	EWeaponTier GetDroppedTier() const { return DroppedTier; }

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Override — anyone can pick up, no respawn */
	virtual void OnOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult) override;

protected:
	UFUNCTION()
	void OnRep_DroppedWeaponData();

	void ApplyDroppedWeaponVisuals();

	void OnLifetimeExpired();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
