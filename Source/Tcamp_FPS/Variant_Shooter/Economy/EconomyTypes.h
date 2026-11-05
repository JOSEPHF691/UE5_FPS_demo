// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "EconomyTypes.generated.h"

class AShooterWeapon;
class AShooterProjectile;
class UTexture2D;

// ---- Enums ----

/** Weapon tier defining stats and cost */
UENUM(BlueprintType)
enum class EWeaponTier : uint8
{
	DefaultPistol	UMETA(DisplayName = "Default Pistol"),
	UpgradedPistol	UMETA(DisplayName = "Upgraded Pistol"),
	Rifle			UMETA(DisplayName = "Rifle"),
	Sniper			UMETA(DisplayName = "Sniper")
};

/** Weapon slot category */
UENUM(BlueprintType)
enum class EWeaponCategory : uint8
{
	Pistol	UMETA(DisplayName = "Pistol"),
	Primary	UMETA(DisplayName = "Primary (Rifle/Sniper)")
};

/** Armor type */
UENUM(BlueprintType)
enum class EArmorType : uint8
{
	None	UMETA(DisplayName = "No Armor"),
	Light	UMETA(DisplayName = "Light Armor"),
	Heavy	UMETA(DisplayName = "Heavy Armor")
};

/** Treasure chest reward type */
UENUM(BlueprintType)
enum class EChestReward : uint8
{
	FullHeal			UMETA(Hidden),
	SpeedBoost			UMETA(Hidden),
	MoneyReward			UMETA(Hidden),
	TeamMoneyAndHeal	UMETA(DisplayName = "Team Money + Claimant Heal")
};

/** Replicated public state of the single treasure objective for a round. */
UENUM(BlueprintType)
enum class ETreasureRoundState : uint8
{
	Disabled			UMETA(DisplayName = "Disabled"),
	WaitingForAnnouncement UMETA(DisplayName = "Waiting For Announcement"),
	Announced			UMETA(DisplayName = "Announced"),
	Guarded				UMETA(DisplayName = "Guarded"),
	Unlocked			UMETA(DisplayName = "Unlocked"),
	Claiming			UMETA(DisplayName = "Claiming"),
	Claimed				UMETA(DisplayName = "Claimed")
};

/** Round phase — replicated via GameState */
UENUM(BlueprintType)
enum class ERoundPhase : uint8
{
	PreRound	UMETA(DisplayName = "Pre-Round"),
	Buy			UMETA(DisplayName = "Buy Phase"),
	Combat		UMETA(DisplayName = "Combat Phase"),
	Overtime	UMETA(Hidden),
	RoundEnd	UMETA(DisplayName = "Round End"),
	MatchEnd	UMETA(DisplayName = "Match End")
};

/** Why a tactical round ended. Used by UI and playtest logging. */
UENUM(BlueprintType)
enum class ERoundEndReason : uint8
{
	Unknown				UMETA(DisplayName = "Unknown"),
	TeamEliminated		UMETA(DisplayName = "Team Eliminated"),
	TimeAliveAdvantage	UMETA(Hidden),
	OvertimeDraw		UMETA(Hidden),
	TimeExpiredDraw		UMETA(DisplayName = "Time Expired Draw"),
	SimultaneousEliminationDraw UMETA(DisplayName = "Simultaneous Elimination Draw")
};

/** Why the complete match ended. Kept separate from round results and scores. */
UENUM(BlueprintType)
enum class EMatchEndReason : uint8
{
	Unknown					UMETA(DisplayName = "Unknown"),
	ScoreVictory			UMETA(DisplayName = "Score Victory"),
	ScoreDraw				UMETA(DisplayName = "Score Draw"),
	TeamDisconnectForfeit	UMETA(DisplayName = "Team Disconnect Forfeit"),
	AllTeamsDisconnectedAbort UMETA(DisplayName = "All Teams Disconnected - Match Aborted"),
	HostLost				UMETA(DisplayName = "Host Lost - Match Aborted")
};

/**
 * One atomic, late-join-safe view of the treasure objective. Timestamps use
 * GameState's synchronized server clock so clients never run authority timers.
 */
USTRUCT(BlueprintType)
struct FTreasureRoundSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	ETreasureRoundState State = ETreasureRoundState::Disabled;

	UPROPERTY(BlueprintReadOnly)
	int32 RoundNumber = 0;

	/** 0..2 after the location is announced; 255 keeps the selection private. */
	UPROPERTY(BlueprintReadOnly)
	uint8 SiteIndex = 255;

	UPROPERTY(BlueprintReadOnly)
	FVector_NetQuantize SiteLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	uint8 RemainingGuards = 0;

	UPROPERTY(BlueprintReadOnly)
	uint8 ClaimingTeam = 255;

	UPROPERTY(BlueprintReadOnly)
	FString ClaimingPlayerName;

	UPROPERTY(BlueprintReadOnly)
	uint8 ClaimedTeam = 255;

	UPROPERTY(BlueprintReadOnly)
	float ActivationServerTime = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float ClaimEndServerTime = 0.0f;

	/** Incremented for every authority publication, including same-state updates. */
	UPROPERTY(BlueprintReadOnly)
	uint8 Revision = 0;
};

// EChestSize removed — active chests always spawn exactly two guards.

// ---- Data Table Rows ----

/** Weapon tier data — driven by DataTable */
USTRUCT(BlueprintType)
struct FWeaponTierRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EWeaponTier Tier = EWeaponTier::DefaultPistol;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EWeaponCategory Category = EWeaponCategory::Pistol;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Cost = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float BodyDamage = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float HeadshotMultiplier = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSubclassOf<AShooterWeapon> WeaponClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = 1, ClampMax = 100))
	int32 MagazineSize = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = 0, ClampMax = 1000))
	int32 ReserveAmmo = 30;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = 0, ClampMax = 1000))
	int32 MaxReserveAmmo = 30;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = 0.0f, ClampMax = 5.0f, Units = "s"))
	float RefireRate = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSubclassOf<AShooterProjectile> ProjectileClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UTexture2D> Icon = nullptr;
};

/** Armor data — driven by DataTable */
USTRUCT(BlueprintType)
struct FArmorDataRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EArmorType Type = EArmorType::Light;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Cost = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float BodyDamageReduction = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float HeadDamageReduction = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float MaxDurability = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TObjectPtr<UTexture2D> Icon = nullptr;
};

// ---- Economy Constants ----

namespace TacticalEconomy
{
	inline constexpr int32 StartingMoney = 1000;
	inline constexpr int32 MaxMoney = 9000;
	inline constexpr int32 KillRewardPlayer = 200;
	inline constexpr int32 KillRewardAI = 0;
	inline constexpr int32 ChestTeamReward = 500;
	inline constexpr float ChestClaimantHeal = 40.0f;
	inline constexpr int32 TreasureSiteCount = 1;
	inline constexpr int32 TreasureGuardCount = 2;
	inline constexpr float TreasureAnnouncementTimeRemaining = 75.0f;
	inline constexpr float TreasureActivationTimeRemaining = 70.0f;
	inline constexpr float TreasureClaimDuration = 2.0f;
	inline constexpr int32 WinRoundReward = 3000;
	inline constexpr int32 DrawRoundReward = 1700;
	inline constexpr int32 MaxLossStreak = 3;
	inline constexpr int32 LossStreakBonus[3] = { 1700, 2100, 2500 };
	inline constexpr int32 MaxRounds = 9;
	inline constexpr int32 RoundsToWin = 5;
	inline constexpr int32 MaxPlayersPerTeam = 3;
	inline constexpr float LightArmorMaxDurability = 100.0f;
	inline constexpr float LightArmorBodyReduction = 0.5f;
	inline constexpr float LightArmorHeadReduction = 0.0f;
	inline constexpr float HeavyArmorMaxDurability = 200.0f;
	inline constexpr float HeavyArmorBodyReduction = 0.5f;
	inline constexpr float HeavyArmorHeadReduction = 0.5f;
	inline constexpr float EliminationResolutionWindow = 0.15f;
	inline constexpr float HuntIntelFirstThreshold = 30.0f;
	inline constexpr float HuntIntelEscalatedThreshold = 15.0f;
	inline constexpr float HuntIntelFirstInterval = 8.0f;
	inline constexpr float HuntIntelEscalatedInterval = 4.0f;
	inline constexpr float HeadshotMultiplier = 3.0f;
	inline constexpr float WeaponDropLifetime = 30.0f;
}
