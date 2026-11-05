// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SprintComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSprintMeterUpdated, float, Percent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSprintStateChanged, bool, bSprinting);

/**
 *  Stamina-based sprint component. Attach to any character that should sprint.
 *  Handles meter drain/recovery and walk speed modulation.
 */
UCLASS(ClassGroup=(Movement), meta=(BlueprintSpawnableComponent))
class TCAMP_FPS_API USprintComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USprintComponent();

	// ---- Configuration ----

	/** How long we can sprint (seconds) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint", meta = (ClampMin = 0.0f, ClampMax = 30.0f))
	float SprintDuration = 3.0f;

	/** Walk speed while sprinting */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint")
	float SprintSpeed = 600.0f;

	/** Speed during stamina recovery */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint")
	float RecoverySpeed = 150.0f;

	/** Default walk speed (snapshot from character on BeginPlay) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint")
	float WalkSpeed = 250.0f;

	/** Tick interval for meter updates */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sprint", meta = (ClampMin = 0.01f, ClampMax = 0.5f))
	float TickInterval = 0.033f;

	// ---- State ----

	UPROPERTY(BlueprintReadOnly, Category = "Sprint")
	bool bIsSprinting = false;

	UPROPERTY(BlueprintReadOnly, Category = "Sprint")
	bool bIsRecovering = false;

	// ---- Delegates ----

	UPROPERTY(BlueprintAssignable, Category = "Sprint")
	FOnSprintMeterUpdated OnSprintMeterUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Sprint")
	FOnSprintStateChanged OnSprintStateChanged;

	// ---- API ----

	UFUNCTION(BlueprintCallable, Category = "Sprint")
	void StartSprint();

	UFUNCTION(BlueprintCallable, Category = "Sprint")
	void StopSprint();

	/** Returns sprint meter as 0.0–1.0 */
	UFUNCTION(BlueprintPure, Category = "Sprint")
	float GetMeterPercent() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void SprintTick();

	float SprintMeter = 0.0f;
	float OriginalWalkSpeed = 250.0f;
	FTimerHandle TickTimer;

	void ApplySpeed(float Speed);
};
