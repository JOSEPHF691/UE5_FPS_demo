// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ReplicatedPhysicsActor.generated.h"

class UStaticMeshComponent;

/**
 *  A static mesh actor whose physics movement replicates to all clients.
 *  Use this for boxes, barrels, etc. that should move when shot.
 */
UCLASS()
class TCAMP_FPS_API AReplicatedPhysicsActor : public AActor
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* Mesh;

public:
	AReplicatedPhysicsActor();
};
