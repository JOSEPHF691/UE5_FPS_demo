// Copyright Epic Games, Inc. All Rights Reserved.

#include "TacticalCharacter.h"
#include "Armor/ArmorComponent.h"
#include "Weapons/TacticalWeapon.h"
#include "Weapons/WeaponDropPickup.h"
#include "Weapons/ShooterProjectile.h"
#include "Components/HealthComponent.h"
#include "Components/WeaponInventoryComponent.h"
#include "TacticalPlayerState.h"
#include "TacticalGameMode.h"
#include "TacticalGameState.h"
#include "TacticalPlayerController.h"
#include "TacticalDiagnostics.h"
#include "Effects/TacticalBodyHitEffect.h"
#include "Effects/TacticalBodyHitRules.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Controller.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "Animation/TacticalFPAnimInstance.h"
#include "Animation/TacticalRARFPAnimInstance.h"
#include "Animation/TacticalRARWeaponAnimInstance.h"
#include "Animation/TacticalSniperFPGripAnimInstance.h"
#include "Tcamp_FPSCharacter.h"
#include "Weapons/ShooterPickup.h"
#include "Treasure/TreasureChest.h"
#include "AI/TacticalGuardArena.h"

namespace
{
	// CurrentWeapon/OwnedWeapons replicate on the pawn channel while each weapon
	// is destroyed on its own actor channel. Keep a retired, hidden weapon alive
	// briefly so clients can consume the inventory transition and unbind UI/AnimBP
	// delegates before the old actor becomes pending-kill.
	constexpr float RetiredWeaponReplicationGraceSeconds = 1.0f;

	void RetireWeaponAfterReplication(AShooterWeapon* Weapon)
	{
		if (!IsValid(Weapon) || Weapon->IsActorBeingDestroyed())
		{
			return;
		}

		Weapon->DeactivateWeapon();
		Weapon->SetActorEnableCollision(false);
		Weapon->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		Weapon->SetLifeSpan(RetiredWeaponReplicationGraceSeconds);
	}

	bool PlayPresentationMontage(
		USkeletalMeshComponent* Mesh, UAnimMontage* Montage)
	{
		if (Mesh && Montage)
		{
			if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
			{
				return AnimInstance->Montage_Play(Montage) > 0.0f;
			}
		}
		return false;
	}

	UAnimMontage* ResolveFirstPersonFireMontage(
		USkeletalMeshComponent* FirstPersonArms, AShooterWeapon* Weapon,
		bool bSightAligned)
	{
		if (!FirstPersonArms || !IsValid(Weapon))
		{
			return nullptr;
		}
		if (UTacticalRARFPAnimInstance* RARAnim =
			Cast<UTacticalRARFPAnimInstance>(FirstPersonArms->GetAnimInstance()))
		{
			if (const ATacticalWeapon* TacticalWeapon = Cast<ATacticalWeapon>(Weapon))
			{
				const float AimAlpha = TacticalWeapon->GetAimBlendAlpha();
				if (TacticalWeapon->IsAiming() && AimAlpha >= 0.99f
					&& bSightAligned)
				{
					return RARAnim->GetAimedFireMontage();
				}
				if (TacticalWeapon->IsAiming()
					|| AimAlpha > UE_KINDA_SMALL_NUMBER)
				{
					// Both RAR fire clips are authored for endpoint poses.
					// From the very first aim-input frame through the last aim-out
					// frame, preserve the continuously blended arms pose and let
					// the gun-only action supply the shot movement.
					return nullptr;
				}
			}
		}
		return Weapon->GetFirstPersonFiringMontage();
	}

	UAnimMontage* ResolveFirstPersonReloadMontage(
		USkeletalMeshComponent* FirstPersonArms, AShooterWeapon* Weapon)
	{
		if (!FirstPersonArms || !IsValid(Weapon))
		{
			return nullptr;
		}
		if (const ATacticalWeapon* TacticalWeapon = Cast<ATacticalWeapon>(Weapon);
			TacticalWeapon && TacticalWeapon->IsAiming())
		{
			if (UTacticalRARFPAnimInstance* RARAnim =
				Cast<UTacticalRARFPAnimInstance>(FirstPersonArms->GetAnimInstance()))
			{
				if (UAnimMontage* AimedReloadMontage = RARAnim->GetAimedReloadMontage())
				{
					return AimedReloadMontage;
				}
			}
		}
		return Weapon->GetFirstPersonReloadMontage();
	}

	// The RAR first-person gun and its world-space copy can share the authored
	// weapon animation graph only when both meshes use the same skeleton. Keep
	// this independent of the character's first-/third-person animation graphs.
	USkeletalMeshComponent* EnsureMatchingRARWorldWeaponAnimation(
		AShooterWeapon* Weapon)
	{
		if (!IsValid(Weapon) || Weapon->IsActorBeingDestroyed())
		{
			return nullptr;
		}

		USkeletalMeshComponent* FirstPersonGun = Weapon->GetFirstPersonMesh();
		USkeletalMeshComponent* WorldGun = Weapon->GetThirdPersonMesh();
		const TSubclassOf<UAnimInstance> AnimClass =
			Weapon->GetFirstPersonWeaponAnimInstanceClass();
		if (!FirstPersonGun || !WorldGun || !AnimClass
			|| !AnimClass->IsChildOf(UTacticalRARWeaponAnimInstance::StaticClass()))
		{
			return nullptr;
		}

		const USkeletalMesh* FirstPersonAsset = FirstPersonGun->GetSkeletalMeshAsset();
		const USkeletalMesh* WorldAsset = WorldGun->GetSkeletalMeshAsset();
		if (!FirstPersonAsset || !WorldAsset || !FirstPersonAsset->GetSkeleton()
			|| FirstPersonAsset->GetSkeleton() != WorldAsset->GetSkeleton())
		{
			return nullptr;
		}

		if (WorldGun->GetAnimClass() != AnimClass.Get()
			|| !WorldGun->GetAnimInstance())
		{
			WorldGun->SetAnimInstanceClass(AnimClass);
		}
		return WorldGun;
	}

	void PlayMatchingRARWorldWeaponMontage(
		AShooterWeapon* Weapon, UAnimMontage* Montage)
	{
		USkeletalMeshComponent* WorldGun =
			EnsureMatchingRARWorldWeaponAnimation(Weapon);
		const USkeletalMesh* WorldAsset = WorldGun
			? WorldGun->GetSkeletalMeshAsset() : nullptr;
		if (WorldAsset && Montage
			&& WorldAsset->GetSkeleton() == Montage->GetSkeleton())
		{
			PlayPresentationMontage(WorldGun, Montage);
		}
	}
}

ATacticalCharacter::ATacticalCharacter()
{
	ArmorComponent = CreateDefaultSubobject<UArmorComponent>(TEXT("Armor"));
	MaxHP = 500.0f;
	HealthComponent->MaxHP = MaxHP;
	TeamByte = 255;
	HealthComponent->TeamByte = TeamByte;

	// The Blueprint supplies the default meshes; a weapon may substitute an
	// authored first-person mesh without changing the world-space body.
	if (USkeletalMeshComponent* ThirdPersonManny = GetMesh())
	{
		ThirdPersonManny->SetRenderInMainPass(true);
		ThirdPersonManny->SetRenderInDepthPass(true);
		ThirdPersonManny->SetCastShadow(true);
		ThirdPersonManny->SetOwnerNoSee(true);
		ThirdPersonManny->SetOnlyOwnerSee(false);
	}
	if (USkeletalMeshComponent* FirstPersonManny = GetFirstPersonMesh())
	{
		FirstPersonManny->SetRenderInMainPass(true);
		FirstPersonManny->SetRenderInDepthPass(true);
		FirstPersonManny->SetCastShadow(false);
		FirstPersonManny->SetOwnerNoSee(false);
		FirstPersonManny->SetOnlyOwnerSee(true);
	}

	// Required by AI SenseEnemies task — tags the player character so NPCs can identify it
	Tags.Add(FName("Player"));
}

void ATacticalCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Restore the render state so an editor hot reload or retained PIE object
	// cannot inherit an old hidden-driver configuration.
	if (USkeletalMeshComponent* ThirdPersonManny = GetMesh())
	{
		ThirdPersonManny->SetRenderInMainPass(true);
		ThirdPersonManny->SetRenderInDepthPass(true);
		ThirdPersonManny->SetCastShadow(true);
		ThirdPersonManny->SetOwnerNoSee(true);
		ThirdPersonManny->SetOnlyOwnerSee(false);
		ThirdPersonManny->SetHiddenInGame(false, false);
		ThirdPersonManny->SetVisibility(true, false);
	}
	if (USkeletalMeshComponent* FirstPersonManny = GetFirstPersonMesh())
	{
		FirstPersonManny->SetRenderInMainPass(true);
		FirstPersonManny->SetRenderInDepthPass(true);
		FirstPersonManny->SetCastShadow(false);
		FirstPersonManny->SetOwnerNoSee(false);
		FirstPersonManny->SetOnlyOwnerSee(true);
		FirstPersonManny->SetHiddenInGame(false, false);
		FirstPersonManny->SetVisibility(true, false);
		FirstPersonManny->VisibilityBasedAnimTickOption = IsLocallyControlled()
			? EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones
			: EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	}

	// ShooterCharacter has migrated the camera and arms onto the stable eye
	// pivot. Cache their relative offset before spectator presentation changes
	// the first-person mesh and animation instance.
	CacheStableSpectatorCameraTransform();

	// The listen-server pawn can be possessed and receive SetFrozen(false)
	// before components have begun play. At that point CurrentHP is still zero,
	// so the early gate calculation sees the pawn as dead and disables firing.
	// Before this point phase changes only cache bFrozen. Recompute every derived
	// movement/weapon gate now that HealthComponent initialized CurrentHP.
	bGameplayGatesInitialized = true;
	ApplyFrozenLocally(bFrozen);
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=CombatGateInitialized Pawn=\"%s\" NetMode=%s Local=%d Health=%.1f Frozen=%d CanFire=%d Weapon=\"%s\""),
		*GetName(), TacticalDiagnostics::GetNetModeName(GetNetMode()), IsLocallyControlled(),
		HealthComponent ? HealthComponent->CurrentHP : 0.0f, bFrozen,
		WeaponInventory ? WeaponInventory->bCanFire : false,
		*GetNameSafe(WeaponInventory ? WeaponInventory->CurrentWeapon.Get() : nullptr));

	// This Tick only feeds the owning player's first-person animation. Remote
	// proxies and the dedicated server do not need to execute it.
	SetActorTickEnabled(IsLocallyControlled());

	if (UCameraComponent* Cam = GetFirstPersonCameraComponent())
	{
		DefaultFOV = Cam->FieldOfView;
		DefaultFirstPersonFOV = Cam->FirstPersonFieldOfView;
		TargetFOV = DefaultFOV;
		TargetFirstPersonFOV = DefaultFirstPersonFOV;
	}
	if (USkeletalMeshComponent* Mesh1P = GetFirstPersonMesh())
	{
		HipFirstPersonMeshRelativeLocation = Mesh1P->GetRelativeLocation();
		HipFirstPersonMeshRelativeRotation = Mesh1P->GetRelativeRotation().Quaternion();
		bHasHipFirstPersonMeshRelativeLocation = true;
	}

	if (HealthComponent)
	{
		HealthComponent->OnProcessDamage.BindUObject(this, &ATacticalCharacter::ProcessArmorDamage);
	}

	// Listen for weapon swaps to re-bind scope delegates
	if (WeaponInventory)
	{
		WeaponInventory->OnWeaponChanged.AddDynamic(this, &ATacticalCharacter::OnWeaponChanged_Scope);
		WeaponInventory->OnWeaponChanged.AddDynamic(
			this, &ATacticalCharacter::OnWeaponChanged_Attachment);
		OnWeaponChanged_Attachment(WeaponInventory->CurrentWeapon.Get(), nullptr);

		// Bind delegates for the weapon that was already created before BeginPlay
		if (IsValid(WeaponInventory->CurrentWeapon.Get())
			&& !WeaponInventory->CurrentWeapon->IsActorBeingDestroyed())
		{
			OnWeaponChanged_Scope(WeaponInventory->CurrentWeapon.Get(), nullptr);
		}
	}

	// Initial RepNotifies may run before AShooterCharacter binds OnDeath. Replay
	// the persistent state after all TacticalCharacter setup is complete so a
	// reconnecting client still renders pawns that died before it joined as dead.
	if (HealthComponent && HealthComponent->IsDead())
	{
		OnDeath();
	}
}

void ATacticalCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	// BeginPlay may have disabled the owner-only viewmodel Tick before this
	// retained pawn became locally controlled during reconnect/possession.
	SetActorTickEnabled(IsLocallyControlled());

	// A retained/reconnected pawn can become locally controlled after BeginPlay.
	// Resume its weapon animation and converge the gun back onto the visible
	// first-person weapon socket.
	if (USkeletalMeshComponent* FirstPersonManny = GetFirstPersonMesh())
	{
		FirstPersonManny->VisibilityBasedAnimTickOption =
			EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

		if (WeaponInventory)
		{
			AShooterWeapon* CurrentWeapon = WeaponInventory->CurrentWeapon.Get();
			if (IsValid(CurrentWeapon) && !CurrentWeapon->IsActorBeingDestroyed())
			{
				// Remote pawns intentionally clear their first-person AnimBP in
				// BeginPlay. A retained pawn that becomes local again must reinstall
				// the current weapon graph before its first rendered reconnect frame.
				OnWeaponChanged(CurrentWeapon, nullptr);
				OnWeaponChanged_Attachment(CurrentWeapon, nullptr);
			}
			else
			{
				UpdateSniperFirstPersonGripOverlay(nullptr);
			}
		}
	}
	UpdateThirdPersonGripOverlay(
		WeaponInventory ? WeaponInventory->CurrentWeapon.Get() : nullptr);
}

USkeletalMeshComponent* ATacticalCharacter::ResolveFirstPersonWeaponAttachmentMesh() const
{
	return GetFirstPersonMesh();
}

void ATacticalCharacter::AttachWeaponMeshes(AShooterWeapon* Weapon)
{
	USkeletalMeshComponent* FirstPersonGripMesh =
		ResolveFirstPersonWeaponAttachmentMesh();
	const FName AuthoredSocket = IsValid(Weapon)
		? Weapon->GetFirstPersonHolderSocketOverride() : NAME_None;
	const FName EffectiveFirstPersonSocket = AuthoredSocket.IsNone()
		? FirstPersonWeaponSocket : AuthoredSocket;
	AShooterWeapon::AttachWeaponToHolder(
		Weapon,
		this,
		FirstPersonGripMesh,
		GetMesh(),
		EffectiveFirstPersonSocket,
		ThirdPersonWeaponSocket);
	EnsureMatchingRARWorldWeaponAnimation(Weapon);

	if (IsValid(Weapon) && FirstPersonGripMesh
		&& Weapon->GetFirstPersonMesh()
		&& Weapon->GetFirstPersonMesh()->GetAttachParent() == FirstPersonGripMesh)
	{
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=FirstPersonWeaponAttached Pawn=\"%s\" Weapon=\"%s\" Parent=\"%s\" Socket=\"%s\""),
			*GetName(), *GetNameSafe(Weapon), *GetNameSafe(FirstPersonGripMesh),
			*Weapon->GetFirstPersonMesh()->GetAttachSocketName().ToString());
	}
}

void ATacticalCharacter::EnsureFirstPersonWeaponAttachment(AShooterWeapon* Weapon)
{
	if (!IsValid(Weapon) || Weapon->IsActorBeingDestroyed())
	{
		return;
	}

	USkeletalMeshComponent* DesiredGripMesh =
		ResolveFirstPersonWeaponAttachmentMesh();
	USkeletalMeshComponent* WeaponMesh = Weapon->GetFirstPersonMesh();
	if (!DesiredGripMesh || !WeaponMesh)
	{
		return;
	}

	FName ExpectedSocket = Weapon->GetFirstPersonHolderSocketOverride().IsNone()
		? FirstPersonWeaponSocket : Weapon->GetFirstPersonHolderSocketOverride();
	if (!DesiredGripMesh->DoesSocketExist(ExpectedSocket))
	{
		// Remote copies keep their default arms until observed. The attachment
		// helper falls back to hand_r there; compare against that actual fallback
		// so each activation does not detach and reattach the same gun again.
		static const FName HandBoneName(TEXT("hand_r"));
		ExpectedSocket = DesiredGripMesh->DoesSocketExist(HandBoneName)
			? HandBoneName : NAME_None;
	}
	if (WeaponMesh->GetAttachParent() != DesiredGripMesh
		|| WeaponMesh->GetAttachSocketName() != ExpectedSocket)
	{
		AttachWeaponMeshes(Weapon);
	}
}

void ATacticalCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction)
		{
			// Poll the processed action value from Tick instead of depending on a
			// Triggered delegate being dispatched every frame in PIE.
			EnhancedInputComponent->BindActionValue(MoveAction);
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Started, this, &ATacticalCharacter::CaptureMoveInput);
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Ongoing, this, &ATacticalCharacter::CaptureMoveInput);
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Completed, this, &ATacticalCharacter::ClearMoveInput);
			EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Canceled, this, &ATacticalCharacter::ClearMoveInput);
		}

		if (AimAction)
		{
			EnhancedInputComponent->BindAction(AimAction, ETriggerEvent::Started, this, &ATacticalCharacter::DoStartAiming);
			EnhancedInputComponent->BindAction(AimAction, ETriggerEvent::Completed, this, &ATacticalCharacter::DoStopAiming);
			EnhancedInputComponent->BindAction(AimAction, ETriggerEvent::Canceled, this, &ATacticalCharacter::DoStopAiming);
		}

		if (HoldBreathAction)
		{
			EnhancedInputComponent->BindAction(HoldBreathAction, ETriggerEvent::Started, this, &ATacticalCharacter::DoStartHoldBreath);
			EnhancedInputComponent->BindAction(HoldBreathAction, ETriggerEvent::Completed, this, &ATacticalCharacter::DoStopHoldBreath);
		}

		if (InteractAction)
		{
			EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &ATacticalCharacter::DoInteract);
			EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Completed, this, &ATacticalCharacter::DoStopInteract);
			EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Canceled, this, &ATacticalCharacter::DoStopInteract);
		}
	}
}

void ATacticalCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority() && NearbyTreasureChest)
	{
		NearbyTreasureChest->CancelClaim(this);
	}
	Super::EndPlay(EndPlayReason);
}

void ATacticalCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Movement input is accumulated for one frame by CharacterMovement. Read the
	// current action value directly, then resubmit it every character tick.
	if (const UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(InputComponent))
	{
		if (MoveAction)
		{
			CachedMoveInput = EnhancedInputComponent->GetBoundActionValue(MoveAction)
				.Get<FVector2D>().GetClampedToMaxSize(1.0f);
		}
	}
	if (IsLocallyControlled() && !bFrozen && !IsDead() && !CachedMoveInput.IsNearlyZero())
	{
		Super::DoMove(CachedMoveInput.X, CachedMoveInput.Y);
	}

	AShooterWeapon* CurrentWeapon = WeaponInventory
		? WeaponInventory->CurrentWeapon.Get() : nullptr;
	ATacticalWeapon* TW = IsValid(CurrentWeapon)
		&& !CurrentWeapon->IsActorBeingDestroyed()
		? Cast<ATacticalWeapon>(CurrentWeapon) : nullptr;
	const bool bAiming = TW && TW->IsAiming() && !bFrozen && !IsDead();
	if (TW)
	{
		TW->AdvanceLocalAimBlend(DeltaTime, bAiming);
	}
	const float AimAlpha = TW ? TW->GetAimBlendAlpha() : 0.0f;

	// This pawn is the owner-side viewmodel driver. The rifle uses a native RAR
	// graph. Other custom graphs derived from TacticalFPAnimInstance can consume
	// these values; the shipped pistol AnimBP derives directly from AnimInstance
	// and keeps its own camera-target Control Rig. Remote pawns do not tick here.

	if (USkeletalMeshComponent* Mesh1P = GetFirstPersonMesh())
	{
		if (UTacticalFPAnimInstance* AnimInst = Cast<UTacticalFPAnimInstance>(Mesh1P->GetAnimInstance()))
		{
			AnimInst->bIsAiming = bAiming;
			AnimInst->AimBlendAlpha = AimAlpha;
		}
		if (UTacticalRARFPAnimInstance* RARAnim =
			Cast<UTacticalRARFPAnimInstance>(Mesh1P->GetAnimInstance()))
		{
			RARAnim->SetAimState(bAiming, AimAlpha);
		}
	}

	const float AimFOV = TW ? TW->GetConfiguredAimFOV() : 0.0f;
	TargetFOV = AimFOV > 0.0f ? AimFOV : DefaultFOV;
	// World zoom and first-person projection are independent. Zooming the gun
	// mesh with the world made its receiver fill and obscure the sight picture.
	// The imported sniper has a long optic and carry guard. At the shared 70-degree
	// viewmodel FOV they look substantially larger than the world and crowd the
	// centre of the screen. Keep the camera/gameplay FOV unchanged; widen only
	// this weapon's owner-side viewmodel projection.
	TargetFirstPersonFOV = TW && TW->GetWeaponTierEnum() == EWeaponTier::Sniper
		? 88.0f : DefaultFirstPersonFOV;
	if (UCameraComponent* Camera = GetFirstPersonCameraComponent())
	{
		// Reuse the pose/root alpha in both directions. A separate FOV timer
		// reaches hip earlier than the gun during release and reads as a pop.
		const float WorldFOV = FMath::Lerp(DefaultFOV, TargetFOV, AimAlpha);
		const float ViewmodelFOV = FMath::FInterpTo(
			Camera->FirstPersonFieldOfView, TargetFirstPersonFOV,
			DeltaTime, 12.0f);
		Camera->SetFieldOfView(WorldFOV);
		Camera->SetFirstPersonFieldOfView(ViewmodelFOV);
	}
	// Keep the same weapon profile while AimAlpha decays. Dropping the weapon on
	// button release made the root correction retreat independently of the
	// authored aim-out pose and looked like an immediate ADS-to-hip pop.
	UpdateFirstPersonIronSight(TW, DeltaTime);
	if (TW && TW->GetWeaponTierEnum() == EWeaponTier::Sniper)
	{
		// This pack has no matching first-person character ADS pose. Once the
		// dedicated scope picture is fully up, remove only owner-side geometry;
		// retain the animated source and world weapon for all other viewers.
		const bool bScopePicture = AimAlpha >= 0.9f && !IsDead();
		if (USkeletalMeshComponent* SniperGun = TW->GetFirstPersonMesh())
		{
			SniperGun->SetRenderInMainPass(!bScopePicture);
			SniperGun->SetRenderInDepthPass(!bScopePicture);
		}
		if (SniperFirstPersonGripOverlay)
		{
			SniperFirstPersonGripOverlay->SetRenderInMainPass(!bScopePicture);
			SniperFirstPersonGripOverlay->SetRenderInDepthPass(!bScopePicture);
		}
	}
}

void ATacticalCharacter::UpdateFirstPersonIronSight(
	ATacticalWeapon* Weapon, float DeltaTime)
{
	const bool bWasIronSightReady = bFirstPersonIronSightReady;
	bFirstPersonIronSightReady = false;
	USkeletalMeshComponent* Arms = GetFirstPersonMesh();
	if (!Arms || !bHasHipFirstPersonMeshRelativeLocation)
	{
		return;
	}

	FVector WantedRelativeLocation = HipFirstPersonMeshRelativeLocation;
	FQuat WantedRelativeRotation = HipFirstPersonMeshRelativeRotation;
	if (Weapon && Weapon->GetWeaponTierEnum() == EWeaponTier::Sniper)
	{
		// Fallback when the optic socket is unavailable. Normally the camera-
		// relative solver below replaces this approximate hip offset.
		WantedRelativeLocation += FVector(0.0f, 8.0f, -2.0f);
	}
	const UCameraComponent* Camera = GetFirstPersonCameraComponent();
	const USceneComponent* Parent = Arms->GetAttachParent();
	USkeletalMeshComponent* Gun = Weapon ? Weapon->GetFirstPersonMesh() : nullptr;
	const FTacticalVisualAimProfile Profile = Weapon
		? Weapon->GetFirstPersonVisualAimProfile() : FTacticalVisualAimProfile();
	const float AimAlpha = Weapon ? Weapon->GetAimBlendAlpha() : 0.0f;
	if (!Weapon || Weapon->GetWeaponTierEnum() != EWeaponTier::Sniper)
	{
		SniperMoveBobWeight = 0.0f;
	}
	if (Weapon && Weapon->GetWeaponTierEnum() == EWeaponTier::Sniper
		&& Camera && Parent && Gun && Gun->GetAttachParent() == Arms
		&& Gun->DoesSocketExist(TEXT("SightRear")))
	{
		// This older arms Control Rig keeps a world-space hand target. As the
		// camera pitches, it moves the sniper toward/away from the lens even
		// though the gun's forward axis follows pitch. Pin one reference on the
		// optic to a comfortable camera-relative hip position instead of
		// guessing a different root offset for every look angle. The source
		// arms, attached gun and support-hand overlay move together.
		// Raise the actual rifle and both hands toward the centre before the
		// full-screen scope takes over. The imported pack has no Manny-compatible
		// aim-in clip, so use the same aim alpha as the FOV/scope transition.
		const FVector HipRearInCamera(26.7f, 14.0f, -11.9f);
		const FVector AimedRearInCamera(28.0f, 0.0f, -4.0f);
		const float RaiseAlpha = FMath::Clamp(AimAlpha / 0.9f, 0.0f, 1.0f);
		const float SmoothRaiseAlpha = RaiseAlpha * RaiseAlpha
			* (3.0f - 2.0f * RaiseAlpha);
		// The source Manny's walking graph barely animates its right hand, so
		// the attached sniper otherwise stays camera-locked while the left arm
		// moves. Drive a restrained walk cycle through the shared arms root:
		// the right hand, gun and IK-constrained support hand all sway together.
		const float MovementSpeed = GetVelocity().Size2D();
		const float MaxMovementSpeed = GetCharacterMovement()
			? FMath::Max(1.0f, GetCharacterMovement()->GetMaxSpeed()) : 600.0f;
		const float MoveAlpha = FMath::Clamp(MovementSpeed / MaxMovementSpeed, 0.0f, 1.0f);
		SniperMoveBobWeight = FMath::FInterpTo(
			SniperMoveBobWeight, MoveAlpha, DeltaTime, 9.0f);
		if (SniperMoveBobWeight > UE_KINDA_SMALL_NUMBER)
		{
			const float StepsPerSecond = FMath::Lerp(1.25f, 2.0f, MoveAlpha);
			SniperMoveBobPhase = FMath::Fmod(
				SniperMoveBobPhase + DeltaTime * 2.0f * PI * StepsPerSecond,
				2.0f * PI);
		}
		const float BobScale = SniperMoveBobWeight * (1.0f - 0.85f * AimAlpha);
		const FVector WalkBobInCamera(
			0.0f,
			0.65f * FMath::Sin(SniperMoveBobPhase) * BobScale,
			-0.55f * (0.5f - 0.5f * FMath::Cos(2.0f * SniperMoveBobPhase))
				* BobScale);
		const FVector DesiredRearInCamera = FMath::Lerp(
			HipRearInCamera, AimedRearInCamera, SmoothRaiseAlpha)
			+ WalkBobInCamera;
		const FVector CurrentRearInCamera = Camera->GetComponentTransform()
			.InverseTransformPosition(Gun->GetSocketLocation(TEXT("SightRear")));
		const bool bActionMontagePlaying = Arms->GetAnimInstance()
			&& Arms->GetAnimInstance()->IsAnyMontagePlaying();
		if (!CurrentRearInCamera.ContainsNaN() && !bActionMontagePlaying)
		{
			const FVector CameraLocalCorrection = (DesiredRearInCamera
				- CurrentRearInCamera).GetClampedToMaxSize(40.0f);
			const FVector ParentLocalCorrection = Parent->GetComponentTransform()
				.InverseTransformVectorNoScale(Camera->GetComponentTransform()
					.TransformVectorNoScale(CameraLocalCorrection));
			WantedRelativeLocation = HipFirstPersonMeshRelativeLocation
				+ (Arms->GetRelativeLocation() + ParentLocalCorrection
					- HipFirstPersonMeshRelativeLocation).GetClampedToMaxSize(70.0f);
		}
		else
		{
			WantedRelativeLocation = Arms->GetRelativeLocation();
		}
	}
	if (CachedVisualAimWeapon.Get() != Weapon)
	{
		CachedVisualAimWeapon = Weapon;
		bHasCachedVisualAimLine = false;
	}
	bool bCheckFinalSight = false;
	FVector DesiredSightWorld = FVector::ZeroVector;
	auto VisualReferenceWorld = [Gun](const FName Socket, const FVector& MeshOffset)
	{
		return Gun->GetSocketLocation(Socket)
			+ Gun->GetComponentTransform().TransformVector(MeshOffset);
	};
	if (Camera && Parent && Gun && !Profile.RearReference.IsNone()
		&& !Profile.FrontReference.IsNone()
		&& Gun->DoesSocketExist(Profile.RearReference)
		&& Gun->DoesSocketExist(Profile.FrontReference))
	{
		// The camera and arms are siblings under a stable eye pivot. Correct only
		// the owner-side arms/attached gun; authoritative shots stay unchanged.
		const bool bArmsMontagePlaying = Arms->GetAnimInstance()
			&& Arms->GetAnimInstance()->IsAnyMontagePlaying();
		const bool bGunMontagePlaying = Gun->GetAnimInstance()
			&& Gun->GetAnimInstance()->IsAnyMontagePlaying();
		const bool bActionMontagePlaying = bArmsMontagePlaying || bGunMontagePlaying;
		const bool bPistolRig = Weapon->GetWeaponCategoryEnum()
			== EWeaponCategory::Pistol;
		if (!bActionMontagePlaying)
		{
			// Cache points in arms-local space, where moving the whole root
			// cannot feed back into the sight geometry. A fire/reload montage can
			// change both arm and gun bones; never solve from its recoil sockets.
			// The pistol Control Rig moves the hand with view pitch even at hip.
			// Follow that live pose instead of latching the pitch at ADS entry:
			// otherwise aiming before and after turning reaches different sights.
			const FTransform ArmsWorld = Arms->GetComponentTransform();
			const FTransform GunWorld = Gun->GetComponentTransform();
			const FVector RearWorld = VisualReferenceWorld(
				Profile.RearReference, Profile.RearMeshOffset);
			const FVector FrontWorld = VisualReferenceWorld(
				Profile.FrontReference, Profile.FrontMeshOffset);
			CachedVisualRearInGun = GunWorld.InverseTransformPosition(RearWorld);
			CachedVisualFrontInGun = GunWorld.InverseTransformPosition(FrontWorld);
			CachedVisualRearInArms = ArmsWorld.InverseTransformPosition(RearWorld);
			CachedVisualFrontInArms = ArmsWorld.InverseTransformPosition(FrontWorld);
			bHasCachedVisualAimLine = true;
		}
		else if (!bPistolRig && !bArmsMontagePlaying
			&& bHasCachedVisualAimLine)
		{
			// The rifle's Aim In moves the hand/weapon attachment even while
			// its gun-only fire montage runs. Follow the gun *component* with a
			// fixed local sight line; sampling the live muzzle bones instead
			// would feed every recoil impulse back into the entire arms root.
			const FTransform ArmsWorld = Arms->GetComponentTransform();
			const FTransform GunWorld = Gun->GetComponentTransform();
			CachedVisualRearInArms = ArmsWorld.InverseTransformPosition(
				GunWorld.TransformPosition(CachedVisualRearInGun));
			CachedVisualFrontInArms = ArmsWorld.InverseTransformPosition(
				GunWorld.TransformPosition(CachedVisualFrontInGun));
		}
		if (AimAlpha > UE_KINDA_SMALL_NUMBER && bActionMontagePlaying
			&& Weapon->IsAiming() && bWasIronSightReady && AimAlpha >= 0.9f)
		{
			// Once aligned, do not chase fire/reload recoil sockets with the
			// entire viewmodel. During aim-in/out the root must keep converging:
			// freezing it on the first fire frame strands an automatic rifle at
			// an intermediate and visibly incorrect angle. A pistol that is already
			// aligned also stays fixed during its fire montage, whose live hand pose
			// contains recoil rather than a clean pitch-only aim reference.
			WantedRelativeLocation = Arms->GetRelativeLocation();
			WantedRelativeRotation = Arms->GetRelativeRotation().Quaternion();
			bFirstPersonIronSightReady = bWasIronSightReady
				&& AimAlpha >= 0.9f;
		}
		else if (AimAlpha > UE_KINDA_SMALL_NUMBER && bHasCachedVisualAimLine)
		{
			// These two points define the gun's forward axis. Some RAR muzzle
			// socket rotations do NOT point along the actual barrel, so do not
			// use a socket's X axis as the aiming direction.
			const FVector RearArms = CachedVisualRearInArms;
			const FVector FrontArms = CachedVisualFrontInArms;
			const FVector ScaledLineArms = Arms->GetRelativeScale3D()
				* (FrontArms - RearArms);
			const FTransform ParentWorld = Parent->GetComponentTransform();
			const FVector HipLineParent = HipFirstPersonMeshRelativeRotation
				.RotateVector(ScaledLineArms).GetSafeNormal();
			const FVector CameraForwardParent = ParentWorld
				.InverseTransformVectorNoScale(Camera->GetForwardVector())
				.GetSafeNormal();
			if (!HipLineParent.IsNearlyZero() && !CameraForwardParent.IsNearlyZero())
			{
				FQuat Correction = FQuat::FindBetweenNormals(
					HipLineParent, CameraForwardParent);
				const float CorrectionRadians = Correction.GetAngle();
				const float LimitRadians = FMath::DegreesToRadians(
					Profile.MaxRotationCorrectionDegrees);
				if (CorrectionRadians > LimitRadians && LimitRadians > 0.0f)
				{
					Correction = FQuat::Slerp(FQuat::Identity, Correction,
						LimitRadians / CorrectionRadians).GetNormalized();
				}
				// The pistol's right-hand Control Rig already owns gun orientation.
				// Rotating the whole arms mesh is opposed by that rig's world-space
				// target and produces a springy ADS overshoot/reversal.
				const FQuat FullRotation = bPistolRig
					? HipFirstPersonMeshRelativeRotation
					: (Correction * HipFirstPersonMeshRelativeRotation).GetNormalized();
				DesiredSightWorld = Camera->GetComponentLocation()
					+ Camera->GetForwardVector() * Profile.SightDistanceCm
					+ Camera->GetUpVector() * Profile.SightHeightCm;
				const FVector DesiredSightParent = ParentWorld
					.InverseTransformPosition(DesiredSightWorld);
				const FVector FullLocation = DesiredSightParent
					- FullRotation.RotateVector(
						Arms->GetRelativeScale3D() * RearArms);
				const FVector FullOffset = (FullLocation
					- HipFirstPersonMeshRelativeLocation).GetClampedToMaxSize(65.0f);
				WantedRelativeLocation = HipFirstPersonMeshRelativeLocation
					+ FullOffset * AimAlpha;
				WantedRelativeRotation = FQuat::Slerp(
					HipFirstPersonMeshRelativeRotation, FullRotation,
					AimAlpha).GetNormalized();
				bCheckFinalSight = !bActionMontagePlaying;
			}
		}
	}

	const FVector NewRelativeLocation = FMath::VInterpTo(
		Arms->GetRelativeLocation(), WantedRelativeLocation,
		DeltaTime, 14.0f);
	const FQuat CurrentRelativeRotation = Arms->GetRelativeRotation().Quaternion();
	const FQuat NewRelativeRotation = FQuat::Slerp(
		CurrentRelativeRotation, WantedRelativeRotation,
		FMath::Clamp(DeltaTime * 14.0f, 0.0f, 1.0f)).GetNormalized();
	if (!Arms->GetRelativeLocation().Equals(NewRelativeLocation, 0.01f))
	{
		Arms->SetRelativeLocation(NewRelativeLocation);
	}
	if (CurrentRelativeRotation.AngularDistance(NewRelativeRotation)
		> FMath::DegreesToRadians(0.02f))
	{
		Arms->SetRelativeRotation(NewRelativeRotation);
	}
	if (bCheckFinalSight)
	{
		const FVector RearWorld = VisualReferenceWorld(
			Profile.RearReference, Profile.RearMeshOffset);
		const FVector FrontWorld = VisualReferenceWorld(
			Profile.FrontReference, Profile.FrontMeshOffset);
		const FVector LineWorld = (FrontWorld - RearWorld).GetSafeNormal();
		bFirstPersonIronSightReady = AimAlpha >= 0.9f
			&& FVector::DistSquared(RearWorld, DesiredSightWorld) < FMath::Square(3.0f)
			&& FVector::DotProduct(LineWorld, Camera->GetForwardVector())
				> FMath::Cos(FMath::DegreesToRadians(6.0f));
	}
}

void ATacticalCharacter::CacheStableSpectatorCameraTransform()
{
	bHasStableSpectatorCameraTransform = false;

	const UCameraComponent* Camera = GetFirstPersonCameraComponent();
	const USkeletalMeshComponent* FirstPersonViewMesh = GetFirstPersonMesh();
	if (!IsValid(Camera) || !IsValid(FirstPersonViewMesh))
	{
		return;
	}

	bool bResolvedFromReferencePose = false;
	if (Camera->GetAttachParent() == FirstPersonViewMesh)
	{
		const FName AttachName = Camera->GetAttachSocketName();
		FTransform AttachmentRelativeToMesh = FTransform::Identity;
		FName AttachmentBoneName = AttachName;

		if (AttachName != NAME_None)
		{
			if (const USkeletalMeshSocket* Socket = FirstPersonViewMesh->GetSocketByName(AttachName))
			{
				AttachmentRelativeToMesh = Socket->GetSocketLocalTransform();
				AttachmentBoneName = Socket->BoneName;
			}

			if (const USkeletalMesh* SkeletalMesh = FirstPersonViewMesh->GetSkeletalMeshAsset())
			{
				const FReferenceSkeleton& ReferenceSkeleton = SkeletalMesh->GetRefSkeleton();
				const int32 BoneIndex = ReferenceSkeleton.FindBoneIndex(AttachmentBoneName);
				if (BoneIndex != INDEX_NONE)
				{
					AttachmentRelativeToMesh = AttachmentRelativeToMesh
						* FAnimationRuntime::GetComponentSpaceTransformRefPose(ReferenceSkeleton, BoneIndex);
					bResolvedFromReferencePose = true;
				}
			}
		}
		else
		{
			bResolvedFromReferencePose = true;
		}

		if (bResolvedFromReferencePose)
		{
			// Match SceneComponent's child-relative * socket * parent transform
			// order, but use the attachment bone's reference pose instead of its
			// currently animated pose.
			StableSpectatorCameraRelativeToMesh = Camera->GetRelativeTransform()
				* AttachmentRelativeToMesh;
		}
	}

	if (!bResolvedFromReferencePose)
	{
		// Defensive fallback for an unexpected Blueprint re-parent or missing
		// skeleton. This still captures a stable transform before spectator
		// presentation starts driving weapon montages.
		StableSpectatorCameraRelativeToMesh = Camera->GetComponentTransform().GetRelativeTransform(
			FirstPersonViewMesh->GetComponentTransform());
	}

	StableSpectatorCameraRelativeToMesh.NormalizeRotation();
	bHasStableSpectatorCameraTransform = !StableSpectatorCameraRelativeToMesh.ContainsNaN();
}

float ATacticalCharacter::GetSpectatorViewPitch() const
{
	// PlayerControllers only replicate to their owning client. A listen server can
	// read a remote player's authoritative ControlRotation directly, while another
	// client must use APawn's COND_SkipOwner RemoteViewPitch16 value.
	if (const AController* CharacterController = GetController())
	{
		return FRotator::NormalizeAxis(CharacterController->GetControlRotation().Pitch);
	}

	return FRotator::NormalizeAxis(FRotator::DecompressAxisFromShort(GetRemoteViewPitch()));
}

void ATacticalCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	Super::CalcCamera(DeltaTime, OutResult);

	// UCameraComponent applies bUsePawnControlRotation only for a local player.
	// The base character updates the eye pivot from replicated view pitch for
	// remote targets; this stable cached offset also keeps the spectator POV
	// independent of any later mesh or weapon-animation presentation changes.
	const AController* CharacterController = GetController();
	if (!CharacterController || !CharacterController->IsLocalPlayerController())
	{
		if (!bHasStableSpectatorCameraTransform)
		{
			CacheStableSpectatorCameraTransform();
		}

		if (bHasStableSpectatorCameraTransform)
		{
			if (const USkeletalMeshComponent* FirstPersonViewMesh = GetFirstPersonMesh())
			{
				FTransform StableCameraWorld = StableSpectatorCameraRelativeToMesh
					* FirstPersonViewMesh->GetComponentTransform();
				StableCameraWorld.NormalizeRotation();

				OutResult.Location = StableCameraWorld.GetLocation();
				OutResult.Rotation = StableCameraWorld.Rotator();
			}
		}

		OutResult.Rotation.Pitch = GetSpectatorViewPitch();
		OutResult.Rotation.Roll = 0.0f;
		OutResult.Rotation.Normalize();

		// Super sourced this optional transform from the animated camera component.
		// Let the view state derive history from the final stable POV instead of
		// supplying a mismatched head-bone transform to motion vectors.
		OutResult.PreviousViewTransform.Reset();
	}
}

void ATacticalCharacter::OnWeaponActivated(AShooterWeapon* Weapon)
{
	Super::OnWeaponActivated(Weapon);
	if (IsDead() || !IsValid(Weapon) || Weapon->IsActorBeingDestroyed() || !WeaponInventory
		|| WeaponInventory->CurrentWeapon.Get() != Weapon)
	{
		return;
	}

	// Covers weapons activated after presentation setup as well as a retained pawn
	// reclaimed during reconnect. The helper is a no-op when attachment is current.
	EnsureFirstPersonWeaponAttachment(Weapon);
	EnsureMatchingRARWorldWeaponAnimation(Weapon);
	UpdateSniperFirstPersonGripOverlay(Weapon);
	UpdateThirdPersonGripOverlay(Weapon);

	// Super calls SetAnimInstanceClass which creates a brand-new AnimInstance
	// with default values (bIsAiming=false, AimBlendAlpha=0). Immediately push
	// the current aim state so bones don't snap to hip-fire for a frame.
	if (ATacticalWeapon* TW = Cast<ATacticalWeapon>(Weapon))
	{
		if (USkeletalMeshComponent* Mesh1P = GetFirstPersonMesh())
		{
			if (UTacticalFPAnimInstance* AnimInst = Cast<UTacticalFPAnimInstance>(Mesh1P->GetAnimInstance()))
			{
				AnimInst->bIsAiming = TW->IsAiming();
				AnimInst->AimBlendAlpha = TW->GetAimBlendAlpha();
			}
			if (UTacticalRARFPAnimInstance* RARAnim =
				Cast<UTacticalRARFPAnimInstance>(Mesh1P->GetAnimInstance()))
			{
				RARAnim->SetAimState(TW->IsAiming(), TW->GetAimBlendAlpha());
			}
		}
	}
}

void ATacticalCharacter::OnWeaponChanged_Attachment(
	AShooterWeapon* NewWeapon,
	AShooterWeapon* OldWeapon)
{
	(void)OldWeapon;
	const bool bHasUsableWeapon = !IsDead()
		&& IsValid(NewWeapon)
		&& !NewWeapon->IsActorBeingDestroyed();
	if (USkeletalMeshComponent* FirstPersonManny = GetFirstPersonMesh())
	{
		// A weapon-specific AnimBP owns the first-person pose. During a legitimate
		// replication/unarmed gap, hide only the mesh (never the attached camera)
		// instead of exposing a reference pose.
		FirstPersonManny->SetHiddenInGame(!bHasUsableWeapon, false);
		FirstPersonManny->SetVisibility(bHasUsableWeapon, false);
	}
	if (bHasUsableWeapon)
	{
		// Replication and inventory switching can publish the active weapon after
		// both actors have already begun play. Converge it onto the active socket.
		EnsureFirstPersonWeaponAttachment(NewWeapon);
	}
	UpdateSniperFirstPersonGripOverlay(bHasUsableWeapon ? NewWeapon : nullptr);
	UpdateThirdPersonGripOverlay(bHasUsableWeapon ? NewWeapon : nullptr);
}

void ATacticalCharacter::UpdateSniperFirstPersonGripOverlay(
	AShooterWeapon* ActiveWeapon)
{
	USkeletalMeshComponent* Arms = GetFirstPersonMesh();
	if (!Arms)
	{
		return;
	}

	const ATacticalWeapon* TacticalWeapon = Cast<ATacticalWeapon>(ActiveWeapon);
	USkeletalMeshComponent* Gun = TacticalWeapon
		? TacticalWeapon->GetFirstPersonMesh() : nullptr;
	const bool bUseSniperGrip = IsLocallyControlled() && !IsDead()
		&& TacticalWeapon && TacticalWeapon->GetWeaponTierEnum() == EWeaponTier::Sniper
		&& Gun && Gun->GetAttachParent() == Arms
		&& Arms->GetSkeletalMeshAsset() && Gun->DoesSocketExist(TEXT("FP_LeftHandGrip"));
	if (!bUseSniperGrip)
	{
		Arms->SetRenderInMainPass(true);
		Arms->SetRenderInDepthPass(true);
		if (SniperFirstPersonGripOverlay)
		{
			SniperFirstPersonGripOverlay->SetVisibility(false, false);
			SniperFirstPersonGripOverlay->SetHiddenInGame(true, false);
		}
		return;
	}

	if (!SniperFirstPersonGripOverlay)
	{
		USkeletalMeshComponent* Overlay = NewObject<USkeletalMeshComponent>(
			this, TEXT("SniperFirstPersonGripOverlay"));
		Overlay->SetupAttachment(Arms);
		Overlay->SetRelativeTransform(FTransform::Identity);
		Overlay->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Overlay->SetGenerateOverlapEvents(false);
		Overlay->SetCastShadow(false);
		Overlay->SetOnlyOwnerSee(true);
		Overlay->SetOwnerNoSee(false);
		Overlay->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
		Overlay->VisibilityBasedAnimTickOption =
			EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		AddInstanceComponent(Overlay);
		Overlay->RegisterComponent();
		Overlay->AddTickPrerequisiteComponent(Arms);
		SniperFirstPersonGripOverlay = Overlay;
	}

	USkeletalMeshComponent* Overlay = SniperFirstPersonGripOverlay;
	if (Overlay->GetSkeletalMeshAsset() != Arms->GetSkeletalMeshAsset())
	{
		Overlay->SetSkeletalMeshAsset(Arms->GetSkeletalMeshAsset());
	}
	// The scope viewmodel copies the complete Manny mesh. Its head can enter
	// the camera when looking sharply down even though the source mesh is not
	// rendered. Hide only this sniper-only overlay head, not the world body or
	// camera (which has already moved to the stable eye pivot).
	static const FName ViewmodelHeadBoneName(TEXT("head"));
	if (Overlay->GetBoneIndex(ViewmodelHeadBoneName) != INDEX_NONE
		&& !Overlay->IsBoneHiddenByName(ViewmodelHeadBoneName))
	{
		Overlay->HideBoneByName(ViewmodelHeadBoneName, PBO_None);
	}
	if (!Overlay->GetAnimInstance()
		|| Overlay->GetAnimClass() != UTacticalSniperFPGripAnimInstance::StaticClass())
	{
		Overlay->SetAnimInstanceClass(UTacticalSniperFPGripAnimInstance::StaticClass());
	}
	UTacticalSniperFPGripAnimInstance* GripAnim =
		Cast<UTacticalSniperFPGripAnimInstance>(Overlay->GetAnimInstance());
	if (!GripAnim || !GripAnim->Configure(Arms, Gun, TEXT("FP_LeftHandGrip"), true))
	{
		Arms->SetRenderInMainPass(true);
		Arms->SetRenderInDepthPass(true);
		Overlay->SetVisibility(false, false);
		Overlay->SetHiddenInGame(true, false);
		return;
	}

	// Keep the authored AnimBP and montages running on the source Manny. Only its
	// pixels are suppressed; the copy-pose overlay supplies the support-hand IK.
	Arms->VisibilityBasedAnimTickOption =
		EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Arms->SetRenderInMainPass(false);
	Arms->SetRenderInDepthPass(false);
	Overlay->SetRenderInMainPass(true);
	Overlay->SetRenderInDepthPass(true);
	Gun->SetRenderInMainPass(true);
	Gun->SetRenderInDepthPass(true);
	Overlay->SetHiddenInGame(false, false);
	Overlay->SetVisibility(true, false);
}

void ATacticalCharacter::SetThirdPersonGripOverlaySuppressedForSpectator(
	bool bSuppress)
{
	if (bThirdPersonGripOverlaySuppressedForSpectator == bSuppress)
	{
		return;
	}
	bThirdPersonGripOverlaySuppressedForSpectator = bSuppress;
	UpdateThirdPersonGripOverlay(
		WeaponInventory ? WeaponInventory->CurrentWeapon.Get() : nullptr);
}

void ATacticalCharacter::UpdateThirdPersonGripOverlay(
	AShooterWeapon* ActiveWeapon)
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!Body)
	{
		return;
	}

	const ATacticalWeapon* TacticalWeapon = Cast<ATacticalWeapon>(ActiveWeapon);
	USkeletalMeshComponent* Gun = TacticalWeapon
		? TacticalWeapon->GetThirdPersonMesh() : nullptr;
	const EWeaponTier Tier = TacticalWeapon
		? TacticalWeapon->GetWeaponTierEnum() : EWeaponTier::DefaultPistol;
	const bool bUseSupportHand = GetNetMode() != NM_DedicatedServer
		&& !IsLocallyControlled() && !IsDead()
		&& !bThirdPersonGripOverlaySuppressedForSpectator
		&& TacticalWeapon && (Tier == EWeaponTier::Rifle || Tier == EWeaponTier::Sniper)
		&& IsValid(Gun) && Gun->GetAttachParent() == Body
		&& Body->GetSkeletalMeshAsset()
		&& Body->DoesSocketExist(TEXT("HandGrip_L"))
		&& Gun->DoesSocketExist(TEXT("GripPoint_002"))
		&& !Body->bHiddenInGame && Body->IsVisible();
	if (!bUseSupportHand)
	{
		if (bThirdPersonGripSourceStateCached)
		{
			Body->SetRenderInMainPass(bCachedThirdPersonBodyMainPass);
			Body->SetRenderInDepthPass(bCachedThirdPersonBodyDepthPass);
			Body->SetCastShadow(bCachedThirdPersonBodyCastShadow);
			Body->VisibilityBasedAnimTickOption =
				static_cast<EVisibilityBasedAnimTickOption>(CachedThirdPersonBodyTickOption);
			bThirdPersonGripSourceStateCached = false;
		}
		if (ThirdPersonGripOverlay)
		{
			ThirdPersonGripOverlay->SetVisibility(false, false);
			ThirdPersonGripOverlay->SetHiddenInGame(true, false);
		}
		return;
	}

	if (!ThirdPersonGripOverlay)
	{
		USkeletalMeshComponent* Overlay = NewObject<USkeletalMeshComponent>(
			this, TEXT("ThirdPersonGripOverlay"));
		Overlay->SetupAttachment(Body);
		Overlay->SetRelativeTransform(FTransform::Identity);
		Overlay->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Overlay->SetGenerateOverlapEvents(false);
		Overlay->SetCastShadow(true);
		Overlay->SetOwnerNoSee(true);
		Overlay->SetOnlyOwnerSee(false);
		Overlay->SetFirstPersonPrimitiveType(
			EFirstPersonPrimitiveType::WorldSpaceRepresentation);
		Overlay->VisibilityBasedAnimTickOption =
			EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		AddInstanceComponent(Overlay);
		Overlay->RegisterComponent();
		Overlay->AddTickPrerequisiteComponent(Body);
		ThirdPersonGripOverlay = Overlay;
	}

	USkeletalMeshComponent* Overlay = ThirdPersonGripOverlay;
	if (Overlay->GetSkeletalMeshAsset() != Body->GetSkeletalMeshAsset())
	{
		Overlay->SetSkeletalMeshAsset(Body->GetSkeletalMeshAsset());
		Overlay->EmptyOverrideMaterials();
	}
	// Preserve team skins and any other per-character material overrides.
	for (int32 Index = 0; Index < Body->GetNumMaterials(); ++Index)
	{
		if (Overlay->GetMaterial(Index) != Body->GetMaterial(Index))
		{
			Overlay->SetMaterial(Index, Body->GetMaterial(Index));
		}
	}
	if (Overlay->GetOverlayMaterial() != Body->GetOverlayMaterial())
	{
		Overlay->SetOverlayMaterial(Body->GetOverlayMaterial());
	}
	Overlay->SetReceivesDecals(Body->bReceivesDecals);
	Overlay->SetRenderCustomDepth(Body->bRenderCustomDepth);
	Overlay->SetCustomDepthStencilValue(Body->CustomDepthStencilValue);
	Overlay->SetCustomDepthStencilWriteMask(Body->CustomDepthStencilWriteMask);
	if (!Overlay->GetAnimInstance()
		|| Overlay->GetAnimClass() != UTacticalSniperFPGripAnimInstance::StaticClass())
	{
		Overlay->SetAnimInstanceClass(UTacticalSniperFPGripAnimInstance::StaticClass());
	}
	UTacticalSniperFPGripAnimInstance* GripAnim =
		Cast<UTacticalSniperFPGripAnimInstance>(Overlay->GetAnimInstance());
	if (!GripAnim || !GripAnim->Configure(Body, Gun, TEXT("GripPoint_002")))
	{
		if (bThirdPersonGripSourceStateCached)
		{
			Body->SetRenderInMainPass(bCachedThirdPersonBodyMainPass);
			Body->SetRenderInDepthPass(bCachedThirdPersonBodyDepthPass);
			Body->SetCastShadow(bCachedThirdPersonBodyCastShadow);
			Body->VisibilityBasedAnimTickOption =
				static_cast<EVisibilityBasedAnimTickOption>(CachedThirdPersonBodyTickOption);
			bThirdPersonGripSourceStateCached = false;
		}
		Overlay->SetVisibility(false, false);
		Overlay->SetHiddenInGame(true, false);
		return;
	}

	if (!bThirdPersonGripSourceStateCached)
	{
		bCachedThirdPersonBodyMainPass = Body->bRenderInMainPass;
		bCachedThirdPersonBodyDepthPass = Body->bRenderInDepthPass;
		bCachedThirdPersonBodyCastShadow = Body->CastShadow;
		CachedThirdPersonBodyTickOption =
			static_cast<uint8>(Body->VisibilityBasedAnimTickOption);
		bThirdPersonGripSourceStateCached = true;
	}
	// ABP_TP_Rifle and its montages remain on the source body. Only the final
	// render pose moves to the copy, which solves the left palm after that graph.
	Body->VisibilityBasedAnimTickOption =
		EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Body->SetRenderInMainPass(false);
	Body->SetRenderInDepthPass(false);
	Body->SetCastShadow(false);
	Overlay->SetRenderInMainPass(bCachedThirdPersonBodyMainPass);
	Overlay->SetRenderInDepthPass(bCachedThirdPersonBodyDepthPass);
	Overlay->SetCastShadow(bCachedThirdPersonBodyCastShadow);
	Overlay->SetHiddenInGame(false, false);
	Overlay->SetVisibility(true, false);
}

// ---- TakeDamage with headshot detection ----

float ATacticalCharacter::TakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// Damage is server-authoritative and valid only during active combat. This is the
	// final backstop against clients firing RPCs during PreRound/Buy/RoundEnd.
	if (HasAuthority())
	{
		const ATacticalGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATacticalGameState>() : nullptr;
		if (!GS || GS->CurrentPhase != ERoundPhase::Combat)
		{
			return 0.0f;
		}
	}

	if (!HealthComponent || HealthComponent->IsDead())
	{
		return 0.0f;
	}

	// The cage is a gameplay boundary, not just a visual/collision mesh. Keep a
	// server-side final check here so a damage source that bypasses the normal
	// shot trace cannot damage a player on the other side of the arena.
	if (HasAuthority())
	{
		if (const ATacticalGuardArena* Arena = ATacticalGuardArena::FindActive(GetWorld()))
		{
			FVector Source = FVector::ZeroVector;
			bool bHasSource = false;
			if (const AShooterProjectile* Projectile = Cast<AShooterProjectile>(DamageCauser))
			{
				Source = Projectile->GetActorLocation();
				bHasSource = true;
			}
			else if (const AShooterWeapon* Weapon = Cast<AShooterWeapon>(DamageCauser))
			{
				Source = Weapon->GetGameplayMuzzleLocation();
				bHasSource = true;
			}
			else if (IsValid(DamageCauser))
			{
				Source = DamageCauser->GetActorLocation();
				bHasSource = true;
			}
			else if (IsValid(EventInstigator) && IsValid(EventInstigator->GetPawn()))
			{
				Source = EventInstigator->GetPawn()->GetActorLocation();
				bHasSource = true;
			}
			if (bHasSource && Arena->DoesSegmentCrossBarrier(Source, GetActorLocation()))
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
		if (const AShooterProjectile* Proj = Cast<AShooterProjectile>(DamageCauser))
		{
			Multiplier = Proj->WeaponHeadshotMultiplier;
		}
		else if (const ATacticalWeapon* TacWeapon = Cast<ATacticalWeapon>(DamageCauser))
		{
			Multiplier = TacWeapon->GetHeadshotMultiplier();
		}
		FinalDamage *= Multiplier;
	}

	const float HealthBefore = HealthComponent->CurrentHP;
	const float ArmorBefore = ArmorComponent ? ArmorComponent->CurrentDurability : 0.0f;
	PendingDamageArmorAbsorbed = 0.0f;
	PendingDamageCauser = DamageCauser;
	const float AppliedDamage = HealthComponent->TakeDamage(FinalDamage, EventInstigator, bHeadshot);
	PendingDamageCauser = nullptr;
	// On lethal damage OnDeath clears remaining equipment synchronously. Do not
	// mistake that cleanup for armor absorbed by the bullet itself.
	const float ResolvedArmorAfter = HealthComponent->IsDead()
		? FMath::Max(0.0f, ArmorBefore - PendingDamageArmorAbsorbed)
		: ArmorComponent ? ArmorComponent->CurrentDurability : 0.0f;
	const uint8 EffectFlags = TacticalBodyHitRules::Classify(HealthBefore, HealthComponent->CurrentHP,
		ArmorBefore, ResolvedArmorAfter, bHeadshot,
		HasAuthority() && DamageEvent.IsOfType(FPointDamageEvent::ClassID)
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
	NotifyOwningPlayerOfDamage(DamageEvent, EventInstigator, DamageCauser, AppliedDamage);
	return AppliedDamage;
}

void ATacticalCharacter::MulticastResolvedBodyHit_Implementation(const uint8 EffectFlags,
	const FVector_NetQuantize ImpactPoint, const FVector_NetQuantizeNormal ImpactNormal,
	const FName BoneName)
{
	ATacticalBodyHitEffect::SpawnForVictim(this, EffectFlags, ImpactPoint, ImpactNormal, BoneName);
}

// ---- Armor via HealthComponent callback ----

float ATacticalCharacter::ProcessArmorDamage(float IncomingDamage, bool bHeadshot, float& OutAbsorbed)
{
	if (!ArmorComponent || !ArmorComponent->HasArmor())
	{
		return IncomingDamage;
	}
	const float RemainingDamage = ArmorComponent->ProcessIncomingProjectileDamage(IncomingDamage, bHeadshot, PendingDamageCauser, OutAbsorbed);
	PendingDamageArmorAbsorbed += OutAbsorbed;
	if (HasAuthority())
	{
		if (ATacticalPlayerState* TPS = GetPlayerState<ATacticalPlayerState>())
		{
			TPS->SetSavedArmor(ArmorComponent->ArmorType, ArmorComponent->CurrentDurability);
		}
	}
	return RemainingDamage;
}

// ---- Death (weapon drops + delegate to super) ----

void ATacticalCharacter::OnDeath()
{
	if (HasAuthority() && !bAuthoritativeDeathHandled)
	{
		bAuthoritativeDeathHandled = true;
		ATacticalPlayerState* TPS = GetPlayerState<ATacticalPlayerState>();
		const EWeaponTier PrimaryBefore = TPS ? TPS->SavedPrimaryTier : PrimaryTier;
		const EWeaponTier PistolBefore = TPS ? TPS->SavedPistolTier : PistolTier;
		const EArmorType ArmorBefore = TPS ? TPS->ArmorType : EArmorType::None;
		const float ArmorDurabilityBefore = TPS ? TPS->ArmorDurability : 0.0f;
		EWeaponTier TierToDrop = EWeaponTier::DefaultPistol;
		const bool bHasDroppableWeapon = GetMostValuableDroppableWeaponTier(TierToDrop);
		if (bHasDroppableWeapon)
		{
			DropWeapon(TierToDrop);
		}

		if (NearbyTreasureChest)
		{
			NearbyTreasureChest->CancelClaim(this);
		}
		if (bTreasureClaimActive)
		{
			SetTreasureClaimActive(false);
		}
		if (TPS)
		{
			TPS->ClearEquipmentOnDeath();
		}
		if (ArmorComponent)
		{
			ArmorComponent->BreakArmor();
		}
		UE_LOG(LogTacticalFlow, Display,
			TEXT("Event=EquipmentClearedOnDeath Player=\"%s\" Team=%s Primary=%s->%s Pistol=%s->%s Armor=%s(%.1f)->None Dropped=%s"),
			TPS ? *TPS->GetPlayerName() : *GetName(),
			TacticalDiagnostics::GetTeamName(TPS ? TPS->TeamByte : 255),
			TacticalDiagnostics::GetWeaponTierName(PrimaryBefore),
			TacticalDiagnostics::GetWeaponTierName(TPS ? TPS->SavedPrimaryTier : EWeaponTier::DefaultPistol),
			TacticalDiagnostics::GetWeaponTierName(PistolBefore),
			TacticalDiagnostics::GetWeaponTierName(TPS ? TPS->SavedPistolTier : EWeaponTier::DefaultPistol),
			TacticalDiagnostics::GetArmorTypeName(ArmorBefore), ArmorDurabilityBefore,
			bHasDroppableWeapon ? TacticalDiagnostics::GetWeaponTierName(TierToDrop) : TEXT("None"));

		// Fire GameState event once so GameMode handles economy and elimination.
		if (ATacticalGameState* TGS = Cast<ATacticalGameState>(GetWorld()->GetGameState()))
		{
			TGS->OnPlayerKilled.Broadcast(
				HealthComponent ? HealthComponent->LastDamageInstigator : nullptr,
				GetController());
		}
	}

	ApplyDeathPresentation();
}

void ATacticalCharacter::ApplyDeathPresentation()
{
	if (bDeathPresentationApplied)
	{
		return;
	}
	bDeathPresentationApplied = true;
	if (HealthComponent && !HealthComponent->DeathTag.IsNone())
	{
		Tags.AddUnique(HealthComponent->DeathTag);
	}

	// Tactical uses round-based respawn. Keep the dead pawn in place so team
	// elimination and avatar death state remain valid until the next round.
	GetWorld()->GetTimerManager().ClearTimer(RespawnTimer);

	if (WeaponInventory)
	{
		WeaponInventory->bCanFire = false;
		WeaponInventory->CancelLocalFirePrediction();
		if (IsValid(WeaponInventory->CurrentWeapon.Get())
			&& !WeaponInventory->CurrentWeapon->IsActorBeingDestroyed())
		{
			WeaponInventory->CurrentWeapon->DeactivateWeapon();
		}
		WeaponInventory->OnAmmoUpdated.Broadcast(0, 0, 0);
	}
	OnAimFOVChanged(false, 0.0f);
	bFirstPersonIronSightReady = false;
	// Restore the real body before ragdoll takes ownership of its bone physics.
	// The copy-pose support-hand mesh must never render a second frozen corpse.
	UpdateThirdPersonGripOverlay(nullptr);

	GetCharacterMovement()->StopMovementImmediately();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	EnterDeathRagdoll();
	DisableInput(nullptr);

	if (ATacticalPlayerController* TacticalPC = Cast<ATacticalPlayerController>(GetController()))
	{
		TacticalPC->HandleControlledPawnDeath(this);
	}

	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=DeathPresentationApplied Pawn=\"%s\" NetMode=%s Local=%d Health=%.1f Dead=%d"),
		*GetName(), TacticalDiagnostics::GetNetModeName(GetNetMode()), IsLocallyControlled(),
		HealthComponent ? HealthComponent->CurrentHP : 0.0f,
		HealthComponent ? HealthComponent->IsDead() : false);
}

void ATacticalCharacter::EnterDeathRagdoll()
{
	if (USkeletalMeshComponent* Mesh1P = GetFirstPersonMesh())
	{
		Mesh1P->SetHiddenInGame(true, true);
		Mesh1P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	USkeletalMeshComponent* Mesh3P = GetMesh();
	if (!Mesh3P)
	{
		return;
	}

	Mesh3P->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	Mesh3P->SetCollisionProfileName(TEXT("Ragdoll"));
	Mesh3P->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh3P->SetAllBodiesSimulatePhysics(true);
	Mesh3P->SetSimulatePhysics(true);
	Mesh3P->WakeAllRigidBodies();
	Mesh3P->bBlendPhysics = true;
}

bool ATacticalCharacter::GetMostValuableDroppableWeaponTier(EWeaponTier& OutTier) const
{
	if (!WeaponInventory)
	{
		return false;
	}

	int32 BestCost = 0;
	EWeaponTier BestTier = EWeaponTier::DefaultPistol;

	constexpr int32 CategoriesToCheck[] =
	{
		static_cast<int32>(EWeaponCategory::Primary),
		static_cast<int32>(EWeaponCategory::Pistol)
	};

	for (const int32 Category : CategoriesToCheck)
	{
		const int32 EquippedTier = WeaponInventory->GetEquippedTier(Category);
		if (EquippedTier < 0 || EquippedTier == static_cast<int32>(EWeaponTier::DefaultPistol))
		{
			continue;
		}

		const EWeaponTier Tier = static_cast<EWeaponTier>(EquippedTier);
		const int32 Cost = ATacticalPlayerState::GetWeaponCost(Tier);
		if (Cost > BestCost)
		{
			BestCost = Cost;
			BestTier = Tier;
		}
	}

	if (BestCost <= 0)
	{
		return false;
	}

	OutTier = BestTier;
	return true;
}
// ---- Weapon slot enforcement ----

void ATacticalCharacter::AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass)
{
	TryAddWeaponClass(WeaponClass);
}

bool ATacticalCharacter::TryAddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass)
{
	if (!HasAuthority() || !WeaponClass || !WeaponInventory)
	{
		return false;
	}

	const ATacticalWeapon* DefaultWeapon = Cast<ATacticalWeapon>(GetDefault<AShooterWeapon>(WeaponClass));
	if (!DefaultWeapon)
	{
		return WeaponInventory->AddWeapon(WeaponClass) != nullptr;
	}

	return TryAddWeaponForTier(WeaponClass, DefaultWeapon->GetWeaponTierEnum());
}

bool ATacticalCharacter::TryAddWeaponForTier(
	const TSubclassOf<AShooterWeapon>& WeaponClass,
	EWeaponTier Tier)
{
	if (!HasAuthority() || !WeaponClass || !WeaponInventory)
	{
		return false;
	}

	if (!Cast<ATacticalWeapon>(GetDefault<AShooterWeapon>(WeaponClass)))
	{
		return false;
	}

	const EWeaponCategory Category =
		(Tier == EWeaponTier::Rifle || Tier == EWeaponTier::Sniper)
			? EWeaponCategory::Primary
			: EWeaponCategory::Pistol;
	if (!HasFreeSlot(Category))
	{
		return false;
	}
	return TryReplaceWeaponInSlot(WeaponClass, Tier, false);
}

bool ATacticalCharacter::TryReplaceWeaponInSlot(
	const TSubclassOf<AShooterWeapon>& WeaponClass,
	EWeaponTier Tier,
	bool bDropReplacedWeapon)
{
	if (!HasAuthority() || !WeaponClass || !WeaponInventory)
	{
		return false;
	}

	const ATacticalWeapon* DefaultWeapon = Cast<ATacticalWeapon>(GetDefault<AShooterWeapon>(WeaponClass));
	if (!DefaultWeapon)
	{
		return false;
	}

	if (WeaponInventory->FindWeaponByTier(static_cast<int32>(Tier)))
	{
		return false;
	}

	const EWeaponCategory Category =
		(Tier == EWeaponTier::Rifle || Tier == EWeaponTier::Sniper)
			? EWeaponCategory::Primary
			: EWeaponCategory::Pistol;
	AShooterWeapon* ExistingSlotWeapon = nullptr;
	for (AShooterWeapon* OwnedWeapon : WeaponInventory->OwnedWeapons)
	{
		if (OwnedWeapon && OwnedWeapon->GetWeaponCategory() == static_cast<int32>(Category))
		{
			ExistingSlotWeapon = OwnedWeapon;
			break;
		}
	}
	const EWeaponTier OldTier = ExistingSlotWeapon
		? static_cast<EWeaponTier>(ExistingSlotWeapon->GetWeaponTier())
		: EWeaponTier::DefaultPistol;

	// Spawn first. If construction fails, the existing slot and saved loadout are
	// left untouched, making purchase/pickup replacement transactional.
	AShooterWeapon* NewWeapon = WeaponInventory->AddWeaponForTier(WeaponClass, Tier);
	if (!NewWeapon)
	{
		return false;
	}

	if (ExistingSlotWeapon)
	{
		if (bDropReplacedWeapon)
		{
			DropWeapon(OldTier);
		}
		else if (AShooterWeapon* ReplacedWeapon = WeaponInventory->RemoveWeaponByTier(static_cast<int32>(OldTier)))
		{
			if (ReplacedWeapon != NewWeapon)
			{
				RetireWeaponAfterReplication(ReplacedWeapon);
				ForceNetUpdate();
			}
		}
	}

	if (Category == EWeaponCategory::Primary)
	{
		PrimaryTier = Tier;
	}
	else
	{
		PistolTier = Tier;
	}

	SaveCurrentLoadoutToPlayerState();
	return true;
}

bool ATacticalCharacter::EquipPurchasedWeapon(
	const TSubclassOf<AShooterWeapon>& WeaponClass,
	EWeaponTier Tier)
{
	return TryReplaceWeaponInSlot(WeaponClass, Tier, false);
}

void ATacticalCharacter::PlayFiringMontage(UAnimMontage* Montage)
{
	AShooterWeapon* Weapon = WeaponInventory
		? WeaponInventory->CurrentWeapon.Get() : nullptr;
	PlayPresentationMontage(GetFirstPersonMesh(), Weapon
		? ResolveFirstPersonFireMontage(GetFirstPersonMesh(), Weapon,
			bFirstPersonIronSightReady) : Montage);
	PlayPresentationMontage(GetMesh(), Weapon
		? Weapon->GetThirdPersonFiringMontage() : Montage);
	PlayPresentationMontage(Weapon ? Weapon->GetFirstPersonMesh() : nullptr,
		Weapon ? Weapon->GetFirstPersonWeaponFiringMontage() : nullptr);
	PlayMatchingRARWorldWeaponMontage(Weapon, Weapon
		? Weapon->GetFirstPersonWeaponFiringMontage() : nullptr);
}

bool ATacticalCharacter::PlayPredictedFirstPersonFire(AShooterWeapon* Weapon)
{
	if (!IsLocallyControlled() || IsDead() || bFrozen || !WeaponInventory
		|| !IsValid(Weapon) || Weapon->IsActorBeingDestroyed()
		|| WeaponInventory->CurrentWeapon.Get() != Weapon)
	{
		return false;
	}

	const bool bArmsStarted = PlayPresentationMontage(
		GetFirstPersonMesh(),
		ResolveFirstPersonFireMontage(GetFirstPersonMesh(), Weapon,
			bFirstPersonIronSightReady));
	const bool bGunStarted = PlayPresentationMontage(
		Weapon->GetFirstPersonMesh(),
		Weapon->GetFirstPersonWeaponFiringMontage());
	return bArmsStarted || bGunStarted;
}

void ATacticalCharacter::PlayReloadMontage(UAnimMontage* Montage)
{
	AShooterWeapon* Weapon = WeaponInventory
		? WeaponInventory->CurrentWeapon.Get() : nullptr;
	UAnimMontage* ArmsMontage = Weapon
		? ResolveFirstPersonReloadMontage(GetFirstPersonMesh(), Weapon) : Montage;
	const bool bUsesRARAimedReload = GetFirstPersonMesh()
		&& Cast<UTacticalRARFPAnimInstance>(
			GetFirstPersonMesh()->GetAnimInstance())
		&& Weapon && ArmsMontage != Weapon->GetFirstPersonReloadMontage();
	// Pistol and non-RAR rifles keep the existing hip reload; the RAR graph can
	// retain ADS because its authored aimed reload shares the gun's action slot.
	if (ATacticalWeapon* TacticalWeapon = Cast<ATacticalWeapon>(Weapon);
		TacticalWeapon && TacticalWeapon->IsAiming()
		&& !bUsesRARAimedReload
		&& (IsLocallyControlled() || HasAuthority()))
	{
		TacticalWeapon->StopAiming();
	}
	PlayPresentationMontage(GetFirstPersonMesh(), ArmsMontage);
	PlayPresentationMontage(GetMesh(), Weapon
		? Weapon->GetThirdPersonReloadMontage() : Montage);
	PlayPresentationMontage(Weapon ? Weapon->GetFirstPersonMesh() : nullptr,
		Weapon ? Weapon->GetFirstPersonWeaponReloadMontage() : nullptr);
	PlayMatchingRARWorldWeaponMontage(Weapon, Weapon
		? Weapon->GetFirstPersonWeaponReloadMontage() : nullptr);
}

// ---- Freeze ----

void ATacticalCharacter::SetFrozen(bool bInFrozen)
{
	if (!HasAuthority())
	{
		return;
	}

	ApplyFrozenLocally(bInFrozen);
}

void ATacticalCharacter::ApplyFrozenLocally(bool bInFrozen)
{
	bFrozen = bInFrozen;
	if (bFrozen)
	{
		bLocalTreasureInteractHeld = false;
		if (IsLocallyControlled() || HasAuthority())
		{
			DoStopAiming();
		}
	}
	if (!bGameplayGatesInitialized)
	{
		return;
	}

	RefreshWeaponFireGate();
	if (bFrozen)
	{
		CachedMoveInput = FVector2D::ZeroVector;
		GetCharacterMovement()->StopMovementImmediately();
		GetCharacterMovement()->DisableMovement();
	}
	else if (!IsDead() && GetCharacterMovement()->MovementMode == MOVE_None)
	{
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}
}

void ATacticalCharacter::PrepareForDisconnectGrace()
{
	if (!HasAuthority())
	{
		return;
	}

	CachedMoveInput = FVector2D::ZeroVector;
	StopJumping();
	GetCharacterMovement()->StopMovementImmediately();
	if (WeaponInventory)
	{
		WeaponInventory->StopFiring();
	}
	DoStopAiming();
	DoStopHoldBreath();
	DoStopInteract();
}

void ATacticalCharacter::SetTreasureClaimActive(bool bActive)
{
	if (!HasAuthority())
	{
		return;
	}

	bTreasureClaimActive = bActive;
	if (!bActive)
	{
		bLocalTreasureInteractHeld = false;
	}
	RefreshWeaponFireGate();
	ForceNetUpdate();
	if (!IsLocallyControlled())
	{
		ClientSetTreasureClaimActive(bActive);
	}
}

void ATacticalCharacter::ClientSetTreasureClaimActive_Implementation(bool bActive)
{
	bTreasureClaimActive = bActive;
	if (!bActive)
	{
		bLocalTreasureInteractHeld = false;
	}
	RefreshWeaponFireGate();
}

void ATacticalCharacter::RefreshWeaponFireGate()
{
	if (!bGameplayGatesInitialized || !WeaponInventory)
	{
		return;
	}

	const bool bAllowFire = !bFrozen && !IsDead() && !IsTreasureClaimBlockingFire();
	WeaponInventory->bCanFire = bAllowFire;
	if (!bAllowFire)
	{
		WeaponInventory->StopFiring();
	}
}

// ---- Queries ----

bool ATacticalCharacter::CanAfford(int32 Cost) const
{
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (const ATacticalPlayerState* TPS = PC->GetPlayerState<ATacticalPlayerState>())
		{
			return TPS->Money >= Cost;
		}
	}
	return false;
}

bool ATacticalCharacter::HasFreeSlot(EWeaponCategory Category) const
{
	return WeaponInventory ? WeaponInventory->HasFreeSlot(static_cast<int32>(Category)) : false;
}

// ---- Drop ----

void ATacticalCharacter::DropWeapon(EWeaponTier Tier)
{
	if (!HasAuthority() || !WeaponInventory)
	{
		return;
	}

	AShooterWeapon* WeaponToDrop = WeaponInventory->RemoveWeaponByTier(static_cast<int32>(Tier));
	if (!WeaponToDrop)
	{
		return;
	}

	FVector DropLocation = GetActorLocation();
	FHitResult GroundHit;
	const FVector TraceStart = GetActorLocation() + FVector(0.0f, 0.0f, 100.0f);
	const FVector TraceEnd = GetActorLocation() - FVector(0.0f, 0.0f, 500.0f);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TacticalWeaponDropGroundTrace), false, this);
	if (GetWorld()->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
	{
		DropLocation = GroundHit.ImpactPoint + FVector(0.0f, 0.0f, 4.0f);
	}

	const FTransform DropTransform(GetActorRotation(), DropLocation);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	if (AWeaponDropPickup* Pickup = GetWorld()->SpawnActor<AWeaponDropPickup>(AWeaponDropPickup::StaticClass(), DropTransform, SpawnParams))
	{
		Pickup->InitializeFromWeapon(WeaponToDrop->GetClass(), Tier);
	}

	RetireWeaponAfterReplication(WeaponToDrop);
	ForceNetUpdate();

	if (PrimaryTier == Tier)
	{
		PrimaryTier = EWeaponTier::DefaultPistol;
	}
	if (PistolTier == Tier)
	{
		PistolTier = EWeaponTier::DefaultPistol;
	}

	SaveCurrentLoadoutToPlayerState();
}

void ATacticalCharacter::SaveCurrentLoadoutToPlayerState() const
{
	if (!HasAuthority())
	{
		return;
	}

	if (ATacticalPlayerState* TPS = GetPlayerState<ATacticalPlayerState>())
	{
		TPS->SetSavedLoadout(PrimaryTier, PistolTier);
	}
}

// ---- Input gated by freeze ----

void ATacticalCharacter::DoStartFiring()
{
	if (!bFrozen && !IsTreasureClaimBlockingFire())
	{
		Super::DoStartFiring();
	}
}
void ATacticalCharacter::DoMove(float R, float F)
{
	if (!bFrozen)
	{
		CachedMoveInput = FVector2D(R, F).GetClampedToMaxSize(1.0f);
	}
}

void ATacticalCharacter::CaptureMoveInput(const FInputActionValue& Value)
{
	if (!bFrozen)
	{
		CachedMoveInput = Value.Get<FVector2D>().GetClampedToMaxSize(1.0f);
	}
}

void ATacticalCharacter::ClearMoveInput(const FInputActionValue& Value)
{
	CachedMoveInput = FVector2D::ZeroVector;
}
void ATacticalCharacter::DoJumpStart()            { if (!bFrozen) Super::DoJumpStart(); }
void ATacticalCharacter::DoJumpEnd()              { if (!bFrozen) Super::DoJumpEnd(); }
void ATacticalCharacter::DoAim(float Y, float P)   { if (!bFrozen) Super::DoAim(Y, P); }

// ---- Scope / Zoom ----

void ATacticalCharacter::DoStartAiming()
{
	if (bFrozen || !WeaponInventory || !IsValid(WeaponInventory->CurrentWeapon.Get())
		|| WeaponInventory->CurrentWeapon->IsActorBeingDestroyed())
	{
		return;
	}

	if (ATacticalWeapon* TW = Cast<ATacticalWeapon>(WeaponInventory->CurrentWeapon.Get()))
	{
		// Ensure FOV delegate is bound (weapon switch may not have fired OnWeaponChanged)
		TW->OnAimStateChanged.RemoveDynamic(this, &ATacticalCharacter::OnAimFOVChanged);
		TW->OnAimStateChanged.AddDynamic(this, &ATacticalCharacter::OnAimFOVChanged);

		// A hip-fire arms montage can still be playing when ADS begins. It sits
		// above RAR's aim transition and leaves the gun offset to the side until
		// that montage ends. Fade only that arms clip; the gun's firing montage
		// and the authoritative shot continue uninterrupted.
		if (USkeletalMeshComponent* Arms = GetFirstPersonMesh())
		{
			if (UTacticalRARFPAnimInstance* RARAnim =
				Cast<UTacticalRARFPAnimInstance>(Arms->GetAnimInstance()))
			{
				if (UAnimMontage* HipFireMontage = TW->GetFirstPersonFiringMontage();
					HipFireMontage && RARAnim->Montage_IsPlaying(HipFireMontage))
				{
					RARAnim->Montage_Stop(0.05f, HipFireMontage);
				}
			}
		}
		TW->StartAiming();
	}
}

void ATacticalCharacter::DoStopAiming()
{
	if (!WeaponInventory || !IsValid(WeaponInventory->CurrentWeapon.Get())
		|| WeaponInventory->CurrentWeapon->IsActorBeingDestroyed())
	{
		return;
	}

	if (ATacticalWeapon* TW = Cast<ATacticalWeapon>(WeaponInventory->CurrentWeapon.Get()))
	{
		TW->OnAimStateChanged.RemoveDynamic(this, &ATacticalCharacter::OnAimFOVChanged);
		TW->OnAimStateChanged.AddDynamic(this, &ATacticalCharacter::OnAimFOVChanged);
		TW->StopAiming();
	}
}

void ATacticalCharacter::DoCycleZoom()
{
	if (bFrozen || !WeaponInventory || !IsValid(WeaponInventory->CurrentWeapon.Get())
		|| WeaponInventory->CurrentWeapon->IsActorBeingDestroyed())
	{
		return;
	}

	if (ATacticalWeapon* TW = Cast<ATacticalWeapon>(WeaponInventory->CurrentWeapon.Get()))
	{
		TW->CycleZoom();
	}
}

void ATacticalCharacter::DoStartHoldBreath()
{
	if (bFrozen || !WeaponInventory || !IsValid(WeaponInventory->CurrentWeapon.Get())
		|| WeaponInventory->CurrentWeapon->IsActorBeingDestroyed())
	{
		return;
	}

	if (ATacticalWeapon* TW = Cast<ATacticalWeapon>(WeaponInventory->CurrentWeapon.Get()))
	{
		TW->StartHoldBreath();
	}
}

void ATacticalCharacter::DoStopHoldBreath()
{
	if (!WeaponInventory || !IsValid(WeaponInventory->CurrentWeapon.Get())
		|| WeaponInventory->CurrentWeapon->IsActorBeingDestroyed())
	{
		return;
	}

	if (ATacticalWeapon* TW = Cast<ATacticalWeapon>(WeaponInventory->CurrentWeapon.Get()))
	{
		TW->StopHoldBreath();
	}
}

// ---- Pickup / Interact ----

void ATacticalCharacter::OfferPickupSwap(AShooterPickup* Pickup)
{
	if (PendingPickup == Pickup)
	{
		return; // Already pending on this pickup
	}

	PendingPickup = Pickup;

	// Only show the UI prompt on the owning client.
	// On the server we still store PendingPickup for authority,
	// but the BP event is a client-side HUD concern.
	if (IsLocallyControlled())
	{
		BP_OnPickupOfferChanged(true);
	}
	else if (HasAuthority())
	{
		ClientSetPickupOffer(Pickup, true);
	}
}

void ATacticalCharacter::ClearPendingPickup(AShooterPickup* Pickup)
{
	if (PendingPickup == Pickup)
	{
		PendingPickup = nullptr;
		if (IsLocallyControlled())
		{
			BP_OnPickupOfferChanged(false);
		}
		else if (HasAuthority())
		{
			ClientSetPickupOffer(Pickup, false);
		}
	}
}

void ATacticalCharacter::ClientSetPickupOffer_Implementation(AShooterPickup* Pickup, bool bHasOffer)
{
	if (bHasOffer && Pickup)
	{
		PendingPickup = Pickup;
		BP_OnPickupOfferChanged(true);
	}
	else if (PendingPickup == Pickup)
	{
		PendingPickup = nullptr;
		BP_OnPickupOfferChanged(false);
	}
}

void ATacticalCharacter::SetNearbyTreasureChest(ATreasureChest* Chest)
{
	NearbyTreasureChest = Chest;
	if (IsLocallyControlled() && Chest)
	{
		BP_OnTreasureOfferChanged(true, Chest->GetClaimDuration());
	}
}

void ATacticalCharacter::ClearNearbyTreasureChest(ATreasureChest* Chest)
{
	if (NearbyTreasureChest == Chest)
	{
		NearbyTreasureChest = nullptr;
		bLocalTreasureInteractHeld = false;
		RefreshWeaponFireGate();
		if (IsLocallyControlled())
		{
			BP_OnTreasureOfferChanged(false, 0.0f);
		}
	}
}

void ATacticalCharacter::DoInteract()
{
	if (bFrozen || IsDead())
	{
		return;
	}

	if (NearbyTreasureChest)
	{
		bLocalTreasureInteractHeld = true;
		RefreshWeaponFireGate();
		Super::DoStopFiring();
		DoStopAiming();
		DoStopHoldBreath();
		if (HasAuthority())
		{
			NearbyTreasureChest->BeginClaim(this);
		}
		else
		{
			ServerBeginTreasureClaim(NearbyTreasureChest);
		}
		return;
	}

	if (!PendingPickup || !WeaponInventory)
	{
		return;
	}

	if (!HasAuthority())
	{
		// Client: hide the prompt immediately for responsiveness,
		// then ask the server to perform the swap.
		PendingPickup = nullptr;
		BP_OnPickupOfferChanged(false);
		ServerDoInteract();
		return;
	}

	// ── Server-authoritative swap below ──

	AShooterPickup* Pickup = PendingPickup;
	PendingPickup = nullptr;
	// Hide UI only if this character is locally controlled (listen-server host).
	// Remote clients already hid their own UI before calling the RPC.
	if (IsLocallyControlled())
	{
		BP_OnPickupOfferChanged(false);
	}

	TSubclassOf<AShooterWeapon> WeaponClass;
	EWeaponTier WeaponTier = EWeaponTier::DefaultPistol;

	if (AWeaponDropPickup* DropPickup = Cast<AWeaponDropPickup>(Pickup))
	{
		WeaponClass = DropPickup->GetDroppedWeaponClass();
		WeaponTier = DropPickup->GetDroppedTier();
	}
	else
	{
		WeaponClass = Pickup->GetWeaponClass();
		if (const ATacticalWeapon* DefaultWeapon = WeaponClass
			? Cast<ATacticalWeapon>(WeaponClass->GetDefaultObject())
			: nullptr)
		{
			WeaponTier = DefaultWeapon->GetWeaponTierEnum();
		}
	}

	if (!TryReplaceWeaponInSlot(WeaponClass, WeaponTier, true))
	{
		// Keep both the existing weapon and pickup intact when spawn/configuration
		// fails, and restore the interaction prompt for another attempt.
		OfferPickupSwap(Pickup);
		return;
	}

	if (AWeaponDropPickup* DropPickup = Cast<AWeaponDropPickup>(Pickup))
	{
		DropPickup->Destroy();
	}
	else
	{
		Pickup->ConsumePickup();
	}
}

void ATacticalCharacter::ServerDoInteract_Implementation()
{
	DoInteract();
}

void ATacticalCharacter::DoStopInteract()
{
	bLocalTreasureInteractHeld = false;
	RefreshWeaponFireGate();
	if (!NearbyTreasureChest)
	{
		return;
	}

	if (HasAuthority())
	{
		NearbyTreasureChest->CancelClaim(this);
	}
	else
	{
		ServerCancelTreasureClaim(NearbyTreasureChest);
	}
}

void ATacticalCharacter::ServerBeginTreasureClaim_Implementation(ATreasureChest* Chest)
{
	if (Chest)
	{
		Chest->BeginClaim(this);
	}
}

void ATacticalCharacter::ServerCancelTreasureClaim_Implementation(ATreasureChest* Chest)
{
	if (Chest)
	{
		Chest->CancelClaim(this);
	}
}

void ATacticalCharacter::OnWeaponChanged_Scope(AShooterWeapon* NewWeapon, AShooterWeapon* OldWeapon)
{
	bFirstPersonIronSightReady = false;
	CachedVisualAimWeapon.Reset();
	bHasCachedVisualAimLine = false;
	if (bHasHipFirstPersonMeshRelativeLocation)
	{
		if (USkeletalMeshComponent* Arms = GetFirstPersonMesh())
		{
			Arms->SetRelativeLocation(HipFirstPersonMeshRelativeLocation);
			Arms->SetRelativeRotation(HipFirstPersonMeshRelativeRotation);
		}
	}
	// Before unbinding the old weapon, stop aiming on it to reset FOV.
	// Otherwise the FOV set by the old weapon's delegate is never cleared
	// and gets stuck at the ADS value (e.g. 55).
	if (ATacticalWeapon* OldTW = IsValid(OldWeapon) && !OldWeapon->IsActorBeingDestroyed()
		? Cast<ATacticalWeapon>(OldWeapon)
		: nullptr)
	{
		// A remote simulated proxy does not own the weapon actor and must never
		// issue its ServerSetAiming RPC. Reset that proxy's camera locally instead.
		if (HasAuthority() || IsLocallyControlled())
		{
			OldTW->StopAiming();
		}
		else
		{
			OnAimFOVChanged(false, 0.0f);
		}
		OldTW->ResetLocalAimBlend();
		OldTW->OnAimStateChanged.RemoveDynamic(this, &ATacticalCharacter::OnAimFOVChanged);
	}

	// Bind new weapon scope delegates
	if (ATacticalWeapon* NewTW = IsValid(NewWeapon) && !NewWeapon->IsActorBeingDestroyed()
		? Cast<ATacticalWeapon>(NewWeapon)
		: nullptr)
	{
		NewTW->OnAimStateChanged.RemoveDynamic(this, &ATacticalCharacter::OnAimFOVChanged);
		NewTW->OnAimStateChanged.AddDynamic(this, &ATacticalCharacter::OnAimFOVChanged);

		// OnWeaponChanged already called SetAnimInstanceClass which created a
		// fresh AnimInstance. Push the new weapon's state immediately so bones
		// don't render at defaults for a frame.
		if (USkeletalMeshComponent* Mesh1P = GetFirstPersonMesh())
		{
			if (UTacticalFPAnimInstance* AnimInst = Cast<UTacticalFPAnimInstance>(Mesh1P->GetAnimInstance()))
			{
				AnimInst->bIsAiming = NewTW->IsAiming();
				AnimInst->AimBlendAlpha = NewTW->GetAimBlendAlpha();
			}
			if (UTacticalRARFPAnimInstance* RARAnim =
				Cast<UTacticalRARFPAnimInstance>(Mesh1P->GetAnimInstance()))
			{
				RARAnim->SetAimState(NewTW->IsAiming(), NewTW->GetAimBlendAlpha());
			}
		}
	}
	else
	{
		OnAimFOVChanged(false, 0.0f);
	}
}

void ATacticalCharacter::OnAimFOVChanged(bool bAiming, float FOV)
{
	if (bAiming && FOV > 0.0f)
	{
		TargetFOV = FOV;
		TargetFirstPersonFOV = DefaultFirstPersonFOV;
	}
	else
	{
		TargetFOV = DefaultFOV;
		TargetFirstPersonFOV = DefaultFirstPersonFOV;
	}
}

// ---- Replication ----

void ATacticalCharacter::OnRep_Frozen()
{
	ApplyFrozenLocally(bFrozen);
}

void ATacticalCharacter::OnRep_TreasureClaimActive()
{
	RefreshWeaponFireGate();
}

void ATacticalCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATacticalCharacter, bFrozen);
	DOREPLIFETIME_CONDITION(ATacticalCharacter, bTreasureClaimActive, COND_OwnerOnly);
}
