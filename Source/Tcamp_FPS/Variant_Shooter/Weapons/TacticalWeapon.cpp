// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapons/TacticalWeapon.h"
#include "Weapons/ShooterWeaponMeshComponent.h"
#include "ShooterProjectile.h"
#include "TacticalGameState.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "../Animation/TacticalFPAnimInstance.h"
#include "Tcamp_FPSCharacter.h"
#include "GameFramework/Pawn.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Tcamp_FPS.h"
#include "UObject/ConstructorHelpers.h"

ATacticalWeapon::ATacticalWeapon()
{
	// Constructor references are serialized on the native CDO, so these repair
	// assets remain reachable by the cooker in packaged builds. Runtime string
	// loading alone would not provide that guarantee.
	static ConstructorHelpers::FObjectFinder<UAnimMontage> PistolFireMontageAsset(
		TEXT("/Game/Characters/Mannequins/Anims/Pistol/MM_Pistol_Fire_Montage.MM_Pistol_Fire_Montage"));
	if (PistolFireMontageAsset.Succeeded())
	{
		PistolFireMontageFallback = PistolFireMontageAsset.Object;
	}

	static ConstructorHelpers::FClassFinder<UAnimInstance> OneHandedThirdPersonAnimClass(
		TEXT("/Game/Variant_Shooter/Anims/ABP_TP_Pistol.ABP_TP_Pistol_C"));
	if (OneHandedThirdPersonAnimClass.Succeeded())
	{
		OneHandedThirdPersonAnimFallback = OneHandedThirdPersonAnimClass.Class;
	}

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> SniperMeshAsset(
		TEXT("/Game/Tactical/Weapons/Sniper/SKM_TacticalSniper.SKM_TacticalSniper"));
	if (SniperMeshAsset.Succeeded())
	{
		SniperPresentationMesh = SniperMeshAsset.Object;
	}
	static ConstructorHelpers::FObjectFinder<UAnimMontage> SniperReloadAsset(
		TEXT("/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Reload_Montage.MM_Rifle_Reload_Montage"));
	if (SniperReloadAsset.Succeeded())
	{
		SniperCharacterReloadMontage = SniperReloadAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> MagazineAsset(
		TEXT("/Game/InfimaGames/RealisticAssaultRifle/Art/Weapon/Meshes/SM_RAR_AssaultRifle_Magazine_Default_Full.SM_RAR_AssaultRifle_Magazine_Default_Full"));
	RARDefaultMagazineMesh = MagazineAsset.Object;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ForestockAsset(
		TEXT("/Game/InfimaGames/RealisticAssaultRifle/Art/Weapon/Meshes/SM_RAR_AssaultRifle_Forestock_Default.SM_RAR_AssaultRifle_Forestock_Default"));
	RARDefaultForestockMesh = ForestockAsset.Object;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FrontSightAsset(
		TEXT("/Game/InfimaGames/RealisticAssaultRifle/Art/Weapon/Meshes/SM_RAR_AssaultRifle_IronSight_F.SM_RAR_AssaultRifle_IronSight_F"));
	RARDefaultFrontSightMesh = FrontSightAsset.Object;
	static ConstructorHelpers::FObjectFinder<UAnimSequence> RARAimedReloadAsset(
		TEXT("/Game/InfimaGames/RealisticAssaultRifle/Art/Character/Animations/Actions/A_RAR_FP_PCH_AssaultRifle_Reload_Aimed.A_RAR_FP_PCH_AssaultRifle_Reload_Aimed"));
	RARAimedReloadSequence = RARAimedReloadAsset.Object;
}

float ATacticalWeapon::GetAdditionalReloadPresentationDuration() const
{
	return WeaponTier == EWeaponTier::Rifle && bIsAiming && RARAimedReloadSequence
		? RARAimedReloadSequence->GetPlayLength() : 0.0f;
}

void ATacticalWeapon::SetRARFirstPersonPartsOnlyOwnerSee(bool bOnlyOwnerSee)
{
	for (UStaticMeshComponent* Part : RARFirstPersonParts)
	{
		if (IsValid(Part))
		{
			Part->SetOnlyOwnerSee(bOnlyOwnerSee);
		}
	}
}

void ATacticalWeapon::InitializeWeaponTier(EWeaponTier InWeaponTier)
{
	if (!HasAuthority())
	{
		return;
	}

	WeaponTier = InWeaponTier;
	CacheWeaponDataFromDataTable();
	ForceNetUpdate();
}

bool ATacticalWeapon::TryGetFirstPersonLeftHandGripTransform(
	FTransform& OutWeaponSpaceTransform) const
{
	// The weapon socket describes where the palm contacts the gun. Its authored
	// rotation is as important as its location; a foregrip location alone leaves
	// the hand floating beside the mesh.
	const FName PalmSocket = GetFirstPersonGripProfile().LeftPalmSocket;
	const USkeletalMeshComponent* Mesh1P = GetFirstPersonMesh();
	if (Mesh1P && !PalmSocket.IsNone() && Mesh1P->DoesSocketExist(PalmSocket))
	{
		OutWeaponSpaceTransform = Mesh1P->GetSocketTransform(
			PalmSocket, RTS_Component);
		OutWeaponSpaceTransform.SetScale3D(FVector::OneVector);
		OutWeaponSpaceTransform.NormalizeRotation();
		return !OutWeaponSpaceTransform.ContainsNaN();
	}

	// Existing non-tactical weapon skins can still supply an explicit palm target.
	return Super::TryGetFirstPersonLeftHandGripTransform(OutWeaponSpaceTransform);
}

FFirstPersonGripProfile ATacticalWeapon::GetFirstPersonGripProfile() const
{
	if (bOverrideFirstPersonGripProfile)
	{
		return FirstPersonGripProfile;
	}

	FFirstPersonGripProfile Profile;
	if (WeaponCategory == EWeaponCategory::Pistol)
	{
		Profile.WristRotationAlpha = 0.65f;
		Profile.MaxWristCorrectionDegrees = 35.0f;
		Profile.ElbowOutwardCm = 15.0f;
		Profile.ElbowDownCm = 85.0f;
		Profile.ElbowPoleBiasAlpha = 0.7f;
	}
	else
	{
		Profile.WristRotationAlpha = 0.85f;
		Profile.MaxWristCorrectionDegrees = 45.0f;
		Profile.ElbowOutwardCm = 12.0f;
		Profile.ElbowDownCm = 80.0f;
		Profile.ElbowPoleBiasAlpha = 0.7f;
	}
	return Profile;
}

FTacticalVisualAimProfile ATacticalWeapon::GetFirstPersonVisualAimProfile() const
{
	FTacticalVisualAimProfile Profile;
	// The sniper pack has no matching first-person ADS arm sequence. Its native
	// scope view takes over at full aim, so never drag the entire Manny mesh
	// toward a bare gun optic and expose the shoulder through the camera.
	if (WeaponTier == EWeaponTier::Rifle)
	{
		// SOCKET_Scope is a bone around component Z=7.3 cm, whereas Muzzle is
		// around Z=0. The front iron sight's top is around Z=11.7 cm. Keep
		// the virtual line just above that tip, not equal offsets from these
		// unequal reference heights (which would pitch the gun roughly 10 deg).
		Profile.RearReference = TEXT("SOCKET_Scope");
		Profile.FrontReference = TEXT("Muzzle");
		Profile.RearMeshOffset = FVector(0.0f, 0.0f, 4.9f);
		Profile.FrontMeshOffset = FVector(0.0f, 0.0f, 12.2f);
		Profile.SightDistanceCm = 40.0f;
		Profile.MaxRotationCorrectionDegrees = 45.0f;
	}
	else if (WeaponTier == EWeaponTier::DefaultPistol
		|| WeaponTier == EWeaponTier::UpgradedPistol)
	{
		// On the shipped pistol the Barrel reference bone and Muzzle are about
		// 12 cm apart along the barrel. Raise the virtual line slightly above
		// the slide so its top does not cover the reticle while blending in.
		Profile.RearReference = TEXT("Barrel");
		Profile.FrontReference = TEXT("Muzzle");
		Profile.RearMeshOffset = FVector(0.0f, 0.0f, 4.5f);
		Profile.FrontMeshOffset = Profile.RearMeshOffset;
		Profile.SightDistanceCm = 44.0f;
		Profile.MaxRotationCorrectionDegrees = 50.0f;
	}
	return Profile;
}

void ATacticalWeapon::ApplySniperPresentation()
{
	if (WeaponTier != EWeaponTier::Sniper || !SniperPresentationMesh)
	{
		return;
	}

	// The Sniper economy row still uses the template launcher class. Replace only
	// that tier's presentation, preserving its server-owned ammo and ballistics.
	FirstPersonWeaponMeshOverride = SniperPresentationMesh;
	ThirdPersonWeaponMeshOverride = SniperPresentationMesh;
	// The imported gun points down its local +X; the template launcher points
	// down +Y. Keep both views aligned to the same authored right-hand socket.
	const FQuat SniperToHolder = FRotator(0.0f, 90.0f, 0.0f).Quaternion();
	// The template launcher's hand socket sits above the imported sniper's
	// pistol grip. Bring the grip back under Manny's right palm; the camera-
	// relative optic solver preserves the established on-screen hip position.
	FirstPersonAttachmentOffset = FTransform(
		FRotator(4.0f, 100.0f, 0.0f).Quaternion(),
		FVector(-2.7f, 15.5f, 5.2f), FVector::OneVector);
	ThirdPersonAttachmentOffset = FTransform(
		SniperToHolder, FVector(0.0f, 20.0f, 0.0f), FVector::OneVector);
	bUseFirstPersonLeftHandGrip = true;
	FirstPersonLeftHandGripSocketName = TEXT("FP_LeftHandGrip");
	MuzzleSocketName = TEXT("Muzzle");
	// Launcher gun-body graphs/montages target another skeleton. The character's
	// existing fire/reload montages remain available, but the gun stays in its
	// safe reference pose until a matching sniper gun-body graph is authored.
	FirstPersonWeaponAnimInstanceClass = nullptr;
	FirstPersonWeaponFiringMontage = nullptr;
	FirstPersonWeaponReloadMontage = nullptr;
	FirstPersonReloadMontage = SniperCharacterReloadMontage;
	ThirdPersonReloadMontage = SniperCharacterReloadMontage;
	ApplyPresentationMeshOverrides();
	if (USkeletalMeshComponent* Gun1P = GetFirstPersonMesh())
	{
		Gun1P->SetAnimInstanceClass(nullptr);
	}
	if (USkeletalMeshComponent* Gun3P = GetThirdPersonMesh())
	{
		Gun3P->SetAnimInstanceClass(nullptr);
	}
}

void ATacticalWeapon::BeginPlay()
{
	// Existing weapon Blueprints may still carry the old bHasScope=false default.
	// These three economy tiers now have ordinary iron sights; preserve every
	// explicitly authored zoom level and leave sniper/other weapon tiers alone.
	const bool bPistolIronSights = WeaponTier == EWeaponTier::DefaultPistol
		|| WeaponTier == EWeaponTier::UpgradedPistol;
	const bool bRifleIronSights = WeaponTier == EWeaponTier::Rifle;
	if (bPistolIronSights || bRifleIronSights)
	{
		bHasScope = true;
		if (ZoomLevels.IsEmpty())
		{
			ZoomLevels.Add(bPistolIronSights ? 75.0f : 60.0f);
		}
	}

	// Do this before AShooterWeapon::BeginPlay attaches the meshes and installs
	// their animation classes on the owning character.
	ApplySniperPresentation();
	ApplyPresentationMeshOverrides();
	SanitizePresentationConfiguration();
	Super::BeginPlay();
	if (bUseRARDefaultFirstPersonParts && GetFirstPersonMesh())
	{
		auto AddPart = [this](USkeletalMeshComponent* Gun,
			TArray<TObjectPtr<UStaticMeshComponent>>& Parts,
			const bool bFirstPerson, UStaticMesh* Mesh,
			const FName Socket, const FTransform& RelativeTransform)
		{
			if (!Gun || !Mesh || (!Socket.IsNone() && !Gun->DoesSocketExist(Socket)))
			{
				return;
			}
			UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
			Part->SetStaticMesh(Mesh);
			Part->SetCollisionProfileName(TEXT("NoCollision"));
			// Match the gun body in each view. Mixing projections creates a
			// see-through seam where the hand touches the foregrip.
			Part->SetFirstPersonPrimitiveType(bFirstPerson
				? EFirstPersonPrimitiveType::FirstPerson
				: EFirstPersonPrimitiveType::WorldSpaceRepresentation);
			Part->SetOnlyOwnerSee(bFirstPerson);
			Part->SetOwnerNoSee(!bFirstPerson);
			Part->SetCastShadow(!bFirstPerson);
			Part->SetupAttachment(Gun, Socket);
			Part->RegisterComponent();
			Part->SetRelativeTransform(RelativeTransform);
			Parts.Add(Part);
		};
		auto AddDefaultParts = [this, &AddPart](USkeletalMeshComponent* Gun,
			TArray<TObjectPtr<UStaticMeshComponent>>& Parts, const bool bFirstPerson)
		{
			AddPart(Gun, Parts, bFirstPerson, RARDefaultMagazineMesh,
				TEXT("SOCKET_Magazine"), FTransform::Identity);
			AddPart(Gun, Parts, bFirstPerson, RARDefaultForestockMesh,
				TEXT("SOCKET_Forestock"), FTransform::Identity);
			AddPart(Gun, Parts, bFirstPerson, RARDefaultFrontSightMesh,
				NAME_None, FTransform(FQuat::Identity,
					FVector(0.0f, 44.092873f, 7.492528f)));
		};
		AddDefaultParts(GetFirstPersonMesh(), RARFirstPersonParts, true);
		// The third-person component must use the same RAR skeletal mesh so
		// these authored sockets and offsets describe the identical gun.
		USkeletalMeshComponent* Gun3P = GetThirdPersonMesh();
		if (Gun3P && Gun3P->GetSkeletalMeshAsset()
			== GetFirstPersonMesh()->GetSkeletalMeshAsset())
		{
			AddDefaultParts(Gun3P, RARThirdPersonParts, false);
		}
	}
	CacheWeaponDataFromDataTable();
	BreathMeter = BreathDuration;
	NotifyAmmoChanged();
}

void ATacticalWeapon::SanitizePresentationConfiguration()
{
	// The pistol Blueprint inherited the template rifle firing montage. Besides
	// looking wrong, that montage drives the arms through a rifle pose and can
	// visibly twist the pistol hand chain. Replace only that known bad pairing so
	// intentionally authored custom pistol montages remain untouched.
	if (WeaponCategory == EWeaponCategory::Pistol
		&& FiringMontage
		&& FiringMontage->GetPathName().Contains(TEXT("FP_Rifle_Shoot_Montage")))
	{
		if (PistolFireMontageFallback)
		{
			FiringMontage = PistolFireMontageFallback;
			UE_LOG(LogTcamp_FPS, Verbose,
				TEXT("%s: replaced the inherited rifle montage with the pistol firing montage."),
				*GetNameSafe(this));
		}
		else
		{
			UE_LOG(LogTcamp_FPS, Warning,
				TEXT("%s: failed to load the pistol firing montage; keeping '%s'."),
				*GetNameSafe(this), *GetNameSafe(FiringMontage));
		}
	}

	// ABP_TP_Rifle reads GripPoint_002, then incorrectly adds Forward * -35,
	// Right * 59 and Up * 8 before feeding the result to its world-space left-hand
	// IK. The rifle socket is already authored on the real foregrip, so compensate
	// only that legacy query without moving the rendered weapon. A weapon without
	// the required socket still uses the safe one-handed locomotion graph.
	static const FName RifleLeftHandSocket(TEXT("GripPoint_002"));
	static const FVector LegacyRifleIKOffset(-35.0f, 59.0f, 8.0f);
	USkeletalMeshComponent* Mesh3P = GetThirdPersonMesh();
	UClass* CurrentThirdPersonClass = ThirdPersonAnimInstanceClass.Get();
	const bool bUsesLegacyRifleGraph = CurrentThirdPersonClass
		&& CurrentThirdPersonClass->GetPathName().Contains(TEXT("ABP_TP_Rifle"));
	if (bUsesLegacyRifleGraph && Mesh3P && Mesh3P->DoesSocketExist(RifleLeftHandSocket))
	{
		if (UShooterWeaponMeshComponent* CorrectableMesh =
			Cast<UShooterWeaponMeshComponent>(Mesh3P))
		{
			CorrectableMesh->SetSocketLocationQueryCompensation(
				RifleLeftHandSocket, LegacyRifleIKOffset);
			UE_LOG(LogTcamp_FPS, Verbose,
				TEXT("%s: corrected the legacy rifle left-hand IK socket query."),
				*GetNameSafe(this));
		}
	}
	else if (bUsesLegacyRifleGraph && Mesh3P)
	{
		if (OneHandedThirdPersonAnimFallback)
		{
			ThirdPersonAnimInstanceClass = OneHandedThirdPersonAnimFallback;
			UE_LOG(LogTcamp_FPS, Log,
				TEXT("%s: weapon mesh has no GripPoint_002; selected the safe third-person armed pose."),
				*GetNameSafe(this));
		}
		else
		{
			UE_LOG(LogTcamp_FPS, Warning,
				TEXT("%s: weapon mesh has no GripPoint_002 and the safe third-person AnimBP could not be loaded."),
				*GetNameSafe(this));
		}
	}
}

void ATacticalWeapon::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorld()->GetTimerManager().ClearTimer(BreathTimer);
	Super::EndPlay(EndPlayReason);
}

void ATacticalWeapon::AdvanceLocalAimBlend(float DeltaTime, bool bShouldAim)
{
	const float Target = bShouldAim ? 1.0f : 0.0f;
	// The RAR rifle now uses this same continuous alpha for both its base pose
	// and owner-view root. Give the aim-out blend enough time to read as an
	// animation instead of dropping most of the pose in just a few frames.
	const float BlendSpeed = WeaponTier == EWeaponTier::Sniper
		|| (!bShouldAim && WeaponTier == EWeaponTier::Rifle)
		? FMath::Min(AimBlendSpeed, 8.0f) : AimBlendSpeed;
	AimBlendAlpha = FMath::FInterpTo(AimBlendAlpha, Target, DeltaTime, BlendSpeed);
}

void ATacticalWeapon::PushAimStateToAnimInstance()
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	if (ATcamp_FPSCharacter* Char = Cast<ATcamp_FPSCharacter>(OwnerPawn))
	{
		if (USkeletalMeshComponent* Mesh1P = Char->GetFirstPersonMesh())
		{
			if (UTacticalFPAnimInstance* AnimInst = Cast<UTacticalFPAnimInstance>(Mesh1P->GetAnimInstance()))
			{
				AnimInst->bIsAiming = bIsAiming;
				AnimInst->AimBlendAlpha = AimBlendAlpha;
			}
		}
	}
}

float ATacticalWeapon::GetEffectiveAimVariance() const
{
	float EffectiveVariance = bIsAiming ? AimVariance * AdsSpreadMultiplier : AimVariance;
	if (bIsAiming && bIsHoldingBreath && !bBreathDepleted)
	{
		EffectiveVariance *= HoldBreathSpreadMultiplier;
	}
	return EffectiveVariance;
}

bool ATacticalWeapon::HasInfiniteReserveAmmo() const
{
	// Reload and ammo transfer are authoritative, so this phase lookup cannot be
	// spoofed by the owning client. ReserveBullets remains untouched in the lobby;
	// the magazine still empties normally and the full reload montage still plays.
	const ATacticalGameState* TacticalGameState = GetWorld()
		? GetWorld()->GetGameState<ATacticalGameState>()
		: nullptr;
	return HasAuthority()
		&& TacticalGameState
		&& TacticalGameState->CurrentPhase == ERoundPhase::PreRound;
}

// ---- DataTable ----

void ATacticalWeapon::CacheWeaponDataFromDataTable()
{
	UDataTable* Table = WeaponDataTable.LoadSynchronous();
	if (!Table)
	{
		return;
	}

	static const FString ContextStr(TEXT("TacticalWeapon::CacheWeaponData"));
	for (FName RowName : Table->GetRowNames())
	{
		if (FWeaponTierRow* Row = Table->FindRow<FWeaponTierRow>(RowName, ContextStr))
		{
			if (Row->Tier == WeaponTier)
			{
				WeaponCategory = Row->Category;
				CachedBodyDamage = Row->BodyDamage;
				CachedHeadshotMultiplier = Row->HeadshotMultiplier;
				MagazineSize = Row->MagazineSize;
				MaxReserveBullets = Row->MaxReserveAmmo;
				RefireRate = Row->RefireRate;
				if (Row->ProjectileClass)
				{
					ProjectileClass = Row->ProjectileClass;
				}
				if (HasAuthority())
				{
					// Runtime ammo is server-owned. A reconnecting client may have
					// already received the retained values before this data-table
					// cache is rebuilt, so never replace them with row defaults here.
					CurrentBullets = MagazineSize;
					ReserveBullets = FMath::Clamp(
						Row->ReserveAmmo, 0, MaxReserveBullets);
				}
				return;
			}
		}
	}
}

// ---- Fire (DataTable-driven) ----

void ATacticalWeapon::ConfigureProjectile(AShooterProjectile* Projectile)
{
	if (!Projectile)
	{
		return;
	}

	Projectile->HitDamage = CachedBodyDamage;
	Projectile->WeaponHeadshotMultiplier = CachedHeadshotMultiplier;
	Projectile->MaxPenetrations = PenetrationCount;
	Projectile->RemainingPenetrations = PenetrationCount;
	Projectile->PenetrationDamageFalloff = PenetrationFalloff;
}

void ATacticalWeapon::ConfigureHitscanParams(float& OutDamage, int32& OutPenetrations, float& OutFalloff)
{
	OutDamage = CachedBodyDamage;
	OutPenetrations = PenetrationCount;
	OutFalloff = PenetrationFalloff;
}

// ---- Scope / Zoom ----

void ATacticalWeapon::StartAiming()
{
	if (!bHasScope)
	{
		return;
	}
	if (!HasAuthority())
	{
		ServerSetAiming(true);
	}
	if (bIsAiming)
	{
		return;
	}

	bIsAiming = true;
	CurrentZoomIndex = 0;
	ApplyScopeFOV();
	PushAimStateToAnimInstance();
}

void ATacticalWeapon::StopAiming()
{
	if (!HasAuthority())
	{
		ServerSetAiming(false);
	}
	if (!bIsAiming)
	{
		return;
	}

	bIsAiming = false;
	StopHoldBreath();

	OnAimStateChanged.Broadcast(false, 0.0f);
	PushAimStateToAnimInstance();
}

void ATacticalWeapon::CycleZoom()
{
	if (!bIsAiming || ZoomLevels.Num() <= 1)
	{
		return;
	}

	CurrentZoomIndex = (CurrentZoomIndex + 1) % ZoomLevels.Num();
	ApplyScopeFOV();
}

float ATacticalWeapon::GetCurrentAimFOV() const
{
	if (!bIsAiming)
	{
		return 0.0f;
	}
	return GetConfiguredAimFOV();
}

float ATacticalWeapon::GetConfiguredAimFOV() const
{
	if (ZoomLevels.Num() == 0)
	{
		return 0.0f;
	}
	return ZoomLevels[FMath::Min(CurrentZoomIndex, ZoomLevels.Num() - 1)];
}

void ATacticalWeapon::ApplyScopeFOV()
{
	const float FOV = GetCurrentAimFOV();
	OnAimStateChanged.Broadcast(true, FOV);
}

// ---- Hold Breath ----

void ATacticalWeapon::StartHoldBreath()
{
	if (!bHasBreathControl || !bIsAiming || bBreathDepleted)
	{
		return;
	}
	if (!HasAuthority())
	{
		ServerSetHoldingBreath(true);
	}

	bIsHoldingBreath = true;

	if (!GetWorld()->GetTimerManager().IsTimerActive(BreathTimer))
	{
		GetWorld()->GetTimerManager().SetTimer(BreathTimer, this, &ATacticalWeapon::BreathTick, BreathTickInterval, true);
	}
}

void ATacticalWeapon::StopHoldBreath()
{
	if (!HasAuthority())
	{
		ServerSetHoldingBreath(false);
	}
	bIsHoldingBreath = false;
}

float ATacticalWeapon::GetBreathPercent() const
{
	return BreathDuration > 0.0f ? BreathMeter / BreathDuration : 0.0f;
}

void ATacticalWeapon::BreathTick()
{
	if (bIsHoldingBreath && !bBreathDepleted)
	{
		BreathMeter = FMath::Max(BreathMeter - BreathTickInterval, 0.0f);

		if (BreathMeter <= 0.0f)
		{
			bBreathDepleted = true;
			bIsHoldingBreath = false;
		}
	}
	else
	{
		BreathMeter = FMath::Min(BreathMeter + (BreathTickInterval * BreathDuration / BreathRecoveryTime), BreathDuration);

		if (BreathMeter >= BreathDuration)
		{
			bBreathDepleted = false;
			GetWorld()->GetTimerManager().ClearTimer(BreathTimer);
		}
	}

	OnBreathChanged.Broadcast(GetBreathPercent());
}

void ATacticalWeapon::ServerSetAiming_Implementation(bool bNewAiming)
{
	if (bNewAiming)
	{
		StartAiming();
	}
	else
	{
		StopAiming();
	}
}

void ATacticalWeapon::ServerSetHoldingBreath_Implementation(bool bNewHoldingBreath)
{
	if (bNewHoldingBreath)
	{
		StartHoldBreath();
	}
	else
	{
		StopHoldBreath();
	}
}

void ATacticalWeapon::OnRep_AimingState()
{
	if (!bIsAiming)
	{
		bIsHoldingBreath = false;
		OnAimStateChanged.Broadcast(false, 0.0f);
	}
	else
	{
		ApplyScopeFOV();
	}
	PushAimStateToAnimInstance();
}

void ATacticalWeapon::OnRep_WeaponTier()
{
	// Weapon classes are shared by multiple economy tiers (for example the
	// default and upgraded pistols). Rebuild all tier-derived runtime values
	// when the authoritative tier reaches a client.
	ApplySniperPresentation();
	if (WeaponOwner && WeaponTier == EWeaponTier::Sniper)
	{
		WeaponOwner->AttachWeaponMeshes(this);
	}
	CacheWeaponDataFromDataTable();
	NotifyAmmoChanged();
}

void ATacticalWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATacticalWeapon, WeaponTier);
	DOREPLIFETIME(ATacticalWeapon, bIsAiming);
	DOREPLIFETIME(ATacticalWeapon, bIsHoldingBreath);
}
