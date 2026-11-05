// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tcamp_FPSCharacter.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TacticalFirstPersonMeshComponent.h"
#include "Components/TacticalThirdPersonMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Tcamp_FPS.h"

ATcamp_FPSCharacter::ATcamp_FPSCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UTacticalThirdPersonMeshComponent>(
		ACharacter::MeshComponentName))
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);
	
	// Create the first person mesh that will be viewed only by this character's owner
	FirstPersonMesh = CreateDefaultSubobject<UTacticalFirstPersonMeshComponent>(TEXT("First Person Mesh"));

	FirstPersonMesh->SetupAttachment(GetMesh());
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	FirstPersonViewPivot = CreateDefaultSubobject<USceneComponent>(TEXT("First Person View Pivot"));
	FirstPersonViewPivot->SetupAttachment(GetCapsuleComponent());

	// Create the Camera Component	
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
	FirstPersonCameraComponent->SetupAttachment(FirstPersonMesh, FName("head"));
	FirstPersonCameraComponent->SetRelativeLocationAndRotation(FVector(-2.8f, 5.89f, 0.0f), FRotator(0.0f, 90.0f, -90.0f));
	FirstPersonCameraComponent->bUsePawnControlRotation = true;
	FirstPersonCameraComponent->bEnableFirstPersonFieldOfView = true;
	FirstPersonCameraComponent->bEnableFirstPersonScale = true;
	FirstPersonCameraComponent->FirstPersonFieldOfView = 70.0f;
	FirstPersonCameraComponent->FirstPersonScale = 0.6f;

	// configure the character comps
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;

	GetCapsuleComponent()->SetCapsuleSize(34.0f, 96.0f);

	// Configure character movement
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;
	GetCharacterMovement()->AirControl = 0.5f;
}

void ATcamp_FPSCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocallyControlled())
	{
		FirstPersonMesh->SetAnimInstanceClass(nullptr);
	}
}

void ATcamp_FPSCharacter::InitializeFirstPersonViewRig()
{
	if (bFirstPersonViewRigInitialized || !FirstPersonViewPivot
		|| !FirstPersonCameraComponent || !FirstPersonMesh)
	{
		return;
	}

	// The Blueprint camera was authored on the animated head bone. Capture its
	// current eye position after the active weapon has installed its arms mesh,
	// then detach it before reparenting the arms (avoiding an attachment cycle).
	const FVector EyeWorldLocation = FirstPersonCameraComponent->GetComponentLocation();
	FirstPersonViewPivot->SetWorldLocationAndRotation(
		EyeWorldLocation, FRotator(0.0f, GetActorRotation().Yaw, 0.0f));
	FirstPersonCameraComponent->AttachToComponent(
		FirstPersonViewPivot, FAttachmentTransformRules::KeepWorldTransform);
	FirstPersonCameraComponent->SetRelativeLocationAndRotation(
		FVector::ZeroVector, FRotator::ZeroRotator);
	FirstPersonMesh->AttachToComponent(
		FirstPersonViewPivot, FAttachmentTransformRules::KeepWorldTransform);

	bFirstPersonViewRigInitialized = true;
	UpdateFirstPersonViewPitch();
}

void ATcamp_FPSCharacter::UpdateFirstPersonViewPitch()
{
	if (!bFirstPersonViewRigInitialized || !FirstPersonViewPivot)
	{
		return;
	}

	// GetBaseAimRotation also uses replicated RemoteViewPitch for a pawn without
	// a local controller, so a spectator sees the same viewmodel pitch.
	const float ViewmodelPitch = ShouldApplyFirstPersonViewPitch()
		? FRotator::NormalizeAxis(GetBaseAimRotation().Pitch) : 0.0f;
	const FRotator DesiredRelativeRotation(ViewmodelPitch, 0.0f, 0.0f);
	if (!FirstPersonViewPivot->GetRelativeRotation().Equals(DesiredRelativeRotation, 0.01f))
	{
		FirstPersonViewPivot->SetRelativeRotation(DesiredRelativeRotation);
	}
}

void ATcamp_FPSCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	UpdateFirstPersonViewPitch();
	Super::CalcCamera(DeltaTime, OutResult);
}

void ATcamp_FPSCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{	
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ATcamp_FPSCharacter::DoJumpStart);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ATcamp_FPSCharacter::DoJumpEnd);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ATcamp_FPSCharacter::MoveInput);

		// Looking/Aiming
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ATcamp_FPSCharacter::LookInput);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ATcamp_FPSCharacter::LookInput);
	}
	else
	{
		UE_LOG(LogTcamp_FPS, Error, TEXT("'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}


void ATcamp_FPSCharacter::MoveInput(const FInputActionValue& Value)
{
	// get the Vector2D move axis
	FVector2D MovementVector = Value.Get<FVector2D>();

	// pass the axis values to the move input
	DoMove(MovementVector.X, MovementVector.Y);

}

void ATcamp_FPSCharacter::LookInput(const FInputActionValue& Value)
{
	// get the Vector2D look axis
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// pass the axis values to the aim input
	DoAim(LookAxisVector.X, LookAxisVector.Y);

}

void ATcamp_FPSCharacter::DoAim(float Yaw, float Pitch)
{
	if (GetController())
	{
		// pass the rotation inputs
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void ATcamp_FPSCharacter::DoMove(float Right, float Forward)
{
	if (GetController())
	{
		// pass the move inputs
		AddMovementInput(GetActorRightVector(), Right);
		AddMovementInput(GetActorForwardVector(), Forward);
	}
}

void ATcamp_FPSCharacter::DoJumpStart()
{
	// pass Jump to the character
	Jump();
}

void ATcamp_FPSCharacter::DoJumpEnd()
{
	// pass StopJumping to the character
	StopJumping();
}
