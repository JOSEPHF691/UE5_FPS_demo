// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapons/WeaponDropPickup.h"
#include "ShooterWeapon.h"
#include "ShooterWeaponHolder.h"
#include "TacticalCharacter.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AWeaponDropPickup::AWeaponDropPickup()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;
}

void AWeaponDropPickup::InitializeFromWeapon(TSubclassOf<AShooterWeapon> InWeaponClass, EWeaponTier Tier)
{
	DroppedWeaponClass = InWeaponClass;
	DroppedTier = Tier;
	ApplyDroppedWeaponVisuals();
}

void AWeaponDropPickup::BeginPlay()
{
	Super::BeginPlay();

	// Auto-destroy after lifetime
	GetWorld()->GetTimerManager().SetTimer(LifetimeTimer, this, &AWeaponDropPickup::OnLifetimeExpired, TacticalEconomy::WeaponDropLifetime, false);
}

void AWeaponDropPickup::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	GetWorld()->GetTimerManager().ClearTimer(LifetimeTimer);
}

void AWeaponDropPickup::OnOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	if (!DroppedWeaponClass)
	{
		return;
	}

	// Check if the overlapper can hold weapons
	if (IShooterWeaponHolder* Holder = Cast<IShooterWeaponHolder>(OtherActor))
	{
		// Check slot availability on tactical characters
		if (ATacticalCharacter* TacChar = Cast<ATacticalCharacter>(OtherActor))
		{
			// Determine category from the tier
			const EWeaponCategory Category = (DroppedTier == EWeaponTier::Rifle || DroppedTier == EWeaponTier::Sniper)
				? EWeaponCategory::Primary : EWeaponCategory::Pistol;

			if (TacChar->HasFreeSlot(Category))
			{
				// Free slot — only authority grants and destroys. Clients wait for
				// replicated inventory/drop state.
				if (HasAuthority() && TacChar->TryAddWeaponForTier(DroppedWeaponClass, DroppedTier))
				{
					Destroy();
				}
			}
			else
			{
				// Slot occupied — wait for interact key
				TacChar->OfferPickupSwap(this);
			}
			return;
		}

		// Non-tactical character — auto-pickup
		if (HasAuthority())
		{
			Holder->AddWeaponClass(DroppedWeaponClass);
			Destroy();
		}
	}
}

void AWeaponDropPickup::OnRep_DroppedWeaponData()
{
	ApplyDroppedWeaponVisuals();
}

void AWeaponDropPickup::ApplyDroppedWeaponVisuals()
{
	if (!DroppedWeaponClass)
	{
		return;
	}

	const AShooterWeapon* DefaultWeapon = DroppedWeaponClass->GetDefaultObject<AShooterWeapon>();
	SetPickupStaticMesh(DefaultWeapon ? DefaultWeapon->GetDropPickupMesh() : nullptr);
	SetPickupMeshRelativeTransform(FTransform(DropMeshRelativeRotation, DropMeshRelativeLocation, DropMeshRelativeScale));
}

void AWeaponDropPickup::OnLifetimeExpired()
{
	Destroy();
}

void AWeaponDropPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AWeaponDropPickup, DroppedWeaponClass);
	DOREPLIFETIME(AWeaponDropPickup, DroppedTier);
}
