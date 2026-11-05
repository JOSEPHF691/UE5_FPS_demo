// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/SprintComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

USprintComponent::USprintComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USprintComponent::BeginPlay()
{
	Super::BeginPlay();

	SprintMeter = SprintDuration;

	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		OriginalWalkSpeed = Character->GetCharacterMovement()->MaxWalkSpeed;
	}

	GetWorld()->GetTimerManager().SetTimer(TickTimer, this, &USprintComponent::SprintTick, TickInterval, true);
}

void USprintComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TickTimer);
	}
	Super::EndPlay(EndPlayReason);
}

void USprintComponent::StartSprint()
{
	bIsSprinting = true;

	if (!bIsRecovering)
	{
		ApplySpeed(SprintSpeed);
		OnSprintStateChanged.Broadcast(true);
	}
}

void USprintComponent::StopSprint()
{
	bIsSprinting = false;

	if (!bIsRecovering)
	{
		ApplySpeed(WalkSpeed);
		OnSprintStateChanged.Broadcast(false);
	}
}

float USprintComponent::GetMeterPercent() const
{
	return SprintDuration > 0.0f ? SprintMeter / SprintDuration : 0.0f;
}

void USprintComponent::SprintTick()
{
	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	if (!Owner)
	{
		return;
	}

	const float CurrentSpeed = Owner->GetVelocity().Length();

	if (bIsSprinting && !bIsRecovering && CurrentSpeed > WalkSpeed)
	{
		if (SprintMeter > 0.0f)
		{
			SprintMeter = FMath::Max(SprintMeter - TickInterval, 0.0f);

			if (SprintMeter <= 0.0f)
			{
				bIsRecovering = true;
				ApplySpeed(RecoverySpeed);
			}
		}
	}
	else
	{
		SprintMeter = FMath::Min(SprintMeter + TickInterval, SprintDuration);

		if (SprintMeter >= SprintDuration)
		{
			bIsRecovering = false;
			ApplySpeed(bIsSprinting ? SprintSpeed : WalkSpeed);
			OnSprintStateChanged.Broadcast(bIsSprinting);
		}
	}

	OnSprintMeterUpdated.Broadcast(GetMeterPercent());
}

void USprintComponent::ApplySpeed(float Speed)
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->GetCharacterMovement()->MaxWalkSpeed = Speed;
	}
}
