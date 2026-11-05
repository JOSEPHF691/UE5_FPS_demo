// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeSpaceConversions.h"
#include "AnimNodes/AnimNode_CopyPoseFromMesh.h"
#include "BoneControllers/AnimNode_Fabrik.h"
#include "TacticalSniperFPGripAnimInstance.generated.h"

class USkeletalMeshComponent;

/**
 * Manny long-gun grip overlay. The source mesh keeps its weapon AnimBP and
 * montages; this instance copies that completed pose and places only the
 * support palm on a weapon-specific foregrip. It serves both viewmodel and
 * world-space rifle/sniper presentation.
 */
UCLASS(Transient, NotBlueprintable)
class TCAMP_FPS_API UTacticalSniperFPGripAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Call after creating the overlay anim instance, and again when the gun changes. */
	bool Configure(USkeletalMeshComponent* InSourceMesh,
		USkeletalMeshComponent* InWeaponMesh,
		FName InGripSocketName = TEXT("FP_LeftHandGrip"),
		bool bInLockWristToWeapon = false);

	/** Solve a hand-bone effector from a desired palm contact and palm socket. */
	static bool BuildLeftHandEffector(
		const FTransform& PalmContactComponentSpace,
		const FTransform& PalmSocketLocalToHand,
		const FQuat& SourceHandRotationComponentSpace,
		FTransform& OutHandComponentSpace);

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
	TWeakObjectPtr<USkeletalMeshComponent> SourceMesh;
	TWeakObjectPtr<USkeletalMeshComponent> WeaponMesh;
	FName GripSocketName = NAME_None;
	bool bLockWristToWeapon = false;

	friend struct FTacticalSniperFPGripAnimInstanceProxy;
};

/** Copy Manny's weapon animation, then constrain only the left arm. */
struct FTacticalSniperFPGripAnimInstanceProxy final : FAnimInstanceProxy
{
	FTacticalSniperFPGripAnimInstanceProxy() = default;
	explicit FTacticalSniperFPGripAnimInstanceProxy(UAnimInstance* InAnimInstance);

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;
	virtual FAnimNode_Base* GetCustomRootNode() override;
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;

	FAnimNode_CopyPoseFromMesh CopySourcePose;
	FAnimNode_ConvertLocalToComponentSpace LocalToComponentSpace;
	FAnimNode_Fabrik LeftHandIK;
	FAnimNode_ConvertComponentToLocalSpace ComponentToLocalSpace;
	bool bReportedReady = false;
	float GripAlpha = 0.0f;
};
