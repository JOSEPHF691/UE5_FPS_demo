// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterWeapon.h"
#include "Engine/World.h"
#include "ShooterProjectile.h"
#include "ShooterWeaponHolder.h"
#include "ShooterWeaponMeshComponent.h"
#include "TacticalBulletTracer.h"
#include "TacticalTracerRules.h"
#include "Components/SceneComponent.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Variant_Shooter/ShooterPlayerController.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "TacticalWeapon.h"
#include "Variant_Shooter/AI/TacticalGuardArena.h"
#include "Variant_Shooter/TacticalPlayerController.h"
#include "Components/WeaponInventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "Tcamp_FPS.h"

namespace
{
	constexpr float MinimumWeaponRefireInterval = 0.01f;
	constexpr float MaxTracerViewDistanceCm = 30000.0f;

	TAutoConsoleVariable<int32> CVarTacticalTracerEnabled(
		TEXT("tc.Tracer.Enabled"), 1,
		TEXT("Enable local cosmetic hitscan tracers. 0 disables them for visual/performance comparison."),
		ECVF_Default);

	FName ResolveHolderSocket(
		const USkeletalMeshComponent* HolderMesh,
		const FName RequestedSocket,
		const TCHAR* Perspective,
		const UObject* Context)
	{
		if (HolderMesh && !RequestedSocket.IsNone() && HolderMesh->DoesSocketExist(RequestedSocket))
		{
			return RequestedSocket;
		}

		static const FName HandBoneName(TEXT("hand_r"));
		if (HolderMesh && HolderMesh->DoesSocketExist(HandBoneName))
		{
			UE_LOG(LogTcamp_FPS, Warning,
				TEXT("%s: %s weapon socket '%s' is missing on '%s'; falling back to hand_r."),
				*GetNameSafe(Context), Perspective, *RequestedSocket.ToString(), *GetNameSafe(HolderMesh));
			return HandBoneName;
		}

		UE_LOG(LogTcamp_FPS, Error,
			TEXT("%s: %s holder mesh '%s' has neither weapon socket '%s' nor hand_r; attaching to the mesh root."),
			*GetNameSafe(Context), Perspective, *GetNameSafe(HolderMesh), *RequestedSocket.ToString());
		return NAME_None;
	}

	FHitResult MakeArenaBarrierHit(const FVector& Start, const FVector& End,
		const FVector& ImpactPoint, const FVector& ImpactNormal)
	{
		FHitResult Hit;
		Hit.bBlockingHit = true;
		Hit.Location = ImpactPoint;
		Hit.ImpactPoint = ImpactPoint;
		Hit.Normal = ImpactNormal;
		Hit.ImpactNormal = ImpactNormal;
		Hit.TraceStart = Start;
		Hit.TraceEnd = End;
		Hit.Time = FVector::DistSquared(Start, End) > UE_KINDA_SMALL_NUMBER
			? FMath::Clamp(FVector::Dist(Start, ImpactPoint) / FVector::Dist(Start, End), 0.0f, 1.0f)
			: 0.0f;
		return Hit;
	}

	void PreferNearerArenaBarrier(const ATacticalGuardArena* Arena,
		const FVector& Start, const FVector& End, FHitResult& InOutHit)
	{
		if (!Arena)
		{
			return;
		}
		FVector Point;
		FVector Normal;
		if (Arena->FindFirstBarrierIntersection(Start, End, Point, Normal)
			&& (!InOutHit.bBlockingHit
				|| FVector::DistSquared(Start, Point)
					< FVector::DistSquared(Start, InOutHit.ImpactPoint)))
		{
			InOutHit = MakeArenaBarrierHit(Start, End, Point, Normal);
		}
	}
}

AShooterWeapon::AShooterWeapon()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;

	// create the root
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// create the first person mesh
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));
	FirstPersonMesh->SetupAttachment(RootComponent);

	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	FirstPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	FirstPersonMesh->bOnlyOwnerSee = true;

	// create the third person mesh
	ThirdPersonMesh = CreateDefaultSubobject<UShooterWeaponMeshComponent>(TEXT("Third Person Mesh"));
	ThirdPersonMesh->SetupAttachment(RootComponent);

	ThirdPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	ThirdPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
	ThirdPersonMesh->bOwnerNoSee = true;
}

void AShooterWeapon::BeginPlay()
{
	Super::BeginPlay();
	ApplyPresentationMeshOverrides();

	if (AActor* MyOwner = GetOwner())
	{
		MyOwner->OnDestroyed.AddDynamic(this, &AShooterWeapon::OnOwnerDestroyed);
	}

	WeaponOwner = Cast<IShooterWeaponHolder>(GetOwner());
	PawnOwner = Cast<APawn>(GetOwner());
	// Remote weapon meshes normally stop first-person animation. Preserve the
	// authored class so a spectator can restore the same gun-body animation
	// graph without relying on a component that has already been cleared.
	CachedFirstPersonWeaponAnimInstanceClass =
		GetFirstPersonWeaponAnimInstanceClass();
	if (FirstPersonWeaponAnimInstanceClass && FirstPersonMesh && PawnOwner
		&& PawnOwner->IsLocallyControlled()
		&& FirstPersonMesh->GetAnimClass()
			!= FirstPersonWeaponAnimInstanceClass.Get())
	{
		FirstPersonMesh->SetAnimInstanceClass(
			FirstPersonWeaponAnimInstanceClass);
	}

	if (HasAuthority())
	{
		CurrentBullets = MagazineSize;
		ReserveBullets = FMath::Clamp(ReserveBullets, 0, MaxReserveBullets);
	}

	if (PawnOwner && !PawnOwner->IsLocallyControlled())
	{
		FirstPersonMesh->SetAnimInstanceClass(nullptr);
	}

	if (WeaponOwner)
	{
		WeaponOwner->AttachWeaponMeshes(this);

		// OnWeaponActivated may have been skipped if ActivateWeapon() ran before
		// BeginPlay() set WeaponOwner — fire it now so the character sets up anim
		// instances and any other activation-time logic.
		WeaponOwner->OnWeaponActivated(this);

		NotifyAmmoChanged();
	}
}

void AShooterWeapon::ApplyPresentationMeshOverrides()
{
	if (FirstPersonMesh && FirstPersonWeaponMeshOverride
		&& FirstPersonMesh->GetSkeletalMeshAsset() != FirstPersonWeaponMeshOverride)
	{
		FirstPersonMesh->SetSkeletalMeshAsset(FirstPersonWeaponMeshOverride);
	}
	if (ThirdPersonMesh && ThirdPersonWeaponMeshOverride
		&& ThirdPersonMesh->GetSkeletalMeshAsset() != ThirdPersonWeaponMeshOverride)
	{
		ThirdPersonMesh->SetSkeletalMeshAsset(ThirdPersonWeaponMeshOverride);
	}
}

void AShooterWeapon::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	StopFiring();
	CancelReload();
	Super::EndPlay(EndPlayReason);
}

void AShooterWeapon::OnOwnerDestroyed(AActor* DestroyedActor)
{
	// ensure this weapon is destroyed when the owner is destroyed
	Destroy();
}

void AShooterWeapon::ActivateWeapon()
{
	SetActorTickEnabled(PrimaryActorTick.bCanEverTick && PawnOwner && PawnOwner->IsLocallyControlled());

	// unhide this weapon
	SetActorHiddenInGame(false);

	// notify the owner
	if (WeaponOwner)
	{
		WeaponOwner->OnWeaponActivated(this);
	}
}

void AShooterWeapon::DeactivateWeapon()
{
	SetActorTickEnabled(false);

	// ensure we're no longer firing this weapon while deactivated
	StopFiring();
	// An inactive weapon must not complete a reload in the background.
	CancelReload();

	// hide the weapon
	SetActorHiddenInGame(true);

	// notify the owner (may be null during death cleanup)
	if (WeaponOwner)
	{
		WeaponOwner->OnWeaponDeactivated(this);
	}
}

void AShooterWeapon::StartFiring(const uint16 PredictionBurstId)
{
	if (bReloadInProgress)
	{
		return;
	}
	ActivePredictionBurstId = PredictionBurstId;
	ServerBurstShotIndex = 0;

	// raise the firing flag
	bIsFiring = true;

	// check how much time has passed since we last shot
	// this may be under the refire rate if the weapon shoots slow enough and the player is spamming the trigger
	const float TimeSinceLastShot = GetWorld()->GetTimeSeconds() - TimeOfLastShot;
	const float EffectiveRefireInterval = FMath::Max(
		RefireRate, MinimumWeaponRefireInterval);

	if (TimeOfLastShot <= 0.0f || TimeSinceLastShot >= EffectiveRefireInterval)
	{
		// fire the weapon right away
		Fire();

	} else {

		// Queue one gated shot for every weapon type. StopFiring cancels this
		// timer, so semi-auto still produces at most one shot for this trigger
		// press while an AI that reacquires during cooldown cannot become stuck.
		const float RemainingCooldown = FMath::Max(
			EffectiveRefireInterval - TimeSinceLastShot, UE_KINDA_SMALL_NUMBER);
		GetWorld()->GetTimerManager().SetTimer(
			RefireTimer, this, &AShooterWeapon::Fire, RemainingCooldown, false);

	}
}

void AShooterWeapon::StopFiring()
{
	// lower the firing flag
	bIsFiring = false;
	ActivePredictionBurstId = 0;
	ServerBurstShotIndex = 0;

	// clear the refire timer
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
}

void AShooterWeapon::Fire()
{
	FireAtTarget(WeaponOwner->GetWeaponTargetLocation());
}

void AShooterWeapon::FireAtTarget(const FVector& TargetLocation)
{
	// Keep the check at the authoritative shot boundary as well. This blocks a
	// queued automatic-fire callback even if it was scheduled before reload.
	if (!bIsFiring || bReloadInProgress || CurrentBullets <= 0)
	{
		return;
	}

	FireProjectile(TargetLocation);
	TimeOfLastShot = GetWorld()->GetTimeSeconds();
	MakeNoise(ShotLoudness, PawnOwner, PawnOwner->GetActorLocation(), ShotNoiseRange, ShotNoiseTag);

	if (bFullAuto)
	{
		GetWorld()->GetTimerManager().SetTimer(
			RefireTimer, this, &AShooterWeapon::Fire,
			FMath::Max(RefireRate, MinimumWeaponRefireInterval), false);
	}
	else
	{
		GetWorld()->GetTimerManager().SetTimer(
			RefireTimer, this, &AShooterWeapon::FireCooldownExpired,
			FMath::Max(RefireRate, MinimumWeaponRefireInterval), false);
	}
}

void AShooterWeapon::FireCooldownExpired()
{
	// notify the owner
	WeaponOwner->OnSemiWeaponRefire();
}

float AShooterWeapon::GetRefireInterval() const
{
	return FMath::Max(RefireRate, MinimumWeaponRefireInterval);
}

void AShooterWeapon::FireProjectile(const FVector& TargetLocation)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bUseHitscan)
	{
		PerformHitscan(TargetLocation);
	}
	else
	{
		bool bProjectileSpawnBlocked = false;
		FTransform ProjectileTransform = CalculateProjectileSpawnTransform(
			TargetLocation, bProjectileSpawnBlocked);

		// Point traces protect the logical muzzle, but a projectile has volume.
		// Never AlwaysSpawn its collision sphere into nearby cover (including a
		// wall caught on the eye-to-muzzle or muzzle-offset path). Keep the ammo
		// and firing feedback for this blocked trigger; only the projectile is
		// suppressed so it cannot emerge on the far side of thin geometry.
		if (!bProjectileSpawnBlocked && ProjectileClass)
		{
			const AShooterProjectile* ProjectileCDO =
				ProjectileClass->GetDefaultObject<AShooterProjectile>();
			const USphereComponent* ProjectileSphere = ProjectileCDO
				? Cast<USphereComponent>(ProjectileCDO->GetRootComponent()) : nullptr;
			const float ProjectileRadius = ProjectileSphere
				? ProjectileSphere->GetUnscaledSphereRadius() : 16.0f;
			FCollisionObjectQueryParams GeometryObjects;
			GeometryObjects.AddObjectTypesToQuery(ECC_WorldStatic);
			GeometryObjects.AddObjectTypesToQuery(ECC_WorldDynamic);
			FCollisionQueryParams SpawnQuery;
			SpawnQuery.AddIgnoredActor(this);
			SpawnQuery.AddIgnoredActor(GetOwner());
			FVector EyeLocation;
			FRotator ViewRotation;
			GetGameplayViewPoint(EyeLocation, ViewRotation);
			const FCollisionShape ProjectileShape = FCollisionShape::MakeSphere(
				FMath::Max(ProjectileRadius, 1.0f));
			FHitResult Obstruction;
			bProjectileSpawnBlocked = GetWorld()->SweepSingleByObjectType(
				Obstruction, EyeLocation, ProjectileTransform.GetLocation(),
				FQuat::Identity, GeometryObjects, ProjectileShape, SpawnQuery)
				|| GetWorld()->OverlapAnyTestByObjectType(
					ProjectileTransform.GetLocation(), FQuat::Identity,
					GeometryObjects, ProjectileShape, SpawnQuery);
		}

		if (!bProjectileSpawnBlocked)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::OverrideRootScale;
			SpawnParams.Owner = GetOwner();
			SpawnParams.Instigator = PawnOwner;

			AShooterProjectile* Projectile = GetWorld()->SpawnActor<AShooterProjectile>(
				ProjectileClass, ProjectileTransform, SpawnParams);
			if (Projectile)
			{
				ConfigureProjectile(Projectile);
			}
		}
	}

	--CurrentBullets;
	CurrentBullets = FMath::Max(CurrentBullets, 0);

	NotifyAmmoChanged();
	if (ActivePredictionBurstId != 0)
	{
		++ServerBurstShotIndex;
		if (ServerBurstShotIndex == 0)
		{
			++ServerBurstShotIndex;
		}
	}
	MulticastFireEffects(ActivePredictionBurstId, ServerBurstShotIndex);
}

void AShooterWeapon::PerformHitscan(const FVector& TargetLocation)
{
	const ATacticalGuardArena* ActiveArena =
		ATacticalGuardArena::FindActive(GetWorld());
	// The camera chooses a target; the stable gameplay muzzle supplies the
	// authoritative origin. A near-wall hit on either pre-muzzle segment is
	// processed as the shot's first impact rather than letting the 10 cm spawn
	// offset or a muzzle clipped through cover skip the wall.
	FHitResult NearWallHit;
	const TacticalBallisticRules::FShotPath ShotPath = ResolveGameplayShotPath(
		TargetLocation, NearWallHit);
	const bool bNearWallBlocked = ShotPath.IsBlocked();
	const FVector TraceStart = ShotPath.Origin;
	FVector BaseAimDir = (ShotPath.Target - TraceStart).GetSafeNormal();
	if (BaseAimDir.IsNearlyZero())
	{
		BaseAimDir = (TargetLocation - GetMuzzleLocation()).GetSafeNormal();
	}
	if (BaseAimDir.IsNearlyZero())
	{
		BaseAimDir = PawnOwner ? PawnOwner->GetActorForwardVector() : GetActorForwardVector();
	}
	const float SpreadRadians = FMath::DegreesToRadians(FMath::Max(0.0f, GetEffectiveAimVariance()));
	const FVector AimDir = !bNearWallBlocked && SpreadRadians > 0.0f
		? FMath::VRandCone(BaseAimDir, SpreadRadians)
		: BaseAimDir;

	// Trace to a long-range endpoint — "infinite" for gameplay purposes
	const FVector TraceEnd = TraceStart + AimDir * 100000.0f;

	// 2. Set up collision query params
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(GetOwner());
	QueryParams.bReturnPhysicalMaterial = false;

	// Hit both world geometry AND character capsules (ECC_Visibility alone ignores Pawn)
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);

	// 3. Gather damage/penetration values from weapon (virtual — overridden by ATacticalWeapon)
	float CurrentDamage = 0.0f;
	int32 RemainingPen = 0;
	float Falloff = 0.0f;
	ConfigureHitscanParams(CurrentDamage, RemainingPen, Falloff);

	TArray<FHitscanHitData> AllHits;
	FVector CurrentStart = TraceStart;
	bool bUseNearWallHit = bNearWallBlocked;
	bool bStoppedAtImpact = false;
	FVector StoppingImpactPoint = FVector::ZeroVector;

	// 4. Penetration loop
	while (true)
	{
		FHitResult Hit;
		bool bHasWorldHit = false;
		if (bUseNearWallHit)
		{
			Hit = NearWallHit;
			bUseNearWallHit = false;
			bHasWorldHit = true;
		}
		else
		{
			bHasWorldHit = GetWorld()->LineTraceSingleByObjectType(
				Hit, CurrentStart, TraceEnd, ObjectParams, QueryParams);
		}

		// The energy wall is a gameplay boundary, not ordinary penetrable cover.
		// Check every segment, including the remaining segment after the last
		// penetration, so a sniper round cannot emerge on the other side.
		FVector ArenaPoint;
		FVector ArenaNormal;
		bool bArenaBlocksHit = ActiveArena
			&& ActiveArena->FindFirstBarrierIntersection(
				CurrentStart, bHasWorldHit ? Hit.ImpactPoint : TraceEnd,
				ArenaPoint, ArenaNormal);
		if (!bArenaBlocksHit && ActiveArena && bHasWorldHit)
		{
			// A pawn capsule can protrude through the shield so its near surface
			// is hit before the trace reaches the wall, even though the pawn is
			// actually on the other side. Match the projectile and TakeDamage
			// checks before emitting a character impact or continuing penetration.
			if (const APawn* HitPawn = Cast<APawn>(Hit.GetActor()))
			{
				bArenaBlocksHit = ActiveArena->FindFirstBarrierIntersection(
					CurrentStart, HitPawn->GetActorLocation(),
					ArenaPoint, ArenaNormal);
			}
		}
		if (bArenaBlocksHit)
		{
			FHitscanHitData BarrierHit;
			BarrierHit.ImpactPoint = ArenaPoint;
			BarrierHit.ImpactNormal = ArenaNormal;
			BarrierHit.HitType = 0;
			AllHits.Add(BarrierHit);
			bStoppedAtImpact = true;
			StoppingImpactPoint = ArenaPoint;
			break;
		}
		if (!bHasWorldHit)
		{
			break;
		}

		FHitscanHitData HitData;
		HitData.ImpactPoint = Hit.ImpactPoint;
		HitData.ImpactNormal = Hit.ImpactNormal;

		// Check if we hit a character
		if (ACharacter* HitCharacter = Cast<ACharacter>(Hit.GetActor()))
		{
			HitData.HitType = 1; // character
			// Default hit data from capsule (no bone info)
			FHitResult DamageHit = Hit;

			// Trace against skeletal mesh for bone-accurate hit (headshot detection)
			if (USkeletalMeshComponent* Mesh = HitCharacter->GetMesh())
			{
				FHitResult MeshHit;
				FCollisionQueryParams MeshParams;
				MeshParams.bTraceComplex = true;
				MeshParams.AddIgnoredActor(this);
				MeshParams.AddIgnoredActor(GetOwner());
				if (Mesh->LineTraceComponent(MeshHit, CurrentStart, TraceEnd, MeshParams))
				{
					DamageHit = MeshHit;
					HitData.ImpactPoint = MeshHit.ImpactPoint;
					HitData.ImpactNormal = MeshHit.ImpactNormal;
					HitData.BoneName = MeshHit.BoneName;
				}
			}
			if (HitData.BoneName.IsNone())
			{
				HitData.BoneName = Hit.BoneName;
			}

			if (HitCharacter != GetOwner())
			{
				APawn* InstigatorPawn = GetInstigator();
				AController* InstigatorController = InstigatorPawn ? InstigatorPawn->GetController() : nullptr;

				// FPointDamageEvent::ShotDirection must describe the real source-to-
				// victim shot path. Surface normals can point elsewhere on sloped or
				// animated body geometry and would produce a wrong directional HUD cue.
				const FPointDamageEvent PointDamageEvent(CurrentDamage, DamageHit,
					AimDir, UDamageType::StaticClass());
				const float AppliedDamage = HitCharacter->TakeDamage(CurrentDamage, PointDamageEvent,
					InstigatorController, this);
				if (AppliedDamage > 0.0f)
				{
					if (AShooterPlayerController* ShooterController =
						Cast<AShooterPlayerController>(InstigatorController))
					{
						ShooterController->NotifyEnemyHitConfirmed();
					}
				}
			}

			// Physics impulse
			if (UPrimitiveComponent* HitComp = Hit.GetComponent())
			{
				if (HitComp->IsSimulatingPhysics())
				{
					HitComp->AddImpulseAtLocation(-Hit.ImpactNormal * HitscanPhysicsForce, Hit.ImpactPoint);
					Hit.GetActor()->SetReplicateMovement(true);
				}
			}
		}
	else
	{
		HitData.HitType = 0; // world geometry

		// Interactive world targets (for example the lobby ready buttons) still
		// receive a server-authored point-damage event. Static level geometry simply
		// returns zero from AActor::TakeDamage and remains unaffected.
		if (AActor* HitActor = Hit.GetActor(); HitActor && HitActor->CanBeDamaged())
		{
			APawn* InstigatorPawn = GetInstigator();
			AController* InstigatorController = InstigatorPawn ? InstigatorPawn->GetController() : nullptr;
			const FPointDamageEvent PointDamageEvent(CurrentDamage, Hit, AimDir, UDamageType::StaticClass());
			HitActor->TakeDamage(CurrentDamage, PointDamageEvent, InstigatorController, this);
		}

		// Physics impulse for non-character physics bodies
			if (UPrimitiveComponent* HitComp = Hit.GetComponent())
			{
				if (HitComp->IsSimulatingPhysics())
				{
					HitComp->AddImpulseAtLocation(-Hit.ImpactNormal * HitscanPhysicsForce, Hit.ImpactPoint);
					Hit.GetActor()->SetReplicateMovement(true);
				}
			}
		}

		AllHits.Add(HitData);

		// Penetration: stop if no more penetrations remain
		if (bNearWallBlocked || RemainingPen <= 0)
		{
			bStoppedAtImpact = true;
			StoppingImpactPoint = HitData.ImpactPoint;
			break;
		}

		// Scale damage for the next penetration
		RemainingPen--;
		CurrentDamage *= Falloff;
		QueryParams.AddIgnoredActor(Hit.GetActor());

		// Advance trace start just past the hit point for the next iteration
		CurrentStart = Hit.ImpactPoint + AimDir * 1.0f;
	}

	// A penetration which exits its final surface is still a miss beyond it;
	// otherwise end at the last genuinely stopping impact. Neither choice can
	// modify the full-range authoritative trace or its damage results.
	const FVector VisualEnd = TacticalTracerRules::ChooseVisualEnd(
		TraceStart, AimDir, bStoppedAtImpact, StoppingImpactPoint);
	++ServerHitscanShotId;
	MulticastHitscanResult(TraceStart, VisualEnd, ServerHitscanShotId, AllHits);
}

void AShooterWeapon::MulticastHitscanResult_Implementation(
	const FVector_NetQuantize& TraceStart, const FVector_NetQuantize& VisualEnd,
	const uint16 ShotId, const TArray<FHitscanHitData>& Hits)
{
	SpawnLocalHitscanTracer(TraceStart, VisualEnd, ShotId);
	for (const FHitscanHitData& HitData : Hits)
	{
		BP_OnHitscanHit(HitData);
	}
}

bool AShooterWeapon::TryGetLocalTracerMuzzle(
	FVector& OutMuzzle, bool& bOutFirstPerson) const
{
	if (IsHidden() || !IsValid(PawnOwner.Get())
		|| PawnOwner->IsActorBeingDestroyed())
	{
		return false;
	}

	const UWorld* World = GetWorld();
	const APlayerController* Viewer = World ? World->GetFirstPlayerController() : nullptr;
	const ATacticalPlayerController* TacticalViewer =
		Cast<ATacticalPlayerController>(Viewer);
	const bool bShowFirstPerson = PawnOwner->IsLocallyControlled()
		|| (TacticalViewer && TacticalViewer->IsLocallyPresentingWeaponFirstPerson(this));
	bOutFirstPerson = bShowFirstPerson;
	const USkeletalMeshComponent* VisualMesh = bShowFirstPerson
		? FirstPersonMesh : ThirdPersonMesh;
	const FName SocketName = GetMuzzleSocketName();
	if (!VisualMesh || !VisualMesh->IsVisible() || VisualMesh->bHiddenInGame
		|| !VisualMesh->DoesSocketExist(SocketName))
	{
		return false;
	}

	OutMuzzle = VisualMesh->GetSocketLocation(SocketName);
	return !OutMuzzle.ContainsNaN();
}

void AShooterWeapon::SpawnLocalHitscanTracer(
	const FVector& TraceStart, const FVector& VisualEnd, const uint16 ShotId)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer
		|| CVarTacticalTracerEnabled.GetValueOnGameThread() == 0
		|| !TacticalTracerRules::ShouldDisplayShot(ShotId, TracerEveryNthShot))
	{
		return;
	}

	APlayerController* Viewer = World->GetFirstPlayerController();
	if (!Viewer || !Viewer->IsLocalController())
	{
		return;
	}

	FVector LocalMuzzle;
	bool bFirstPerson = false;
	if (!TryGetLocalTracerMuzzle(LocalMuzzle, bFirstPerson))
	{
		return;
	}

	// A retired/reattached weapon, a near-zero hit, or a stale packet must not
	// leave a streak pointing backwards or originating from an old gun location.
	const FVector ServerDirection = (VisualEnd - TraceStart).GetSafeNormal();
	const FVector VisualPath = VisualEnd - LocalMuzzle;
	if (ServerDirection.IsNearlyZero() || VisualPath.ContainsNaN()
		|| VisualPath.SizeSquared() < FMath::Square(5.0f)
		|| FVector::DotProduct(VisualPath, ServerDirection) <= 0.0f
		|| FVector::DistSquared(LocalMuzzle, TraceStart) > FMath::Square(500.0f))
	{
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	Viewer->GetPlayerViewPoint(ViewLocation, ViewRotation);
	if (FVector::DistSquared(ViewLocation, LocalMuzzle)
		> FMath::Square(MaxTracerViewDistanceCm))
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATacticalBulletTracer* Tracer = World->SpawnActor<ATacticalBulletTracer>(
		ATacticalBulletTracer::StaticClass(), FTransform::Identity, SpawnParameters);
	if (Tracer)
	{
		Tracer->InitializePath(LocalMuzzle, VisualEnd, bFirstPerson);
	}
}

void AShooterWeapon::Reload()
{
	ReloadWithMinimumDuration(0.0f);
}

void AShooterWeapon::ReloadWithMinimumDuration(const float MinimumDuration)
{
	if (!HasAuthority() || bReloadInProgress
		|| CurrentBullets >= MagazineSize
		|| (!HasInfiniteReserveAmmo() && ReserveBullets <= 0))
	{
		return;
	}

	// Reload is an authoritative action rather than a cosmetic-only montage.
	// Stop full-auto/refire work first, then block every shot until completion.
	StopFiring();
	bReloadInProgress = true;
	MulticastReloadEffects();

	float ReloadDuration = 0.0f;
	// The owner's first-person character and gun-body animations gate gameplay.
	// A third-person proxy montage may differ in length and must not delay the
	// authoritative ammunition transfer beyond the local action.
	for (const UAnimMontage* PresentationMontage :
		{GetFirstPersonReloadMontage(), GetFirstPersonWeaponReloadMontage()})
	{
		if (!PresentationMontage)
		{
			continue;
		}
		const float EffectiveRateScale = FMath::Abs(PresentationMontage->RateScale);
		if (EffectiveRateScale > UE_KINDA_SMALL_NUMBER)
		{
			ReloadDuration = FMath::Max(ReloadDuration,
				PresentationMontage->GetPlayLength() / EffectiveRateScale);
		}
	}
	if (const ATacticalWeapon* TacticalWeapon = Cast<ATacticalWeapon>(this))
	{
		ReloadDuration = FMath::Max(ReloadDuration,
			TacticalWeapon->GetAdditionalReloadPresentationDuration());
	}
	ReloadDuration = FMath::Max(ReloadDuration, FMath::Max(0.0f, MinimumDuration));

	UWorld* World = GetWorld();
	if (!World || ReloadDuration <= UE_KINDA_SMALL_NUMBER)
	{
		CompleteReload();
		return;
	}

	World->GetTimerManager().SetTimer(
		ReloadCompletionTimer,
		this,
		&AShooterWeapon::CompleteReload,
		ReloadDuration,
		false);
}

void AShooterWeapon::CompleteReload()
{
	if (!HasAuthority() || !bReloadInProgress)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReloadCompletionTimer);
	}
	bReloadInProgress = false;

	// Re-evaluate the live authoritative values in case another gameplay system
	// changed ammunition or the game phase while the animation was playing.
	const int32 BulletsNeeded = FMath::Max(0, MagazineSize - CurrentBullets);
	const bool bInfiniteReserve = HasInfiniteReserveAmmo();
	const int32 BulletsToLoad = bInfiniteReserve
		? BulletsNeeded
		: FMath::Min(BulletsNeeded, FMath::Max(0, ReserveBullets));
	CurrentBullets += BulletsToLoad;
	if (!bInfiniteReserve)
	{
		ReserveBullets -= BulletsToLoad;
	}
	NotifyAmmoChanged();
}

void AShooterWeapon::CancelReload()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReloadCompletionTimer);
	}
	bReloadInProgress = false;
}

void AShooterWeapon::MulticastFireEffects_Implementation(
	const uint16 PredictionBurstId, const uint16 BurstShotIndex)
{
	if (!WeaponOwner)
	{
		return;
	}

	UWeaponInventoryComponent* Inventory = nullptr;
	if (const AShooterCharacter* Character = Cast<AShooterCharacter>(PawnOwner.Get()))
	{
		Inventory = Character->GetWeaponInventory();
	}
	const bool bSuppressOwnerPresentation = Inventory
		&& Inventory->ShouldSuppressOwnerFireEcho(this,
			PredictionBurstId, BurstShotIndex);
	if (!bSuppressOwnerPresentation)
	{
		WeaponOwner->PlayFiringMontage(GetFirstPersonFiringMontage());
	}

	// Controller recoil stays authoritative and is never applied by the local
	// prediction path. Discard a late echo from a retired or frozen weapon.
	if (PawnOwner && PawnOwner->IsLocallyControlled()
		&& (!Inventory || (Inventory->CurrentWeapon.Get() == this
			&& Inventory->bCanFire)))
	{
		WeaponOwner->AddWeaponRecoil(FiringRecoil);
	}
}

void AShooterWeapon::MulticastReloadEffects_Implementation()
{
	if (!WeaponOwner)
	{
		return;
	}
	if (const AShooterCharacter* Character = Cast<AShooterCharacter>(PawnOwner.Get()))
	{
		if (UWeaponInventoryComponent* Inventory = Character->GetWeaponInventory())
		{
			Inventory->NotifyAuthoritativeReloadStarted(this);
		}
	}

	WeaponOwner->PlayReloadMontage(GetFirstPersonReloadMontage());
}

void AShooterWeapon::OnRep_CurrentBullets()
{
	NotifyAmmoChanged();
}

void AShooterWeapon::OnRep_ReserveBullets()
{
	NotifyAmmoChanged();
}

void AShooterWeapon::NotifyAmmoChanged() const
{
	if (WeaponOwner)
	{
		WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize, ReserveBullets);
	}
}

FVector AShooterWeapon::GetMuzzleLocation() const
{
	FTransform HolderMuzzleTransform;
	if (WeaponOwner && WeaponOwner->TryGetWeaponMuzzleTransform(HolderMuzzleTransform))
	{
		return HolderMuzzleTransform.GetLocation();
	}

	// Remote first-person animation is intentionally disabled and third-person
	// bones may stop refreshing when not rendered (including dedicated servers).
	// A camera-relative gameplay origin is therefore the same for host and remote
	// players regardless of visual mesh, animation, or ADS pose.
	FVector ViewLocation;
	FRotator ViewRotation;
	GetGameplayViewPoint(ViewLocation, ViewRotation);
	return ViewLocation + ViewRotation.RotateVector(GameplayMuzzleViewOffset);
}

void AShooterWeapon::GetGameplayViewPoint(FVector& OutLocation,
	FRotator& OutRotation) const
{
	OutLocation = GetActorLocation();
	OutRotation = GetActorRotation();
	if (!PawnOwner)
	{
		return;
	}

	PawnOwner->GetActorEyesViewPoint(OutLocation, OutRotation);
	if (const APlayerController* PlayerController =
		Cast<APlayerController>(PawnOwner->GetController()))
	{
		PlayerController->GetPlayerViewPoint(OutLocation, OutRotation);
	}
}

TacticalBallisticRules::FShotPath AShooterWeapon::ResolveGameplayShotPath(
	const FVector& TargetLocation, FHitResult& OutObstruction) const
{
	OutObstruction = FHitResult();
	const FVector GameplayMuzzle = GetMuzzleLocation();
	const FVector FallbackForward = PawnOwner
		? PawnOwner->GetActorForwardVector() : GetActorForwardVector();
	const FVector DesiredSpawn = TacticalBallisticRules::CalculateDesiredSpawn(
		GameplayMuzzle, TargetLocation, FallbackForward, MuzzleOffset);

	if (!GetWorld() || !PawnOwner)
	{
		return {DesiredSpawn, TargetLocation,
			TacticalBallisticRules::EBlockedSegment::None};
	}

	// Check environmental blockers using the same world object types as the
	// normal shot trace. Ready flags are WorldDynamic and remain shootable.
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(GetOwner());

	FVector EyeLocation;
	FRotator UnusedViewRotation;
	GetGameplayViewPoint(EyeLocation, UnusedViewRotation);
	FHitResult EyeToMuzzleHit;
	if (!EyeLocation.Equals(GameplayMuzzle, KINDA_SMALL_NUMBER))
	{
		GetWorld()->LineTraceSingleByObjectType(EyeToMuzzleHit,
			EyeLocation, GameplayMuzzle, ObjectParams, QueryParams);
	}
	const ATacticalGuardArena* ActiveArena =
		ATacticalGuardArena::FindActive(GetWorld());
	PreferNearerArenaBarrier(ActiveArena, EyeLocation, GameplayMuzzle,
		EyeToMuzzleHit);

	// Blueprint-authored MuzzleOffset must not move a bullet past a thin wall.
	// This second test is independent of eye-to-muzzle because the two segments
	// can pass on different sides of an edge or thin prop.
	FHitResult MuzzleToSpawnHit;
	if (!EyeToMuzzleHit.bBlockingHit
		&& !GameplayMuzzle.Equals(DesiredSpawn, KINDA_SMALL_NUMBER))
	{
		GetWorld()->LineTraceSingleByObjectType(MuzzleToSpawnHit,
			GameplayMuzzle, DesiredSpawn, ObjectParams, QueryParams);
		PreferNearerArenaBarrier(ActiveArena, GameplayMuzzle, DesiredSpawn,
			MuzzleToSpawnHit);
	}
	const TacticalBallisticRules::FShotPath Result =
		TacticalBallisticRules::SelectShotPath(EyeLocation, GameplayMuzzle,
			DesiredSpawn, TargetLocation, EyeToMuzzleHit, MuzzleToSpawnHit);
	OutObstruction = EyeToMuzzleHit.bBlockingHit
		? EyeToMuzzleHit : MuzzleToSpawnHit;
	return Result;
}

FTransform AShooterWeapon::CalculateProjectileSpawnTransform(
	const FVector& TargetLocation, bool& bOutNearWallBlocked) const
{
	FHitResult NearWallHit;
	const TacticalBallisticRules::FShotPath ShotPath = ResolveGameplayShotPath(
		TargetLocation, NearWallHit);
	const bool bNearWallBlocked = ShotPath.IsBlocked();
	bOutNearWallBlocked = bNearWallBlocked;
	const FVector SpawnLoc = ShotPath.Origin;

	// Apply a true angular cone. AimVariance is authored in degrees, so spread
	// remains consistent at every target distance. A barrel blocked by cover is
	// deliberately aimed straight into that cover instead of spread past it.
	FVector BaseAimDir = (ShotPath.Target - SpawnLoc).GetSafeNormal();
	if (BaseAimDir.IsNearlyZero())
	{
		BaseAimDir = (TargetLocation - GetMuzzleLocation()).GetSafeNormal();
	}
	if (BaseAimDir.IsNearlyZero())
	{
		BaseAimDir = PawnOwner ? PawnOwner->GetActorForwardVector() : GetActorForwardVector();
	}
	const float SpreadRadians = FMath::DegreesToRadians(FMath::Max(0.0f, GetEffectiveAimVariance()));
	const FVector AimDir = !bNearWallBlocked && SpreadRadians > 0.0f
		? FMath::VRandCone(BaseAimDir, SpreadRadians)
		: BaseAimDir;
	const FRotator AimRot = AimDir.Rotation();

	// return the built transform
	return FTransform(AimRot, SpawnLoc, FVector::OneVector);
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetFirstPersonAnimInstanceClass() const
{
	return FirstPersonAnimInstanceClass;
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetThirdPersonAnimInstanceClass() const
{
	return ThirdPersonAnimInstanceClass;
}

TSubclassOf<UAnimInstance> AShooterWeapon::GetFirstPersonWeaponAnimInstanceClass() const
{
	if (FirstPersonWeaponAnimInstanceClass)
	{
		return FirstPersonWeaponAnimInstanceClass;
	}
	if (CachedFirstPersonWeaponAnimInstanceClass)
	{
		return CachedFirstPersonWeaponAnimInstanceClass;
	}
	return FirstPersonMesh ? FirstPersonMesh->GetAnimClass() : nullptr;
}

bool AShooterWeapon::TryGetFirstPersonLeftHandGripTransform(
	FTransform& OutWeaponSpaceTransform) const
{
	OutWeaponSpaceTransform = FTransform::Identity;
	if (!bUseFirstPersonLeftHandGrip || FirstPersonLeftHandGrip.ContainsNaN())
	{
		return false;
	}

	FTransform ResolvedTransform = FirstPersonLeftHandGrip;
	ResolvedTransform.SetScale3D(FVector::OneVector);
	ResolvedTransform.NormalizeRotation();

	if (!FirstPersonLeftHandGripSocketName.IsNone())
	{
		if (!FirstPersonMesh
			|| !FirstPersonMesh->DoesSocketExist(FirstPersonLeftHandGripSocketName))
		{
			return false;
		}

		FTransform SocketTransform = FirstPersonMesh->GetSocketTransform(
			FirstPersonLeftHandGripSocketName, RTS_Component);
		SocketTransform.SetScale3D(FVector::OneVector);
		SocketTransform.NormalizeRotation();
		ResolvedTransform = ResolvedTransform * SocketTransform;
	}

	ResolvedTransform.SetScale3D(FVector::OneVector);
	ResolvedTransform.NormalizeRotation();
	if (ResolvedTransform.ContainsNaN())
	{
		return false;
	}

	OutWeaponSpaceTransform = ResolvedTransform;
	return true;
}

void AShooterWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AShooterWeapon, CurrentBullets, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AShooterWeapon, ReserveBullets, COND_OwnerOnly);
}

void AShooterWeapon::AttachWeaponToHolder(AShooterWeapon* Weapon, AActor* Holder,
	USkeletalMeshComponent* HolderMesh1P, USkeletalMeshComponent* HolderMesh3P,
	FName Socket1P, FName Socket3P)
{
	if (!Weapon || !Holder || !HolderMesh1P || !HolderMesh3P)
	{
		return;
	}

	const FName ResolvedSocket1P = ResolveHolderSocket(
		HolderMesh1P, Socket1P, TEXT("first-person"), Weapon);
	const FName ResolvedSocket3P = ResolveHolderSocket(
		HolderMesh3P, Socket3P, TEXT("third-person"), Weapon);
	if (!Weapon->bAttachmentBaseScalesCached)
	{
		Weapon->AttachmentBaseScale1P = Weapon->GetFirstPersonMesh()->GetRelativeScale3D();
		Weapon->AttachmentBaseScale3P = Weapon->GetThirdPersonMesh()->GetRelativeScale3D();
		Weapon->bAttachmentBaseScalesCached = true;
	}

	// Do not copy scale authored on a character bone into the weapon. Position and
	// rotation still snap exactly to the grip socket. Reapply the weapon mesh's
	// authored scale together with any explicit per-view correction so a future
	// skin whose source mesh is not unit-scaled is not silently resized.
	const FAttachmentTransformRules AttachmentRule = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
	Weapon->AttachToActor(Holder, AttachmentRule);
	Weapon->GetFirstPersonMesh()->AttachToComponent(HolderMesh1P, AttachmentRule, ResolvedSocket1P);
	FTransform FirstPersonRelativeTransform = Weapon->FirstPersonAttachmentOffset;
	FirstPersonRelativeTransform.SetScale3D(
		Weapon->AttachmentBaseScale1P * FirstPersonRelativeTransform.GetScale3D());
	Weapon->GetFirstPersonMesh()->SetRelativeTransform(FirstPersonRelativeTransform);
	Weapon->GetThirdPersonMesh()->AttachToComponent(HolderMesh3P, AttachmentRule, ResolvedSocket3P);
	FTransform ThirdPersonRelativeTransform = Weapon->ThirdPersonAttachmentOffset;
	ThirdPersonRelativeTransform.SetScale3D(
		Weapon->AttachmentBaseScale3P * ThirdPersonRelativeTransform.GetScale3D());
	Weapon->GetThirdPersonMesh()->SetRelativeTransform(ThirdPersonRelativeTransform);
}

void AShooterWeapon::ClientNotifyEnemyHit_Implementation()
{
	OnEnemyHit.Broadcast();
}
