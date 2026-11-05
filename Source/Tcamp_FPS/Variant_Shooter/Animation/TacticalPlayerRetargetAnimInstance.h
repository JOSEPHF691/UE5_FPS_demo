// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeSpaceConversions.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "BoneControllers/AnimNode_Fabrik.h"
#include "TacticalPlayerRetargetAnimInstance.generated.h"

class UIKRetargeter;
class USkeletalMeshComponent;
struct FRetargetProfile;

/**
 * Runtime-only animation bridge used by the SciFi Trooper presentation mesh.
 * The existing Manny mesh remains the authoritative animation driver, so every
 * weapon graph and montage is retargeted without duplicating gameplay assets.
 */
UCLASS(Transient, NotBlueprintable)
class TCAMP_FPS_API UTacticalPlayerRetargetAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UTacticalPlayerRetargetAnimInstance(const FObjectInitializer& ObjectInitializer);

	bool ConfigureRetarget(
		UIKRetargeter* InRetargeter,
		USkeletalMeshComponent* InSourceMeshComponent,
		bool bMatchSourceHandTransform = false);

	/**
	 * Builds the first-person-only profile that keeps both target wrists aligned
	 * with the final Manny weapon pose. Exposed as a pure native helper so the
	 * runtime contract can be covered without editing or duplicating the asset.
	 */
	static bool BuildFirstPersonWeaponGripProfile(
		const UIKRetargeter* InRetargeter,
		FRetargetProfile& OutProfile);

	/**
	 * Convert the completed Manny left-hand pose into an effector transform
	 * relative to HandGrip_R. Applying that transform in the matching target
	 * socket space preserves pistol/rifle grip, fire and reload motion without reading the
	 * visible weapon back into the animation graph.
	 */
	static bool BuildFirstPersonLeftHandGripEffector(
		const FTransform& SourceLeftHandComponentTransform,
		const FTransform& SourceWeaponGripComponentTransform,
		FTransform& OutEffectorTransform);

	/**
	 * Builds the target hand_r component-space transform whose HandGrip_R socket
	 * exactly matches the completed source HandGrip_R. This removes skeleton-axis
	 * drift before the visible weapon and left support hand consume that socket.
	 */
	static bool BuildFirstPersonRightHandGripEffector(
		const FTransform& SourceWeaponGripComponentTransform,
		const FTransform& TargetWeaponGripLocalTransform,
		FTransform& OutEffectorTransform);

	/** Builds the same socket-matching hand transform while rotating the complete
	 * hand/weapon assembly until the weapon muzzle faces the requested direction. */
	static bool BuildFirstPersonAimedRightHandGripEffector(
		const FTransform& SourceWeaponGripComponentTransform,
		const FTransform& TargetWeaponGripLocalTransform,
		const FTransform& WeaponMuzzleRelativeToGripTransform,
		const FVector& DesiredMuzzleForwardComponentSpace,
		FTransform& OutEffectorTransform);

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
	UPROPERTY(Transient)
	FAnimNode_RetargetPoseFromMesh RetargetNode;

	/** Only the owner/spectator first-person bridge runs the final left-arm IK. */
	bool bUseFirstPersonLeftHandGripIK = false;

	friend struct FTacticalPlayerRetargetAnimInstanceProxy;
};

/** Runtime proxy: retarget pose, then constrain the visible left wrist to the source grip. */
struct FTacticalPlayerRetargetAnimInstanceProxy final : FAnimInstanceProxy
{
	FTacticalPlayerRetargetAnimInstanceProxy() = default;
	FTacticalPlayerRetargetAnimInstanceProxy(
		UAnimInstance* InAnimInstance,
		FAnimNode_RetargetPoseFromMesh* InRetargetNode);

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void CacheBones() override;
	virtual bool Evaluate(FPoseContext& Output) override;
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;
	virtual FAnimNode_Base* GetCustomRootNode() override;
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;

	FAnimNode_RetargetPoseFromMesh* RetargetNode = nullptr;
	FAnimNode_ConvertLocalToComponentSpace LocalToComponentSpace;
	FAnimNode_Fabrik RightHandGripIK;
	FAnimNode_Fabrik LeftHandGripIK;
	FAnimNode_ConvertComponentToLocalSpace ComponentToLocalSpace;
	TArray<FCompactPoseBoneIndex> LeftFingerBoneIndices;
	float LeftFingerGripAlpha = 0.0f;
	bool bUseRifleLeftFingerGrip = false;
	bool bReportedLeftHandGripIKReady = false;
};
