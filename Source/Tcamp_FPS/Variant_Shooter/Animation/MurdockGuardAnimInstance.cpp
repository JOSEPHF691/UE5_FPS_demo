// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variant_Shooter/Animation/MurdockGuardAnimInstance.h"

#include "Animation/BlendSpace.h"
#include "GameFramework/Pawn.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float MinAimAngle = -90.0f;
	constexpr float MaxAimAngle = 90.0f;
}

UMurdockGuardAnimInstance::UMurdockGuardAnimInstance()
{
	static ConstructorHelpers::FObjectFinder<UBlendSpace> LocomotionFinder(
		TEXT("/Game/ParagonMurdock/Characters/Heroes/Murdock/Animations/BlendSpaces/Murdock_Locomotion_BS.Murdock_Locomotion_BS"));
	LocomotionBlendSpace = LocomotionFinder.Object;

	static ConstructorHelpers::FObjectFinder<UBlendSpace> AimOffsetFinder(
		TEXT("/Game/ParagonMurdock/Characters/Heroes/Murdock/Animations/AimOffsets/Idle_AO_BlendSpace.Idle_AO_BlendSpace"));
	AimOffsetBlendSpace = AimOffsetFinder.Object;
}

FAnimInstanceProxy* UMurdockGuardAnimInstance::CreateAnimInstanceProxy()
{
	return new FMurdockGuardAnimInstanceProxy(this);
}

void FMurdockGuardAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	if (const UMurdockGuardAnimInstance* GuardAnimInstance =
		Cast<UMurdockGuardAnimInstance>(InAnimInstance))
	{
		Locomotion.SetBlendSpace(GuardAnimInstance->GetLocomotionBlendSpace());
		AimOffset.SetBlendSpace(GuardAnimInstance->GetAimOffsetBlendSpace());
	}

	Locomotion.SetLoop(true);
	Locomotion.SetPlayRate(1.0f);

	AimOffset.SetLoop(true);
	AimOffset.SetPlayRate(1.0f);

	AimLayer.AlphaInputType = EAnimAlphaInputType::Float;
	AimLayer.Alpha = 1.0f;
	AimLayer.bRootSpaceAdditive = false;
	AimLayer.Base.SetLinkNode(&Locomotion);
	AimLayer.Additive.SetLinkNode(&AimOffset);

	UpperBodySlot.SlotName = TEXT("UpperBody");
	UpperBodySlot.bAlwaysUpdateSourcePose = true;
	UpperBodySlot.Source.SetLinkNode(&AimLayer);

	FAnimInstanceProxy::Initialize(InAnimInstance);
}

void FMurdockGuardAnimInstanceProxy::PreUpdate(
	UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	const APawn* Pawn = InAnimInstance ? InAnimInstance->TryGetPawnOwner() : nullptr;
	if (!Pawn)
	{
		Speed = 0.0f;
		Direction = 0.0f;
		AimYaw = 0.0f;
		AimPitch = 0.0f;
		return;
	}

	const FVector Velocity = Pawn->GetVelocity();
	Speed = Velocity.Size2D();

	const FVector LocalVelocity =
		Pawn->GetActorTransform().InverseTransformVectorNoScale(Velocity);
	Direction = LocalVelocity.IsNearlyZero()
		? 0.0f
		: FMath::RadiansToDegrees(FMath::Atan2(LocalVelocity.Y, LocalVelocity.X));

	const FRotator AimDelta =
		(Pawn->GetBaseAimRotation() - Pawn->GetActorRotation()).GetNormalized();
	AimYaw = FMath::Clamp(AimDelta.Yaw, MinAimAngle, MaxAimAngle);
	AimPitch = FMath::Clamp(AimDelta.Pitch, MinAimAngle, MaxAimAngle);
}

void FMurdockGuardAnimInstanceProxy::UpdateAnimationNode(
	const FAnimationUpdateContext& InContext)
{
	UpdateCounter.Increment();
	Locomotion.SetPosition(FVector(Direction, Speed, 0.0f));
	AimOffset.SetPosition(FVector(AimYaw, AimPitch, 0.0f));
	UpperBodySlot.Update_AnyThread(InContext);
}

bool FMurdockGuardAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	UpperBodySlot.Evaluate_AnyThread(Output);
	return true;
}
