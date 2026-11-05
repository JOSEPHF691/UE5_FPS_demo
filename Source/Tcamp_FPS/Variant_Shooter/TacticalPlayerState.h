// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterPlayerState.h"
#include "Economy/EconomyTypes.h"
#include "TacticalPlayerState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMoneyUpdated, int32, NewMoney);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnArmorUpdated, EArmorType, ArmorType, float, DurabilityPercent);

/** Privacy-safe connection state visible to teammates/scoreboard presentation. */
UENUM(BlueprintType)
enum class ETacticalPublicConnectionStatus : uint8
{
	Connected,
	Reconnecting,
	Disconnected,
	Abandoned
};

/**
 *  PlayerState for the tactical FPS game.
 *  Extends AShooterPlayerState with economy and armor replication.
 */
UCLASS()
class TCAMP_FPS_API ATacticalPlayerState : public AShooterPlayerState
{
	GENERATED_BODY()

public:
	/** Current money balance — replicates to owning client */
	UPROPERTY(ReplicatedUsing = OnRep_Money, BlueprintReadOnly, Category = "Economy")
	int32 Money = 0;

	/** Consecutive round losses for loss streak bonus */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Economy")
	int32 LossStreak = 0;

	/** Equipped armor type */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Armor")
	EArmorType ArmorType = EArmorType::None;

	/** Remaining armor durability */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Armor")
	float ArmorDurability = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_Armor)
	uint8 ArmorStateSerial = 0;

	/** Rounds won by this player's team */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Score")
	int32 RoundsWon = 0;

	/** Primary weapon carried into the next round. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Loadout")
	EWeaponTier SavedPrimaryTier = EWeaponTier::DefaultPistol;

	/** Pistol carried into the next round. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Loadout")
	EWeaponTier SavedPistolTier = EWeaponTier::DefaultPistol;

	/** Team byte (0 or 1) — replicated so clients can filter */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Team")
	uint8 TeamByte = 255;

	/** Lobby eligibility before start, then active-roster participation after start. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Team")
	bool bIsActiveParticipant = false;

	/** Lobby readiness. Only relevant while the match is in PreRound. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Lobby")
	bool bIsReady = false;

	/** False while the participant's roster slot is being held for reconnect. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Network")
	bool bIsConnected = true;

	/** Contains no account, address, session, or credential data. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Network")
	ETacticalPublicConnectionStatus ConnectionStatus =
		ETacticalPublicConnectionStatus::Connected;

	/**
	 * Absolute server-world time at which the public reconnect-status window ends.
	 * The legacy field name is retained for Blueprint/serialization compatibility;
	 * expiry is not a retained-pawn death or same-round reclaim deadline.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Network")
	float ReconnectGraceEndServerTime = 0.0f;

	/** Server-only round number still owed one economy settlement after a disconnect. Zero means none. */
	int32 PendingSettlementRound = 0;

	/** Fires on owning client when money changes */
	UPROPERTY(BlueprintAssignable, Category = "Economy")
	FOnMoneyUpdated OnMoneyUpdated;

	/** Fires on owning client when armor changes */
	UPROPERTY(BlueprintAssignable, Category = "Armor")
	FOnArmorUpdated OnArmorUpdated;

public:
	/** Adds money — server only */
	void AddMoney(int32 Amount);

	/** Spends money if affordable — server only. */
	bool SpendMoney(int32 Amount);

	/** Resets per-match economy and score state. */
	void InitializeForMatch();

	/** Sets the server-authoritative lobby readiness flag. */
	void SetReady(bool bReady);

	/** Updates public connection state and its authoritative status-window deadline together. */
	void SetConnectionStatus(ETacticalPublicConnectionStatus NewStatus,
		float NewReconnectGraceEndServerTime = 0.0f);

	/** Remaining public reconnect-status time derived from the synchronized GameState clock. */
	UFUNCTION(BlueprintPure, Category = "Network")
	float GetReconnectGraceTimeRemaining() const;

	/** Advances the loss streak and grants the configured loss income. */
	void AwardRoundLoss();

	/** Grants draw income without changing the loss streak. */
	void AwardRoundDraw();

	/** Called when this player's team wins the round */
	void AwardRoundWin();

	/** Stores the weapon tiers that should be restored on next spawn. */
	void SetSavedLoadout(EWeaponTier PrimaryTier, EWeaponTier PistolTier);

	/** Clears all non-default equipment after death. */
	void ClearEquipmentOnDeath();

	/** Stores authoritative armor state for next-round restoration. */
	void SetSavedArmor(EArmorType Type, float Durability);

	/** Returns the durability as a 0.0–1.0 fraction */
	UFUNCTION(BlueprintPure, Category = "Armor")
	float GetArmorDurabilityPercent() const;

	/** Returns the cost of a given weapon tier */
	UFUNCTION(BlueprintPure, Category = "Economy")
	static int32 GetWeaponCost(EWeaponTier Tier);

	/** Returns the cost of a given armor type */
	UFUNCTION(BlueprintPure, Category = "Economy")
	static int32 GetArmorCost(EArmorType Type);

	static float GetArmorMaxDurability(EArmorType Type);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_Money();

	UFUNCTION()
	void OnRep_Armor();
};
