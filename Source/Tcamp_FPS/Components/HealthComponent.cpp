// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/HealthComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Controller.h"

UHealthComponent::UHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	// CurrentHP is authoritative replicated state. On a reconnecting client the
	// initial property bunch can be applied before BeginPlay; assigning MaxHP on
	// every instance would then erase the retained health until the next damage
	// update forces another replication notification.
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		CurrentHP = MaxHP;
		bDead = false;
	}
}

float UHealthComponent::TakeDamage(float Damage, AController* EventInstigator, bool bHeadshot)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || IsDead() || Damage <= 0.0f)
	{
		return 0.0f;
	}

	if (EventInstigator)
	{
		LastDamageInstigator = EventInstigator;
	}

	float AbsorbedDamage = 0.0f;
	float FinalDamage = Damage;

	// External processing (armor, buffs, etc.)
	if (OnProcessDamage.IsBound())
	{
		FinalDamage = OnProcessDamage.Execute(Damage, bHeadshot, AbsorbedDamage);
	}

	CurrentHP = FMath::Max(0.0f, CurrentHP - FinalDamage);
	OnDamaged.Broadcast(GetHealthPercent());

	if (CurrentHP <= 0.0f)
	{
		bDead = true;
		AActor* Owner = GetOwner();
		if (Owner && !DeathTag.IsNone())
		{
			Owner->Tags.AddUnique(DeathTag);
		}
		OnDeath.Broadcast();
		if (Owner)
		{
			Owner->ForceNetUpdate();
		}
	}

	return FinalDamage;
}

void UHealthComponent::Heal(float Amount)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || IsDead() || Amount <= 0.0f)
	{
		return;
	}

	CurrentHP = FMath::Min(MaxHP, CurrentHP + Amount);
	OnHealed.Broadcast(GetHealthPercent());
}

void UHealthComponent::ResetToFull()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	bDead = false;
	CurrentHP = MaxHP;
	LastDamageInstigator = nullptr;

	AActor* Owner = GetOwner();
	if (Owner && !DeathTag.IsNone())
	{
		Owner->Tags.Remove(DeathTag);
	}

	OnHealed.Broadcast(1.0f);
	GetOwner()->ForceNetUpdate();
}

float UHealthComponent::GetHealthPercent() const
{
	return MaxHP > 0.0f ? FMath::Max(0.0f, CurrentHP / MaxHP) : 0.0f;
}

void UHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UHealthComponent, CurrentHP);
	DOREPLIFETIME(UHealthComponent, bDead);
}

void UHealthComponent::OnRep_CurrentHP(float PreviousHP)
{
	const float HealthPercent = GetHealthPercent();
	if (CurrentHP < PreviousHP)
	{
		// Only an authoritative decrease represents a real hit. Initial actor
		// replication commonly changes the client default (0) to the retained
		// positive value and must not play damage feedback.
		OnDamaged.Broadcast(HealthPercent);
	}
	else if (CurrentHP > PreviousHP)
	{
		OnHealed.Broadcast(HealthPercent);
	}

	// Death is replicated separately from HP. A reconnecting client creates this
	// component with CurrentHP == 0, so an already-dead pawn has no HP transition
	// and would otherwise miss the one-shot death presentation entirely.
}

void UHealthComponent::OnRep_Dead(bool bWasDead)
{
	AActor* Owner = GetOwner();
	if (bDead)
	{
		if (Owner && !DeathTag.IsNone())
		{
			Owner->Tags.AddUnique(DeathTag);
		}

		if (!bWasDead)
		{
			OnDeath.Broadcast();
		}
	}
	else if (Owner && !DeathTag.IsNone())
	{
		Owner->Tags.Remove(DeathTag);
	}
}
