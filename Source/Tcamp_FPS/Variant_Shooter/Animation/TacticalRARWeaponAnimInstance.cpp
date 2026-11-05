// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variant_Shooter/Animation/TacticalRARWeaponAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "UObject/ConstructorHelpers.h"

UTacticalRARWeaponAnimInstance::UTacticalRARWeaponAnimInstance()
{
	static ConstructorHelpers::FObjectFinder<UAnimSequence> ReferenceFinder(
		TEXT("/Game/InfimaGames/RealisticAssaultRifle/Art/Weapon/Animations/A_RAR_FP_WEP_AssaultRifle_Reference.A_RAR_FP_WEP_AssaultRifle_Reference"));
	ReferenceSequence = ReferenceFinder.Object;
}

FAnimInstanceProxy* UTacticalRARWeaponAnimInstance::CreateAnimInstanceProxy()
{
	return new FTacticalRARWeaponAnimInstanceProxy(this);
}

void FTacticalRARWeaponAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	if (const UTacticalRARWeaponAnimInstance* RARInstance =
		Cast<UTacticalRARWeaponAnimInstance>(InAnimInstance))
	{
		ReferencePose.SetSequence(RARInstance->GetReferenceSequence());
	}
	ReferencePose.SetLoopAnimation(true);
	ReferencePose.SetPlayRate(1.0f);

	// The RAR weapon fire and reload montages both author a Standing track.
	// Their "Default" section name is not the animation slot.
	MontageSlot.SlotName = TEXT("Standing");
	MontageSlot.bAlwaysUpdateSourcePose = true;
	MontageSlot.Source.SetLinkNode(&ReferencePose);

	FAnimInstanceProxy::Initialize(InAnimInstance);
}

void FTacticalRARWeaponAnimInstanceProxy::UpdateAnimationNode(
	const FAnimationUpdateContext& InContext)
{
	UpdateCounter.Increment();
	MontageSlot.Update_AnyThread(InContext);
}

bool FTacticalRARWeaponAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	MontageSlot.Evaluate_AnyThread(Output);
	return true;
}
