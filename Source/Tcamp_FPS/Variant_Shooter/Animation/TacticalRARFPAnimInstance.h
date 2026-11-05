// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_ApplyMeshSpaceAdditive.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "AnimNodes/AnimNode_ApplyAdditive.h"
#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "AnimNodes/AnimNode_Slot.h"
#include "AnimNodes/AnimNode_TwoWayBlend.h"
#include "TacticalRARFPAnimInstance.generated.h"

class UAnimSequence;
class UAnimMontage;
class UBlendSpace;

/**
 * A small, self-contained first-person graph for the RAR rifle animations.
 *
 * RAR's ABP_IG_Character expects its own character/weapon Blueprint interfaces.
 * This graph uses the authored animations without importing that gameplay stack.
 */
USTRUCT()
struct TCAMP_FPS_API FTacticalRARFPAnimInstanceProxy final : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FTacticalRARFPAnimInstanceProxy() = default;
	explicit FTacticalRARFPAnimInstanceProxy(UAnimInstance* InAnimInstance)
		: FAnimInstanceProxy(InAnimInstance)
	{
	}

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;
	virtual bool Evaluate(FPoseContext& Output) override;
	virtual FAnimNode_Base* GetCustomRootNode() override { return &FireSlot; }

private:
	// RAR's movement/jump assets are additive, so they need an absolute base pose.
	FAnimNode_SequencePlayer_Standalone IdlePose;
	FAnimNode_SequencePlayer_Standalone TransitionIdlePose;
	FAnimNode_SequencePlayer_Standalone AimPose;
	FAnimNode_BlendSpacePlayer_Standalone Locomotion;
	FAnimNode_BlendSpacePlayer_Standalone TransitionLocomotion;
	FAnimNode_SequencePlayer_Standalone AimedIdleLoop;
	FAnimNode_SequencePlayer_Standalone AimedWalkLoop;
	FAnimNode_SequencePlayer_Standalone AimInTransition;
	FAnimNode_SequencePlayer_Standalone AimOutTransition;
	FAnimNode_SequencePlayer_Standalone FallingLoop;
	FAnimNode_ApplyAdditive GroundedAdditive;
	FAnimNode_ApplyAdditive TransitionGroundedAdditive;
	FAnimNode_ApplyAdditive AimedIdleAdditive;
	FAnimNode_ApplyAdditive AimedWalkAdditive;
	FAnimNode_TwoWayBlend AimPoseBlend;
	FAnimNode_ApplyMeshSpaceAdditive AimInAdditive;
	FAnimNode_ApplyMeshSpaceAdditive AimOutAdditive;
	FAnimNode_TwoWayBlend AimTransitionSelector;
	FAnimNode_TwoWayBlend AimTransitionBlend;
	FAnimNode_ApplyMeshSpaceAdditive AirborneAdditive;

	// RAR's character reload/equip and fire montages use these distinct slots.
	FAnimNode_Slot ActionSlot;
	FAnimNode_Slot FireSlot;

	FVector BlendInput = FVector::ZeroVector;
	float AirborneAlpha = 0.0f;
	float AimAlpha = 0.0f;
	float AimedWalkAlpha = 0.0f;
	float AimTransitionAlpha = 0.0f;
	float AimTransitionExitAlpha = 0.0f;
	float AimTransitionElapsed = 0.0f;
	float AimInDuration = 0.0f;
	float AimOutDuration = 0.0f;
	bool bWasAiming = false;
	bool bTransitionOut = false;
	bool bAimTransitionActive = false;
	float HorizontalMin = -1.0f;
	float HorizontalMax = 1.0f;
	float VerticalMin = -1.0f;
	float VerticalMax = 1.0f;
};

UCLASS(Transient, NotBlueprintable)
class TCAMP_FPS_API UTacticalRARFPAnimInstance final : public UAnimInstance
{
	GENERATED_BODY()

public:
	UTacticalRARFPAnimInstance();

	UAnimSequence* GetIdlePoseSequence() const { return IdlePoseSequence; }
	UAnimSequence* GetAimPoseSequence() const { return AimPoseSequence; }
	UBlendSpace* GetLocomotionBlendSpace() const { return LocomotionBlendSpace; }
	UAnimSequence* GetAimedIdleLoopSequence() const { return AimedIdleLoopSequence; }
	UAnimSequence* GetAimedWalkLoopSequence() const { return AimedWalkLoopSequence; }
	UAnimSequence* GetAimInTransitionSequence() const { return AimInTransitionSequence; }
	UAnimSequence* GetAimOutTransitionSequence() const { return AimOutTransitionSequence; }
	UAnimSequence* GetFallingLoopSequence() const { return FallingLoopSequence; }
	/** RAR's aimed fire is an additive sequence played through the existing overlay slot. */
	UAnimMontage* GetAimedFireMontage();
	UAnimMontage* GetAimedReloadMontage();
	void SetAimState(bool bInAiming, float InAlpha)
	{
		bIsAiming = bInAiming;
		AimBlendAlpha = FMath::Clamp(InAlpha, 0.0f, 1.0f);
	}
	bool IsAiming() const { return bIsAiming; }
	float GetAimBlendAlpha() const { return AimBlendAlpha; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	UPROPERTY()
	TObjectPtr<UAnimSequence> IdlePoseSequence;

	UPROPERTY()
	TObjectPtr<UAnimSequence> AimPoseSequence;

	UPROPERTY()
	TObjectPtr<UBlendSpace> LocomotionBlendSpace;

	UPROPERTY()
	TObjectPtr<UAnimSequence> AimedIdleLoopSequence;

	UPROPERTY()
	TObjectPtr<UAnimSequence> AimedWalkLoopSequence;

	UPROPERTY()
	TObjectPtr<UAnimSequence> AimInTransitionSequence;

	UPROPERTY()
	TObjectPtr<UAnimSequence> AimOutTransitionSequence;

	UPROPERTY()
	TObjectPtr<UAnimSequence> FallingLoopSequence;

	UPROPERTY()
	TObjectPtr<UAnimSequence> AimedFireSequence;

	UPROPERTY()
	TObjectPtr<UAnimSequence> AimedReloadSequence;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> AimedFireMontage;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> AimedReloadMontage;

	float AimBlendAlpha = 0.0f;
	bool bIsAiming = false;
};
