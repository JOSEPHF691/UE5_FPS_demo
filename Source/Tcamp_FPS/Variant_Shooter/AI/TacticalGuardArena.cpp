// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variant_Shooter/AI/TacticalGuardArena.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float WallThickness = 12.0f;

	void SetCubeDimensions(UStaticMeshComponent* Mesh, const FVector& Center,
		const FVector& Dimensions, UMaterialInterface* Material)
	{
		Mesh->SetRelativeLocation(Center);
		Mesh->SetRelativeScale3D(Dimensions / 100.0f);
		Mesh->SetMaterial(0, Material);
	}
}

ATacticalGuardArena::ATacticalGuardArena()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);

	ArenaRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ArenaRoot"));
	SetRootComponent(ArenaRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShieldFinder(
		TEXT("/Game/Variant_Shooter/Materials/M_TacticalArenaBarrier.M_TacticalArenaBarrier"));
	BarrierMaterial = ShieldFinder.Succeeded() ? ShieldFinder.Object : nullptr;

	for (int32 Index = 0; Index < 6; ++Index)
	{
		UStaticMeshComponent* Wall = CreateDefaultSubobject<UStaticMeshComponent>(
			*FString::Printf(TEXT("SolidWall_%d"), Index));
		Wall->SetupAttachment(ArenaRoot);
		Wall->SetStaticMesh(CubeFinder.Object);
		Wall->SetCanEverAffectNavigation(false);
		SolidWalls.Add(Wall);
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UStaticMeshComponent* Field = CreateDefaultSubobject<UStaticMeshComponent>(
			*FString::Printf(TEXT("EntranceField_%d"), Index));
		Field->SetupAttachment(ArenaRoot);
		Field->SetStaticMesh(CubeFinder.Object);
		Field->SetCanEverAffectNavigation(false);
		EntranceFields.Add(Field);
	}
	Roof = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Roof"));
	Roof->SetupAttachment(ArenaRoot);
	Roof->SetStaticMesh(CubeFinder.Object);
	Roof->SetCanEverAffectNavigation(false);
	Floor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Floor"));
	Floor->SetupAttachment(ArenaRoot);
	Floor->SetStaticMesh(CubeFinder.Object);
	Floor->SetCanEverAffectNavigation(false);
}

void ATacticalGuardArena::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateGeometry();
	UpdateVisibilityAndCollision();
}

void ATacticalGuardArena::BeginPlay()
{
	Super::BeginPlay();
	UpdateGeometry();
	UpdateVisibilityAndCollision();
}

void ATacticalGuardArena::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATacticalGuardArena, bArenaActive);
}

ATacticalGuardArena* ATacticalGuardArena::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ATacticalGuardArena> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

ATacticalGuardArena* ATacticalGuardArena::FindActive(const UWorld* World)
{
	ATacticalGuardArena* Arena = Find(World);
	return Arena && Arena->IsArenaActive() ? Arena : nullptr;
}

bool ATacticalGuardArena::IsPointInside(const FVector& WorldPoint) const
{
	return IsPointInsideWithInset(WorldPoint, 0.0f);
}

bool ATacticalGuardArena::IsPointInsideWithInset(const FVector& WorldPoint,
	const float Inset) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldPoint);
	// Guard/nav points are projected onto the ground. Clearance is needed from
	// the four side walls, but shrinking Z would reject valid ground posts when
	// the authored bottom edge sits only slightly below the map floor.
	const float SideInset = FMath::Max(0.0f, Inset);
	const FVector Extent = HalfExtent.GetAbs() - FVector(SideInset, SideInset, 0.0f);
	return Extent.GetMin() > 0.0f
		&& FMath::Abs(Local.X) <= Extent.X
		&& FMath::Abs(Local.Y) <= Extent.Y
		&& FMath::Abs(Local.Z) <= Extent.Z;
}

bool ATacticalGuardArena::DoesSegmentCrossBarrier(const FVector& Start,
	const FVector& End) const
{
	FVector Point, Normal;
	return FindFirstBarrierIntersection(Start, End, Point, Normal);
}

bool ATacticalGuardArena::FindFirstBarrierIntersection(const FVector& Start,
	const FVector& End, FVector& OutPoint, FVector& OutNormal) const
{
	return bArenaActive && FindBoxBoundaryIntersection(
		GetActorTransform(), HalfExtent, Start, End, OutPoint, OutNormal);
}

bool ATacticalGuardArena::FindBoxBoundaryIntersection(
	const FTransform& BoxTransform, const FVector& BoxHalfExtent,
	const FVector& Start, const FVector& End, FVector& OutPoint, FVector& OutNormal)
{
	const FVector Extent = BoxHalfExtent.GetAbs();
	if (Extent.GetMin() <= UE_KINDA_SMALL_NUMBER || Start.Equals(End))
	{
		return false;
	}
	const FVector LocalStart = BoxTransform.InverseTransformPosition(Start);
	const FVector LocalEnd = BoxTransform.InverseTransformPosition(End);
	const FVector Delta = LocalEnd - LocalStart;
	// The four panels have finite height, but no roof or floor. Test each side
	// plane separately so a shot through the open top/bottom is never blocked.
	float FirstHitT = TNumericLimits<float>::Max();
	FVector FirstHitNormal = FVector::ZeroVector;
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		const float D = Delta[Axis];
		if (FMath::IsNearlyZero(D))
		{
			continue;
		}
		const int32 OtherAxis = 1 - Axis;
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			const float T = (Side * Extent[Axis] - LocalStart[Axis]) / D;
			if (T < 0.0f || T > 1.0f || T >= FirstHitT)
			{
				continue;
			}
			const FVector LocalHit = LocalStart + Delta * T;
			if (FMath::Abs(LocalHit[OtherAxis]) > Extent[OtherAxis] + UE_KINDA_SMALL_NUMBER
				|| FMath::Abs(LocalHit.Z) > Extent.Z + UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}
			FirstHitT = T;
			FirstHitNormal = FVector::ZeroVector;
			FirstHitNormal[Axis] = static_cast<float>(Side);
		}
	}
	if (FirstHitNormal.IsNearlyZero())
	{
		return false;
	}
	OutPoint = FMath::Lerp(Start, End, FirstHitT);
	OutNormal = BoxTransform.TransformVectorNoScale(FirstHitNormal).GetSafeNormal();
	return true;
}

void ATacticalGuardArena::SetArenaActive(const bool bNewActive)
{
	if (!HasAuthority() || bArenaActive == bNewActive)
	{
		return;
	}
	bArenaActive = bNewActive;
	UpdateVisibilityAndCollision();
	ForceNetUpdate();
}

void ATacticalGuardArena::OnRep_ArenaActive()
{
	UpdateVisibilityAndCollision();
}

void ATacticalGuardArena::UpdateGeometry()
{
	const FVector Extent = HalfExtent.GetAbs().ComponentMax(FVector(200.0f));
	const float HalfGate = FMath::Clamp(EntranceWidth * 0.5f, 75.0f, Extent.X - 100.0f);
	const float SideLength = Extent.X - HalfGate;
	const float SideOffset = (Extent.X + HalfGate) * 0.5f;
	for (int32 YSide = 0; YSide < 2; ++YSide)
	{
		const float Y = YSide == 0 ? Extent.Y : -Extent.Y;
		for (int32 XSide = 0; XSide < 2; ++XSide)
		{
			const float X = XSide == 0 ? SideOffset : -SideOffset;
			SetCubeDimensions(SolidWalls[YSide * 2 + XSide], FVector(X, Y, 0.0f),
				FVector(SideLength, WallThickness, 2.0f * Extent.Z), BarrierMaterial);
		}
		SetCubeDimensions(EntranceFields[YSide], FVector(0.0f, Y, 0.0f),
			FVector(2.0f * HalfGate, WallThickness, 2.0f * Extent.Z), BarrierMaterial);
	}
	for (int32 Side = 0; Side < 2; ++Side)
	{
		SetCubeDimensions(SolidWalls[4 + Side],
			FVector(Side == 0 ? Extent.X : -Extent.X, 0.0f, 0.0f),
			FVector(WallThickness, 2.0f * Extent.Y, 2.0f * Extent.Z), BarrierMaterial);
	}
}

void ATacticalGuardArena::UpdateVisibilityAndCollision()
{
	// Keep the authored cage visible while editing even though every round
	// starts with the barrier disabled. PIE/game worlds obey the event state.
	const bool bEditorPreview = GetWorld() && !GetWorld()->IsGameWorld();
	const bool bShowBarrier = bArenaActive || bEditorPreview;
	for (UStaticMeshComponent* Wall : SolidWalls)
	{
		Wall->SetVisibility(bShowBarrier);
		Wall->SetHiddenInGame(!bArenaActive);
		Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Wall->SetCollisionResponseToAllChannels(ECR_Ignore);
	}
	for (UStaticMeshComponent* Field : EntranceFields)
	{
		Field->SetVisibility(bShowBarrier);
		Field->SetHiddenInGame(!bArenaActive);
		Field->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Field->SetCollisionResponseToAllChannels(ECR_Ignore);
	}
	// These names remain for compatibility with the placed actor's serialized
	// components, but the arena deliberately has no roof or floor.
	for (UStaticMeshComponent* LegacyPanel : { Roof.Get(), Floor.Get() })
	{
		LegacyPanel->SetVisibility(false);
		LegacyPanel->SetHiddenInGame(true);
		LegacyPanel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		LegacyPanel->SetCollisionResponseToAllChannels(ECR_Ignore);
	}
}
