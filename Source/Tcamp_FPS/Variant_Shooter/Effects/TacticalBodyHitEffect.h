#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TacticalBodyHitEffect.generated.h"

class ACharacter;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/** Short-lived client-local impact card. Never replicated or used for damage. */
UCLASS(NotBlueprintable)
class TCAMP_FPS_API ATacticalBodyHitEffect : public AActor
{
	GENERATED_BODY()

public:
	ATacticalBodyHitEffect();

	static void SpawnForVictim(ACharacter* Victim, uint8 EffectFlags,
		const FVector& ImpactPoint, const FVector& ImpactNormal, FName BoneName);

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> ImpactCard;

	/** A wider armor-colored halo, enabled only when the same hit also removes HP. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> ArmorHaloCard;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ArmorDynamicMaterial;

	float RemainingTime = 0.0f;
	float TotalTime = 0.18f;
	float BaseOpacity = 1.0f;
	float ArmorBaseOpacity = 0.85f;
};
