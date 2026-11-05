// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/WeaponInventoryComponent.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Variant_Shooter/Weapons/ShooterWeaponHolder.h"
#include "Variant_Shooter/Weapons/TacticalWeapon.h"
#include "Variant_Shooter/TacticalCharacter.h"
#include "Variant_Shooter/TacticalGameState.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

namespace
{
	constexpr double PredictedFireEchoLifetimeSeconds = 5.0;
	constexpr int32 MaxPendingPredictedFireEchoes = 64;
	constexpr float LocalReloadNetworkMarginSeconds = 0.15f;
	constexpr double LocalSwitchConfirmationTimeoutSeconds = 3.0;
}

UWeaponInventoryComponent::UWeaponInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UWeaponInventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	// Subobject and actor references may resolve before or after BeginPlay. Keep
	// only usable state locally and begin watching the current actor immediately.
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		OwnedWeapons.RemoveAll([](const AShooterWeapon* Weapon)
		{
			return !IsValid(Weapon);
		});
	}
	ClearInvalidCurrentWeapon();
	CachedCurrentWeapon = CurrentWeapon;
	TrackCurrentWeaponLifetime(CurrentWeapon.Get());
}

AShooterWeapon* UWeaponInventoryComponent::AddWeapon(TSubclassOf<AShooterWeapon> WeaponClass)
{
	if (!GetOwner() || !WeaponClass || !GetOwner()->HasAuthority())
	{
		return nullptr;
	}

	// Check for duplicates
	if (AShooterWeapon* Existing = FindWeaponOfClass(WeaponClass))
	{
		SetCurrentWeapon(Existing);
		return Existing;
	}

	// Enforce capacity
	if (!bUseSlotSystem && OwnedWeapons.Num() >= MaxWeapons)
	{
		return nullptr;
	}

	// Spawn the weapon
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = Cast<APawn>(GetOwner());
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot;

	AShooterWeapon* NewWeapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponClass, GetOwner()->GetActorTransform(), SpawnParams);
	if (!NewWeapon)
	{
		return nullptr;
	}

	OwnedWeapons.Add(NewWeapon);
	SetCurrentWeapon(NewWeapon);
	return NewWeapon;
}

AShooterWeapon* UWeaponInventoryComponent::AddWeaponForTier(
	TSubclassOf<AShooterWeapon> WeaponClass,
	EWeaponTier Tier)
{
	AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!OwnerActor || !World || !WeaponClass || !OwnerActor->HasAuthority())
	{
		return nullptr;
	}

	if (!Cast<ATacticalWeapon>(WeaponClass->GetDefaultObject()))
	{
		return nullptr;
	}

	// Tier identity, rather than class identity, is the duplicate key for the
	// tactical inventory. Default/Upgraded Pistol and Rifle/Sniper may share a
	// visual Blueprint class.
	if (AShooterWeapon* Existing = FindWeaponByTier(static_cast<int32>(Tier)))
	{
		SetCurrentWeapon(Existing);
		return Existing;
	}

	if (!bUseSlotSystem && OwnedWeapons.Num() >= MaxWeapons)
	{
		return nullptr;
	}

	const FTransform SpawnTransform = OwnerActor->GetActorTransform();
	AShooterWeapon* NewWeapon = World->SpawnActorDeferred<AShooterWeapon>(
		WeaponClass,
		SpawnTransform,
		OwnerActor,
		Cast<APawn>(OwnerActor),
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!NewWeapon)
	{
		return nullptr;
	}

	ATacticalWeapon* TacticalWeapon = CastChecked<ATacticalWeapon>(NewWeapon);
	TacticalWeapon->InitializeWeaponTier(Tier);
	NewWeapon->FinishSpawning(SpawnTransform);

	OwnedWeapons.Add(NewWeapon);
	SetCurrentWeapon(NewWeapon);
	return NewWeapon;
}

void UWeaponInventoryComponent::SwitchWeapon()
{
	if (!GetOwner())
	{
		return;
	}

	if (!GetOwner()->HasAuthority())
	{
		if (IsAutonomousLocalOwner() && GetWorld()
			&& IsLocalSwitchPending(GetWorld()->GetTimeSeconds()))
		{
			return;
		}
		if (IsAutonomousLocalOwner() && OwnedWeapons.Num() > 1)
		{
			StopLocalFirePrediction();
			bLocalSwitchPending = true;
			LocalSwitchBlockUntil = GetWorld()->GetTimeSeconds()
				+ LocalSwitchConfirmationTimeoutSeconds;
		}
		ServerSwitchWeapon();
		return;
	}

	OwnedWeapons.RemoveAll([](const AShooterWeapon* Weapon)
	{
		return !IsValid(Weapon) || Weapon->IsActorBeingDestroyed();
	});
	if (OwnedWeapons.Num() <= 1)
	{
		return;
	}

	int32 Index = OwnedWeapons.Find(CurrentWeapon);
	Index = (Index == OwnedWeapons.Num() - 1) ? 0 : Index + 1;
	SetCurrentWeapon(OwnedWeapons[Index]);
}

void UWeaponInventoryComponent::SwitchToCategory(int32 Category)
{
	if (!GetOwner())
	{
		return;
	}

	if (!GetOwner()->HasAuthority())
	{
		if (IsAutonomousLocalOwner() && GetWorld()
			&& IsLocalSwitchPending(GetWorld()->GetTimeSeconds()))
		{
			return;
		}
		if (IsAutonomousLocalOwner())
		{
			for (AShooterWeapon* Weapon : OwnedWeapons)
			{
				if (IsValid(Weapon) && !Weapon->IsActorBeingDestroyed()
					&& Weapon->GetWeaponCategory() == Category
					&& Weapon != CurrentWeapon.Get())
				{
					StopLocalFirePrediction();
					bLocalSwitchPending = true;
					LocalSwitchBlockUntil = GetWorld()->GetTimeSeconds()
						+ LocalSwitchConfirmationTimeoutSeconds;
					break;
				}
			}
		}
		ServerSwitchToCategory(Category);
		return;
	}

	for (AShooterWeapon* Weapon : OwnedWeapons)
	{
		if (IsValid(Weapon) && !Weapon->IsActorBeingDestroyed()
			&& Weapon->GetWeaponCategory() == Category && Weapon != CurrentWeapon)
		{
			SetCurrentWeapon(Weapon);
			return;
		}
	}
}

void UWeaponInventoryComponent::StartFiring()
{
	if (!GetOwner())
	{
		return;
	}

	if (!GetOwner()->HasAuthority())
	{
		if (!IsAutonomousLocalOwner() || bLocalTriggerHeld
			|| !bCanFire || !IsValid(CurrentWeapon.Get())
			|| CurrentWeapon->IsActorBeingDestroyed() || !GetWorld()
			|| GetWorld()->GetTimeSeconds() < LocalReloadBlockUntil
			|| IsLocalSwitchPending(GetWorld()->GetTimeSeconds()))
		{
			return;
		}
		AShooterWeapon* RequestedWeapon = CurrentWeapon.Get();
		++NextPredictionBurstId;
		if (NextPredictionBurstId == 0)
		{
			++NextPredictionBurstId;
		}
		ActivePredictionBurstId = NextPredictionBurstId;
		NextPredictionShotIndex = 0;
		LocalAttemptedShotsInBurst = 0;
		LocalPredictedWeapon = CurrentWeapon.Get();
		LocalAmmoAtBurstStart = CurrentWeapon->GetBulletCount();
		bLocalTriggerHeld = true;
		TryPlayNextPredictedFire();
		// TryPlayNextPredictedFire may cancel its local timer (for example when
		// replicated ammo is still zero), but this input still needs its own ID.
		ServerStartFiring(RequestedWeapon, NextPredictionBurstId);
		return;
	}

	if (bCanFire && IsValid(CurrentWeapon.Get()) && !CurrentWeapon->IsActorBeingDestroyed())
	{
		CurrentWeapon->StartFiring();
	}
}

void UWeaponInventoryComponent::StopFiring()
{
	if (!GetOwner())
	{
		return;
	}

	if (!GetOwner()->HasAuthority())
	{
		AShooterWeapon* RequestedWeapon = CurrentWeapon.Get();
		StopLocalFirePrediction();
		ServerStopFiring(RequestedWeapon);
		return;
	}

	if (IsValid(CurrentWeapon.Get()) && !CurrentWeapon->IsActorBeingDestroyed())
	{
		CurrentWeapon->StopFiring();
	}
}

void UWeaponInventoryComponent::Reload()
{
	if (!GetOwner())
	{
		return;
	}

	if (!GetOwner()->HasAuthority())
	{
		AShooterWeapon* RequestedWeapon = CurrentWeapon.Get();
		const ATacticalGameState* TacticalGameState = GetWorld()
			? GetWorld()->GetGameState<ATacticalGameState>() : nullptr;
		const bool bHasLocalReloadAmmo = CurrentWeapon.Get()
			&& (CurrentWeapon->GetReserveBulletCount() > 0
				|| (TacticalGameState
					&& TacticalGameState->CurrentPhase == ERoundPhase::PreRound));
		if (IsAutonomousLocalOwner() && bCanFire
			&& IsValid(CurrentWeapon.Get()) && !CurrentWeapon->IsActorBeingDestroyed()
			&& CurrentWeapon->GetBulletCount() < CurrentWeapon->GetMagazineSize()
			&& bHasLocalReloadAmmo && GetWorld())
		{
			StopLocalFirePrediction();
			LocalReloadBlockUntil = GetWorld()->GetTimeSeconds()
				+ GetLocalReloadPresentationDuration(CurrentWeapon.Get())
				+ LocalReloadNetworkMarginSeconds;
		}
		ServerReload(RequestedWeapon);
		return;
	}

	if (bCanFire && IsValid(CurrentWeapon.Get()) && !CurrentWeapon->IsActorBeingDestroyed())
	{
		CurrentWeapon->Reload();
	}
}

bool UWeaponInventoryComponent::IsAutonomousLocalOwner() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	return OwnerPawn && OwnerPawn->IsLocallyControlled() && !OwnerPawn->HasAuthority();
}

float UWeaponInventoryComponent::GetLocalReloadPresentationDuration(
	const AShooterWeapon* Weapon) const
{
	if (!Weapon)
	{
		return 0.0f;
	}

	float Duration = 0.0f;
	for (const UAnimMontage* Montage :
		{Weapon->GetFirstPersonReloadMontage(), Weapon->GetFirstPersonWeaponReloadMontage()})
	{
		if (Montage && FMath::Abs(Montage->RateScale) > UE_KINDA_SMALL_NUMBER)
		{
			Duration = FMath::Max(Duration,
				Montage->GetPlayLength() / FMath::Abs(Montage->RateScale));
		}
	}
	if (const ATacticalWeapon* TacticalWeapon = Cast<ATacticalWeapon>(Weapon))
	{
		Duration = FMath::Max(Duration,
			TacticalWeapon->GetAdditionalReloadPresentationDuration());
	}
	return Duration;
}

bool UWeaponInventoryComponent::IsLocalSwitchPending(const double Now)
{
	if (bLocalSwitchPending && Now >= LocalSwitchBlockUntil)
	{
		bLocalSwitchPending = false;
		LocalSwitchBlockUntil = 0.0;
	}
	return bLocalSwitchPending;
}

void UWeaponInventoryComponent::TryPlayNextPredictedFire()
{
	UWorld* World = GetWorld();
	AShooterWeapon* Weapon = LocalPredictedWeapon.Get();
	if (!World || !IsAutonomousLocalOwner() || !bLocalTriggerHeld
		|| !bCanFire || !IsValid(Weapon) || Weapon->IsActorBeingDestroyed()
		|| CurrentWeapon.Get() != Weapon
		|| World->GetTimeSeconds() < LocalReloadBlockUntil
		|| IsLocalSwitchPending(World->GetTimeSeconds())
		|| LocalAttemptedShotsInBurst >= LocalAmmoAtBurstStart)
	{
		StopLocalFirePrediction();
		return;
	}

	const double Now = World->GetTimeSeconds();
	LocalWeaponFireTimings.RemoveAll([](const FLocalWeaponFireTiming& Timing)
	{
		return !Timing.Weapon.IsValid();
	});
	FLocalWeaponFireTiming* Timing = LocalWeaponFireTimings.FindByPredicate(
		[Weapon](const FLocalWeaponFireTiming& Candidate)
		{
			return Candidate.Weapon.Get() == Weapon;
		});
	if (!Timing)
	{
		Timing = &LocalWeaponFireTimings.AddDefaulted_GetRef();
		Timing->Weapon = Weapon;
	}
	if (Now < Timing->NextAllowedFireTime)
	{
		World->GetTimerManager().SetTimer(LocalPredictedFireTimer, this,
			&UWeaponInventoryComponent::TryPlayNextPredictedFire,
			FMath::Max(0.01f,
				static_cast<float>(Timing->NextAllowedFireTime - Now)), false);
		return;
	}

	++LocalAttemptedShotsInBurst;
	++NextPredictionShotIndex;
	if (NextPredictionShotIndex == 0)
	{
		++NextPredictionShotIndex;
	}
	PrunePredictedFireEchoes(Now);
	bool bAlreadyPlayedByServer = false;
	for (int32 Index = 0; Index < ObservedAuthoritativeFireEchoes.Num(); ++Index)
	{
		const FPendingPredictedFireEcho& Echo = ObservedAuthoritativeFireEchoes[Index];
		if (Echo.Weapon.Get() == Weapon && Echo.BurstId == ActivePredictionBurstId
			&& Echo.ShotIndex == NextPredictionShotIndex)
		{
			ObservedAuthoritativeFireEchoes.RemoveAtSwap(Index);
			bAlreadyPlayedByServer = true;
			break;
		}
	}
	if (!bAlreadyPlayedByServer)
	{
		if (ATacticalCharacter* TacticalOwner = Cast<ATacticalCharacter>(GetOwner()))
		{
			if (TacticalOwner->PlayPredictedFirstPersonFire(Weapon))
			{
				FPendingPredictedFireEcho& Echo = PendingPredictedFireEchoes.AddDefaulted_GetRef();
				Echo.Weapon = Weapon;
				Echo.BurstId = ActivePredictionBurstId;
				Echo.ShotIndex = NextPredictionShotIndex;
				Echo.ExpiresAt = Now + PredictedFireEchoLifetimeSeconds;
				if (PendingPredictedFireEchoes.Num() > MaxPendingPredictedFireEchoes)
				{
					PendingPredictedFireEchoes.RemoveAt(0);
				}
			}
		}
	}

	Timing->NextAllowedFireTime = Now + Weapon->GetRefireInterval();
	if (Weapon->IsFullAuto() && LocalAttemptedShotsInBurst < LocalAmmoAtBurstStart)
	{
		World->GetTimerManager().SetTimer(LocalPredictedFireTimer, this,
			&UWeaponInventoryComponent::TryPlayNextPredictedFire,
			FMath::Max(0.01f, Weapon->GetRefireInterval()), false);
	}
}

void UWeaponInventoryComponent::StopLocalFirePrediction()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LocalPredictedFireTimer);
	}
	bLocalTriggerHeld = false;
	LocalPredictedWeapon = nullptr;
	ActivePredictionBurstId = 0;
	NextPredictionShotIndex = 0;
	LocalAmmoAtBurstStart = 0;
	LocalAttemptedShotsInBurst = 0;
}

void UWeaponInventoryComponent::CancelLocalFirePrediction()
{
	StopLocalFirePrediction();
}

void UWeaponInventoryComponent::PrunePredictedFireEchoes(const double Now)
{
	PendingPredictedFireEchoes.RemoveAll([Now](const FPendingPredictedFireEcho& Echo)
	{
		return !Echo.Weapon.IsValid() || Echo.ExpiresAt < Now;
	});
	ObservedAuthoritativeFireEchoes.RemoveAll([Now](const FPendingPredictedFireEcho& Echo)
	{
		return !Echo.Weapon.IsValid() || Echo.ExpiresAt < Now;
	});
}

bool UWeaponInventoryComponent::ShouldSuppressOwnerFireEcho(
	AShooterWeapon* Weapon, const uint16 PredictionBurstId,
	const uint16 BurstShotIndex)
{
	if (!IsAutonomousLocalOwner())
	{
		return false;
	}
	if (!IsValid(Weapon) || Weapon->IsActorBeingDestroyed()
		|| CurrentWeapon.Get() != Weapon || !bCanFire)
	{
		return true;
	}

	const UWorld* World = GetWorld();
	PrunePredictedFireEchoes(World ? World->GetTimeSeconds() : 0.0);
	if (PredictionBurstId == 0 || BurstShotIndex == 0)
	{
		return false;
	}
	for (int32 Index = 0; Index < PendingPredictedFireEchoes.Num(); ++Index)
	{
		const FPendingPredictedFireEcho& Echo = PendingPredictedFireEchoes[Index];
		if (Echo.Weapon.Get() == Weapon && Echo.BurstId == PredictionBurstId
			&& Echo.ShotIndex == BurstShotIndex)
		{
			PendingPredictedFireEchoes.RemoveAtSwap(Index);
			return true;
		}
	}
	// An authoritative shot can overtake a locally queued cosmetic cooldown.
	// Let the multicast play now, then skip the matching future local timer.
	if (bLocalTriggerHeld && LocalPredictedWeapon.Get() == Weapon
		&& ActivePredictionBurstId == PredictionBurstId
		&& BurstShotIndex > NextPredictionShotIndex)
	{
		FPendingPredictedFireEcho& Echo = ObservedAuthoritativeFireEchoes.AddDefaulted_GetRef();
		Echo.Weapon = Weapon;
		Echo.BurstId = PredictionBurstId;
		Echo.ShotIndex = BurstShotIndex;
		Echo.ExpiresAt = (World ? World->GetTimeSeconds() : 0.0)
			+ PredictedFireEchoLifetimeSeconds;
		if (ObservedAuthoritativeFireEchoes.Num() > MaxPendingPredictedFireEchoes)
		{
			ObservedAuthoritativeFireEchoes.RemoveAt(0);
		}
	}
	return false;
}

void UWeaponInventoryComponent::NotifyAuthoritativeReloadStarted(
	AShooterWeapon* Weapon)
{
	if (!IsAutonomousLocalOwner() || !IsValid(Weapon)
		|| CurrentWeapon.Get() != Weapon)
	{
		return;
	}
	StopLocalFirePrediction();
	if (const UWorld* World = GetWorld())
	{
		LocalReloadBlockUntil = FMath::Max(LocalReloadBlockUntil,
			static_cast<double>(World->GetTimeSeconds()
				+ GetLocalReloadPresentationDuration(Weapon)
				+ LocalReloadNetworkMarginSeconds));
	}
}

void UWeaponInventoryComponent::ResetLocalFirePrediction()
{
	StopLocalFirePrediction();
	PendingPredictedFireEchoes.Reset();
	ObservedAuthoritativeFireEchoes.Reset();
	LocalWeaponFireTimings.Reset();
	LocalReloadBlockUntil = 0.0;
	LocalSwitchBlockUntil = 0.0;
	bLocalSwitchPending = false;
}

AShooterWeapon* UWeaponInventoryComponent::FindWeaponByTier(int32 Tier)
{
	for (AShooterWeapon* Weapon : OwnedWeapons)
	{
		if (IsValid(Weapon) && !Weapon->IsActorBeingDestroyed() && Weapon->GetWeaponTier() == Tier)
		{
			return Weapon;
		}
	}
	return nullptr;
}

bool UWeaponInventoryComponent::HasFreeSlot(int32 Category) const
{
	for (const AShooterWeapon* Weapon : OwnedWeapons)
	{
		if (IsValid(Weapon) && !Weapon->IsActorBeingDestroyed()
			&& Weapon->GetWeaponCategory() == Category)
		{
			return false;
		}
	}
	return true;
}

int32 UWeaponInventoryComponent::GetEquippedTier(int32 Category) const
{
	for (const AShooterWeapon* Weapon : OwnedWeapons)
	{
		if (IsValid(Weapon) && !Weapon->IsActorBeingDestroyed()
			&& Weapon->GetWeaponCategory() == Category)
		{
			return Weapon->GetWeaponTier();
		}
	}
	return -1;
}

AShooterWeapon* UWeaponInventoryComponent::RemoveWeaponByTier(int32 Tier)
{
	for (int32 i = 0; i < OwnedWeapons.Num(); ++i)
	{
		if (IsValid(OwnedWeapons[i]) && !OwnedWeapons[i]->IsActorBeingDestroyed()
			&& OwnedWeapons[i]->GetWeaponTier() == Tier)
		{
			AShooterWeapon* Removed = OwnedWeapons[i];
			OwnedWeapons.RemoveAt(i);

			// Switch the public replicated pointer and notify every listener before
			// the caller is allowed to retire/destroy Removed.
			if (CurrentWeapon.Get() == Removed || !IsValid(CurrentWeapon.Get()))
			{
				SetCurrentWeapon(FindFirstValidWeapon());
			}
			else
			{
				Removed->DeactivateWeapon();
			}

			if (AActor* OwnerActor = GetOwner(); OwnerActor && OwnerActor->HasAuthority())
			{
				OwnerActor->ForceNetUpdate();
			}
			return Removed;
		}
	}
	return nullptr;
}

AShooterWeapon* UWeaponInventoryComponent::FindWeaponOfClass(TSubclassOf<AShooterWeapon> WeaponClass) const
{
	for (AShooterWeapon* Weapon : OwnedWeapons)
	{
		if (IsValid(Weapon) && !Weapon->IsActorBeingDestroyed() && Weapon->IsA(WeaponClass))
		{
			return Weapon;
		}
	}
	return nullptr;
}

void UWeaponInventoryComponent::SetCurrentWeapon(AShooterWeapon* NewWeapon)
{
	AShooterWeapon* ValidNewWeapon = IsValid(NewWeapon) && !NewWeapon->IsActorBeingDestroyed()
		? NewWeapon
		: nullptr;
	AShooterWeapon* RawOldWeapon = CurrentWeapon.Get();
	AShooterWeapon* ValidOldWeapon = IsValid(RawOldWeapon) && !RawOldWeapon->IsActorBeingDestroyed()
		? RawOldWeapon
		: nullptr;

	if (RawOldWeapon == ValidNewWeapon)
	{
		CachedCurrentWeapon = ValidNewWeapon;
		TrackCurrentWeaponLifetime(ValidNewWeapon);
		return;
	}

	// Publish the new pointer before any deactivation callback executes. AnimBP,
	// HUD and scope listeners are therefore never called while CurrentWeapon still
	// names the weapon that is being retired.
	CurrentWeapon = ValidNewWeapon;
	CachedCurrentWeapon = ValidNewWeapon;
	TrackCurrentWeaponLifetime(ValidNewWeapon);

	if (ValidOldWeapon && ValidOldWeapon != ValidNewWeapon)
	{
		ValidOldWeapon->DeactivateWeapon();
	}
	if (ValidNewWeapon)
	{
		ValidNewWeapon->ActivateWeapon();
	}

	OnWeaponChanged.Broadcast(ValidNewWeapon, ValidOldWeapon);
	if (AActor* OwnerActor = GetOwner(); OwnerActor && OwnerActor->HasAuthority())
	{
		// Component replication is carried by the owning pawn's actor channel.
		OwnerActor->ForceNetUpdate();
	}
}

void UWeaponInventoryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetLocalFirePrediction();
	// Clear every externally visible reference and notify listeners before any
	// weapon enters pending-kill. This is especially important during seamless
	// PIE travel, where animation/UI objects can tick during world teardown.
	TArray<AShooterWeapon*> WeaponsToDestroy = MoveTemp(OwnedWeapons);
	AShooterWeapon* PreviousWeapon = IsValid(CurrentWeapon.Get()) ? CurrentWeapon.Get() : nullptr;
	CurrentWeapon = nullptr;
	CachedCurrentWeapon = nullptr;
	TrackCurrentWeaponLifetime(nullptr);
	OnWeaponChanged.Broadcast(nullptr, PreviousWeapon);
	OnAmmoUpdated.Broadcast(0, 0, 0);
	OnWeaponChanged.Clear();
	OnAmmoUpdated.Clear();

	for (AShooterWeapon* Weapon : WeaponsToDestroy)
	{
		if (IsValid(Weapon))
		{
			Weapon->Destroy();
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UWeaponInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UWeaponInventoryComponent, OwnedWeapons);
	DOREPLIFETIME(UWeaponInventoryComponent, CurrentWeapon);
}

void UWeaponInventoryComponent::OnRep_OwnedWeapons()
{
	// Do not compact this replicated array on clients. A null entry can be an
	// unresolved actor NetGUID that will be fixed up after its actor channel opens.
	ClearInvalidCurrentWeapon();
}

void UWeaponInventoryComponent::OnRep_CurrentWeapon()
{
	AShooterWeapon* RawNewWeapon = CurrentWeapon.Get();
	AShooterWeapon* NewWeapon = IsValid(RawNewWeapon) && !RawNewWeapon->IsActorBeingDestroyed()
		? RawNewWeapon
		: nullptr;
	AShooterWeapon* OldWeapon = IsValid(CachedCurrentWeapon.Get())
		&& !CachedCurrentWeapon->IsActorBeingDestroyed()
		? CachedCurrentWeapon.Get()
		: nullptr;

	if (RawNewWeapon != NewWeapon)
	{
		CurrentWeapon = nullptr;
	}
	TrackCurrentWeaponLifetime(NewWeapon);
	CachedCurrentWeapon = NewWeapon;

	if (NewWeapon != OldWeapon || RawNewWeapon != NewWeapon)
	{
		StopLocalFirePrediction();
		LocalReloadBlockUntil = 0.0;
		LocalSwitchBlockUntil = 0.0;
		bLocalSwitchPending = false;
		OnWeaponChanged.Broadcast(NewWeapon, OldWeapon);
	}
}

void UWeaponInventoryComponent::OnTrackedCurrentWeaponDestroyed(AActor* DestroyedActor)
{
	AShooterWeapon* DestroyedWeapon = Cast<AShooterWeapon>(DestroyedActor);
	if (!DestroyedWeapon)
	{
		return;
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		OwnedWeapons.RemoveAll([DestroyedWeapon](const AShooterWeapon* Weapon)
		{
			return Weapon == DestroyedWeapon || !IsValid(Weapon);
		});
	}

	if (CurrentWeapon.Get() != DestroyedWeapon && IsValid(CurrentWeapon.Get()))
	{
		return;
	}
	StopLocalFirePrediction();

	// Never forward a pending-kill object as OldWeapon. Blueprint listeners often
	// treat a non-null UObject pointer as usable even after IsValid has become false.
	CurrentWeapon = nullptr;
	CachedCurrentWeapon = nullptr;
	TrackedCurrentWeapon = nullptr;

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (AShooterWeapon* Replacement = FindFirstValidWeapon())
		{
			SetCurrentWeapon(Replacement);
		}
		else
		{
			OnWeaponChanged.Broadcast(nullptr, nullptr);
			OnAmmoUpdated.Broadcast(0, 0, 0);
			GetOwner()->ForceNetUpdate();
		}
	}
	else
	{
		OnWeaponChanged.Broadcast(nullptr, nullptr);
		OnAmmoUpdated.Broadcast(0, 0, 0);
	}
}

void UWeaponInventoryComponent::TrackCurrentWeaponLifetime(AShooterWeapon* Weapon)
{
	AShooterWeapon* ValidWeapon = IsValid(Weapon) && !Weapon->IsActorBeingDestroyed()
		? Weapon
		: nullptr;
	if (TrackedCurrentWeapon.Get() == ValidWeapon)
	{
		return;
	}

	if (IsValid(TrackedCurrentWeapon.Get()))
	{
		TrackedCurrentWeapon->OnDestroyed.RemoveDynamic(
			this, &UWeaponInventoryComponent::OnTrackedCurrentWeaponDestroyed);
	}
	TrackedCurrentWeapon = nullptr;

	if (ValidWeapon)
	{
		ValidWeapon->OnDestroyed.RemoveDynamic(
			this, &UWeaponInventoryComponent::OnTrackedCurrentWeaponDestroyed);
		ValidWeapon->OnDestroyed.AddDynamic(
			this, &UWeaponInventoryComponent::OnTrackedCurrentWeaponDestroyed);
		TrackedCurrentWeapon = ValidWeapon;
	}
}

void UWeaponInventoryComponent::ClearInvalidCurrentWeapon()
{
	if (!CurrentWeapon || IsValid(CurrentWeapon.Get()))
	{
		return;
	}

	CurrentWeapon = nullptr;
	CachedCurrentWeapon = nullptr;
	StopLocalFirePrediction();
	TrackCurrentWeaponLifetime(nullptr);
	OnWeaponChanged.Broadcast(nullptr, nullptr);
	OnAmmoUpdated.Broadcast(0, 0, 0);
}

AShooterWeapon* UWeaponInventoryComponent::FindFirstValidWeapon() const
{
	for (AShooterWeapon* Weapon : OwnedWeapons)
	{
		if (IsValid(Weapon) && !Weapon->IsActorBeingDestroyed())
		{
			return Weapon;
		}
	}
	return nullptr;
}

void UWeaponInventoryComponent::ServerStartFiring_Implementation(
	AShooterWeapon* ExpectedWeapon, const uint16 PredictionBurstId)
{
	if (bCanFire && IsValid(ExpectedWeapon)
		&& !ExpectedWeapon->IsActorBeingDestroyed()
		&& ExpectedWeapon == CurrentWeapon.Get()
		&& !ExpectedWeapon->IsReloadInProgress()
		&& ExpectedWeapon->GetBulletCount() > 0)
	{
		// Derive aim from the authoritative pawn/controller state. Never trust a
		// client-supplied world position for hit direction.
		ExpectedWeapon->StartFiring(PredictionBurstId);
	}
	else
	{
		ClientFireBurstRejected(PredictionBurstId);
	}
}

void UWeaponInventoryComponent::ClientFireBurstRejected_Implementation(
	const uint16 PredictionBurstId)
{
	// A delayed rejection belongs to exactly one trigger burst. Keep pending
	// tombstones from earlier accepted shots so their multicast cannot replay.
	if (PredictionBurstId != 0 && ActivePredictionBurstId == PredictionBurstId)
	{
		StopLocalFirePrediction();
	}
}

void UWeaponInventoryComponent::ServerStopFiring_Implementation(
	AShooterWeapon* ExpectedWeapon)
{
	if (IsValid(ExpectedWeapon) && !ExpectedWeapon->IsActorBeingDestroyed()
		&& ExpectedWeapon == CurrentWeapon.Get())
	{
		ExpectedWeapon->StopFiring();
	}
}

void UWeaponInventoryComponent::ServerReload_Implementation(
	AShooterWeapon* ExpectedWeapon)
{
	if (bCanFire && IsValid(ExpectedWeapon)
		&& !ExpectedWeapon->IsActorBeingDestroyed()
		&& ExpectedWeapon == CurrentWeapon.Get())
	{
		ExpectedWeapon->Reload();
	}
}

void UWeaponInventoryComponent::ServerSwitchWeapon_Implementation()
{
	OwnedWeapons.RemoveAll([](const AShooterWeapon* Weapon)
	{
		return !IsValid(Weapon) || Weapon->IsActorBeingDestroyed();
	});
	if (OwnedWeapons.Num() <= 1)
	{
		return;
	}

	int32 Index = OwnedWeapons.Find(CurrentWeapon);
	Index = (Index == OwnedWeapons.Num() - 1) ? 0 : Index + 1;
	SetCurrentWeapon(OwnedWeapons[Index]);
}

void UWeaponInventoryComponent::ServerSwitchToCategory_Implementation(int32 Category)
{
	for (AShooterWeapon* Weapon : OwnedWeapons)
	{
		if (IsValid(Weapon) && !Weapon->IsActorBeingDestroyed()
			&& Weapon->GetWeaponCategory() == Category && Weapon != CurrentWeapon)
		{
			SetCurrentWeapon(Weapon);
			return;
		}
	}
}
