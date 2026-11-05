// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TacticalBulletTracer.generated.h"

class UStaticMeshComponent;

/** Local-only moving streak used if the authored Niagara effect is unavailable. */
UCLASS(NotBlueprintable)
class TCAMP_FPS_API ATacticalBulletTracer : public AActor
{
	GENERATED_BODY()

public:
	ATacticalBulletTracer();
	virtual void Tick(float DeltaSeconds) override;

	void InitializePath(const FVector& InStart, const FVector& InEnd,
		bool bFirstPersonView);

private:
	UPROPERTY(VisibleAnywhere, Category = "Tracer")
	TObjectPtr<UStaticMeshComponent> StreakMesh;

	FVector Start = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	float PathLength = 0.0f;
	float TravelTime = 0.07f;
	float Age = 0.0f;
};
