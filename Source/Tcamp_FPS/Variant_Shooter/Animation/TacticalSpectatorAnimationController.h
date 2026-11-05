// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Controller.h"
#include "TacticalSpectatorAnimationController.generated.h"

class APawn;

/**
 * Local, non-replicated controller used only while legacy animation graphs
 * evaluate a remote pawn. It exposes RemoteViewPitch16 as ControlRotation
 * without changing gameplay possession.
 */
UCLASS(Transient, NotBlueprintable)
class TCAMP_FPS_API ATacticalSpectatorAnimationController final : public AController
{
	GENERATED_BODY()

public:
	ATacticalSpectatorAnimationController();

	void SetAnimationSourcePawn(APawn* InPawn);

	virtual bool IsLocalController() const override { return false; }
	virtual FRotator GetControlRotation() const override;

private:
	TWeakObjectPtr<APawn> AnimationSourcePawn;
};
