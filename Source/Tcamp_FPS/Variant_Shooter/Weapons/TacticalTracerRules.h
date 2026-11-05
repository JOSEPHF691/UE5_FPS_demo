// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Presentation-only rules. Gameplay traces always keep their full range. */
namespace TacticalTracerRules
{
	constexpr float MaxVisualDistanceCm = 20000.0f;

	inline FVector ChooseVisualEnd(const FVector& TraceStart, const FVector& AimDirection,
		const bool bStoppedAtImpact, const FVector& StoppingImpactPoint)
	{
		if (TraceStart.ContainsNaN())
		{
			return FVector::ZeroVector;
		}
		const FVector Direction = AimDirection.GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			return TraceStart;
		}

		// A penetrating shot which exits its last target continues to the bounded
		// miss endpoint. Only a genuinely stopping hit shortens the visual path.
		if (bStoppedAtImpact)
		{
			if (StoppingImpactPoint.ContainsNaN())
			{
				return TraceStart;
			}
			const float AlongShot = FVector::DotProduct(
				StoppingImpactPoint - TraceStart, Direction);
			if (AlongShot > UE_KINDA_SMALL_NUMBER)
			{
				return TraceStart + Direction * FMath::Min(AlongShot, MaxVisualDistanceCm);
			}
			return TraceStart;
		}

		return TraceStart + Direction * MaxVisualDistanceCm;
	}

	inline bool ShouldDisplayShot(const uint16 ShotId, const int32 EveryNthShot)
	{
		return EveryNthShot <= 1 || (static_cast<uint32>(ShotId) % EveryNthShot) == 1u;
	}
}
