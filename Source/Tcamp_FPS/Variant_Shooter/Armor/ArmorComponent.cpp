// Copyright Epic Games, Inc. All Rights Reserved.

#include "Armor/ArmorComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/DataTable.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "TacticalDiagnostics.h"

UArmorComponent::UArmorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	ShieldMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShieldMesh"));
	ShieldMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShieldMesh->SetCastShadow(false);
	ShieldMesh->SetRelativeScale3D(FVector(0.82f, 0.82f, 1.9f));
	ShieldMesh->SetVisibility(false);
	ShieldMesh->SetHiddenInGame(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultShieldMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (DefaultShieldMesh.Succeeded())
	{
		ShieldMesh->SetStaticMesh(DefaultShieldMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultShieldMaterial(
		TEXT("/Game/Variant_Shooter/Materials/M_Shield.M_Shield"));
	if (DefaultShieldMaterial.Succeeded())
	{
		ShieldMesh->SetMaterial(0, DefaultShieldMaterial.Object);
	}
}

void UArmorComponent::BeginPlay()
{
	Super::BeginPlay();

	UStaticMeshComponent* Visual = ResolveShieldVisual();
	// Blueprint ShieldSphere is preferred only if it has a mesh and material.
	// Otherwise, the native mesh must be attached and registered at runtime.
	if (Visual == ShieldMesh)
	{
		PrepareNativeShieldVisual();
	}

	// Hide at game start — only appears on hit
	HideShieldMesh();
}

bool UArmorComponent::HasRenderableShieldVisual(const UStaticMeshComponent* Candidate)
{
	return IsValid(Candidate) && IsValid(Candidate->GetStaticMesh())
		&& IsValid(Candidate->GetMaterial(0));
}

void UArmorComponent::PrepareNativeShieldVisual()
{
	if (!HasRenderableShieldVisual(ShieldMesh))
	{
		return;
	}

	if (AActor* Owner = GetOwner(); Owner && Owner->GetRootComponent())
	{
		if (ShieldMesh->GetAttachParent() != Owner->GetRootComponent())
		{
			ShieldMesh->AttachToComponent(Owner->GetRootComponent(),
				FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		}
		// The fallback sphere is deliberately oval around the character capsule.
		ShieldMesh->SetRelativeScale3D(FVector(0.82f, 0.82f, 1.9f));
	}
	if (!ShieldMesh->IsRegistered() && GetWorld())
	{
		ShieldMesh->RegisterComponentWithWorld(GetWorld());
	}
}

UStaticMeshComponent* UArmorComponent::ResolveShieldVisual()
{
	if (HasRenderableShieldVisual(ActiveShieldMesh))
	{
		return ActiveShieldMesh;
	}
	ActiveShieldMesh = nullptr;

	if (AActor* Owner = GetOwner())
	{
		TInlineComponentArray<UStaticMeshComponent*> StaticMeshes(Owner);
		for (UStaticMeshComponent* Candidate : StaticMeshes)
		{
			if (HasRenderableShieldVisual(Candidate) && (Candidate->GetFName() == TEXT("ShieldSphere")
				|| Candidate->ComponentHasTag(TEXT("ArmorShield"))))
			{
				ActiveShieldMesh = Candidate;
				return Candidate;
			}
		}
	}

	ActiveShieldMesh = HasRenderableShieldVisual(ShieldMesh) ? ShieldMesh : nullptr;
	return ActiveShieldMesh;
}

void UArmorComponent::CacheArmorData(EArmorType Type)
{
	// Start from gameplay-safe native tuning. The current DT_ArmorData rows were
	// authored with both reduction fields at zero; treating that as intentional
	// would create paid armor that never absorbs damage or produces hit feedback.
	switch (Type)
	{
	case EArmorType::Light:
		MaxDurability = TacticalEconomy::LightArmorMaxDurability;
		BodyDamageReduction = TacticalEconomy::LightArmorBodyReduction;
		HeadDamageReduction = TacticalEconomy::LightArmorHeadReduction;
		break;
	case EArmorType::Heavy:
		MaxDurability = TacticalEconomy::HeavyArmorMaxDurability;
		BodyDamageReduction = TacticalEconomy::HeavyArmorBodyReduction;
		HeadDamageReduction = TacticalEconomy::HeavyArmorHeadReduction;
		break;
	default:
		MaxDurability = 0.0f;
		BodyDamageReduction = 0.0f;
		HeadDamageReduction = 0.0f;
		break;
	}

	UDataTable* Table = ArmorDataTable.LoadSynchronous();
	if (!Table)
	{
		return;
	}

	static const FString ContextStr(TEXT("ArmorComponent::CacheArmorData"));
	for (FName RowName : Table->GetRowNames())
	{
		if (FArmorDataRow* Row = Table->FindRow<FArmorDataRow>(RowName, ContextStr))
		{
			if (Row->Type == Type)
			{
				if (Row->MaxDurability > 0.0f)
				{
					MaxDurability = Row->MaxDurability;
				}

				const float RowBodyReduction = FMath::Clamp(Row->BodyDamageReduction, 0.0f, 1.0f);
				const float RowHeadReduction = FMath::Clamp(Row->HeadDamageReduction, 0.0f, 1.0f);
				if (Type == EArmorType::None || RowBodyReduction > 0.0f || RowHeadReduction > 0.0f)
				{
					BodyDamageReduction = RowBodyReduction;
					HeadDamageReduction = RowHeadReduction;
				}
				else
				{
					UE_LOG(LogTacticalFlow, Warning,
						TEXT("Event=ArmorDataFallback Type=%d Reason=ZeroReduction Body=%.2f Head=%.2f"),
						static_cast<int32>(Type), BodyDamageReduction, HeadDamageReduction);
				}
				return;
			}
		}
	}
}

float UArmorComponent::ProcessIncomingDamage(float IncomingDamage, bool bHeadshot, float& OutAbsorbed)
{
	return ProcessIncomingProjectileDamage(IncomingDamage, bHeadshot, nullptr, OutAbsorbed);
}

float UArmorComponent::ProcessIncomingProjectileDamage(float IncomingDamage, bool bHeadshot, AActor* DamageCauser, float& OutAbsorbed)
{
	OutAbsorbed = 0.0f;
	const float ClampedDamage = FMath::Max(IncomingDamage, 0.0f);
	if (ClampedDamage <= 0.0f)
	{
		return 0.0f;
	}

	if (ArmorType == EArmorType::None || CurrentDurability <= 0.0f)
	{
		BreakArmor();
		return ClampedDamage;
	}

	const float Reduction = bHeadshot ? HeadDamageReduction : BodyDamageReduction;
	const float RequestedAbsorption = ClampedDamage * Reduction;
	if (RequestedAbsorption <= 0.0f)
	{
		return ClampedDamage;
	}

	const float Absorbed = FMath::Min(RequestedAbsorption, CurrentDurability);
	const bool bPenetrated = Absorbed + UE_KINDA_SMALL_NUMBER < RequestedAbsorption;
	CurrentDurability = FMath::Max(0.0f, CurrentDurability - Absorbed);
	++ArmorStateSerial;

	OutAbsorbed = Absorbed;
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=ArmorHit Pawn=\"%s\" Type=%d Incoming=%.1f Absorbed=%.1f Durability=%.1f/%.1f Penetrated=%d"),
		*GetNameSafe(GetOwner()), static_cast<int32>(ArmorType), ClampedDamage, Absorbed,
		CurrentDurability, MaxDurability, bPenetrated);
	OnArmorChanged.Broadcast(ArmorType, GetDurabilityPercent());
	OnShieldHit.Broadcast(GetDurabilityPercent(), bPenetrated);
	BP_ShieldHit(GetDurabilityPercent(), bPenetrated);
	MulticastShieldHitEffect();

	if (CurrentDurability <= 0.0f)
	{
		BreakArmor();
	}

	return FMath::Max(0.0f, ClampedDamage - Absorbed);
}

void UArmorComponent::EquipArmor(EArmorType Type)
{
	if (!GetOwner()->HasAuthority())
	{
		return;
	}

	CacheArmorData(Type);

	ArmorType = Type;
	CurrentDurability = MaxDurability;
	++ArmorStateSerial;

	OnArmorChanged.Broadcast(ArmorType, GetDurabilityPercent());
}

void UArmorComponent::RestoreArmor(EArmorType Type, float Durability)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (Type == EArmorType::None || Durability <= 0.0f)
	{
		BreakArmor();
		return;
	}

	CacheArmorData(Type);
	ArmorType = Type;
	CurrentDurability = FMath::Clamp(Durability, 0.0f, MaxDurability);
	++ArmorStateSerial;
	OnArmorChanged.Broadcast(ArmorType, GetDurabilityPercent());
}

void UArmorComponent::BreakArmor()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (ArmorType == EArmorType::None && CurrentDurability <= 0.0f)
	{
		return;
	}

	ArmorType = EArmorType::None;
	CurrentDurability = 0.0f;
	++ArmorStateSerial;

	OnArmorChanged.Broadcast(ArmorType, 0.0f);
}

bool UArmorComponent::HasArmor() const
{
	return ArmorType != EArmorType::None && CurrentDurability > 0.0f;
}

float UArmorComponent::GetDurabilityPercent() const
{
	if (!HasArmor() || MaxDurability <= 0.0f)
	{
		return 0.0f;
	}
	return CurrentDurability / MaxDurability;
}

void UArmorComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UArmorComponent, ArmorType);
	DOREPLIFETIME(UArmorComponent, CurrentDurability);
	DOREPLIFETIME(UArmorComponent, MaxDurability);
	DOREPLIFETIME(UArmorComponent, ArmorStateSerial);
}

void UArmorComponent::OnRep_ArmorState()
{
	OnArmorChanged.Broadcast(ArmorType, GetDurabilityPercent());
}

void UArmorComponent::MulticastShieldHitEffect_Implementation()
{
	ShowShieldMesh();
}

void UArmorComponent::ShowShieldMesh()
{
	UStaticMeshComponent* Visual = ResolveShieldVisual();
	if (!Visual)
	{
		return;
	}
	if (Visual == ShieldMesh)
	{
		PrepareNativeShieldVisual();
	}

	Visual->SetHiddenInGame(false, true);
	Visual->SetVisibility(true, true);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ShieldHideTimer);
		World->GetTimerManager().SetTimer(ShieldHideTimer, this, &UArmorComponent::HideShieldMesh, ShieldHitShowDuration, false);
	}
}

void UArmorComponent::HideShieldMesh()
{
	if (UStaticMeshComponent* Visual = ResolveShieldVisual())
	{
		Visual->SetVisibility(false, true);
		Visual->SetHiddenInGame(true, true);
	}
}
