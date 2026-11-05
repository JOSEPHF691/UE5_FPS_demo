// Copyright Epic Games, Inc. All Rights Reserved.

#include "Treasure/TreasureChest.h"

#include "AI/ShooterNPC.h"
#include "AI/TacticalGuardArena.h"
#include "AI/TacticalGuardSpawnPoint.h"
#include "TacticalCharacter.h"
#include "TacticalDiagnostics.h"
#include "TacticalGameMode.h"
#include "TacticalGameState.h"
#include "TacticalPlayerState.h"
#include "TacticalRules.h"
#include "Components/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ATreasureChest::ATreasureChest()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	ChestMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Chest Mesh"));
	ChestMesh->SetupAttachment(RootComponent);
	ChestMesh->SetCollisionProfileName(FName("BlockAll"));

	FallbackBodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Fallback Chest Body"));
	FallbackBodyMesh->SetupAttachment(RootComponent);
	FallbackBodyMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 30.0f));
	FallbackBodyMesh->SetRelativeScale3D(FVector(0.8f, 1.2f, 0.6f));
	FallbackBodyMesh->SetCollisionProfileName(FName("BlockAll"));

	FallbackLidHinge = CreateDefaultSubobject<USceneComponent>(TEXT("Fallback Lid Hinge"));
	FallbackLidHinge->SetupAttachment(RootComponent);
	FallbackLidHinge->SetRelativeLocation(FVector(-42.0f, 0.0f, 60.0f));

	FallbackLidMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Fallback Chest Lid"));
	FallbackLidMesh->SetupAttachment(FallbackLidHinge);
	FallbackLidMesh->SetRelativeLocation(FVector(42.0f, 0.0f, 9.0f));
	FallbackLidMesh->SetRelativeScale3D(FVector(0.86f, 1.28f, 0.18f));

	FallbackBandLeftMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Fallback Chest Band Left"));
	FallbackBandLeftMesh->SetupAttachment(RootComponent);
	FallbackBandLeftMesh->SetRelativeLocation(FVector(0.0f, -38.0f, 35.0f));
	FallbackBandLeftMesh->SetRelativeScale3D(FVector(0.84f, 0.1f, 0.7f));

	FallbackBandRightMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Fallback Chest Band Right"));
	FallbackBandRightMesh->SetupAttachment(RootComponent);
	FallbackBandRightMesh->SetRelativeLocation(FVector(0.0f, 38.0f, 35.0f));
	FallbackBandRightMesh->SetRelativeScale3D(FVector(0.84f, 0.1f, 0.7f));

	FallbackLockMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Fallback Chest Lock"));
	FallbackLockMesh->SetupAttachment(RootComponent);
	FallbackLockMesh->SetRelativeLocation(FVector(43.0f, 0.0f, 47.0f));
	FallbackLockMesh->SetRelativeScale3D(FVector(0.08f, 0.22f, 0.24f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> ChamferCubeMeshFinder(
		TEXT("/Game/LevelPrototyping/Meshes/SM_ChamferCube.SM_ChamferCube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> EngineCubeMeshFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterialFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UStaticMesh* CubeMesh = ChamferCubeMeshFinder.Succeeded()
		? ChamferCubeMeshFinder.Object
		: EngineCubeMeshFinder.Succeeded() ? EngineCubeMeshFinder.Object : nullptr;
	UMaterialInterface* BasicMaterial = BasicMaterialFinder.Succeeded()
		? BasicMaterialFinder.Object
		: nullptr;
	for (UStaticMeshComponent* Part : {
		FallbackBodyMesh,
		FallbackLidMesh,
		FallbackBandLeftMesh,
		FallbackBandRightMesh,
		FallbackLockMesh })
	{
		Part->SetStaticMesh(CubeMesh);
		Part->SetMaterial(0, BasicMaterial);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
	}
	FallbackBodyMesh->SetCollisionProfileName(FName("BlockAll"));

	InteractionZone = CreateDefaultSubobject<USphereComponent>(TEXT("Interaction Zone"));
	InteractionZone->SetupAttachment(RootComponent);
	InteractionZone->SetSphereRadius(128.0f);
	InteractionZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionZone->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionZone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void ATreasureChest::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ConfigureVisualComponents();
}

void ATreasureChest::BeginPlay()
{
	Super::BeginPlay();

	// An older Blueprint CDO still contains the legacy 2.5-second value.
	ClaimDuration = TacticalEconomy::TreasureClaimDuration;
	InteractionZone->OnComponentBeginOverlap.AddDynamic(
		this, &ATreasureChest::OnInteractionOverlap);
	InteractionZone->OnComponentEndOverlap.AddDynamic(
		this, &ATreasureChest::OnInteractionEndOverlap);
	InitializeFallbackMaterials();
	ApplyActiveState();
}

void ATreasureChest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		ReleaseClaim(false);
		DestroySpawnedGuards();
	}
	else if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ClaimTimer);
		World->GetTimerManager().ClearTimer(ClaimValidationTimer);
	}

	Super::EndPlay(EndPlayReason);
}

void ATreasureChest::PrepareForRound(int32 RoundNumber, uint8 SiteIndex)
{
	if (!HasAuthority())
	{
		return;
	}

	DeactivateForRound();
	PreparedRound = RoundNumber;
	ActiveSiteIndex = SiteIndex;
	ClaimedTeam = 255;
	bRewardCommitted = false;
	LastLooter = nullptr;
	CurrentReward = EChestReward::TeamMoneyAndHeal;
	ForceNetUpdate();
}

bool ATreasureChest::ActivateForRound()
{
	if (!HasAuthority() || PreparedRound <= 0
		|| ActiveSiteIndex >= TacticalEconomy::TreasureSiteCount)
	{
		return false;
	}

	ReleaseClaim(false);
	DestroySpawnedGuards();
	bIsActiveForRound = true;
	bIsOpenable = false;
	bIsLooted = false;
	bRewardCommitted = false;
	ClaimedTeam = 255;
	LastLooter = nullptr;
	ApplyActiveState();

	if (!SpawnGuards())
	{
		ClearInteractionOffers();
		DestroySpawnedGuards();
		bIsActiveForRound = false;
		bIsOpenable = false;
		bIsLooted = false;
		ApplyActiveState();
		ForceNetUpdate();
		UE_LOG(LogTacticalFlow, Error,
			TEXT("Event=TreasureActivationFailed Round=%d Site=%d Chest=\"%s\" Reason=RequiredGuardsNotSpawned"),
			PreparedRound, ActiveSiteIndex, *GetName());
		PublishAuthoritativeState();
		return false;
	}

	ForceNetUpdate();
	PublishAuthoritativeState();
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=TreasureActivated Round=%d Site=%d Location=%s Guards=%d"),
		PreparedRound, ActiveSiteIndex, *GetActorLocation().ToCompactString(), LivingGuards.Num());
	return true;
}

void ATreasureChest::DeactivateForRound()
{
	if (!HasAuthority())
	{
		return;
	}
	const bool bHadRoundState = PreparedRound != INDEX_NONE
		|| bIsActiveForRound || bIsOpenable || bIsLooted
		|| ClaimingCharacter != nullptr || !AllSpawnedGuards.IsEmpty();

	ReleaseClaim(true);
	ClearInteractionOffers();
	DestroySpawnedGuards();
	bIsActiveForRound = false;
	bIsOpenable = false;
	bIsLooted = false;
	CurrentReward = EChestReward::TeamMoneyAndHeal;
	ClaimedTeam = 255;
	bRewardCommitted = false;
	LastLooter = nullptr;
	PreparedRound = INDEX_NONE;
	ActiveSiteIndex = 255;
	ApplyActiveState();
	if (bHadRoundState)
	{
		BP_OnChestClosed();
	}
	ForceNetUpdate();
}

void ATreasureChest::ResetForRound(bool bActive)
{
	if (!HasAuthority())
	{
		return;
	}
	if (!bActive)
	{
		DeactivateForRound();
		return;
	}

	const ATacticalGameState* GS = GetWorld()
		? GetWorld()->GetGameState<ATacticalGameState>()
		: nullptr;
	PrepareForRound(GS ? GS->RoundNumber : 1, 0);
	ActivateForRound();
}

bool ATreasureChest::SpawnGuards()
{
	if (!HasAuthority() || !bIsActiveForRound || !GuardClass || !GetWorld())
	{
		return false;
	}

	UNavigationSystemV1* NavigationSystem =
		UNavigationSystemV1::GetCurrent(GetWorld());
	const ANavigationData* NavigationData = NavigationSystem
		? NavigationSystem->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)
		: nullptr;
	if (!NavigationSystem || !NavigationData)
	{
		UE_LOG(LogTacticalFlow, Error,
			TEXT("Event=TreasureGuardSpawnFailed Round=%d Site=%d Reason=NavigationUnavailable"),
			PreparedRound, ActiveSiteIndex);
		return false;
	}

	// Resolve every post before spawning anything. A partial pair must never be
	// left alive if one authored ground-level post is no longer on navigation.
	TArray<AActor*> AuthoredGuardActors;
	UGameplayStatics::GetAllActorsOfClass(
		GetWorld(), ATacticalGuardSpawnPoint::StaticClass(), AuthoredGuardActors);
	if (AuthoredGuardActors.Num() != TacticalEconomy::TreasureGuardCount)
	{
		UE_LOG(LogTacticalFlow, Error,
			TEXT("Event=TreasureGuardSpawnFailed Reason=GuardMarkerCount Actual=%d Expected=%d"),
			AuthoredGuardActors.Num(), TacticalEconomy::TreasureGuardCount);
		return false;
	}
	ATacticalGuardSpawnPoint* AuthoredPosts[TacticalEconomy::TreasureGuardCount] = {};
	for (AActor* Actor : AuthoredGuardActors)
	{
		ATacticalGuardSpawnPoint* Marker = Cast<ATacticalGuardSpawnPoint>(Actor);
		if (!Marker || Marker->GuardIndex < 0
			|| Marker->GuardIndex >= TacticalEconomy::TreasureGuardCount
			|| AuthoredPosts[Marker->GuardIndex])
		{
			UE_LOG(LogTacticalFlow, Error,
				TEXT("Event=TreasureGuardSpawnFailed Reason=InvalidOrDuplicateGuardIndex Marker=%s Index=%d"),
				*GetNameSafe(Marker), Marker ? Marker->GuardIndex : INDEX_NONE);
			return false;
		}
		AuthoredPosts[Marker->GuardIndex] = Marker;
	}
	const ATacticalGuardArena* Arena = ATacticalGuardArena::FindActive(GetWorld());
	if (!Arena)
	{
		UE_LOG(LogTacticalFlow, Error,
			TEXT("Event=TreasureGuardSpawnFailed Reason=ActiveArenaMissing"));
		return false;
	}
	TArray<FVector> ProjectedGuardPosts;
	ProjectedGuardPosts.Reserve(TacticalEconomy::TreasureGuardCount);
	for (int32 Index = 0; Index < TacticalEconomy::TreasureGuardCount; ++Index)
	{
		const FVector AuthoredPost = AuthoredPosts[Index]->GetActorLocation();
		FNavLocation ProjectedPost;
		const bool bProjected = NavigationSystem->ProjectPointToNavigation(
			AuthoredPost, ProjectedPost, FVector(180.0f, 180.0f, 250.0f), NavigationData);
		const bool bNearAuthoredPost = bProjected
			&& FVector::DistSquared2D(AuthoredPost, ProjectedPost.Location) <= FMath::Square(100.0f)
			&& FMath::Abs(AuthoredPost.Z - ProjectedPost.Location.Z) <= 120.0f
			&& FVector::DistSquared2D(ProjectedPost.Location, GetActorLocation())
				>= FMath::Square(400.0f)
			&& Arena->IsPointInsideWithInset(ProjectedPost.Location, 100.0f);
		if (!bNearAuthoredPost)
		{
			UE_LOG(LogTacticalFlow, Error,
				TEXT("Event=TreasureGuardSpawnFailed Round=%d Site=%d Reason=GuardPostNotOnGroundOrOutsideArena GuardIndex=%d Location=%s Projected=%d"),
				PreparedRound, ActiveSiteIndex, Index, *AuthoredPost.ToCompactString(), bProjected);
			return false;
		}
		ProjectedGuardPosts.Add(ProjectedPost.Location);
	}

	constexpr float MinimumGuardPostSeparation = 150.0f;
	for (int32 FirstIndex = 0; FirstIndex < ProjectedGuardPosts.Num(); ++FirstIndex)
	{
		for (int32 SecondIndex = FirstIndex + 1;
			SecondIndex < ProjectedGuardPosts.Num(); ++SecondIndex)
		{
			if (FVector::DistSquared2D(
				ProjectedGuardPosts[FirstIndex], ProjectedGuardPosts[SecondIndex])
				< FMath::Square(MinimumGuardPostSeparation))
			{
				UE_LOG(LogTacticalFlow, Error,
					TEXT("Event=TreasureGuardSpawnFailed Round=%d Site=%d Reason=GuardPostsOverlap First=%d Second=%d"),
					PreparedRound, ActiveSiteIndex, FirstIndex, SecondIndex);
				return false;
			}
		}
	}

	bGuardSpawnInProgress = true;
	for (int32 Index = 0; Index < TacticalEconomy::TreasureGuardCount; ++Index)
	{
		const AShooterNPC* GuardDefault = GuardClass->GetDefaultObject<AShooterNPC>();
		const float CapsuleHalfHeight = GuardDefault && GuardDefault->GetCapsuleComponent()
			? GuardDefault->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
			: 90.0f;
		const FVector SpawnLocation = ProjectedGuardPosts[Index]
			+ FVector(0.0f, 0.0f, CapsuleHalfHeight + 2.0f);
		const FTransform SpawnTransform(AuthoredPosts[Index]->GetActorRotation(), SpawnLocation);

		AShooterNPC* Guard = GetWorld()->SpawnActorDeferred<AShooterNPC>(
			GuardClass, SpawnTransform, this, nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
		if (!Guard)
		{
			bGuardSpawnInProgress = false;
			return false;
		}

		Guard->GuardedChest = this;
		Guard->GuardHomeLocation = SpawnLocation;
		// Register before FinishSpawning because Blueprint BeginPlay may
		// synchronously kill or destroy the guard and notify this chest.
		AllSpawnedGuards.Add(Guard);
		LivingGuards.Add(Guard);
		Guard->FinishSpawning(SpawnTransform);
		if (!IsValid(Guard) || Guard->IsActorBeingDestroyed()
			|| !Guard->GetHealthComponent() || Guard->GetHealthComponent()->IsDead())
		{
			bGuardSpawnInProgress = false;
			return false;
		}
	}

	bGuardSpawnInProgress = false;
	return LivingGuards.Num() == TacticalEconomy::TreasureGuardCount;
}

void ATreasureChest::DestroySpawnedGuards()
{
	for (AShooterNPC* Guard : AllSpawnedGuards)
	{
		if (IsValid(Guard) && !Guard->IsActorBeingDestroyed())
		{
			Guard->GuardedChest = nullptr;
			Guard->Destroy();
		}
	}
	AllSpawnedGuards.Empty();
	LivingGuards.Empty();
}

void ATreasureChest::NotifyGuardKilled(AShooterNPC* KilledGuard)
{
	if (!HasAuthority() || !bIsActiveForRound || bIsOpenable || !KilledGuard)
	{
		return;
	}

	if (LivingGuards.RemoveSingleSwap(KilledGuard) != 1)
	{
		return;
	}

	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=TreasureGuardKilled Round=%d Site=%d Guard=\"%s\" Remaining=%d"),
		PreparedRound, ActiveSiteIndex, *GetNameSafe(KilledGuard), LivingGuards.Num());

	if (bGuardSpawnInProgress)
	{
		// ActivateForRound will fail closed after FinishSpawning returns. Do not
		// transiently publish Guarded(0) or Unlocked from a failed activation.
		return;
	}

	if (LivingGuards.IsEmpty())
	{
		bIsOpenable = true;
		ConfigureVisualComponents();
		BP_OnChestUnlocked();
		RefreshInteractionOffers();
	}

	ForceNetUpdate();
	PublishAuthoritativeState();
}

bool ATreasureChest::BeginClaim(ATacticalCharacter* Claimant)
{
	if (!HasAuthority())
	{
		return false;
	}

	if (ClaimingCharacter)
	{
		// Reliable RPC retransmission or repeated input from the accepted claimant
		// must not restart the timer or release the authority firing lock.
		if (ClaimingCharacter == Claimant)
		{
			Claimant->SetTreasureClaimActive(true);
			return true;
		}
		if (Claimant)
		{
			Claimant->SetTreasureClaimActive(false);
		}
		return false;
	}

	if (!IsClaimantValid(Claimant))
	{
		if (Claimant)
		{
			Claimant->SetTreasureClaimActive(false);
		}
		return false;
	}

	ClaimingCharacter = Claimant;
	ClaimingPlayerState = Claimant->GetPlayerState<ATacticalPlayerState>();
	++ClaimSerial;
	const ATacticalGameState* GS = GetWorld()->GetGameState<ATacticalGameState>();
	ClaimEndServerTime = GS
		? GS->GetServerWorldTimeSeconds() + ClaimDuration
		: GetWorld()->GetTimeSeconds() + ClaimDuration;
	Claimant->SetTreasureClaimActive(true);
	BP_OnClaimStarted(Claimant, ClaimDuration);

	FTimerManager& TimerManager = GetWorld()->GetTimerManager();
	TimerManager.SetTimer(ClaimTimer, this, &ATreasureChest::CompleteClaim, ClaimDuration, false);
	TimerManager.SetTimer(
		ClaimValidationTimer, this, &ATreasureChest::ValidateActiveClaim, 0.1f, true);
	ForceNetUpdate();
	PublishAuthoritativeState();

	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=TreasureClaimStarted Round=%d Site=%d Player=\"%s\" Team=%s Duration=%.2f"),
		PreparedRound, ActiveSiteIndex,
		ClaimingPlayerState ? *ClaimingPlayerState->GetPlayerName() : *GetNameSafe(Claimant),
		TacticalDiagnostics::GetTeamName(ClaimingPlayerState ? ClaimingPlayerState->TeamByte : 255),
		ClaimDuration);
	return true;
}

void ATreasureChest::CancelClaim(ATacticalCharacter* Claimant)
{
	if (!HasAuthority() || ClaimingCharacter != Claimant)
	{
		return;
	}

	const FString PlayerName = ClaimingPlayerState
		? ClaimingPlayerState->GetPlayerName()
		: GetNameSafe(Claimant);
	ReleaseClaim(true);
	ForceNetUpdate();
	PublishAuthoritativeState();
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=TreasureClaimCancelled Round=%d Site=%d Player=\"%s\""),
		PreparedRound, ActiveSiteIndex, *PlayerName);
}

void ATreasureChest::ValidateActiveClaim()
{
	if (ClaimingCharacter && !IsClaimantValid(ClaimingCharacter))
	{
		CancelClaim(ClaimingCharacter);
	}
}

void ATreasureChest::ReleaseClaim(bool bNotifyBlueprint)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ClaimTimer);
		World->GetTimerManager().ClearTimer(ClaimValidationTimer);
	}

	ATacticalCharacter* PreviousClaimant = ClaimingCharacter;
	ClaimingCharacter = nullptr;
	ClaimingPlayerState = nullptr;
	ClaimEndServerTime = 0.0f;
	if (PreviousClaimant)
	{
		PreviousClaimant->SetTreasureClaimActive(false);
		if (bNotifyBlueprint)
		{
			BP_OnClaimCancelled(PreviousClaimant);
		}
	}
}

void ATreasureChest::CompleteClaim()
{
	ATacticalCharacter* Claimant = ClaimingCharacter;
	if (!IsClaimantValid(Claimant))
	{
		CancelClaim(Claimant);
		return;
	}

	ApplyReward(Claimant);
}

bool ATreasureChest::IsClaimantValid(const ATacticalCharacter* Claimant) const
{
	if (!Claimant || Claimant->IsDead() || !bIsActiveForRound || !bIsOpenable
		|| bIsLooted || bRewardCommitted || !InteractionZone->IsOverlappingActor(Claimant))
	{
		return false;
	}

	const ATacticalGameState* GS = GetWorld()
		? GetWorld()->GetGameState<ATacticalGameState>()
		: nullptr;
	const ATacticalPlayerState* TPS = Claimant->GetPlayerState<ATacticalPlayerState>();
	return GS && TPS && TacticalRules::CanClaimTreasure(
		bIsActiveForRound,
		bIsOpenable,
		bIsLooted || bRewardCommitted,
		!Claimant->IsDead(),
		InteractionZone->IsOverlappingActor(Claimant),
		TPS->bIsActiveParticipant,
		TPS->TeamByte,
		GS->CurrentPhase,
		PreparedRound,
		GS->RoundNumber);
}

void ATreasureChest::ApplyReward(AActor* Looter)
{
	ATacticalCharacter* Character = Cast<ATacticalCharacter>(Looter);
	ATacticalPlayerState* ClaimantPS = Character
		? Character->GetPlayerState<ATacticalPlayerState>()
		: nullptr;
	ATacticalGameMode* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<ATacticalGameMode>()
		: nullptr;
	if (!Character || !ClaimantPS || !GameMode || bRewardCommitted || bIsLooted)
	{
		ReleaseClaim(true);
		return;
	}

	const TWeakObjectPtr<ATacticalCharacter> ClaimantWeak(Character);
	const FString ClaimantName = ClaimantPS->GetPlayerName();
	const uint8 Team = ClaimantPS->TeamByte;

	// Commit before AddMoney or Blueprint delegates can synchronously re-enter.
	bRewardCommitted = true;
	LastLooter = Character;
	++LootSerial;
	bIsLooted = true;
	ClaimedTeam = Team;
	ReleaseClaim(false);
	ClearInteractionOffers();
	ForceNetUpdate();
	PublishAuthoritativeState();

	const int32 RewardedPlayers = GameMode->CommitTreasureTeamReward(Team);
	float HealthBefore = 0.0f;
	float HealthAfter = 0.0f;
	if (ATacticalCharacter* LiveClaimant = ClaimantWeak.Get();
		IsValid(LiveClaimant) && !LiveClaimant->IsDead())
	{
		if (UHealthComponent* Health = LiveClaimant->GetHealthComponent())
		{
			HealthBefore = Health->CurrentHP;
			Health->Heal(TacticalEconomy::ChestClaimantHeal);
			HealthAfter = Health->CurrentHP;
		}
	}

	BP_OnChestLooted(Character, CurrentReward);
	UE_LOG(LogTacticalFlow, Display,
		TEXT("Event=TreasureClaimCompleted Round=%d Site=%d Player=\"%s\" Team=%s RewardedPlayers=%d TeamReward=%d Health=%.1f->%.1f ScoreEffect=0"),
		PreparedRound, ActiveSiteIndex, *ClaimantName,
		TacticalDiagnostics::GetTeamName(Team), RewardedPlayers,
		TacticalEconomy::ChestTeamReward, HealthBefore, HealthAfter);
}

void ATreasureChest::OnInteractionOverlap(
	UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!bIsActiveForRound || !bIsOpenable || bIsLooted)
	{
		return;
	}

	if (ATacticalCharacter* Character = Cast<ATacticalCharacter>(Other);
		Character && !Character->IsDead())
	{
		Character->SetNearbyTreasureChest(this);
	}
}

void ATreasureChest::OnInteractionEndOverlap(
	UPrimitiveComponent* OverlappedComp, AActor* Other,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (ATacticalCharacter* Character = Cast<ATacticalCharacter>(Other))
	{
		if (HasAuthority())
		{
			CancelClaim(Character);
		}
		Character->ClearNearbyTreasureChest(this);
	}
}

void ATreasureChest::RefreshInteractionOffers()
{
	if (!bIsActiveForRound || !bIsOpenable || bIsLooted)
	{
		return;
	}

	TArray<AActor*> OverlappingActors;
	InteractionZone->GetOverlappingActors(OverlappingActors, ATacticalCharacter::StaticClass());
	for (AActor* Actor : OverlappingActors)
	{
		if (ATacticalCharacter* Character = Cast<ATacticalCharacter>(Actor);
			Character && !Character->IsDead())
		{
			Character->SetNearbyTreasureChest(this);
		}
	}
}

void ATreasureChest::ClearInteractionOffers()
{
	TArray<AActor*> OverlappingActors;
	InteractionZone->GetOverlappingActors(OverlappingActors, ATacticalCharacter::StaticClass());
	for (AActor* Actor : OverlappingActors)
	{
		if (ATacticalCharacter* Character = Cast<ATacticalCharacter>(Actor))
		{
			Character->ClearNearbyTreasureChest(this);
		}
	}
}

void ATreasureChest::ApplyActiveState()
{
	ConfigureVisualComponents();
	SetActorHiddenInGame(!bIsActiveForRound);
	SetActorEnableCollision(bIsActiveForRound);
	InteractionZone->SetCollisionEnabled(
		bIsActiveForRound ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}

void ATreasureChest::ConfigureVisualComponents()
{
	if (!ChestMesh || !FallbackBodyMesh || !FallbackLidHinge || !FallbackLidMesh
		|| !FallbackBandLeftMesh || !FallbackBandRightMesh || !FallbackLockMesh)
	{
		return;
	}

	const bool bUseFallback = ChestMesh->GetStaticMesh() == nullptr;
	ChestMesh->SetVisibility(!bUseFallback, true);
	ChestMesh->SetCollisionEnabled(
		bUseFallback ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);

	for (UStaticMeshComponent* Part : {
		FallbackBodyMesh,
		FallbackLidMesh,
		FallbackBandLeftMesh,
		FallbackBandRightMesh })
	{
		Part->SetVisibility(bUseFallback, true);
		Part->SetHiddenInGame(!bUseFallback);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	FallbackBodyMesh->SetCollisionEnabled(
		bUseFallback ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);

	const bool bOpen = bIsOpenable || bIsLooted;
	FallbackLidHinge->SetRelativeRotation(
		bOpen ? FRotator(65.0f, 0.0f, 0.0f) : FRotator::ZeroRotator);
	FallbackLockMesh->SetVisibility(bUseFallback && !bOpen, true);
	FallbackLockMesh->SetHiddenInGame(!bUseFallback || bOpen);
	FallbackLockMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ATreasureChest::InitializeFallbackMaterials()
{
	if (!ChestMesh || ChestMesh->GetStaticMesh() != nullptr || !FallbackBodyMesh)
	{
		return;
	}

	UMaterialInterface* ParentMaterial = FallbackBodyMesh->GetMaterial(0);
	if (!ParentMaterial)
	{
		return;
	}

	if (!FallbackWoodMaterial)
	{
		FallbackWoodMaterial = UMaterialInstanceDynamic::Create(ParentMaterial, this);
		FallbackWoodMaterial->SetVectorParameterValue(
			TEXT("Color"), FLinearColor(0.22f, 0.055f, 0.012f, 1.0f));
	}
	if (!FallbackMetalMaterial)
	{
		FallbackMetalMaterial = UMaterialInstanceDynamic::Create(ParentMaterial, this);
		FallbackMetalMaterial->SetVectorParameterValue(
			TEXT("Color"), FLinearColor(0.95f, 0.42f, 0.035f, 1.0f));
	}

	FallbackBodyMesh->SetMaterial(0, FallbackWoodMaterial);
	FallbackLidMesh->SetMaterial(0, FallbackWoodMaterial);
	FallbackBandLeftMesh->SetMaterial(0, FallbackMetalMaterial);
	FallbackBandRightMesh->SetMaterial(0, FallbackMetalMaterial);
	FallbackLockMesh->SetMaterial(0, FallbackMetalMaterial);
}

void ATreasureChest::PublishAuthoritativeState()
{
	if (!HasAuthority())
	{
		return;
	}
	if (ATacticalGameMode* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<ATacticalGameMode>()
		: nullptr)
	{
		GameMode->NotifyTreasureChestStateChanged(this);
	}
}

void ATreasureChest::OnRep_ActiveForRound()
{
	ApplyActiveState();
	if (!bIsActiveForRound)
	{
		BP_OnChestClosed();
	}
}

void ATreasureChest::OnRep_Openable()
{
	ConfigureVisualComponents();
	if (bIsOpenable && !bIsLooted)
	{
		BP_OnChestUnlocked();
		RefreshInteractionOffers();
	}
	else
	{
		ClearInteractionOffers();
	}
}

void ATreasureChest::OnRep_Looted()
{
	ConfigureVisualComponents();
	if (!bIsLooted)
	{
		bLootPresentationActive = false;
		PresentedLootSerial = 0;
		return;
	}

	ClearInteractionOffers();
	// If this actor reference is still waiting on its NetGUID, its own RepNotify
	// calls us again after resolution. Do not permanently consume the serial yet.
	if (LastLooter
		&& (!bLootPresentationActive || PresentedLootSerial != LootSerial))
	{
		bLootPresentationActive = true;
		PresentedLootSerial = LootSerial;
		BP_OnChestLooted(LastLooter, CurrentReward);
	}
}

void ATreasureChest::OnRep_ClaimState()
{
	// GameState owns the authoritative HUD. Mirror the optional actor-local
	// Blueprint events exactly once so remote clients receive chest VFX as well.
	if (ClaimingCharacter && ClaimEndServerTime > 0.0f)
	{
		if (!bClaimPresentationActive || PresentedClaimSerial != ClaimSerial)
		{
			if (bClaimPresentationActive)
			{
				if (ATacticalCharacter* Previous = PresentedClaimant.Get())
				{
					BP_OnClaimCancelled(Previous);
				}
			}
			bClaimPresentationActive = true;
			PresentedClaimSerial = ClaimSerial;
			PresentedClaimant = ClaimingCharacter;
			const ATacticalGameState* GS = GetWorld()
				? GetWorld()->GetGameState<ATacticalGameState>()
				: nullptr;
			const float RemainingDuration = GS
				? FMath::Max(0.0f,
					ClaimEndServerTime - GS->GetServerWorldTimeSeconds())
				: ClaimDuration;
			BP_OnClaimStarted(ClaimingCharacter, RemainingDuration);
		}
		return;
	}

	if (bClaimPresentationActive)
	{
		ATacticalCharacter* Previous = PresentedClaimant.Get();
		bClaimPresentationActive = false;
		PresentedClaimant.Reset();
		if (!bIsLooted && Previous)
		{
			BP_OnClaimCancelled(Previous);
		}
	}
}

void ATreasureChest::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATreasureChest, bIsActiveForRound);
	DOREPLIFETIME(ATreasureChest, bIsOpenable);
	DOREPLIFETIME(ATreasureChest, bIsLooted);
	DOREPLIFETIME(ATreasureChest, CurrentReward);
	DOREPLIFETIME(ATreasureChest, ClaimingPlayerState);
	DOREPLIFETIME(ATreasureChest, ClaimingCharacter);
	DOREPLIFETIME(ATreasureChest, ClaimEndServerTime);
	DOREPLIFETIME(ATreasureChest, ClaimSerial);
	DOREPLIFETIME(ATreasureChest, LastLooter);
	DOREPLIFETIME(ATreasureChest, LootSerial);
}
