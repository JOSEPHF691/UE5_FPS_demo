// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variant_Shooter/Animation/TacticalRARFPAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/BlendSpace.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	float MapSignedInputToBlendAxis(float SignedInput, float Min, float Max)
	{
		// RAR's axes are named Horizontal/Vertical. Read their actual bounds so
		// the graph remains correct if the asset's authored range is not [-1, 1].
		return FMath::GetMappedRangeValueClamped(
			FVector2D(-1.0f, 1.0f), FVector2D(Min, Max),
			FMath::Clamp(SignedInput, -1.0f, 1.0f));
	}
}

UTacticalRARFPAnimInstance::UTacticalRARFPAnimInstance()
{
	static ConstructorHelpers::FObjectFinder<UAnimSequence> IdlePoseFinder(
		TEXT("/Game/InfimaGames/RealisticAssaultRifle/Art/Character/Animations/Poses/A_RAR_FP_PCH_AssaultRifle_Idle_Pose.A_RAR_FP_PCH_AssaultRifle_Idle_Pose"));
	IdlePoseSequence = IdlePoseFinder.Object;
	static ConstructorHelpers::FObjectFinder<UAnimSequence> AimPoseFinder(
		TEXT("/Game/InfimaGames/RealisticAssaultRifle/Art/Character/Animations/Poses/A_RAR_FP_PCH_AssaultRifle_Aim_Pose.A_RAR_FP_PCH_AssaultRifle_Aim_Pose"));
	AimPoseSequence = AimPoseFinder.Object;

	static ConstructorHelpers::FObjectFinder<UBlendSpace> LocomotionFinder(
		TEXT("/Game/InfimaGames/RealisticAssaultRifle/Art/Character/Animations/Movement/BS_RAR_FP_PCH_AssaultRifle_Movement.BS_RAR_FP_PCH_AssaultRifle_Movement"));
	LocomotionBlendSpace = LocomotionFinder.Object;
	static ConstructorHelpers::FObjectFinder<UAnimSequence> AimedIdleFinder(
		TEXT("/Game/InfimaGames/RealisticAssaultRifle/Art/Character/Animations/Movement/A_RAR_FP_PCH_AssaultRifle_Idle_Loop_Aimed.A_RAR_FP_PCH_AssaultRifle_Idle_Loop_Aimed"));
	AimedIdleLoopSequence = AimedIdleFinder.Object;
	static ConstructorHelpers::FObjectFinder<UAnimSequence> AimedWalkFinder(
		TEXT("/Game/InfimaGames/RealisticAssaultRifle/Art/Character/Animations/Movement/A_RAR_FP_PCH_AssaultRifle_Walk_F_Loop_Aimed.A_RAR_FP_PCH_AssaultRifle_Walk_F_Loop_Aimed"));
	AimedWalkLoopSequence = AimedWalkFinder.Object;
	static ConstructorHelpers::FObjectFinder<UAnimSequence> AimInFinder(
		TEXT("/Game/InfimaGames/RealisticAssaultRifle/Art/Character/Animations/Transitions/A_RAR_FP_PCH_AssaultRifle_Transition_Aim_In.A_RAR_FP_PCH_AssaultRifle_Transition_Aim_In"));
	AimInTransitionSequence = AimInFinder.Object;
	static ConstructorHelpers::FObjectFinder<UAnimSequence> AimOutFinder(
		TEXT("/Game/InfimaGames/RealisticAssaultRifle/Art/Character/Animations/Transitions/A_RAR_FP_PCH_AssaultRifle_Transition_Aim_Out.A_RAR_FP_PCH_AssaultRifle_Transition_Aim_Out"));
	AimOutTransitionSequence = AimOutFinder.Object;

	static ConstructorHelpers::FObjectFinder<UAnimSequence> FallingFinder(
		TEXT("/Game/InfimaGames/RealisticAssaultRifle/Art/Character/Animations/Movement/A_RAR_FP_PCH_AssaultRifle_Jump_Falling_Loop.A_RAR_FP_PCH_AssaultRifle_Jump_Falling_Loop"));
	FallingLoopSequence = FallingFinder.Object;
	static ConstructorHelpers::FObjectFinder<UAnimSequence> AimedFireFinder(
		TEXT("/Game/InfimaGames/RealisticAssaultRifle/Art/Character/Animations/Actions/A_RAR_FP_PCH_AssaultRifle_Fire_Aimed.A_RAR_FP_PCH_AssaultRifle_Fire_Aimed"));
	AimedFireSequence = AimedFireFinder.Object;
	static ConstructorHelpers::FObjectFinder<UAnimSequence> AimedReloadFinder(
		TEXT("/Game/InfimaGames/RealisticAssaultRifle/Art/Character/Animations/Actions/A_RAR_FP_PCH_AssaultRifle_Reload_Aimed.A_RAR_FP_PCH_AssaultRifle_Reload_Aimed"));
	AimedReloadSequence = AimedReloadFinder.Object;
}

UAnimMontage* UTacticalRARFPAnimInstance::GetAimedFireMontage()
{
	if (!AimedFireMontage && AimedFireSequence)
	{
		AimedFireMontage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(
			AimedFireSequence, TEXT("Overlay Standing"), 0.03f, 0.08f,
			1.0f, 1);
	}
	return AimedFireMontage;
}

UAnimMontage* UTacticalRARFPAnimInstance::GetAimedReloadMontage()
{
	if (!AimedReloadMontage && AimedReloadSequence)
	{
		AimedReloadMontage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(
			AimedReloadSequence, TEXT("Action Standing"), 0.08f, 0.12f,
			1.0f, 1);
	}
	return AimedReloadMontage;
}

FAnimInstanceProxy* UTacticalRARFPAnimInstance::CreateAnimInstanceProxy()
{
	return new FTacticalRARFPAnimInstanceProxy(this);
}

void FTacticalRARFPAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	if (const UTacticalRARFPAnimInstance* RARInstance =
		Cast<UTacticalRARFPAnimInstance>(InAnimInstance))
	{
		IdlePose.SetSequence(RARInstance->GetIdlePoseSequence());
		TransitionIdlePose.SetSequence(RARInstance->GetIdlePoseSequence());
		AimPose.SetSequence(RARInstance->GetAimPoseSequence());
		if (UBlendSpace* BlendSpace = RARInstance->GetLocomotionBlendSpace())
		{
			Locomotion.SetBlendSpace(BlendSpace);
			TransitionLocomotion.SetBlendSpace(BlendSpace);
			HorizontalMin = BlendSpace->GetBlendParameter(0).Min;
			HorizontalMax = BlendSpace->GetBlendParameter(0).Max;
			VerticalMin = BlendSpace->GetBlendParameter(1).Min;
			VerticalMax = BlendSpace->GetBlendParameter(1).Max;
		}
		FallingLoop.SetSequence(RARInstance->GetFallingLoopSequence());
		AimedIdleLoop.SetSequence(RARInstance->GetAimedIdleLoopSequence());
		AimedWalkLoop.SetSequence(RARInstance->GetAimedWalkLoopSequence());
		AimInTransition.SetSequence(RARInstance->GetAimInTransitionSequence());
		AimOutTransition.SetSequence(RARInstance->GetAimOutTransitionSequence());
		AimInDuration = RARInstance->GetAimInTransitionSequence()
			? RARInstance->GetAimInTransitionSequence()->GetPlayLength() : 0.0f;
		AimOutDuration = RARInstance->GetAimOutTransitionSequence()
			? RARInstance->GetAimOutTransitionSequence()->GetPlayLength() : 0.0f;
	}

	IdlePose.SetLoopAnimation(true);
	IdlePose.SetPlayRate(1.0f);
	TransitionIdlePose.SetLoopAnimation(true);
	TransitionIdlePose.SetPlayRate(1.0f);
	AimPose.SetLoopAnimation(true);
	AimPose.SetPlayRate(1.0f);
	Locomotion.SetLoop(true);
	Locomotion.SetPlayRate(1.0f);
	TransitionLocomotion.SetLoop(true);
	TransitionLocomotion.SetPlayRate(1.0f);
	AimedIdleLoop.SetLoopAnimation(true);
	AimedIdleLoop.SetPlayRate(1.0f);
	AimedWalkLoop.SetLoopAnimation(true);
	AimedWalkLoop.SetPlayRate(1.0f);
	AimInTransition.SetLoopAnimation(false);
	AimInTransition.SetPlayRate(1.0f);
	AimOutTransition.SetLoopAnimation(false);
	AimOutTransition.SetPlayRate(1.0f);
	FallingLoop.SetLoopAnimation(true);
	FallingLoop.SetPlayRate(1.0f);

	// The movement blend space is local-space additive, while the falling loop
	// is mesh-space additive. Evaluating either as a complete pose gives the
	// additive identity (zero bone scale), collapsing the camera and weapon.
	GroundedAdditive.AlphaInputType = EAnimAlphaInputType::Float;
	GroundedAdditive.Base.SetLinkNode(&IdlePose);
	GroundedAdditive.Additive.SetLinkNode(&Locomotion);
	TransitionGroundedAdditive.AlphaInputType = EAnimAlphaInputType::Float;
	TransitionGroundedAdditive.Base.SetLinkNode(&TransitionIdlePose);
	TransitionGroundedAdditive.Additive.SetLinkNode(&TransitionLocomotion);

	// Aim_Pose is an absolute pose. The authored aimed idle/walk loops are
	// local-space additive, so evaluate them on that pose before blending with
	// hip fire. Never evaluate an additive sequence as a stand-alone full pose.
	AimedIdleAdditive.AlphaInputType = EAnimAlphaInputType::Float;
	AimedIdleAdditive.Base.SetLinkNode(&AimPose);
	AimedIdleAdditive.Additive.SetLinkNode(&AimedIdleLoop);
	AimedIdleAdditive.Alpha = 1.0f;
	AimedWalkAdditive.AlphaInputType = EAnimAlphaInputType::Float;
	AimedWalkAdditive.Base.SetLinkNode(&AimedIdleAdditive);
	AimedWalkAdditive.Additive.SetLinkNode(&AimedWalkLoop);

	AimPoseBlend.AlphaInputType = EAnimAlphaInputType::Float;
	AimPoseBlend.A.SetLinkNode(&GroundedAdditive);
	AimPoseBlend.B.SetLinkNode(&AimedWalkAdditive);

	// RAR's Aim In/Out clips are mesh-space additives authored against the
	// hip idle pose. Aim In runs on that base and fades into steady ADS over
	// the clip. The separately authored Aim Out does not match the steady ADS
	// start pose, so release uses AimPoseBlend's continuous alpha instead.
	AimInAdditive.AlphaInputType = EAnimAlphaInputType::Float;
	AimInAdditive.Alpha = 1.0f;
	AimInAdditive.Base.SetLinkNode(&TransitionGroundedAdditive);
	AimInAdditive.Additive.SetLinkNode(&AimInTransition);
	AimOutAdditive.AlphaInputType = EAnimAlphaInputType::Float;
	AimOutAdditive.Alpha = 1.0f;
	AimOutAdditive.Base.SetLinkNode(&TransitionGroundedAdditive);
	AimOutAdditive.Additive.SetLinkNode(&AimOutTransition);
	AimTransitionSelector.AlphaInputType = EAnimAlphaInputType::Float;
	AimTransitionSelector.A.SetLinkNode(&AimInAdditive);
	AimTransitionSelector.B.SetLinkNode(&AimOutAdditive);
	AimTransitionBlend.AlphaInputType = EAnimAlphaInputType::Float;
	AimTransitionBlend.A.SetLinkNode(&AimPoseBlend);
	AimTransitionBlend.B.SetLinkNode(&AimTransitionSelector);

	AirborneAdditive.AlphaInputType = EAnimAlphaInputType::Float;
	AirborneAdditive.Base.SetLinkNode(&AimTransitionBlend);
	AirborneAdditive.Additive.SetLinkNode(&FallingLoop);

	ActionSlot.SlotName = TEXT("Action Standing");
	ActionSlot.bAlwaysUpdateSourcePose = true;
	ActionSlot.Source.SetLinkNode(&AirborneAdditive);

	FireSlot.SlotName = TEXT("Overlay Standing");
	FireSlot.bAlwaysUpdateSourcePose = true;
	FireSlot.Source.SetLinkNode(&ActionSlot);

	FAnimInstanceProxy::Initialize(InAnimInstance);
}

void FTacticalRARFPAnimInstanceProxy::PreUpdate(
	UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	const APawn* Pawn = InAnimInstance ? InAnimInstance->TryGetPawnOwner() : nullptr;
	const UPawnMovementComponent* Movement = Pawn ? Pawn->GetMovementComponent() : nullptr;
	const UCharacterMovementComponent* CharacterMovement =
		Cast<UCharacterMovementComponent>(Movement);
	if (!Pawn)
	{
		BlendInput = FVector(
			MapSignedInputToBlendAxis(0.0f, HorizontalMin, HorizontalMax),
			MapSignedInputToBlendAxis(0.0f, VerticalMin, VerticalMax), 0.0f);
		AirborneAlpha = 0.0f;
		AimAlpha = 0.0f;
		AimedWalkAlpha = 0.0f;
		AimTransitionAlpha = 0.0f;
		AimTransitionExitAlpha = 0.0f;
		bAimTransitionActive = false;
		return;
	}

	const FVector LocalVelocity = Pawn->GetActorTransform().InverseTransformVectorNoScale(
		Pawn->GetVelocity());
	const float MovementSpeed = CharacterMovement
		? FMath::Max(CharacterMovement->GetMaxSpeed(), 1.0f)
		: 600.0f;
	BlendInput = FVector(
		MapSignedInputToBlendAxis(
			LocalVelocity.Y / MovementSpeed, HorizontalMin, HorizontalMax),
		MapSignedInputToBlendAxis(
			LocalVelocity.X / MovementSpeed, VerticalMin, VerticalMax),
		0.0f);

	const float TargetAirborneAlpha = CharacterMovement && CharacterMovement->IsFalling()
		? 1.0f : 0.0f;
	AirborneAlpha = FMath::FInterpTo(
		AirborneAlpha, TargetAirborneAlpha, DeltaSeconds, 12.0f);
	const UTacticalRARFPAnimInstance* RARInstance =
		Cast<UTacticalRARFPAnimInstance>(InAnimInstance);
	const float PreviousAimAlpha = AimAlpha;
	AimAlpha = RARInstance ? RARInstance->GetAimBlendAlpha() : 0.0f;
	const bool bAimingNow = RARInstance && RARInstance->IsAiming();
	if (bAimingNow != bWasAiming)
	{
		const bool bAlreadyFadingAimIn = bTransitionOut && bAimTransitionActive;
		bWasAiming = bAimingNow;
		if (!bAimingNow)
		{
			bTransitionOut = true;
			AimTransitionElapsed = 0.0f;
			// Keep any in-progress Aim In pose and fade it away. Switching the
			// selector straight to Aim Out frame zero replaces the whole pose on
			// the release frame, even when the gun was already fully aimed.
			AimTransitionExitAlpha = AimTransitionAlpha;
			bAimTransitionActive = AimTransitionExitAlpha > UE_KINDA_SMALL_NUMBER;
		}
		else if (!bAlreadyFadingAimIn)
		{
			bTransitionOut = false;
			AimTransitionElapsed = 0.0f;
			// A quick re-aim from halfway out is already near the target pose;
			// restarting the hip-authored Aim In clip there would jump backward.
			bAimTransitionActive = PreviousAimAlpha < 0.2f;
			if (bAimTransitionActive)
			{
				AimInTransition.SetAccumulatedTime(0.0f);
			}
		}
		// If the prior Aim In is already fading, let that short fade finish
		// despite a quick re-press. Replacing it mid-fade also creates a pop.
	}
	AimTransitionAlpha = 0.0f;
	if (bAimTransitionActive)
	{
		AimTransitionElapsed += DeltaSeconds;
		const float Duration = bTransitionOut ? 0.08f : AimInDuration;
		const float Progress = Duration > UE_KINDA_SMALL_NUMBER
			? FMath::Clamp(AimTransitionElapsed / Duration, 0.0f, 1.0f)
			: 1.0f;
		const float SmoothedProgress = Progress * Progress * (3.0f - 2.0f * Progress);
		AimTransitionAlpha = (bTransitionOut ? AimTransitionExitAlpha : 1.0f)
			* (1.0f - SmoothedProgress);
		if (AimTransitionElapsed >= Duration)
		{
			bAimTransitionActive = false;
		}
	}
	// The authored aimed walk is a forward gun-bob additive, not a directional
	// full-body locomotion cycle. Scale it by horizontal movement magnitude.
	AimedWalkAlpha = FMath::Clamp(
		FVector(LocalVelocity.X, LocalVelocity.Y, 0.0f).Size() / MovementSpeed,
		0.0f, 1.0f);
}

void FTacticalRARFPAnimInstanceProxy::UpdateAnimationNode(
	const FAnimationUpdateContext& InContext)
{
	UpdateCounter.Increment();
	Locomotion.SetPosition(BlendInput);
	TransitionLocomotion.SetPosition(BlendInput);
	GroundedAdditive.Alpha = 1.0f - AirborneAlpha;
	TransitionGroundedAdditive.Alpha = 1.0f - AirborneAlpha;
	AimedIdleAdditive.Alpha = 1.0f - AimedWalkAlpha;
	AimedWalkAdditive.Alpha = AimedWalkAlpha;
	AimPoseBlend.Alpha = AimAlpha;
	// Aim Out is driven by the same continuous AimAlpha as the base pose and
	// visual root. The separately authored AimOut clip's frame zero does not
	// match the steady ADS pose, so do not hard-select that branch on release.
	AimTransitionSelector.Alpha = 0.0f;
	AimTransitionBlend.Alpha = AimTransitionAlpha;
	AirborneAdditive.Alpha = AirborneAlpha;
	FireSlot.Update_AnyThread(InContext);
}

bool FTacticalRARFPAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	FireSlot.Evaluate_AnyThread(Output);
	return true;
}
