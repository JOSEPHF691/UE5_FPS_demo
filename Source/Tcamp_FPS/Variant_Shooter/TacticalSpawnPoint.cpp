// Copyright Epic Games, Inc. All Rights Reserved.

#include "TacticalSpawnPoint.h"

#include "Components/ArrowComponent.h"
#include "Components/CapsuleComponent.h"

ATacticalSpawnPoint::ATacticalSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetActorEnableCollision(false);

	CapsulePreview = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Spawn Capsule Preview"));
	SetRootComponent(CapsulePreview);
	CapsulePreview->InitCapsuleSize(42.0f, 96.0f);
	CapsulePreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CapsulePreview->SetGenerateOverlapEvents(false);
	CapsulePreview->SetHiddenInGame(true);

	FacingArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Facing Arrow"));
	FacingArrow->SetupAttachment(CapsulePreview);
	FacingArrow->SetRelativeLocation(FVector(0.0f, 0.0f, 12.0f));
	FacingArrow->ArrowSize = 2.0f;
	FacingArrow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FacingArrow->SetHiddenInGame(true);

}

void ATacticalSpawnPoint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (FacingArrow)
	{
		const FColor GroupColor = Group == ETacticalSpawnGroup::Red
			? FColor::Red
			: Group == ETacticalSpawnGroup::Blue ? FColor::Blue : FColor::Yellow;
		FacingArrow->ArrowColor = GroupColor;
	}
}
