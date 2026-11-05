// Copyright Epic Games, Inc. All Rights Reserved.

#include "TacticalReadyTarget.h"
#include "TacticalGameMode.h"
#include "TacticalGameState.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ATacticalReadyTarget::ATacticalReadyTarget()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetCanBeDamaged(true);

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);

	TargetMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ready Target Mesh"));
	TargetMesh->SetupAttachment(SceneRoot);
	TargetMesh->SetRelativeScale3D(FVector(0.12f, 0.9f, 0.9f));
	TargetMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TargetMesh->SetCollisionResponseToAllChannels(ECR_Block);
	TargetMesh->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		TargetMesh->SetStaticMesh(CubeMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BaseMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BaseMaterial.Succeeded())
	{
		TargetMesh->SetMaterial(0, BaseMaterial.Object);
	}

	TargetLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Ready Target Label"));
	TargetLabel->SetupAttachment(SceneRoot);
	TargetLabel->SetRelativeLocation(FVector(8.0f, 0.0f, 0.0f));
	TargetLabel->SetRelativeRotation(FRotator(0.0f, 90.0f, 90.0f));
	TargetLabel->SetHorizontalAlignment(EHTA_Center);
	TargetLabel->SetVerticalAlignment(EVRTA_TextCenter);
	TargetLabel->SetWorldSize(24.0f);
	TargetLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ATacticalReadyTarget::BeginPlay()
{
	Super::BeginPlay();
	ApplyTeamVisual();
}

void ATacticalReadyTarget::InitializeForTeam(uint8 InTeamByte)
{
	if (!HasAuthority())
	{
		return;
	}

	TeamByte = FMath::Min<uint8>(InTeamByte, 1);
	ApplyTeamVisual();
}

float ATacticalReadyTarget::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	if (!HasAuthority() || !EventInstigator)
	{
		return 0.0f;
	}

	const ATacticalGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATacticalGameState>() : nullptr;
	if (!GS || GS->CurrentPhase != ERoundPhase::PreRound)
	{
		return 0.0f;
	}

	if (ATacticalGameMode* GM = GetWorld()->GetAuthGameMode<ATacticalGameMode>())
	{
		return GM->TrySelectTeamAndReady(Cast<APlayerController>(EventInstigator), TeamByte)
			? FMath::Max(1.0f, DamageAmount)
			: 0.0f;
	}

	return 0.0f;
}

void ATacticalReadyTarget::OnRep_Team()
{
	ApplyTeamVisual();
}

void ATacticalReadyTarget::ApplyTeamVisual()
{
	const FLinearColor TeamColor = TeamByte == 0
		? FLinearColor(0.85f, 0.03f, 0.03f)
		: FLinearColor(0.03f, 0.15f, 0.9f);

	if (TargetMesh)
	{
		if (UMaterialInstanceDynamic* Material = TargetMesh->CreateAndSetMaterialInstanceDynamic(0))
		{
			Material->SetVectorParameterValue(TEXT("Color"), TeamColor);
		}
	}

	if (TargetLabel)
	{
		TargetLabel->SetText(TeamByte == 0
			? NSLOCTEXT("TacticalReadyTarget", "JoinRed", "JOIN RED")
			: NSLOCTEXT("TacticalReadyTarget", "JoinBlue", "JOIN BLUE"));
		TargetLabel->SetTextRenderColor(TeamByte == 0
			? FColor(255, 40, 40)
			: FColor(40, 100, 255));
	}
}

void ATacticalReadyTarget::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATacticalReadyTarget, TeamByte);
}
