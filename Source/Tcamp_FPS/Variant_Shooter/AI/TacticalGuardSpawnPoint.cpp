// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variant_Shooter/AI/TacticalGuardSpawnPoint.h"

#include "Components/ArrowComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"

ATacticalGuardSpawnPoint::ATacticalGuardSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	MarkerRoot = CreateDefaultSubobject<USceneComponent>(TEXT("MarkerRoot"));
	SetRootComponent(MarkerRoot);

	FacingArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("FacingArrow"));
	FacingArrow->SetupAttachment(MarkerRoot);
	FacingArrow->ArrowColor = FColor(64, 220, 255);

	GuardCapsulePreview = CreateDefaultSubobject<UCapsuleComponent>(TEXT("GuardCapsulePreview"));
	GuardCapsulePreview->SetupAttachment(MarkerRoot);
	GuardCapsulePreview->SetCapsuleSize(40.0f, 90.0f);
	GuardCapsulePreview->SetRelativeLocation(FVector(0.0f, 0.0f, 90.0f));
	GuardCapsulePreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GuardCapsulePreview->SetHiddenInGame(true);
}
