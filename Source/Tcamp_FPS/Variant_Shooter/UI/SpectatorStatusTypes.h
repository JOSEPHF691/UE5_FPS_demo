// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Economy/EconomyTypes.h"
#include "SpectatorStatusTypes.generated.h"

/**
 * Server-authored data required by the local spectator HUD.
 *
 * This deliberately contains values instead of actor pointers so it is safe to
 * pass through an RPC even while the observed pawn is changing or being torn
 * down. TargetIndex is zero-based; the widget converts it to a player-facing
 * one-based number.
 */
USTRUCT(BlueprintType)
struct TCAMP_FPS_API FSpectatorHudSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Spectator")
	bool bIsValid = false;

	UPROPERTY(BlueprintReadWrite, Category = "Spectator")
	int32 TargetPlayerId = INDEX_NONE;

	UPROPERTY(BlueprintReadWrite, Category = "Spectator")
	FString TargetPlayerName;

	/** Red = 0, blue = 1, unset/unknown = 255. */
	UPROPERTY(BlueprintReadWrite, Category = "Spectator")
	uint8 TeamByte = 255;

	/** Zero-based position in the viewer's valid teammate list. */
	UPROPERTY(BlueprintReadWrite, Category = "Spectator")
	int32 TargetIndex = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Spectator")
	int32 TargetCount = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Spectator")
	float CurrentHealth = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Spectator")
	float MaxHealth = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Spectator")
	EArmorType ArmorType = EArmorType::None;

	UPROPERTY(BlueprintReadWrite, Category = "Spectator")
	float CurrentArmor = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Spectator")
	float MaxArmor = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Spectator")
	bool bHasWeapon = false;

	UPROPERTY(BlueprintReadWrite, Category = "Spectator")
	EWeaponTier WeaponTier = EWeaponTier::DefaultPistol;

	UPROPERTY(BlueprintReadWrite, Category = "Spectator")
	int32 CurrentMagazineAmmo = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Spectator")
	int32 MagazineCapacity = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Spectator")
	int32 ReserveAmmo = 0;

	/** Monotonic value used by the receiver to reject stale snapshots. */
	UPROPERTY(BlueprintReadWrite, Category = "Spectator")
	int32 Revision = 0;
};

