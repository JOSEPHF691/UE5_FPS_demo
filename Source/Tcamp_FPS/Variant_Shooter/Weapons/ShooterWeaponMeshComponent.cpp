// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterWeaponMeshComponent.h"

void UShooterWeaponMeshComponent::SetSocketLocationQueryCompensation(
	FName InSocketName, const FVector& LocalOffset)
{
	CompensatedSocketName = InSocketName;
	CompensatedLocalOffset = LocalOffset;
}

void UShooterWeaponMeshComponent::ClearSocketLocationQueryCompensation()
{
	CompensatedSocketName = NAME_None;
	CompensatedLocalOffset = FVector::ZeroVector;
}

FVector UShooterWeaponMeshComponent::GetSocketLocation(FName InSocketName) const
{
	const FVector RawLocation = Super::GetSocketLocation(InSocketName);
	if (InSocketName != CompensatedSocketName || CompensatedLocalOffset.IsNearlyZero())
	{
		return RawLocation;
	}

	return RawLocation
		- GetComponentTransform().TransformVectorNoScale(CompensatedLocalOffset);
}
