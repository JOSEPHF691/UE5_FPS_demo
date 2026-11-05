// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TacticalSpawnPoint.generated.h"

class UArrowComponent;
class UCapsuleComponent;

/** One movable, non-colliding spawn marker. Place six lobby and three per team. */
UENUM(BlueprintType)
enum class ETacticalSpawnGroup : uint8
{
	Lobby UMETA(DisplayName = "Preparation Lobby"),
	Red UMETA(DisplayName = "Red Team"),
	Blue UMETA(DisplayName = "Blue Team")
};

/**
 * The actor transform is the pawn capsule centre; its arrow is the facing direction.
 * SlotIndex is one-based. Lobby uses 1-6; Red and Blue each use 1-3.
 * This deliberately is not an APlayerStart: the engine's generic starts and
 * the ready-target anchor must not mistake these markers for one another.
 */
UCLASS(BlueprintType, Blueprintable)
class TCAMP_FPS_API ATacticalSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	ATacticalSpawnPoint();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactical|Spawn")
	ETacticalSpawnGroup Group = ETacticalSpawnGroup::Lobby;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tactical|Spawn", meta = (ClampMin = "1", ClampMax = "6"))
	int32 SlotIndex = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactical|Spawn")
	TObjectPtr<UCapsuleComponent> CapsulePreview;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactical|Spawn")
	TObjectPtr<UArrowComponent> FacingArrow;

	virtual void OnConstruction(const FTransform& Transform) override;
};
