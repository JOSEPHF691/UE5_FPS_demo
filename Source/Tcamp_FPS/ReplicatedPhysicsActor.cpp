// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReplicatedPhysicsActor.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

AReplicatedPhysicsActor::AReplicatedPhysicsActor()
{
	bReplicates = true;
	SetReplicateMovement(true);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetSimulatePhysics(true);
	Mesh->SetIsReplicated(true);
}
