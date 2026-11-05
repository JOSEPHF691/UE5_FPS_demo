// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TacticalGuardArena.generated.h"

class UMaterialInterface;
class USceneComponent;
class UStaticMeshComponent;

/**
 * Authored rectangular treasure arena. Its visible side panels are non-colliding;
 * bullets use the four finite side faces as a logical barrier instead of mesh
 * collision. The top and bottom are open, and pawns may cross the panels.
 */
UCLASS(Blueprintable)
class TCAMP_FPS_API ATacticalGuardArena : public AActor
{
	GENERATED_BODY()

public:
	ATacticalGuardArena();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Find the authored arena in this world. A level should contain exactly one. */
	static ATacticalGuardArena* Find(const UWorld* World);
	static ATacticalGuardArena* FindActive(const UWorld* World);

	UFUNCTION(BlueprintPure, Category="Treasure|Arena")
	bool IsArenaActive() const { return bArenaActive; }

	/** Includes the authored side-wall height, in the actor's rotated local frame. */
	UFUNCTION(BlueprintPure, Category="Treasure|Arena")
	bool IsPointInside(const FVector& WorldPoint) const;

	/** Inset is used for guard capsule clearance from the wall. */
	bool IsPointInsideWithInset(const FVector& WorldPoint, float Inset) const;

	/** True if an active barrier cuts the segment, including outside-to-outside shots. */
	bool DoesSegmentCrossBarrier(const FVector& Start, const FVector& End) const;

	/** Returns the first contacted side face along the segment and its outward normal. */
	bool FindFirstBarrierIntersection(const FVector& Start, const FVector& End,
		FVector& OutPoint, FVector& OutNormal) const;

	/** Pure geometry form, convenient for deterministic tests. */
	static bool FindBoxBoundaryIntersection(const FTransform& BoxTransform,
		const FVector& BoxHalfExtent, const FVector& Start, const FVector& End,
		FVector& OutPoint, FVector& OutNormal);

	void SetArenaActive(bool bNewActive);

	FVector GetArenaHalfExtent() const { return HalfExtent; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Treasure|Arena")
	TObjectPtr<USceneComponent> ArenaRoot;

	/** Drag, rotate and resize this volume to match the guard navigation square. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Treasure|Arena", meta=(ClampMin="200.0"))
	FVector HalfExtent = FVector(1700.0f, 1750.0f, 420.0f);

	/** Legacy width of the central visual panel on the local Y faces. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Treasure|Arena", meta=(ClampMin="150.0"))
	float EntranceWidth = 500.0f;

	/** Light translucent material for the four side panels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Treasure|Arena")
	TObjectPtr<UMaterialInterface> BarrierMaterial;

	UPROPERTY(ReplicatedUsing=OnRep_ArenaActive, VisibleInstanceOnly, BlueprintReadOnly,
		Category="Treasure|Arena")
	bool bArenaActive = false;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> SolidWalls;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> EntranceFields;

	/** Legacy components retained so existing placed actors load; never shown or collidable. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Roof;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Floor;

	UFUNCTION()
	void OnRep_ArenaActive();

private:
	void UpdateGeometry();
	void UpdateVisibilityAndCollision();
};
