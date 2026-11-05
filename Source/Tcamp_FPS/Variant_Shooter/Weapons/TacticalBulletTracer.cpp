// Copyright Epic Games, Inc. All Rights Reserved.

#include "TacticalBulletTracer.h"

#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ATacticalBulletTracer::ATacticalBulletTracer()
{
	bReplicates = false;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	SetActorEnableCollision(false);

	StreakMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Moving Tracer Streak"));
	RootComponent = StreakMesh;
	StreakMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StreakMesh->SetCanEverAffectNavigation(false);
	StreakMesh->SetCastShadow(false);
	StreakMesh->bReceivesDecals = false;
	StreakMesh->SetHiddenInGame(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		StreakMesh->SetStaticMesh(Cylinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowMaterial(
		TEXT("/Game/Tactical/FX/M_TacticalTracer.M_TacticalTracer"));
	if (GlowMaterial.Succeeded())
	{
		StreakMesh->SetMaterial(0, GlowMaterial.Object);
	}
}

void ATacticalBulletTracer::InitializePath(const FVector& InStart, const FVector& InEnd,
	const bool bFirstPersonView)
{
	StreakMesh->SetFirstPersonPrimitiveType(bFirstPersonView
		? EFirstPersonPrimitiveType::FirstPerson
		: EFirstPersonPrimitiveType::WorldSpaceRepresentation);
	Start = InStart;
	const FVector Path = InEnd - InStart;
	PathLength = Path.Size();
	Direction = Path.GetSafeNormal();
	Age = 0.0f;
	TravelTime = FMath::Clamp(PathLength / 120000.0f, 0.045f, 0.12f);
	SetLifeSpan(TravelTime + 0.02f);
	SetActorLocation(Start);
}

void ATacticalBulletTracer::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += FMath::Max(0.0f, DeltaSeconds);
	if (PathLength <= UE_KINDA_SMALL_NUMBER || Age >= TravelTime)
	{
		Destroy();
		return;
	}

	const float HeadDistance = PathLength * FMath::Clamp(Age / TravelTime, 0.0f, 1.0f);
	const float TailDistance = FMath::Max(0.0f, HeadDistance - 280.0f);
	const float VisibleLength = HeadDistance - TailDistance;
	if (VisibleLength <= 1.0f || !StreakMesh || !StreakMesh->GetStaticMesh())
	{
		return;
	}

	StreakMesh->SetHiddenInGame(false);
	// Engine's Cylinder is 100 cm tall on Z with a 50 cm radius.
	StreakMesh->SetWorldLocation(Start + Direction * (TailDistance + VisibleLength * 0.5f));
	StreakMesh->SetWorldRotation(FQuat::FindBetweenNormals(FVector::UpVector, Direction));
	StreakMesh->SetWorldScale3D(FVector(0.024f, 0.024f, VisibleLength / 100.0f));
}
