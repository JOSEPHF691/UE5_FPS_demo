// Copyright Epic Games, Inc. All Rights Reserved.

#include "TacticalFirstPersonMeshComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimClassInterface.h"
#include "AnimNodes/AnimNode_CopyPoseFromMesh.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "UObject/UnrealType.h"
#include "Variant_Shooter/TacticalDiagnostics.h"

void UTacticalFirstPersonMeshComponent::BeginPlay()
{
	Super::BeginPlay();
	BindCopyPoseToCharacterMesh();
}

void UTacticalFirstPersonMeshComponent::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	ReleaseCopyPoseSource();
	Super::EndPlay(EndPlayReason);
}

void UTacticalFirstPersonMeshComponent::SetAnimInstanceClass(UClass* NewClass)
{
	Super::SetAnimInstanceClass(NewClass);
	BindCopyPoseToCharacterMesh();
}

void UTacticalFirstPersonMeshComponent::BindCopyPoseToCharacterMesh()
{
	// The pistol AnimBP was authored while this component was attached directly
	// to the world-body mesh. Its CopyPose node has no explicit source and used
	// the attached parent. The stable eye pivot is now a scene component, so
	// that implicit lookup can no longer find a skeletal mesh.
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* BodyMesh = Character ? Character->GetMesh() : nullptr;
	UAnimInstance* AnimInstance = GetAnimInstance();
	const IAnimClassInterface* AnimClassInterface = AnimInstance
		? IAnimClassInterface::GetFromClass(AnimInstance->GetClass()) : nullptr;
	if (!IsValid(BodyMesh) || !AnimClassInterface)
	{
		ReleaseCopyPoseSource();
		return;
	}

	bool bCopiesBodyPose = false;
	bool bBoundNewSource = false;
	for (FStructProperty* NodeProperty : AnimClassInterface->GetAnimNodeProperties())
	{
		if (!NodeProperty || NodeProperty->Struct != FAnimNode_CopyPoseFromMesh::StaticStruct())
		{
			continue;
		}

		FAnimNode_CopyPoseFromMesh* CopyPose =
			NodeProperty->ContainerPtrToValuePtr<FAnimNode_CopyPoseFromMesh>(AnimInstance);
		if (!CopyPose)
		{
			continue;
		}
		if (!CopyPose->SourceMeshComponent.IsValid() && CopyPose->bUseAttachedParent)
		{
			CopyPose->SourceMeshComponent = BodyMesh;
			bBoundNewSource = true;
		}
		bCopiesBodyPose |= CopyPose->SourceMeshComponent.Get() == BodyMesh;
	}

	if (bCopiesBodyPose)
	{
		if (BoundCopyPoseSource.Get() != BodyMesh)
		{
			ReleaseCopyPoseSource();
			// Reparenting removed the old parent-child animation tick order.
			AddTickPrerequisiteComponent(BodyMesh);
			BoundCopyPoseSource = BodyMesh;
			PreviousSourceTickOption = BodyMesh->VisibilityBasedAnimTickOption;
			bOverrodeSourceTickOption = PreviousSourceTickOption
				!= EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			if (bOverrodeSourceTickOption)
			{
				// The world body is owner-hidden but must still produce a fresh
				// pose for the visible pistol arms (also while spectated).
				BodyMesh->VisibilityBasedAnimTickOption =
					EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			}
		}
	}
	else
	{
		ReleaseCopyPoseSource();
	}
	if (bBoundNewSource)
	{
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=FirstPersonCopyPoseBound Pawn=\"%s\" Anim=\"%s\" Source=\"%s\""),
			*GetNameSafe(GetOwner()), *GetNameSafe(AnimInstance),
			*GetNameSafe(BodyMesh));
	}
}

void UTacticalFirstPersonMeshComponent::ReleaseCopyPoseSource()
{
	if (USkeletalMeshComponent* Source = BoundCopyPoseSource.Get())
	{
		RemoveTickPrerequisiteComponent(Source);
		if (bOverrodeSourceTickOption
			&& Source->VisibilityBasedAnimTickOption
				== EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
		{
			Source->VisibilityBasedAnimTickOption = PreviousSourceTickOption;
		}
	}
	BoundCopyPoseSource.Reset();
	bOverrodeSourceTickOption = false;
}

void UTacticalFirstPersonMeshComponent::SetSpectatorAnimationController(
	AController* InController)
{
	SpectatorAnimationController = InController;
}

void UTacticalFirstPersonMeshComponent::SetSpectatorAnimInstanceClass(
	TSubclassOf<UAnimInstance> InAnimClass)
{
	APawn* PawnOwner = Cast<APawn>(GetOwner());
	AController* AnimationController = SpectatorAnimationController.Get();
	AController* ViewController = IsValid(PawnOwner) && IsValid(PawnOwner->GetController())
		? PawnOwner->GetController()
		: AnimationController;
	const bool bInjectAnimationController = IsValid(PawnOwner)
		&& IsValid(AnimationController)
		&& !IsValid(PawnOwner->GetController());

	UCameraComponent* ViewCamera = IsValid(PawnOwner)
		? PawnOwner->FindComponentByClass<UCameraComponent>()
		: nullptr;
	FTransform CachedCameraRelativeTransform;
	const bool bDriveSpectatorCamera = IsValid(AnimationController)
		&& IsValid(ViewController)
		&& IsValid(ViewCamera);
	if (bDriveSpectatorCamera)
	{
		CachedCameraRelativeTransform = ViewCamera->GetRelativeTransform();
		// Remote first-person AnimBPs may read camera forward while initializing.
		ViewCamera->SetWorldRotation(ViewController->GetControlRotation());
	}

	if (bInjectAnimationController)
	{
		PawnOwner->SetController(AnimationController);
	}

	SetAnimInstanceClass(InAnimClass);
	if (UAnimInstance* AnimInstance = GetAnimInstance())
	{
		// The temporary controller is available only during this component's
		// game-thread update, so remote animation must not update in parallel.
		AnimInstance->bUseMultiThreadedAnimationUpdate = false;
	}
	if (IsValid(AnimationController))
	{
		HandleExistingParallelEvaluationTask(true, true);
	}

	if (bDriveSpectatorCamera && IsValid(ViewCamera))
	{
		ViewCamera->SetRelativeTransform(CachedCameraRelativeTransform);
	}

	if (bInjectAnimationController
		&& IsValid(PawnOwner)
		&& PawnOwner->GetController() == AnimationController)
	{
		PawnOwner->SetController(nullptr);
	}
}

void UTacticalFirstPersonMeshComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	if (IsValid(SpectatorAnimationController))
	{
		if (UAnimInstance* AnimInstance = GetAnimInstance())
		{
			AnimInstance->bUseMultiThreadedAnimationUpdate = false;
		}
	}

	APawn* PawnOwner = Cast<APawn>(GetOwner());
	AController* AnimationController = SpectatorAnimationController.Get();
	AController* ViewController = IsValid(PawnOwner) && IsValid(PawnOwner->GetController())
		? PawnOwner->GetController()
		: AnimationController;
	const bool bInjectAnimationController = IsValid(PawnOwner)
		&& IsValid(AnimationController)
		&& !IsValid(PawnOwner->GetController());

	UCameraComponent* ViewCamera = IsValid(PawnOwner)
		? PawnOwner->FindComponentByClass<UCameraComponent>()
		: nullptr;
	FTransform CachedCameraRelativeTransform;
	const bool bDriveSpectatorCamera = IsValid(AnimationController)
		&& IsValid(ViewController)
		&& IsValid(ViewCamera);
	if (bDriveSpectatorCamera)
	{
		CachedCameraRelativeTransform = ViewCamera->GetRelativeTransform();
		ViewCamera->SetWorldRotation(ViewController->GetControlRotation());
	}

	if (bInjectAnimationController)
	{
		PawnOwner->SetController(AnimationController);
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (IsValid(AnimationController))
	{
		// Complete any parallel evaluation before restoring the temporary view.
		HandleExistingParallelEvaluationTask(true, true);
	}

	if (bDriveSpectatorCamera && IsValid(ViewCamera))
	{
		ViewCamera->SetRelativeTransform(CachedCameraRelativeTransform);
	}

	if (bInjectAnimationController
		&& IsValid(PawnOwner)
		&& PawnOwner->GetController() == AnimationController)
	{
		PawnOwner->SetController(nullptr);
	}
}
