// Copyright Epic Games, Inc. All Rights Reserved.

#include "TacticalSpectatorFPAnimInstance.h"

void FTacticalSpectatorFPAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	// FirstPersonMesh is attached directly to the character's third-person mesh.
	// Copying that pose gives the observer the same replicated locomotion without
	// depending on a PlayerController that does not exist on this client.
	CopyPoseFromMesh.bUseAttachedParent = true;
	CopyPoseFromMesh.bCopyCurves = true;
	CopyPoseFromMesh.bCopyCustomAttributes = true;

	// All configured first-person fire/reload montages use the Arms slot.
	MontageSlot.SlotName = TEXT("Arms");
	MontageSlot.bAlwaysUpdateSourcePose = true;
	MontageSlot.Source.SetLinkNode(&CopyPoseFromMesh);

	FAnimInstanceProxy::Initialize(InAnimInstance);
}

void FTacticalSpectatorFPAnimInstanceProxy::PreUpdate(
	UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	CopyPoseFromMesh.PreUpdate(InAnimInstance);
}

void FTacticalSpectatorFPAnimInstanceProxy::UpdateAnimationNode(
	const FAnimationUpdateContext& InContext)
{
	UpdateCounter.Increment();
	MontageSlot.Update_AnyThread(InContext);
}

bool FTacticalSpectatorFPAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	MontageSlot.Evaluate_AnyThread(Output);
	return true;
}

FAnimInstanceProxy* UTacticalSpectatorFPAnimInstance::CreateAnimInstanceProxy()
{
	return new FTacticalSpectatorFPAnimInstanceProxy(this);
}
