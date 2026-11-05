// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_ApplyMeshSpaceAdditive.h"
#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "AnimNodes/AnimNode_Slot.h"
#include "MurdockGuardAnimInstance.generated.h"

class UBlendSpace;

/**
 * Small native animation graph for the treasure guard.
 *
 * The marketplace Murdock AnimBP is coupled to its demo character through
 * melee-combo events. Keeping the guard graph native lets us reuse the clean
 * locomotion and aim assets without loading or depending on that demo logic.
 */
USTRUCT()
struct TCAMP_FPS_API FMurdockGuardAnimInstanceProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FMurdockGuardAnimInstanceProxy() = default;
	explicit FMurdockGuardAnimInstanceProxy(UAnimInstance* InAnimInstance)
		: FAnimInstanceProxy(InAnimInstance)
	{
	}

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;
	virtual bool Evaluate(FPoseContext& Output) override;
	virtual FAnimNode_Base* GetCustomRootNode() override { return &UpperBodySlot; }

private:
	/** Direction (X) and speed (Y) drive Murdock_Locomotion_BS. */
	FAnimNode_BlendSpacePlayer_Standalone Locomotion;

	/** Yaw (X) and pitch (Y) sample Murdock's mesh-space additive aim pose. */
	FAnimNode_BlendSpacePlayer_Standalone AimOffset;

	/** Layers the sampled aim offset over the full-body locomotion pose. */
	FAnimNode_ApplyMeshSpaceAdditive AimLayer;

	/** Murdock's authored fire montage uses the UpperBody slot. */
	FAnimNode_Slot UpperBodySlot;

	float Speed = 0.0f;
	float Direction = 0.0f;
	float AimYaw = 0.0f;
	float AimPitch = 0.0f;
};

UCLASS(Transient, NotBlueprintable)
class TCAMP_FPS_API UMurdockGuardAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UMurdockGuardAnimInstance();

	UBlendSpace* GetLocomotionBlendSpace() const { return LocomotionBlendSpace; }
	UBlendSpace* GetAimOffsetBlendSpace() const { return AimOffsetBlendSpace; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UBlendSpace> LocomotionBlendSpace;

	UPROPERTY(Transient)
	TObjectPtr<UBlendSpace> AimOffsetBlendSpace;
};
