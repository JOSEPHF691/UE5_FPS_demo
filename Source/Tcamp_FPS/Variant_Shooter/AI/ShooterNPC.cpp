// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variant_Shooter/AI/ShooterNPC.h"
#include "ShooterWeapon.h"
#include "ShooterProjectile.h"
#include "TacticalWeapon.h"
#include "ShooterGameState.h"
#include "Components/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/World.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "Treasure/TreasureChest.h"
#include "TacticalGameState.h"
#include "ShooterAIController.h"
#include "TacticalGuardArena.h"
#include "Variant_Shooter/TacticalCharacter.h"
#include "Engine/DamageEvents.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimationAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Effects/TacticalBodyHitEffect.h"
#include "Effects/TacticalBodyHitRules.h"

AShooterNPC::AShooterNPC()
{
	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	HealthComponent->MaxHP = 100.0f;
	HealthComponent->TeamByte = TeamByte;
	HealthComponent->DeathTag = FName("Dead");
}

void AShooterNPC::BeginPlay()
{
	Super::BeginPlay();

	HealthComponent->OnDeath.AddDynamic(this, &AShooterNPC::OnDeath);

	if (HasAuthority())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		Weapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponClass, GetActorTransform(), SpawnParams);
	}
}

void AShooterNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorld()->GetTimerManager().ClearTimer(DeathTimer);
	StopShooting();
	if (HasAuthority() && IsValid(Weapon) && !Weapon->IsActorBeingDestroyed())
	{
		Weapon->Destroy();
	}
	Weapon = nullptr;
	Super::EndPlay(EndPlayReason);
}

float AShooterNPC::TakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!HasAuthority() || !HealthComponent || HealthComponent->IsDead() || Damage <= 0.0f)
	{
		return 0.0f;
	}

	if (const ATacticalGameState* TGS = GetWorld()->GetGameState<ATacticalGameState>())
	{
		if (TGS->CurrentPhase != ERoundPhase::Combat)
		{
			return 0.0f;
		}
	}
	if (IsValid(GuardedChest))
	{
		const ATacticalGuardArena* Arena = ATacticalGuardArena::FindActive(GetWorld());
		if (!Arena || !Arena->IsPointInside(GetActorLocation()))
		{
			return 0.0f;
		}
		const APawn* Attacker = EventInstigator ? EventInstigator->GetPawn() : nullptr;
		if (!Attacker && DamageCauser)
		{
			Attacker = DamageCauser->GetInstigator();
			if (!Attacker)
			{
				Attacker = Cast<APawn>(DamageCauser->GetOwner());
			}
		}
		if (Cast<ATacticalCharacter>(Attacker)
			&& !Arena->IsPointInside(Attacker->GetActorLocation()))
		{
			return 0.0f;
		}
		// Also validate the actual damage source. A projectile can outlive its
		// shooter, and a muzzle may be on the other side while its owner stands
		// inside the gate. Direct Blueprint damage must obey the same boundary.
		if (IsValid(DamageCauser))
		{
			FVector Source = DamageCauser->GetActorLocation();
			if (const AShooterWeapon* WeaponCauser = Cast<AShooterWeapon>(DamageCauser))
			{
				Source = WeaponCauser->GetGameplayMuzzleLocation();
			}
			if (!Arena->IsPointInside(Source)
				|| Arena->DoesSegmentCrossBarrier(Source, GetActorLocation()))
			{
				return 0.0f;
			}
		}
	}

	bool bHeadshot = false;
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const FPointDamageEvent* PointEvent = static_cast<const FPointDamageEvent*>(&DamageEvent);
		bHeadshot = PointEvent->HitInfo.BoneName == HeadBoneName;
	}

	float FinalDamage = Damage;
	if (bHeadshot)
	{
		float Multiplier = TacticalEconomy::HeadshotMultiplier;
		if (const AShooterProjectile* Projectile = Cast<AShooterProjectile>(DamageCauser))
		{
			Multiplier = Projectile->WeaponHeadshotMultiplier;
		}
		else if (const ATacticalWeapon* WeaponCauser = Cast<ATacticalWeapon>(DamageCauser))
		{
			Multiplier = WeaponCauser->GetHeadshotMultiplier();
		}
		FinalDamage *= Multiplier;
	}

	const float HealthBefore = HealthComponent->CurrentHP;
	const float AppliedDamage = HealthComponent->TakeDamage(FinalDamage, EventInstigator, bHeadshot);
	const uint8 EffectFlags = TacticalBodyHitRules::Classify(HealthBefore, HealthComponent->CurrentHP,
		0.0f, 0.0f, bHeadshot,
		DamageEvent.IsOfType(FPointDamageEvent::ClassID)
			&& TacticalBodyHitRules::IsEnemyContact(this, EventInstigator));
	if (EffectFlags != TacticalBodyHitRules::None)
	{
		FVector ImpactPoint;
		FVector ImpactNormal;
		FName ImpactBone;
		if (TacticalBodyHitRules::TryResolvePointImpact(this, DamageEvent,
			ImpactPoint, ImpactNormal, ImpactBone))
		{
			MulticastResolvedBodyHit(EffectFlags, ImpactPoint, ImpactNormal, ImpactBone);
		}
	}
	return AppliedDamage;
}

void AShooterNPC::MulticastResolvedBodyHit_Implementation(const uint8 EffectFlags,
	const FVector_NetQuantize ImpactPoint, const FVector_NetQuantizeNormal ImpactNormal,
	const FName BoneName)
{
	ATacticalBodyHitEffect::SpawnForVictim(this, EffectFlags, ImpactPoint, ImpactNormal, BoneName);
}

void AShooterNPC::OnDeath()
{
	StopShooting();
	// Notify GameState so GameMode handles scoring — no Cast to GameMode
	if (AGameStateBase* GS = GetWorld()->GetGameState())
	{
		if (AShooterGameState* SGS = Cast<AShooterGameState>(GS))
		{
			SGS->OnKillEvent.Broadcast(HealthComponent->LastDamageInstigator, GetController());
		}
		if (ATacticalGameState* TGS = Cast<ATacticalGameState>(GS))
		{
			TGS->OnNPCKilled.Broadcast(HealthComponent->LastDamageInstigator, this);
		}
	}

	// Notify guarding chest
	if (GuardedChest)
	{
		GuardedChest->NotifyGuardKilled(this);
	}

	OnPawnDeath.Broadcast();

	// Death presentation. Some marketplace PhysicsAssets contain only coarse
	// query bodies and cannot safely be used as a humanoid ragdoll.
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->StopActiveMovement();
	GetCharacterMovement()->DisableMovement();

	if (USkeletalMeshComponent* CharacterMesh = GetMesh())
	{
		if (bRagdollOnDeath && CharacterMesh->GetPhysicsAsset())
		{
			CharacterMesh->SetCollisionProfileName(RagdollCollisionProfile);
			CharacterMesh->SetSimulatePhysics(true);
			CharacterMesh->SetPhysicsBlendWeight(1.0f);
		}
		else
		{
			CharacterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			CharacterMesh->SetSimulatePhysics(false);
			if (NPCDeathAnimation && IsAnimationCompatible(NPCDeathAnimation))
			{
				CharacterMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
				CharacterMesh->PlayAnimation(NPCDeathAnimation, false);
			}
		}
	}

	GetWorld()->GetTimerManager().SetTimer(DeathTimer, this, &AShooterNPC::DeferredDestruction, DeferredDestructionTime, false);
}

void AShooterNPC::DeferredDestruction()
{
	Destroy();
}

// ---- IShooterWeaponHolder ----

void AShooterNPC::AttachWeaponMeshes(AShooterWeapon* WeaponToAttach)
{
	const FName WorldWeaponSocket = bUseCharacterMuzzle
		? CharacterMuzzleSocket
		: ThirdPersonWeaponSocket;
	AShooterWeapon::AttachWeaponToHolder(WeaponToAttach, this, GetFirstPersonMesh(), GetMesh(),
		FirstPersonWeaponSocket, WorldWeaponSocket);
	ApplyWeaponVisualPolicy(WeaponToAttach);
}

void AShooterNPC::PlayFiringMontage(UAnimMontage* Montage)
{
	// Weapon montages are authored for the player skeleton. NPCs deliberately
	// play only their character-specific override to avoid cross-skeleton errors.
	if (NPCFiringMontage && IsAnimationCompatible(NPCFiringMontage))
	{
		if (UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
		{
			AnimInstance->Montage_Play(NPCFiringMontage);
		}
	}
}

void AShooterNPC::PlayReloadMontage(UAnimMontage* Montage)
{
	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return;
	}

	if (NPCReloadMontage && IsAnimationCompatible(NPCReloadMontage))
	{
		AnimInstance->Montage_Play(NPCReloadMontage);
	}
	else if (NPCReloadAnimation && IsAnimationCompatible(NPCReloadAnimation))
	{
		AnimInstance->PlaySlotAnimationAsDynamicMontage(
			NPCReloadAnimation, NPCAnimationSlotName);
	}
}
void AShooterNPC::AddWeaponRecoil(float Recoil) {}
void AShooterNPC::UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize, int32 ReserveAmmo) {}

FVector AShooterNPC::GetWeaponTargetLocation()
{
	FTransform CharacterMuzzleTransform;
	const bool bHasCharacterMuzzle = TryGetWeaponMuzzleTransform(CharacterMuzzleTransform);
	const UCameraComponent* AimCamera = GetFirstPersonCameraComponent();
	const FVector AimSource = bHasCharacterMuzzle
		? CharacterMuzzleTransform.GetLocation()
		: AimCamera->GetComponentLocation();
	FVector AimDir, AimTarget = FVector::ZeroVector;

	if (AActor* AimTargetActor = CurrentAimTarget.Get(); IsValid(AimTargetActor))
	{
		AimTarget = AimTargetActor->GetActorLocation();
		if (GuardedChest)
		{
			// Character actor locations sit near the capsule centre. Aim slightly
			// above it so treasure guards favour the torso instead of the legs.
			AimTarget.Z += 20.0f;
		}
		else
		{
			AimTarget.Z += FMath::RandRange(MinAimOffsetZ, MaxAimOffsetZ);
		}
		AimDir = (AimTarget - AimSource).GetSafeNormal();
		// The weapon applies its own authoritative cone spread. Treasure guards
		// supply an exact torso target here so their accuracy is not randomized twice.
		const float EffectiveVariance = GuardedChest ? 0.0f : AimVarianceHalfAngle;
		AimDir = UKismetMathLibrary::RandomUnitVectorInConeInDegrees(AimDir, EffectiveVariance);
	}
	else
	{
		const FVector Forward = bHasCharacterMuzzle
			? CharacterMuzzleTransform.GetRotation().GetForwardVector()
			: AimCamera->GetForwardVector();
		AimDir = UKismetMathLibrary::RandomUnitVectorInConeInDegrees(Forward, AimVarianceHalfAngle);
	}

	AimTarget = AimSource + (AimDir * AimRange);

	FHitResult OutHit;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	GetWorld()->LineTraceSingleByChannel(OutHit, AimSource, AimTarget, ECC_Visibility, QueryParams);

	return OutHit.bBlockingHit ? OutHit.ImpactPoint : OutHit.TraceEnd;
}

void AShooterNPC::AddWeaponClass(const TSubclassOf<AShooterWeapon>& InWeaponClass) {}
void AShooterNPC::OnWeaponActivated(AShooterWeapon* InWeapon)
{
	ApplyWeaponVisualPolicy(InWeapon);
}
void AShooterNPC::OnWeaponDeactivated(AShooterWeapon* InWeapon) {}

bool AShooterNPC::TryGetWeaponMuzzleTransform(FTransform& OutTransform) const
{
	if (!bUseCharacterMuzzle)
	{
		return false;
	}

	const USkeletalMeshComponent* CharacterMesh = GetMesh();
	if (CharacterMesh && CharacterMesh->DoesSocketExist(CharacterMuzzleSocket))
	{
		OutTransform = CharacterMesh->GetSocketTransform(CharacterMuzzleSocket, RTS_World);
		return true;
	}

	if (!bReportedMissingCharacterMuzzle)
	{
		bReportedMissingCharacterMuzzle = true;
		UE_LOG(LogTemp, Warning,
			TEXT("%s: integrated weapon muzzle socket '%s' is missing; using pawn view location."),
			*GetName(), *CharacterMuzzleSocket.ToString());
	}

	OutTransform = FTransform(GetActorRotation(), GetPawnViewLocation());
	return true;
}

void AShooterNPC::ApplyWeaponVisualPolicy(AShooterWeapon* InWeapon) const
{
	if (!bHideAttachedWeaponMeshes || !IsValid(InWeapon))
	{
		return;
	}

	for (USkeletalMeshComponent* WeaponMesh : {
		InWeapon->GetFirstPersonMesh(),
		InWeapon->GetThirdPersonMesh() })
	{
		if (WeaponMesh)
		{
			WeaponMesh->SetHiddenInGame(true, true);
			WeaponMesh->SetVisibility(false, true);
			WeaponMesh->SetComponentTickEnabled(false);
		}
	}
}

bool AShooterNPC::IsAnimationCompatible(const UAnimationAsset* Animation) const
{
	const USkeletalMeshComponent* CharacterMesh = GetMesh();
	const USkeletalMesh* SkeletalMesh = CharacterMesh
		? CharacterMesh->GetSkeletalMeshAsset()
		: nullptr;
	return Animation && SkeletalMesh && Animation->GetSkeleton()
		&& Animation->GetSkeleton() == SkeletalMesh->GetSkeleton();
}

void AShooterNPC::OnSemiWeaponRefire()
{
	if (bIsShooting && Weapon)
	{
		Weapon->StartFiring();
	}
}

void AShooterNPC::StartShooting(AActor* ActorToShoot)
{
	if (!Weapon || !HasAuthority()
		|| !AShooterAIController::IsValidGuardTarget(ActorToShoot))
	{
		return;
	}
	if (Weapon->GetBulletCount() <= 0)
	{
		StopShooting();
		RequestWeaponReload();
		return;
	}
	if (bIsShooting && CurrentAimTarget.Get() == ActorToShoot)
	{
		return;
	}
	CurrentAimTarget = ActorToShoot;
	bIsShooting = true;
	Weapon->StartFiring();
}

void AShooterNPC::StopShooting()
{
	if (!HasAuthority())
	{
		return;
	}
	bIsShooting = false;
	CurrentAimTarget = nullptr;
	if (Weapon)
	{
		Weapon->StopFiring();
	}
}

bool AShooterNPC::IsWeaponMagazineEmpty() const
{
	return !Weapon || Weapon->GetBulletCount() <= 0;
}

bool AShooterNPC::CanReloadWeapon() const
{
	return HasAuthority() && Weapon && !Weapon->IsReloadInProgress()
		&& Weapon->GetBulletCount() < Weapon->GetMagazineSize()
		&& Weapon->GetReserveBulletCount() > 0;
}

bool AShooterNPC::IsWeaponReloading() const
{
	return Weapon && Weapon->IsReloadInProgress();
}

bool AShooterNPC::RequestWeaponReload()
{
	if (!CanReloadWeapon())
	{
		return false;
	}

	StopShooting();
	float VisibleReloadDuration = 0.0f;
	if (NPCReloadMontage && IsAnimationCompatible(NPCReloadMontage))
	{
		const float EffectiveRateScale = FMath::Abs(NPCReloadMontage->RateScale);
		if (EffectiveRateScale > UE_KINDA_SMALL_NUMBER)
		{
			VisibleReloadDuration = NPCReloadMontage->GetPlayLength() / EffectiveRateScale;
		}
	}
	else if (NPCReloadAnimation && IsAnimationCompatible(NPCReloadAnimation))
	{
		const float EffectiveRateScale = FMath::Abs(NPCReloadAnimation->RateScale);
		if (EffectiveRateScale > UE_KINDA_SMALL_NUMBER)
		{
			VisibleReloadDuration = NPCReloadAnimation->GetPlayLength() / EffectiveRateScale;
		}
	}
	Weapon->ReloadWithMinimumDuration(VisibleReloadDuration);
	return true;
}
