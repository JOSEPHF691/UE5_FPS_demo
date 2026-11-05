// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "TacticalThirdPersonMeshComponent.generated.h"

class ATacticalSpectatorAnimationController;

/**
 * Third-person character mesh compatibility layer for legacy AnimBPs that read
 * Pawn.Controller directly. PlayerControllers are not replicated to other
 * clients, so a simulated proxy otherwise loses its replicated aim pitch.
 */
UCLASS(ClassGroup = (Rendering), NotBlueprintable)
class TCAMP_FPS_API UTacticalThirdPersonMeshComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickPose(float DeltaTime, bool bNeedsValidRootMotion) override;

private:
	void EnsureRemoteAnimationController();
	void ReleaseRemoteAnimationController();

	UPROPERTY(Transient)
	TObjectPtr<ATacticalSpectatorAnimationController> RemoteAnimationController;
};
