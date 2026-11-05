// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterPickup.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ShooterWeaponHolder.h"
#include "ShooterWeapon.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "../TacticalCharacter.h"
#include "TacticalWeapon.h"

AShooterPickup::AShooterPickup()
{
	bReplicates = true;
 	PrimaryActorTick.bCanEverTick = false;

	// create the root
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// create the collision sphere
	SphereCollision = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere Collision"));
	SphereCollision->SetupAttachment(RootComponent);

	SphereCollision->SetRelativeLocation(FVector(0.0f, 0.0f, 84.0f));
	SphereCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SphereCollision->SetCollisionObjectType(ECC_WorldStatic);
	SphereCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	SphereCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	SphereCollision->bFillCollisionUnderneathForNavmesh = true;

	// subscribe to the collision overlap on the sphere
	SphereCollision->OnComponentBeginOverlap.AddDynamic(this, &AShooterPickup::OnOverlap);
	SphereCollision->OnComponentEndOverlap.AddDynamic(this, &AShooterPickup::OnEndOverlap);

	// create the mesh
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(SphereCollision);

	Mesh->SetCollisionProfileName(FName("NoCollision"));
}

void AShooterPickup::SetPickupStaticMesh(UStaticMesh* InMesh)
{
	if (Mesh)
	{
		Mesh->SetStaticMesh(InMesh);
	}
}

void AShooterPickup::SetPickupMeshRelativeTransform(const FTransform& InTransform)
{
	if (Mesh)
	{
		Mesh->SetRelativeTransform(InTransform);
	}
}

void AShooterPickup::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (FWeaponTableRow* WeaponData = WeaponType.GetRow<FWeaponTableRow>(FString()))
	{
		// set the mesh
		Mesh->SetStaticMesh(WeaponData->StaticMesh.LoadSynchronous());
	}
}

void AShooterPickup::BeginPlay()
{
	Super::BeginPlay();

	if (FWeaponTableRow* WeaponData = WeaponType.GetRow<FWeaponTableRow>(FString()))
	{
		// copy the weapon class
		WeaponClass = WeaponData->WeaponToSpawn;
	}
}

void AShooterPickup::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the respawn timer
	GetWorld()->GetTimerManager().ClearTimer(RespawnTimer);
}

void AShooterPickup::OnOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// have we collided against a weapon holder?
	if (IShooterWeaponHolder* WeaponHolder = Cast<IShooterWeaponHolder>(OtherActor))
	{
		// For tactical characters, check if the slot is free.
		// If free → auto-pickup (server only). If occupied → wait for interact key.
		if (ATacticalCharacter* TacChar = Cast<ATacticalCharacter>(OtherActor))
		{
			const ATacticalWeapon* DefaultWeapon = WeaponClass
				? Cast<ATacticalWeapon>(WeaponClass->GetDefaultObject()) : nullptr;
			const EWeaponCategory Category = DefaultWeapon
				? DefaultWeapon->GetWeaponCategoryEnum() : EWeaponCategory::Pistol;

			if (TacChar->HasFreeSlot(Category))
			{
				// Authority check: only the server can grant weapons and consume the pickup.
				// On clients the overlap still fires, but we skip — the server will replicate
				// the consumed state and the new weapon via OwnedWeapons replication.
				if (HasAuthority())
				{
					if (TacChar->TryAddWeaponClass(WeaponClass))
					{
						ConsumePickup();
					}
				}
			}
			else
			{
				// OfferPickupSwap runs on both client and server:
				// - Client: stores PendingPickup locally, shows "press E" UI via BP event
				// - Server: stores PendingPickup so DoInteract can consume it
				TacChar->OfferPickupSwap(this);
			}
			return;
		}

		// Non-tactical characters: auto-pickup (server only)
		if (HasAuthority())
		{
			WeaponHolder->AddWeaponClass(WeaponClass);
			ConsumePickup();
		}
	}
}

void AShooterPickup::OnEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (ATacticalCharacter* TacChar = Cast<ATacticalCharacter>(OtherActor))
	{
		TacChar->ClearPendingPickup(this);
	}
}

void AShooterPickup::ConsumePickup()
{
	if (!HasAuthority())
	{
		return;
	}

	// hide this mesh
	SetActorHiddenInGame(true);

	// disable collision
	SetActorEnableCollision(false);

	// schedule the respawn
	GetWorld()->GetTimerManager().SetTimer(RespawnTimer, this, &AShooterPickup::RespawnPickup, RespawnTime, false);
}

void AShooterPickup::RespawnPickup()
{
	if (!HasAuthority())
	{
		return;
	}

	// unhide this pickup
	SetActorHiddenInGame(false);

	// call the BP handler
	BP_OnRespawn();
}

void AShooterPickup::FinishRespawn()
{
	if (!HasAuthority())
	{
		return;
	}

	// enable collision
	SetActorEnableCollision(true);

}
