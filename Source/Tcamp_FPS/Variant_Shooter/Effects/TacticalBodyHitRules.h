#pragma once

#include "CoreMinimal.h"

class AActor;
class ACharacter;
class AController;
struct FDamageEvent;

/** Compact, cosmetic-only flags sent with a resolved damage event. */
namespace TacticalBodyHitRules
{
	constexpr uint8 None = 0;
	constexpr uint8 Health = 1 << 0;
	constexpr uint8 Armor = 1 << 1;
	constexpr uint8 Headshot = 1 << 2;

	inline uint8 Classify(const float HealthBefore, const float HealthAfter,
		const float ArmorBefore, const float ArmorAfter, const bool bHeadshot,
		const bool bEnemyPointHit)
	{
		if (!bEnemyPointHit)
		{
			return None;
		}

		uint8 Result = None;
		if (HealthBefore - HealthAfter > KINDA_SMALL_NUMBER)
		{
			Result |= Health;
		}
		if (ArmorBefore - ArmorAfter > KINDA_SMALL_NUMBER)
		{
			Result |= Armor;
		}
		if (Result != None && bHeadshot)
		{
			Result |= Headshot;
		}
		return Result;
	}

	/** Presentation gate only; this must not alter gameplay damage or friendly-fire rules. */
	TCAMP_FPS_API bool IsEnemyContact(const AActor* Victim, const AController* EventInstigator);

	/** Uses the authoritative point hit, with a safe mesh-centered fallback for explosions. */
	TCAMP_FPS_API bool TryResolvePointImpact(const ACharacter* Victim,
		const FDamageEvent& DamageEvent, FVector& OutPoint, FVector& OutNormal, FName& OutBone);
}
