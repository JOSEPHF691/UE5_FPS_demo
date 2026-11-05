// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TacticalGuardSpawnPoint.generated.h"

class UArrowComponent;
class UCapsuleComponent;
class USceneComponent;

/** Drag one marker per guard onto ground-level navigation around the chest. */
UCLASS(Blueprintable)
class TCAMP_FPS_API ATacticalGuardSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	ATacticalGuardSpawnPoint();

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Treasure|Guard", meta=(ClampMin="0", ClampMax="1"))
	int32 GuardIndex = 0;

private:
	UPROPERTY(VisibleAnywhere, Category="Treasure|Guard")
	TObjectPtr<USceneComponent> MarkerRoot;

	UPROPERTY(VisibleAnywhere, Category="Treasure|Guard")
	TObjectPtr<UArrowComponent> FacingArrow;

	UPROPERTY(VisibleAnywhere, Category="Treasure|Guard")
	TObjectPtr<UCapsuleComponent> GuardCapsulePreview;
};
