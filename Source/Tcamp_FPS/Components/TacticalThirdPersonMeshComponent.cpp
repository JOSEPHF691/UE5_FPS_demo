// Copyright Epic Games, Inc. All Rights Reserved.

#include "TacticalThirdPersonMeshComponent.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Tcamp_FPS.h"
#include "Variant_Shooter/Animation/TacticalSpectatorAnimationController.h"

void UTacticalThirdPersonMeshComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureRemoteAnimationController();
}

void UTacticalThirdPersonMeshComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseRemoteAnimationController();

	Super::EndPlay(EndPlayReason);
}

void UTacticalThirdPersonMeshComponent::ReleaseRemoteAnimationController()
{
	if (!IsValid(RemoteAnimationController))
	{
		RemoteAnimationController = nullptr;
		return;
	}

	if (APawn* PawnOwner = Cast<APawn>(GetOwner());
		IsValid(PawnOwner) && PawnOwner->GetController() == RemoteAnimationController)
	{
		PawnOwner->SetController(nullptr);
	}

	RemoteAnimationController->SetAnimationSourcePawn(nullptr);
	RemoteAnimationController->Destroy();
	RemoteAnimationController = nullptr;
}

void UTacticalThirdPersonMeshComponent::EnsureRemoteAnimationController()
{
	APawn* PawnOwner = Cast<APawn>(GetOwner());
	const bool bEligibleRemotePlayer = IsValid(PawnOwner)
		&& PawnOwner->GetLocalRole() == ROLE_SimulatedProxy
		&& PawnOwner->GetNetMode() == NM_Client
		&& IsValid(PawnOwner->GetPlayerState());
	if (!bEligibleRemotePlayer)
	{
		ReleaseRemoteAnimationController();
		return;
	}

	bool bCreatedController = false;
	if (!IsValid(RemoteAnimationController))
	{
		UWorld* World = GetWorld();
		if (!World)
		{
			return;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = PawnOwner;
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		RemoteAnimationController =
			World->SpawnActor<ATacticalSpectatorAnimationController>(
				ATacticalSpectatorAnimationController::StaticClass(),
				FTransform::Identity,
				SpawnParameters);
		bCreatedController = IsValid(RemoteAnimationController);
	}

	if (IsValid(RemoteAnimationController))
	{
		RemoteAnimationController->SetAnimationSourcePawn(PawnOwner);
		if (bCreatedController)
		{
			UE_LOG(LogTcamp_FPS, Verbose,
				TEXT("%s: remote third-person animation now reads replicated view pitch."),
				*GetNameSafe(PawnOwner));
		}
	}
}

void UTacticalThirdPersonMeshComponent::TickPose(
	float DeltaTime,
	bool bNeedsValidRootMotion)
{
	if (!IsInGameThread())
	{
		Super::TickPose(DeltaTime, bNeedsValidRootMotion);
		return;
	}

	EnsureRemoteAnimationController();

	APawn* PawnOwner = Cast<APawn>(GetOwner());
	const bool bInjectRemoteController = IsValid(PawnOwner)
		&& PawnOwner->GetLocalRole() == ROLE_SimulatedProxy
		&& !IsValid(PawnOwner->GetController())
		&& IsValid(RemoteAnimationController);

	if (bInjectRemoteController)
	{
		PawnOwner->SetController(RemoteAnimationController);
	}

	// BlueprintUpdateAnimation runs during TickPose and caches the legacy
	// Controller-derived aim values. Pose evaluation remains free to execute in
	// parallel after the proxy has been removed again.
	Super::TickPose(DeltaTime, bNeedsValidRootMotion);

	// Do not overwrite a real controller that may have arrived during the tick.
	if (bInjectRemoteController
		&& IsValid(PawnOwner)
		&& PawnOwner->GetController() == RemoteAnimationController)
	{
		PawnOwner->SetController(nullptr);
	}
}
