// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Pure decision helpers for the server-authoritative treasure guard brain.
 * Keeping these rules independent from actors makes the distance hysteresis
 * and move-throttling contract cheap to cover with automation tests.
 */
namespace TreasureGuardRules
{
	enum class ECombatDecision : uint8
	{
		Retreat,
		HoldAndFire,
		Reposition
	};

	inline ECombatDecision ChooseCombatDecision(
		const float TargetDistance,
		const bool bHasLineOfSight,
		const bool bWasRetreating,
		const float RetreatEnterDistance,
		const float RetreatExitDistance,
		const float MaxHoldAndFireDistance)
	{
		const bool bInsideRetreatBand = bWasRetreating
			? TargetDistance < RetreatExitDistance
			: TargetDistance < RetreatEnterDistance;
		if (bInsideRetreatBand)
		{
			return ECombatDecision::Retreat;
		}

		return bHasLineOfSight && TargetDistance <= MaxHoldAndFireDistance
			? ECombatDecision::HoldAndFire
			: ECombatDecision::Reposition;
	}

	inline bool ShouldIssueMove(
		const bool bHasActiveMove,
		const bool bPurposeChanged,
		const float DestinationChangeSquared,
		const float TargetChangeSquared,
		const float SecondsSinceLastRequest,
		const float MinimumRequestInterval,
		const float ReplanDistance)
	{
		if (bPurposeChanged)
		{
			return true;
		}

		if (SecondsSinceLastRequest < MinimumRequestInterval)
		{
			return false;
		}
		if (!bHasActiveMove)
		{
			return true;
		}

		const float ReplanDistanceSquared = FMath::Square(FMath::Max(0.0f, ReplanDistance));
		return DestinationChangeSquared > ReplanDistanceSquared
			|| TargetChangeSquared > ReplanDistanceSquared;
	}

	inline bool ShouldPlanMove(
		const bool bHasActiveMove,
		const bool bPurposeChanged,
		const float SecondsSinceLastPlan,
		const float MinimumPlanInterval)
	{
		return (bHasActiveMove && bPurposeChanged)
			|| SecondsSinceLastPlan >= MinimumPlanInterval;
	}

	inline FVector MakeRetreatDirection2D(
		const FVector& GuardLocation,
		const FVector& TargetLocation,
		const FVector& FallbackForward)
	{
		FVector Direction = GuardLocation - TargetLocation;
		Direction.Z = 0.0f;
		if (!Direction.Normalize())
		{
			Direction = -FallbackForward;
			Direction.Z = 0.0f;
			if (!Direction.Normalize())
			{
				Direction = FVector::BackwardVector;
			}
		}
		return Direction;
	}

	inline FVector ClampPointToRadius2D(
		const FVector& Point,
		const FVector& Anchor,
		const float Radius)
	{
		FVector Offset = Point - Anchor;
		Offset.Z = 0.0f;
		const float SafeRadius = FMath::Max(0.0f, Radius);
		if (Offset.SizeSquared() <= FMath::Square(SafeRadius))
		{
			return Point;
		}

		const FVector Horizontal = Offset.GetSafeNormal() * SafeRadius;
		return FVector(Anchor.X + Horizontal.X, Anchor.Y + Horizontal.Y, Point.Z);
	}

	inline bool CanCompleteReturn(
		const bool bMatchingRequest,
		const float DistanceToHome,
		const float CompletionRadius)
	{
		// Path-following success may include agent radius and acceptance radius,
		// so physical arrival at this guard's own post is the final authority.
		return bMatchingRequest && DistanceToHome <= CompletionRadius;
	}
}
