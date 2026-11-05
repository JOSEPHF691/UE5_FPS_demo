// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "TacticalFirstPersonMeshComponent.generated.h"

class AController;
class AShooterWeapon;
class UAnimInstance;
class USkeletalMeshComponent;

/**
 * First-person mesh with an animation-only controller scope for remote pawns.
 *
 * PlayerControllers intentionally do not exist on other clients, while the
 * project first-person AnimBPs read Pawn.Controller during their update. When
 * a remote pawn is spectated, this component exposes a non-local controller
 * only while Super::TickComponent updates that AnimBP. All gameplay outside
 * the animation tick continues to observe the pawn's real controller state.
 */
UCLASS(ClassGroup = (Rendering), meta = (BlueprintSpawnableComponent))
class TCAMP_FPS_API UTacticalFirstPersonMeshComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetAnimInstanceClass(UClass* NewClass) override;

	void SetSpectatorAnimationController(AController* InController);
	void SetSpectatorAnimInstanceClass(TSubclassOf<UAnimInstance> InAnimClass);

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Keep CopyPose AnimBPs working after the view mesh moves under the eye pivot. */
	void BindCopyPoseToCharacterMesh();
	void ReleaseCopyPoseSource();

	TWeakObjectPtr<USkeletalMeshComponent> BoundCopyPoseSource;
	EVisibilityBasedAnimTickOption PreviousSourceTickOption =
		EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	bool bOverrodeSourceTickOption = false;

	UPROPERTY(Transient)
	TObjectPtr<AController> SpectatorAnimationController;
};
