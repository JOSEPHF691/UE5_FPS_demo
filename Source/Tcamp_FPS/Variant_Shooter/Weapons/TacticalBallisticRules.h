// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"

/** Pure shot-path rules shared by server hitscan/projectile launch and tests. */
namespace TacticalBallisticRules
{
	enum class EBlockedSegment : uint8
	{
		None,
		EyeToMuzzle,
		MuzzleToSpawn
	};

	struct FShotPath
	{
		FVector Origin = FVector::ZeroVector;
		FVector Target = FVector::ZeroVector;
		EBlockedSegment BlockedSegment = EBlockedSegment::None;

		bool IsBlocked() const { return BlockedSegment != EBlockedSegment::None; }
	};

	inline FVector CalculateDesiredSpawn(const FVector& Muzzle,
		const FVector& CameraTarget, const FVector& FallbackForward,
		const float MuzzleOffset)
	{
		FVector Direction = (CameraTarget - Muzzle).GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			Direction = FallbackForward.GetSafeNormal();
		}
		return Muzzle + Direction * FMath::Max(0.0f, MuzzleOffset);
	}

	inline FShotPath SelectShotPath(const FVector& Eye,
		const FVector& Muzzle, const FVector& DesiredSpawn,
		const FVector& CameraTarget, const FHitResult& EyeToMuzzleHit,
		const FHitResult& MuzzleToSpawnHit)
	{
		// The eye-to-muzzle barrier always wins: a muzzle visually clipped
		// beyond cover must never turn a camera-visible target into a hit.
		if (EyeToMuzzleHit.bBlockingHit)
		{
			return {Eye, EyeToMuzzleHit.ImpactPoint,
				EBlockedSegment::EyeToMuzzle};
		}
		if (MuzzleToSpawnHit.bBlockingHit)
		{
			return {Muzzle, MuzzleToSpawnHit.ImpactPoint,
				EBlockedSegment::MuzzleToSpawn};
		}
		return {DesiredSpawn, CameraTarget, EBlockedSegment::None};
	}
}
