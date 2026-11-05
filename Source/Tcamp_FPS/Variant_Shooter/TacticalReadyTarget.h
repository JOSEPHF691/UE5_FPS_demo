// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TacticalReadyTarget.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * Shootable lobby target. During PreRound, hitting RED or BLUE selects that
 * team and marks the shooter ready; regular combat damage is unaffected.
 */
UCLASS()
class TCAMP_FPS_API ATacticalReadyTarget : public AActor
{
	GENERATED_BODY()

public:
	ATacticalReadyTarget();

	void InitializeForTeam(uint8 InTeamByte);
	uint8 GetTeamByte() const { return TeamByte; }

	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, Category = "Ready Target")
	TObjectPtr<UStaticMeshComponent> TargetMesh;

	UPROPERTY(VisibleAnywhere, Category = "Ready Target")
	TObjectPtr<UTextRenderComponent> TargetLabel;

	UPROPERTY(ReplicatedUsing = OnRep_Team, VisibleInstanceOnly, Category = "Ready Target")
	uint8 TeamByte = 0;

	UFUNCTION()
	void OnRep_Team();

	void ApplyTeamVisual();
};
