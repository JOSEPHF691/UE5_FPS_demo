// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "AnimNodes/AnimNode_CopyPoseFromMesh.h"
#include "AnimNodes/AnimNode_Slot.h"
#include "TacticalSpectatorFPAnimInstance.generated.h"

/**
 * Native animation graph used only for a remote pawn's first-person mesh while
 * it is being observed. It deliberately has no Blueprint update graph because
 * remote pawns do not receive their PlayerController on other clients.
 */
USTRUCT()
struct TCAMP_FPS_API FTacticalSpectatorFPAnimInstanceProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FTacticalSpectatorFPAnimInstanceProxy() = default;
	explicit FTacticalSpectatorFPAnimInstanceProxy(UAnimInstance* InAnimInstance)
		: FAnimInstanceProxy(InAnimInstance)
	{
	}

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;
	virtual bool Evaluate(FPoseContext& Output) override;
	virtual FAnimNode_Base* GetCustomRootNode() override { return &MontageSlot; }

private:
	/** Baseline pose comes from the attached, already replicated third-person mesh. */
	FAnimNode_CopyPoseFromMesh CopyPoseFromMesh;

	/** Keeps the normal weapon fire/reload montage path available to spectators. */
	FAnimNode_Slot MontageSlot;
};

UCLASS(Transient, NotBlueprintable)
class TCAMP_FPS_API UTacticalSpectatorFPAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
};

