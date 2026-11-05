// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterCharacter.h"
#include "ShooterWeapon.h"
#include "ShooterGameState.h"
#include "Components/HealthComponent.h"
#include "Components/WeaponInventoryComponent.h"
#include "EnhancedInputComponent.h"
#include "Components/InputComponent.h"
#include "Components/PawnNoiseEmitterComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "TimerManager.h"
#include "ShooterPlayerState.h"
#include "ShooterPlayerController.h"
#include "Animation/AnimInstance.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/SkeletalMesh.h"
#include "Weapons/TacticalWeapon.h"

AShooterCharacter::AShooterCharacter()
{
	static ConstructorHelpers::FClassFinder<UAnimInstance> UnarmedAnimClass(
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed_C"));
	if (UnarmedAnimClass.Succeeded())
	{
		SafeUnarmedThirdPersonAnimClass = UnarmedAnimClass.Class;
	}

	PawnNoiseEmitter = CreateDefaultSubobject<UPawnNoiseEmitterComponent>(TEXT("PawnNoiseEmitter"));

	HealthComponent = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	HealthComponent->MaxHP = MaxHP;
	HealthComponent->TeamByte = TeamByte;
	HealthComponent->DeathTag = FName("Dead");

	WeaponInventory = CreateDefaultSubobject<UWeaponInventoryComponent>(TEXT("WeaponInventory"));

	GetCharacterMovement()->RotationRate = FRotator(0.0f, 600.0f, 0.0f);
}

void AShooterCharacter::BeginPlay()
{
	Super::BeginPlay();

	HealthComponent->MaxHP = MaxHP;
	HealthComponent->TeamByte = TeamByte;

	HealthComponent->OnDamaged.AddDynamic(this, &AShooterCharacter::OnHealthDamaged);
	HealthComponent->OnDeath.AddDynamic(this, &AShooterCharacter::OnDeath);

	WeaponInventory->OnWeaponChanged.AddDynamic(this, &AShooterCharacter::OnWeaponChanged);
	WeaponInventory->OnAmmoUpdated.AddDynamic(this, &AShooterCharacter::OnAmmoChanged);

	// OnRep_CurrentWeapon may fire before BeginPlay, while on a remote proxy the
	// weapon actor may legitimately arrive a frame or more after its character.
	// Always establish an animation state now: a valid weapon selects its weapon
	// AnimBP; no weapon selects the safe unarmed AnimBP instead of allowing a
	// weapon AnimBP to tick and dereference a null CurrentWeapon.
	OnWeaponChanged(IsValid(WeaponInventory->CurrentWeapon.Get())
		? WeaponInventory->CurrentWeapon.Get()
		: nullptr, nullptr);
	InitializeFirstPersonViewRig();

	// Broadcast initial health state for HUD
	OnHealthDamaged(1.0f);
}

bool AShooterCharacter::ShouldApplyFirstPersonViewPitch() const
{
	// The legacy pistol AnimBP already aims its bones from camera/control pitch.
	// Rotating its entire viewmodel as well doubles that correction and pushes
	// the hands out of frame. RAR primary arms have no such pitch animation.
	const AShooterWeapon* Weapon = WeaponInventory
		? WeaponInventory->CurrentWeapon.Get() : nullptr;
	const ATacticalWeapon* TacticalWeapon = Cast<ATacticalWeapon>(Weapon);
	return !TacticalWeapon
		|| TacticalWeapon->GetWeaponCategoryEnum() != EWeaponCategory::Pistol;
}

void AShooterCharacter::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	GetWorld()->GetTimerManager().ClearTimer(RespawnTimer);
	Super::EndPlay(EndPlayReason);
}

void AShooterCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Started, this, &AShooterCharacter::DoStartFiring);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Completed, this, &AShooterCharacter::DoStopFiring);
		EnhancedInputComponent->BindAction(SwitchWeaponAction, ETriggerEvent::Started, this, &AShooterCharacter::DoSwitchWeapon);
		EnhancedInputComponent->BindAction(SelectPrimaryAction, ETriggerEvent::Started, this, &AShooterCharacter::DoSelectPrimary);
		EnhancedInputComponent->BindAction(SelectSecondaryAction, ETriggerEvent::Started, this, &AShooterCharacter::DoSelectSecondary);
		if (ReloadAction)
		{
			EnhancedInputComponent->BindAction(ReloadAction, ETriggerEvent::Started, this, &AShooterCharacter::DoReload);
		}
	}
}

// ---- Health ----

float AShooterCharacter::TakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!HealthComponent || HealthComponent->IsDead())
	{
		return 0.0f;
	}

	const float AppliedDamage = HealthComponent->TakeDamage(Damage, EventInstigator);
	NotifyOwningPlayerOfDamage(DamageEvent, EventInstigator, DamageCauser, AppliedDamage);
	return AppliedDamage;
}

void AShooterCharacter::NotifyOwningPlayerOfDamage(
	const FDamageEvent& DamageEvent,
	AController* EventInstigator,
	AActor* DamageCauser,
	float AppliedDamage) const
{
	if (!HasAuthority() || AppliedDamage <= UE_KINDA_SMALL_NUMBER || IsDead())
	{
		return;
	}

	FVector WorldDirectionToSource = FVector::ZeroVector;
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const FPointDamageEvent* PointDamageEvent =
			static_cast<const FPointDamageEvent*>(&DamageEvent);
		// FPointDamageEvent stores projectile travel direction (source to victim).
		// The HUD needs the inverse: victim to source.
		WorldDirectionToSource = (-PointDamageEvent->ShotDirection).GetSafeNormal();
	}

	if (WorldDirectionToSource.IsNearlyZero() && EventInstigator)
	{
		if (const APawn* InstigatorPawn = EventInstigator->GetPawn();
			IsValid(InstigatorPawn) && InstigatorPawn != this)
		{
			WorldDirectionToSource =
				(InstigatorPawn->GetActorLocation() - GetActorLocation()).GetSafeNormal();
		}
	}

	if (WorldDirectionToSource.IsNearlyZero()
		&& IsValid(DamageCauser) && DamageCauser != this)
	{
		WorldDirectionToSource =
			(DamageCauser->GetActorLocation() - GetActorLocation()).GetSafeNormal();
	}

	// A disconnected retained body is possessed by the server-only
	// ATacticalDisconnectedController, so this cast naturally suppresses transient
	// feedback while preserving normal health/death processing.
	if (AShooterPlayerController* VictimController =
		Cast<AShooterPlayerController>(GetController()))
	{
		VictimController->NotifyDamageReceived(WorldDirectionToSource, 1.0f);
	}
}

void AShooterCharacter::OnHealthDamaged(float HealthPercent)
{
	// HUD update — can be overridden or bound via delegate
}

void AShooterCharacter::OnDeath()
{
	if (WeaponInventory)
	{
		WeaponInventory->bCanFire = false;
		if (IsValid(WeaponInventory->CurrentWeapon.Get())
			&& !WeaponInventory->CurrentWeapon->IsActorBeingDestroyed())
		{
			WeaponInventory->CurrentWeapon->DeactivateWeapon();
		}
	}

	// Notify GameState so GameMode can handle scoring — no direct Cast to GameMode
	if (AGameStateBase* GS = GetWorld()->GetGameState())
	{
		if (AShooterGameState* SGS = Cast<AShooterGameState>(GS))
		{
			SGS->OnKillEvent.Broadcast(HealthComponent->LastDamageInstigator, GetController());
		}
	}

	GetCharacterMovement()->StopMovementImmediately();
	DisableInput(nullptr);

	// Reset HUD
	if (WeaponInventory)
	{
		WeaponInventory->OnAmmoUpdated.Broadcast(0, 0, 0);
	}

	GetWorld()->GetTimerManager().SetTimer(RespawnTimer, this, &AShooterCharacter::OnRespawn, RespawnTime, false);
}

void AShooterCharacter::OnRespawn()
{
	Destroy();
}

bool AShooterCharacter::IsDead() const
{
	return HealthComponent ? HealthComponent->IsDead() : false;
}

void AShooterCharacter::SetTeam(uint8 NewTeam)
{
	TeamByte = NewTeam;
	if (HealthComponent)
	{
		HealthComponent->TeamByte = NewTeam;
	}
}

// ---- Weapons (IHolder interface) ----

void AShooterCharacter::OnWeaponChanged(AShooterWeapon* NewWeapon, AShooterWeapon* OldWeapon)
{
	const bool bHasValidWeapon = IsValid(NewWeapon) && !NewWeapon->IsActorBeingDestroyed();
	USkeletalMeshComponent* Mesh1P = GetFirstPersonMesh();
	USkeletalMeshComponent* Mesh3P = GetMesh();
	if (Mesh1P && !bCapturedDefaultFirstPersonCharacterMesh)
	{
		DefaultFirstPersonCharacterMesh = Mesh1P->GetSkeletalMeshAsset();
		bCapturedDefaultFirstPersonCharacterMesh = true;
	}

	// On a late connection the weapon actor/reference can resolve after the
	// owner's persistent death state. Never let that delayed inventory update
	// reinstall a weapon animation or make a dead pawn look armed and alive.
	if (IsDead())
	{
		if (bHasValidWeapon)
		{
			NewWeapon->DeactivateWeapon();
		}
		if (WeaponInventory)
		{
			WeaponInventory->OnAmmoUpdated.Broadcast(0, 0, 0);
		}
		return;
	}

	if (!bHasValidWeapon)
	{
		// First-person arms are weapon-specific and are not needed while unarmed.
		if (Mesh1P && Mesh1P->GetAnimClass() != nullptr)
		{
			Mesh1P->SetAnimInstanceClass(nullptr);
		}
		if (Mesh1P && Mesh1P->GetSkeletalMeshAsset()
			!= DefaultFirstPersonCharacterMesh.Get())
		{
			Mesh1P->SetSkeletalMeshAsset(DefaultFirstPersonCharacterMesh.Get());
		}

		// ABP_TP_Rifle/ABP_TP_Pistol directly inspect CurrentWeapon. Use the
		// project-provided unarmed locomotion graph during replication gaps and
		// teardown so those graphs never update against null/pending-kill actors.
		UClass* SafeAnimClass = SafeUnarmedThirdPersonAnimClass.Get();
		if (Mesh3P && Mesh3P->GetAnimClass() != SafeAnimClass)
		{
			Mesh3P->SetAnimInstanceClass(SafeAnimClass);
		}

		if (WeaponInventory)
		{
			WeaponInventory->OnAmmoUpdated.Broadcast(0, 0, 0);
		}
		return;
	}

	if (IsLocallyControlled() && Mesh1P)
	{
		USkeletalMesh* DesiredFirstPersonMesh =
			NewWeapon->GetFirstPersonCharacterMeshOverride();
		if (!DesiredFirstPersonMesh)
		{
			DesiredFirstPersonMesh = DefaultFirstPersonCharacterMesh.Get();
		}
		if (Mesh1P->GetSkeletalMeshAsset() != DesiredFirstPersonMesh)
		{
			Mesh1P->SetSkeletalMeshAsset(DesiredFirstPersonMesh);
		}
		UClass* DesiredFirstPersonClass = NewWeapon->GetFirstPersonAnimInstanceClass().Get();
		if (Mesh1P->GetAnimClass() != DesiredFirstPersonClass)
		{
			Mesh1P->SetAnimInstanceClass(DesiredFirstPersonClass);
		}
		if (USkeletalMeshComponent* WeaponMesh = NewWeapon->GetFirstPersonMesh())
		{
			UClass* DesiredWeaponClass =
				NewWeapon->GetFirstPersonWeaponAnimInstanceClass().Get();
			if (DesiredWeaponClass && WeaponMesh->GetAnimClass() != DesiredWeaponClass)
			{
				WeaponMesh->SetAnimInstanceClass(DesiredWeaponClass);
			}
		}

		// The inventory can announce a newly spawned rifle before its BeginPlay,
		// so its first attachment may fall back to hand_r on the old arms mesh.
		// Reattach after the weapon-specific arms are installed; otherwise that
		// fallback can persist on the locally viewed gun in network PIE.
		AttachWeaponMeshes(NewWeapon);
	}

	if (Mesh3P)
	{
		UClass* DesiredThirdPersonClass = NewWeapon->GetThirdPersonAnimInstanceClass().Get();
		if (!DesiredThirdPersonClass)
		{
			DesiredThirdPersonClass = SafeUnarmedThirdPersonAnimClass.Get();
		}
		if (Mesh3P->GetAnimClass() != DesiredThirdPersonClass)
		{
			Mesh3P->SetAnimInstanceClass(DesiredThirdPersonClass);
		}
	}

	if (WeaponInventory)
	{
		WeaponInventory->OnAmmoUpdated.Broadcast(NewWeapon->GetBulletCount(), NewWeapon->GetMagazineSize(), NewWeapon->GetReserveBulletCount());
	}
}

void AShooterCharacter::OnAmmoChanged(int32 CurrentAmmo, int32 MagazineSize, int32 ReserveAmmo)
{
	// Broadcast to UI — PlayerController listens and updates widget
}

// ---- Input ----

void AShooterCharacter::DoStartFiring()
{
	if (!IsDead() && WeaponInventory)
	{
		WeaponInventory->StartFiring();
	}
}

void AShooterCharacter::DoStopFiring()
{
	if (WeaponInventory)
	{
		WeaponInventory->StopFiring();
	}
}

void AShooterCharacter::DoSwitchWeapon()
{
	if (!IsDead() && WeaponInventory)
	{
		WeaponInventory->SwitchWeapon();
	}
}

void AShooterCharacter::DoSelectPrimary()
{
	if (!IsDead() && WeaponInventory)
	{
		WeaponInventory->SwitchToCategory(1);
	}
}

void AShooterCharacter::DoSelectSecondary()
{
	if (!IsDead() && WeaponInventory)
	{
		WeaponInventory->SwitchToCategory(0);
	}
}

void AShooterCharacter::DoReload()
{
	if (!IsDead() && WeaponInventory)
	{
		WeaponInventory->Reload();
	}
}

void AShooterCharacter::DoAim(float Yaw, float Pitch)
{
	if (!IsDead())
	{
		Super::DoAim(Yaw, Pitch);
	}
}

void AShooterCharacter::DoMove(float Right, float Forward)
{
	if (!IsDead())
	{
		Super::DoMove(Right, Forward);
	}
}

void AShooterCharacter::DoJumpStart()
{
	if (!IsDead())
	{
		Super::DoJumpStart();
	}
}

void AShooterCharacter::DoJumpEnd()
{
	if (!IsDead())
	{
		Super::DoJumpEnd();
	}
}

// ---- IShooterWeaponHolder ----

void AShooterCharacter::AttachWeaponMeshes(AShooterWeapon* Weapon)
{
	AShooterWeapon::AttachWeaponToHolder(Weapon, this, GetFirstPersonMesh(), GetMesh(),
		FirstPersonWeaponSocket, ThirdPersonWeaponSocket);
}

void AShooterCharacter::PlayFiringMontage(UAnimMontage* Montage)
{
	// Override in subclass for montage playback
}

void AShooterCharacter::PlayReloadMontage(UAnimMontage* Montage)
{
	// Override in subclass for montage playback
}

void AShooterCharacter::AddWeaponRecoil(float Recoil)
{
	AddControllerPitchInput(Recoil);
}

void AShooterCharacter::UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize, int32 ReserveAmmo)
{
	WeaponInventory->OnAmmoUpdated.Broadcast(CurrentAmmo, MagazineSize, ReserveAmmo);
}

FVector AShooterCharacter::GetWeaponTargetLocation()
{
	FHitResult OutHit;
	FVector Start;
	FRotator ViewRotation;

	if (AController* OwnerController = GetController())
	{
		OwnerController->GetPlayerViewPoint(Start, ViewRotation);
	}
	else if (GetFirstPersonCameraComponent())
	{
		Start = GetFirstPersonCameraComponent()->GetComponentLocation();
		ViewRotation = GetFirstPersonCameraComponent()->GetComponentRotation();
	}
	else
	{
		Start = GetActorLocation();
		ViewRotation = GetActorRotation();
	}

	const FVector End = Start + (ViewRotation.Vector() * MaxAimDistance);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, QueryParams);
	return OutHit.bBlockingHit ? OutHit.ImpactPoint : OutHit.TraceEnd;
}

void AShooterCharacter::AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass)
{
	if (WeaponInventory)
	{
		WeaponInventory->AddWeapon(WeaponClass);
	}
}

void AShooterCharacter::GrantWeaponClass(TSubclassOf<AShooterWeapon> WeaponClass)
{
	AddWeaponClass(WeaponClass);
}

void AShooterCharacter::OnWeaponActivated(AShooterWeapon* Weapon)
{
	// A weapon actor may begin play after an already-dead owner was reconstructed
	// on a reconnecting client. Keep the delayed actor hidden and inactive.
	if (IsDead())
	{
		if (IsValid(Weapon) && !Weapon->IsActorBeingDestroyed())
		{
			Weapon->DeactivateWeapon();
		}
		return;
	}

	// A replicated weapon actor can BeginPlay before the inventory property that
	// identifies it as CurrentWeapon. Its BeginPlay still invokes this callback;
	// do not install a weapon AnimBP until the inventory transition is visible.
	if (!IsValid(Weapon) || Weapon->IsActorBeingDestroyed() || !WeaponInventory
		|| WeaponInventory->CurrentWeapon.Get() != Weapon)
	{
		return;
	}

	// Route every activation path through the same guarded animation/HUD update.
	// This preserves the safe unarmed fallback during replication/reconnect gaps
	// instead of directly installing a possibly-null class here.
	OnWeaponChanged(Weapon, nullptr);
}

void AShooterCharacter::OnWeaponDeactivated(AShooterWeapon* Weapon)
{
	// No-op — child classes may override
}

void AShooterCharacter::OnSemiWeaponRefire()
{
	// No-op — child classes may override
}
