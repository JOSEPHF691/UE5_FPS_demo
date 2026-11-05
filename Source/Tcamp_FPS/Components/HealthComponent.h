// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthDamaged, float, HealthPercent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnHealthDepleted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthHealed, float, HealthPercent);

/** Callback for external damage processing (armor, resistances). Returns modified damage. */
DECLARE_DELEGATE_RetVal_ThreeParams(float, FOnProcessDamage, float /*IncomingDamage*/, bool /*bHeadshot*/, float& /*OutAbsorbed*/);

/**
 *  Reusable health component. Attach to any actor that needs HP / damage / death.
 *  Emits delegates for UI and game logic — no direct dependency on GameMode.
 */
UCLASS(ClassGroup=(Combat), meta=(BlueprintSpawnableComponent))
class TCAMP_FPS_API UHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHealthComponent();

	// ---- Configuration ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health")
	float MaxHP = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health")
	uint8 TeamByte = 0;

	/** Tag granted to owner on death */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health")
	FName DeathTag = FName("Dead");

	// ---- Runtime State ----

	UPROPERTY(ReplicatedUsing = OnRep_CurrentHP, BlueprintReadOnly, Category = "Health")
	float CurrentHP = 0.0f;

	/**
	 * Persistent authoritative death state.
	 *
	 * CurrentHP defaults to zero on a newly-created client replica. An actor that
	 * was already dead before that client connected also has zero HP, so HP alone
	 * cannot produce a replication transition that replays the death presentation.
	 * This flag defaults to false and therefore always carries an observable initial
	 * update for an already-dead actor.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Dead, BlueprintReadOnly, Category = "Health")
	bool bDead = false;

	/** Controller that last damaged us (kill credit) */
	UPROPERTY(Transient)
	TObjectPtr<AController> LastDamageInstigator;

	// ---- Damage Pipeline Callback ----

	/** Hook for external damage processing (armor, buffs). Called before HP is reduced. */
	FOnProcessDamage OnProcessDamage;

	// ---- Delegates ----

	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnHealthDamaged OnDamaged;

	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnHealthDepleted OnDeath;

	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnHealthHealed OnHealed;

	// ---- API ----

	/** Apply damage. Returns actual damage dealt after processing. */
	float TakeDamage(float Damage, AController* EventInstigator, bool bHeadshot = false);

	/** Heal by amount. Clamps to MaxHP. */
	void Heal(float Amount);

	/** Set HP to max and reset death state. */
	void ResetToFull();

	/** Returns the authoritative replicated death state. */
	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const { return bDead; }

	/** Returns HP as 0.0–1.0 fraction */
	UFUNCTION(BlueprintPure, Category = "Health")
	float GetHealthPercent() const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_CurrentHP(float PreviousHP);

	UFUNCTION()
	void OnRep_Dead(bool bWasDead);
};
