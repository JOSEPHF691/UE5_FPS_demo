// Copyright Epic Games, Inc. All Rights Reserved.

#include "TacticalPlayerState.h"
#include "TacticalRules.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

void ATacticalPlayerState::AddMoney(int32 Amount)
{
	if (!HasAuthority())
	{
		return;
	}

	Money = FMath::Clamp(Money + Amount, 0, TacticalEconomy::MaxMoney);
	OnMoneyUpdated.Broadcast(Money);
}

bool ATacticalPlayerState::SpendMoney(int32 Amount)
{
	if (!HasAuthority() || Amount < 0 || Money < Amount)
	{
		return false;
	}

	Money -= Amount;
	OnMoneyUpdated.Broadcast(Money);
	return true;
}

void ATacticalPlayerState::InitializeForMatch()
{
	if (!HasAuthority())
	{
		return;
	}

	Money = TacticalEconomy::StartingMoney;
	LossStreak = 0;
	RoundsWon = 0;
	PendingSettlementRound = 0;
	SetConnectionStatus(ETacticalPublicConnectionStatus::Connected);
	ArmorType = EArmorType::None;
	ArmorDurability = 0.0f;
	++ArmorStateSerial;
	SavedPrimaryTier = EWeaponTier::DefaultPistol;
	SavedPistolTier = EWeaponTier::DefaultPistol;
	OnMoneyUpdated.Broadcast(Money);
	OnArmorUpdated.Broadcast(ArmorType, 0.0f);
}

void ATacticalPlayerState::SetReady(bool bReady)
{
	if (HasAuthority())
	{
		bIsReady = bReady;
	}
}

void ATacticalPlayerState::SetConnectionStatus(
	ETacticalPublicConnectionStatus NewStatus,
	float NewReconnectGraceEndServerTime)
{
	if (!HasAuthority())
	{
		return;
	}

	ConnectionStatus = NewStatus;
	bIsConnected = NewStatus == ETacticalPublicConnectionStatus::Connected;
	ReconnectGraceEndServerTime = NewStatus == ETacticalPublicConnectionStatus::Reconnecting
		? FMath::Max(0.0f, NewReconnectGraceEndServerTime)
		: 0.0f;
}

float ATacticalPlayerState::GetReconnectGraceTimeRemaining() const
{
	if (ConnectionStatus != ETacticalPublicConnectionStatus::Reconnecting
		|| ReconnectGraceEndServerTime <= 0.0f)
	{
		return 0.0f;
	}

	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	const float ServerWorldTime = GameState
		? GameState->GetServerWorldTimeSeconds()
		: World ? World->GetTimeSeconds() : 0.0f;
	return TacticalRules::GetReconnectGraceTimeRemaining(
		ReconnectGraceEndServerTime, ServerWorldTime);
}

void ATacticalPlayerState::AwardRoundLoss()
{
	if (!HasAuthority())
	{
		return;
	}

	TacticalRules::ApplyRoundEconomy(TacticalRules::ERoundEconomyOutcome::Loss,
		Money, LossStreak, RoundsWon);
	OnMoneyUpdated.Broadcast(Money);
}

void ATacticalPlayerState::AwardRoundDraw()
{
	if (!HasAuthority())
	{
		return;
	}

	TacticalRules::ApplyRoundEconomy(TacticalRules::ERoundEconomyOutcome::Draw,
		Money, LossStreak, RoundsWon);
	OnMoneyUpdated.Broadcast(Money);
}

void ATacticalPlayerState::AwardRoundWin()
{
	if (!HasAuthority())
	{
		return;
	}

	TacticalRules::ApplyRoundEconomy(TacticalRules::ERoundEconomyOutcome::Win,
		Money, LossStreak, RoundsWon);
	OnMoneyUpdated.Broadcast(Money);
}

void ATacticalPlayerState::SetSavedLoadout(EWeaponTier PrimaryTier, EWeaponTier PistolTier)
{
	if (!HasAuthority())
	{
		return;
	}

	SavedPrimaryTier = PrimaryTier;
	SavedPistolTier = PistolTier;
}

void ATacticalPlayerState::ClearEquipmentOnDeath()
{
	if (!HasAuthority())
	{
		return;
	}

	SavedPrimaryTier = EWeaponTier::DefaultPistol;
	SavedPistolTier = EWeaponTier::DefaultPistol;
	ArmorType = EArmorType::None;
	ArmorDurability = 0.0f;
	++ArmorStateSerial;
	OnArmorUpdated.Broadcast(ArmorType, 0.0f);
}

void ATacticalPlayerState::SetSavedArmor(EArmorType Type, float Durability)
{
	if (!HasAuthority())
	{
		return;
	}

	ArmorType = Durability > 0.0f ? Type : EArmorType::None;
	ArmorDurability = FMath::Max(0.0f, Durability);
	++ArmorStateSerial;
	OnArmorUpdated.Broadcast(ArmorType, GetArmorDurabilityPercent());
}

float ATacticalPlayerState::GetArmorDurabilityPercent() const
{
	if (ArmorType == EArmorType::None || ArmorDurability <= 0.0f)
	{
		return 0.0f;
	}
	return FMath::Clamp(ArmorDurability / GetArmorMaxDurability(ArmorType), 0.0f, 1.0f);
}

int32 ATacticalPlayerState::GetWeaponCost(EWeaponTier Tier)
{
	switch (Tier)
	{
	case EWeaponTier::DefaultPistol:	return 0;
	case EWeaponTier::UpgradedPistol:	return 500;
	case EWeaponTier::Rifle:			return 2500;
	case EWeaponTier::Sniper:			return 4000;
	default:							return 0;
	}
}

int32 ATacticalPlayerState::GetArmorCost(EArmorType Type)
{
	switch (Type)
	{
	case EArmorType::Light:		return 650;
	case EArmorType::Heavy:		return 1250;
	default:					return 0;
	}
}

float ATacticalPlayerState::GetArmorMaxDurability(EArmorType Type)
{
	switch (Type)
	{
	case EArmorType::Heavy: return TacticalEconomy::HeavyArmorMaxDurability;
	case EArmorType::Light: return TacticalEconomy::LightArmorMaxDurability;
	default: return 1.0f;
	}
}

void ATacticalPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(ATacticalPlayerState, Money, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ATacticalPlayerState, LossStreak, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ATacticalPlayerState, ArmorType, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ATacticalPlayerState, ArmorDurability, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ATacticalPlayerState, ArmorStateSerial, COND_OwnerOnly);
	DOREPLIFETIME(ATacticalPlayerState, RoundsWon);
	DOREPLIFETIME_CONDITION(ATacticalPlayerState, SavedPrimaryTier, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ATacticalPlayerState, SavedPistolTier, COND_OwnerOnly);
	DOREPLIFETIME(ATacticalPlayerState, TeamByte);
	DOREPLIFETIME(ATacticalPlayerState, bIsActiveParticipant);
	DOREPLIFETIME(ATacticalPlayerState, bIsReady);
	DOREPLIFETIME(ATacticalPlayerState, bIsConnected);
	DOREPLIFETIME(ATacticalPlayerState, ConnectionStatus);
	DOREPLIFETIME(ATacticalPlayerState, ReconnectGraceEndServerTime);
}

void ATacticalPlayerState::OnRep_Money()
{
	OnMoneyUpdated.Broadcast(Money);
}

void ATacticalPlayerState::OnRep_Armor()
{
	OnArmorUpdated.Broadcast(ArmorType, GetArmorDurabilityPercent());
}
