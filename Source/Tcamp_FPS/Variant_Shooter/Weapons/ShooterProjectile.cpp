// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterProjectile.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "Variant_Shooter/ShooterPlayerController.h"
#include "Variant_Shooter/AI/TacticalGuardArena.h"

AShooterProjectile::AShooterProjectile()
{
	bReplicates = true;
	// The arena wall is deliberately non-colliding for players; only live server
	// projectiles need a swept path check after ProjectileMovement has advanced.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	RootComponent = CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("Collision Component"));

	CollisionComponent->SetSphereRadius(16.0f);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionComponent->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Projectile Movement"));

	ProjectileMovement->InitialSpeed = 3000.0f;
	ProjectileMovement->MaxSpeed = 3000.0f;
	ProjectileMovement->bShouldBounce = true;

	HitDamageType = UDamageType::StaticClass();
}

void AShooterProjectile::BeginPlay()
{
	Super::BeginPlay();

	RemainingPenetrations = MaxPenetrations;

	CollisionComponent->IgnoreActorWhenMoving(GetInstigator(), true);
	LastBarrierCheckPosition = GetActorLocation();
	CachedArena = ATacticalGuardArena::Find(GetWorld());
	SetActorTickEnabled(HasAuthority());
}

const ATacticalGuardArena* AShooterProjectile::GetActiveArena()
{
	if (!CachedArena.IsValid())
	{
		CachedArena = ATacticalGuardArena::Find(GetWorld());
	}
	return CachedArena.IsValid() && CachedArena->IsArenaActive()
		? CachedArena.Get() : nullptr;
}

void AShooterProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || bHit)
	{
		return;
	}
	const FVector CurrentPosition = GetActorLocation();
	if (!StopAtArenaBarrier(LastBarrierCheckPosition, CurrentPosition))
	{
		LastBarrierCheckPosition = CurrentPosition;
	}
}

void AShooterProjectile::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	GetWorld()->GetTimerManager().ClearTimer(DestructionTimer);
}

void AShooterProjectile::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!HasAuthority() || bHit)
	{
		return;
	}
	// A single movement frame can reach a target before the actor's post-physics
	// sweep. Resolve the arena boundary first, before direct or radial damage.
	if (StopAtArenaBarrier(LastBarrierCheckPosition, HitLocation))
	{
		return;
	}
	// A large pawn capsule may be touched across the shield before the
	// projectile center itself reaches the boundary. Resolve against the
	// pawn's owning side before showing an impact or consuming penetration.
	if (Cast<APawn>(Other)
		&& StopAtArenaBarrier(LastBarrierCheckPosition, Other->GetActorLocation()))
	{
		return;
	}

	const FVector TravelDirection = ProjectileMovement && !ProjectileMovement->Velocity.IsNearlyZero()
		? ProjectileMovement->Velocity.GetSafeNormal()
		: GetActorForwardVector();
	const float TravelSpeed = ProjectileMovement ? ProjectileMovement->Velocity.Size() : 0.0f;

	if (bExplodeOnHit)
	{
		ExplosionCheck(GetActorLocation());
	}
	else
	{
		// Preserve the actual projectile path for point-damage consumers. The
		// contact normal is geometry-dependent and is not a reliable source side.
		ProcessHit(Other, OtherComp, Hit, TravelDirection);
	}

	MakeNoise(NoiseLoudness, GetInstigator(), GetActorLocation(), NoiseRange, NoiseTag);

	MulticastOnHit(Hit);

	if (!bExplodeOnHit && RemainingPenetrations > 0)
	{
		RemainingPenetrations--;
		HitDamage *= PenetrationDamageFalloff;

		// Do not immediately collide with the same actor/component again. Advance
		// beyond the contact plane and restore the pre-impact travel direction so
		// projectile bounce handling cannot turn penetration into a re-hit.
		if (Other)
		{
			CollisionComponent->IgnoreActorWhenMoving(Other, true);
		}
		if (OtherComp)
		{
			CollisionComponent->IgnoreComponentWhenMoving(OtherComp, true);
		}
		SetActorLocation(HitLocation + TravelDirection * 10.0f, false, nullptr, ETeleportType::TeleportPhysics);
		SetActorRotation(TravelDirection.Rotation());
		if (ProjectileMovement)
		{
			ProjectileMovement->Velocity = TravelDirection * TravelSpeed;
			ProjectileMovement->UpdateComponentVelocity();
		}
		return;
	}

	FinishImpact();
}

bool AShooterProjectile::StopAtArenaBarrier(const FVector& SegmentStart,
	const FVector& SegmentEnd)
{
	if (!HasAuthority() || bHit)
	{
		return false;
	}
	const ATacticalGuardArena* Arena = GetActiveArena();
	FVector ImpactPoint;
	FVector ImpactNormal;
	if (!Arena || !Arena->FindFirstBarrierIntersection(
		SegmentStart, SegmentEnd, ImpactPoint, ImpactNormal))
	{
		return false;
	}

	const FVector Direction = (SegmentEnd - SegmentStart).GetSafeNormal();
	// Keep the explosion on the entry side, never centered exactly on the
	// numerical boundary where inside/outside classification is ambiguous.
	const FVector RestingPoint = ImpactPoint - Direction * 2.0f;
	SetActorLocation(RestingPoint, false, nullptr, ETeleportType::TeleportPhysics);
	if (ProjectileMovement)
	{
		ProjectileMovement->StopMovementImmediately();
		ProjectileMovement->Deactivate();
	}
	if (bExplodeOnHit)
	{
		ExplosionCheck(RestingPoint);
	}
	MakeNoise(NoiseLoudness, GetInstigator(), RestingPoint, NoiseRange, NoiseTag);
	FHitResult BarrierHit;
	BarrierHit.bBlockingHit = true;
	BarrierHit.Location = RestingPoint;
	BarrierHit.ImpactPoint = ImpactPoint;
	BarrierHit.Normal = ImpactNormal;
	BarrierHit.ImpactNormal = ImpactNormal;
	BarrierHit.TraceStart = SegmentStart;
	BarrierHit.TraceEnd = SegmentEnd;
	MulticastArenaBarrierHit(BarrierHit);
	FinishImpact();
	return true;
}

void AShooterProjectile::FinishImpact()
{
	bHit = true;
	SetActorTickEnabled(false);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (DeferredDestructionTime > 0.0f)
	{
		GetWorld()->GetTimerManager().SetTimer(DestructionTimer, this,
			&AShooterProjectile::OnDeferredDestruction, DeferredDestructionTime, false);
	}
	else
	{
		Destroy();
	}
}

void AShooterProjectile::MulticastOnHit_Implementation(const FHitResult& Hit)
{
	BP_OnProjectileHit(Hit);
}

void AShooterProjectile::MulticastArenaBarrierHit_Implementation(
	const FHitResult& Hit)
{
	SetActorLocation(Hit.Location, false, nullptr, ETeleportType::TeleportPhysics);
	if (ProjectileMovement)
	{
		ProjectileMovement->StopMovementImmediately();
		ProjectileMovement->Deactivate();
	}
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BP_OnProjectileHit(Hit);
}

void AShooterProjectile::ExplosionCheck(const FVector& ExplosionCenter)
{
	TArray<FOverlapResult> Overlaps;

	FCollisionShape OverlapShape;
	OverlapShape.SetSphere(ExplosionRadius);

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	if (!bDamageOwner)
	{
		QueryParams.AddIgnoredActor(GetInstigator());
	}

	GetWorld()->OverlapMultiByObjectType(Overlaps, ExplosionCenter, FQuat::Identity, ObjectParams, OverlapShape, QueryParams);

	TArray<AActor*> DamagedActors;

	for (const FOverlapResult& CurrentOverlap : Overlaps)
	{
		if (DamagedActors.Find(CurrentOverlap.GetActor()) == INDEX_NONE)
		{
			DamagedActors.Add(CurrentOverlap.GetActor());

			const FVector& ExplosionDir = CurrentOverlap.GetActor()->GetActorLocation() - GetActorLocation();
			FHitResult ExplosionHit(CurrentOverlap.GetActor(), CurrentOverlap.GetComponent(), GetActorLocation(), ExplosionDir.GetSafeNormal());

			ProcessHit(CurrentOverlap.GetActor(), CurrentOverlap.GetComponent(), ExplosionHit, ExplosionDir.GetSafeNormal());
		}
	}
}

void AShooterProjectile::ProcessHit(AActor* HitActor, UPrimitiveComponent* HitComp, const FHitResult& Hit, const FVector& HitDirection)
{
	// Direct impacts can touch a capsule across the wall while the projectile's
	// center remains outside it. Radial damage must obey the same rule.
	if (HitActor)
	{
		if (const ATacticalGuardArena* Arena = GetActiveArena())
		{
			if (Arena->DoesSegmentCrossBarrier(GetActorLocation(),
				HitActor->GetActorLocation()))
			{
				return;
			}
		}
	}
	if (HitActor && (HitActor != GetOwner() || bDamageOwner))
	{
		APawn* InstigatorPawn = GetInstigator();
		AController* InstigatorController = InstigatorPawn ? InstigatorPawn->GetController() : nullptr;
		const FPointDamageEvent PointDamageEvent(HitDamage, Hit, HitDirection, HitDamageType);
		const float AppliedDamage = HitActor->TakeDamage(
			HitDamage, PointDamageEvent, InstigatorController, this);
		if (AppliedDamage > 0.0f)
		{
			if (AShooterPlayerController* ShooterController =
				Cast<AShooterPlayerController>(InstigatorController))
			{
				ShooterController->NotifyEnemyHitConfirmed();
			}
		}
	}

	if (HitComp && HitComp->IsSimulatingPhysics())
	{
		HitComp->AddImpulseAtLocation(HitDirection * PhysicsForce, Hit.ImpactPoint);
		HitActor->SetReplicateMovement(true);
	}
}

void AShooterProjectile::OnDeferredDestruction()
{
	Destroy();
}

void AShooterProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShooterProjectile, HitDamage);
	DOREPLIFETIME(AShooterProjectile, WeaponHeadshotMultiplier);
	DOREPLIFETIME(AShooterProjectile, MaxPenetrations);
	DOREPLIFETIME(AShooterProjectile, PenetrationDamageFalloff);
}
