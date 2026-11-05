// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "ShooterWeaponMeshComponent.generated.h"

/**
 * Weapon mesh that can compensate a legacy animation graph's socket-location
 * query without moving the rendered weapon or modifying the source asset.
 */
UCLASS(NotBlueprintable)
class TCAMP_FPS_API UShooterWeaponMeshComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:
	/**
	 * The legacy graph adds LocalOffset again after GetSocketLocation. Returning
	 * the inverse-adjusted query location makes its final target land on the real
	 * socket while leaving every rendered transform unchanged.
	 */
	void SetSocketLocationQueryCompensation(FName InSocketName, const FVector& LocalOffset);
	void ClearSocketLocationQueryCompensation();

	virtual FVector GetSocketLocation(FName InSocketName) const override;

private:
	FName CompensatedSocketName = NAME_None;
	FVector CompensatedLocalOffset = FVector::ZeroVector;
};
