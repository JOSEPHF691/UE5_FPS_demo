// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/AI/ShooterAIController.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/AI/TreasureGuardRules.h"
#include "Variant_Shooter/AI/TacticalGuardArena.h"
#include "Variant_Shooter/TacticalCharacter.h"
#include "Variant_Shooter/TacticalGameState.h"
#include "Variant_Shooter/TacticalPlayerState.h"
#include "Variant_Shooter/Treasure/TreasureChest.h"
#include "Components/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StateTreeAIComponent.h"
#include "StateTreeReference.h"
#include "StateTree.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Sight.h"
#include "Navigation/PathFollowingComponent.h"
#include "AI/Navigation/PathFollowingAgentInterface.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"

AShooterAIController::AShooterAIController()
{
	// create the StateTree component
	StateTreeAI = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTreeAI"));
	StateTreeAI->SetStartLogicAutomatically(false);

	// create the AI perception component. It will be configured in BP
	AIPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));

	// subscribe to the AI perception delegates
	AIPerception->OnTargetPerceptionUpdated.AddDynamic(this, &AShooterAIController::OnPerceptionUpdated);
	AIPerception->OnTargetPerceptionForgotten.AddDynamic(this, &AShooterAIController::OnPerceptionForgotten);

	// sentinel so the first diagnostic tick reports the initial move status as a change
	LastMoveStatus = static_cast<EPathFollowingStatus::Type>(255);
}

void AShooterAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	AShooterNPC* NPC = Cast<AShooterNPC>(InPawn);
	if (!NPC)
	{
		return;
	}

	NPC->Tags.AddUnique(TeamTag);
	SetGenericTeamId(FGenericTeamId(1));
	NPC->OnPawnDeath.AddUniqueDynamic(this, &AShooterAIController::OnPawnDeath);

	bUseTreasureGuardBrain = IsValid(NPC->GuardedChest);
	ConfigurePerception(bUseTreasureGuardBrain);

	if (bEnableDiagnostics)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[AI-DEBUG] OnPossess: NPC=%s TeamId=%d GuardHome=%s DedicatedTreasureBrain=%d"),
			*NPC->GetName(), GetGenericTeamId().GetId(),
			*NPC->GuardHomeLocation.ToCompactString(), bUseTreasureGuardBrain);
	}

	if (bUseTreasureGuardBrain)
	{
		// Treasure guards have one authoritative movement owner. Starting the
		// generic roam/sniper StateTree here would reintroduce MoveTo contention.
		StartTreasureGuardBrain();
	}
	else
	{
		StateTreeAI->StartLogic();
		StartLeashCheck();
	}

	StartDiagnostics();
}

void AShooterAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopTreasureGuardBrain();
	ClearLeashCheck();
	ClearDiagnostics();
	Super::EndPlay(EndPlayReason);
}

void AShooterAIController::OnUnPossess()
{
	if (AShooterNPC* OutgoingNPC = Cast<AShooterNPC>(GetPawn()))
	{
		OutgoingNPC->OnPawnDeath.RemoveDynamic(this, &AShooterAIController::OnPawnDeath);
	}
	if (StateTreeAI && StateTreeAI->IsRunning())
	{
		StateTreeAI->StopLogic(TEXT("Controller unpossessed pawn"));
	}
	StopTreasureGuardBrain();
	ClearLeashCheck();
	ClearDiagnostics();
	StopTreasureGuardCombat();
	ClearCurrentTarget();
	Super::OnUnPossess();
}

void AShooterAIController::OnPawnDeath()
{
	// Stop the generic behaviour owner before aborting navigation so it cannot
	// react to the completion callback and enqueue teardown-time movement.
	if (StateTreeAI && StateTreeAI->IsRunning())
	{
		StateTreeAI->StopLogic(TEXT("Pawn died"));
	}
	StopTreasureGuardBrain();
	ClearLeashCheck();
	ClearDiagnostics();
	StopTreasureGuardCombat();
	if (UPathFollowingComponent* PathFollowing = GetPathFollowingComponent();
		PathFollowing && PathFollowing->GetStatus() != EPathFollowingStatus::Idle)
	{
		PathFollowing->AbortMove(*this, FPathFollowingResultFlags::UserAbort);
	}

	// clean up any StateTree-bound perception delegates so they don't fire on a dead pawn
	OnShooterPerceptionUpdated.Unbind();
	OnShooterPerceptionForgotten.Unbind();

	// unpossess the pawn
	UnPossess();

	// destroy this controller
	Destroy();
}

void AShooterAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);

	if (bEnableDiagnostics)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[AI-DEBUG] MoveCompleted RequestID=%u Success=%d Code=%d Flags=0x%X Pawn=%s Tracked=%d"),
			RequestID.GetID(), Result.IsSuccess(), static_cast<int32>(Result.Code),
			static_cast<int32>(Result.Flags), *GetNameSafe(GetPawn()),
			RequestID == ActiveTreasureGuardMoveId);
	}

	if (bUseTreasureGuardBrain)
	{
		if (RequestID != ActiveTreasureGuardMoveId)
		{
			return;
		}

		const ETreasureGuardMovePurpose CompletedPurpose = ActiveTreasureGuardMovePurpose;
		ActiveTreasureGuardMoveId = FAIRequestID::InvalidRequest;
		ActiveTreasureGuardMovePurpose = ETreasureGuardMovePurpose::None;
		ActiveTreasureGuardDestination = FVector::ZeroVector;

		if (CompletedPurpose == ETreasureGuardMovePurpose::ReturnHome)
		{
			FVector HomeLocation;
			const APawn* MyPawn = GetPawn();
			const float DistanceToHome = GetGuardHomeLocation(HomeLocation) && MyPawn
				? FVector::Dist2D(MyPawn->GetActorLocation(), HomeLocation)
				: BIG_NUMBER;
			if (TreasureGuardRules::CanCompleteReturn(
				true, DistanceToHome, TreasureGuardReturnCompleteRadius))
			{
				bLeashed = false;
				SetTreasureGuardState(ETreasureGuardState::Guarding);
			}
			else
			{
				// Preserve return ownership and observe the full request cooldown
				// after a failed path instead of entering a retry loop.
				bLeashed = true;
				LastTreasureGuardMoveRequestTime = GetWorld()
					? GetWorld()->GetTimeSeconds()
					: LastTreasureGuardMoveRequestTime;
			}
		}
		return;
	}

	// Legacy non-treasure leash completion is conservative: an unrelated or
	// aborted StateTree move can no longer release the leash.
	if (bLeashed && Result.IsSuccess())
	{
		FVector HomeLocation;
		if (const APawn* MyPawn = GetPawn(); GetGuardHomeLocation(HomeLocation) && MyPawn
			&& FVector::DistSquared2D(MyPawn->GetActorLocation(), HomeLocation)
				<= FMath::Square(TreasureGuardReturnCompleteRadius))
		{
			bLeashed = false;
		}
	}
}

void AShooterAIController::SetCurrentTarget(AActor* Target)
{
	TargetEnemy = IsValidGuardTarget(Target) ? Target : nullptr;
}

void AShooterAIController::ClearCurrentTarget()
{
	TargetEnemy = nullptr;
}

void AShooterAIController::OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (bUseTreasureGuardBrain)
	{
		// The dedicated brain rebuilds and scores the current sight candidates.
		// Perception event order must never make the last stimulus win.
		return;
	}

	// Directly manage the target on the controller, so the StateTree SenseEnemies
	// task can read GetCurrentTarget() every tick — this avoids relying on the
	// StateTree instance data being writable from a perception event lambda.
	if (Stimulus.WasSuccessfullySensed() && IsValidGuardTarget(Actor))
	{
		SetCurrentTarget(Actor);
	}
	else if (Actor == GetCurrentTarget())
	{
		ClearCurrentTarget();
	}

	// pass the data to the StateTree delegate hook
	OnShooterPerceptionUpdated.ExecuteIfBound(Actor, Stimulus);
}

bool AShooterAIController::IsValidGuardTarget(const AActor* Candidate)
{
	const ATacticalCharacter* Character = Cast<ATacticalCharacter>(Candidate);
	if (!Character || Character->IsDead())
	{
		return false;
	}

	const ATacticalPlayerState* PlayerState =
		Character->GetPlayerState<ATacticalPlayerState>();
	const ATacticalGameState* GameState = Candidate && Candidate->GetWorld()
		? Candidate->GetWorld()->GetGameState<ATacticalGameState>()
		: nullptr;
	return PlayerState && PlayerState->bIsActiveParticipant
		&& PlayerState->TeamByte <= 1
		&& GameState && GameState->CurrentPhase == ERoundPhase::Combat;
}

void AShooterAIController::OnPerceptionForgotten(AActor* Actor)
{
	if (bUseTreasureGuardBrain)
	{
		return;
	}

	// Suppress perception events while leashed
	if (bLeashed)
	{
		return;
	}

	// pass the data to the StateTree delegate hook
	OnShooterPerceptionForgotten.ExecuteIfBound(Actor);
}

void AShooterAIController::ConfigurePerception(const bool bForTreasureGuard)
{
	if (!AIPerception)
	{
		return;
	}

	const FAISenseID SightID = UAISense::GetSenseID<UAISense_Sight>();
	UAISenseConfig_Sight* SightConfig = Cast<UAISenseConfig_Sight>(
		const_cast<UAISenseConfig*>(AIPerception->GetSenseConfig(SightID)));
	if (!SightConfig)
	{
		SightConfig = NewObject<UAISenseConfig_Sight>(this, UAISenseConfig_Sight::StaticClass());
	}

	SightConfig->SightRadius = bForTreasureGuard
		? TreasureGuardThreatRadius + TreasureGuardMovementRadius
		: 3000.0f;
	SightConfig->LoseSightRadius = bForTreasureGuard
		? TreasureGuardThreatRadius + TreasureGuardMovementRadius + 300.0f
		: 3500.0f;
	SightConfig->PeripheralVisionAngleDegrees = bForTreasureGuard ? 120.0f : 90.0f;
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;
	AIPerception->ConfigureSense(*SightConfig);
	AIPerception->SetDominantSense(SightConfig->GetSenseImplementation());
	AIPerception->RequestStimuliListenerUpdate();
}

void AShooterAIController::StartTreasureGuardBrain()
{
	if (!HasAuthority() || !bUseTreasureGuardBrain || !GetWorld())
	{
		return;
	}

	ClearLeashCheck();
	if (StateTreeAI && StateTreeAI->IsRunning())
	{
		StateTreeAI->StopLogic(TEXT("Dedicated treasure guard brain owns behaviour"));
	}

	bLeashed = false;
	bTreasureGuardRetreating = false;
	ActiveTreasureGuardMoveId = FAIRequestID::InvalidRequest;
	ActiveTreasureGuardMovePurpose = ETreasureGuardMovePurpose::None;
	LastTreasureGuardPlanPurpose = ETreasureGuardMovePurpose::None;
	LastTreasureGuardMoveRequestTime = -BIG_NUMBER;
	LastTreasureGuardPlanTime = -BIG_NUMBER;
	LastTreasureGuardPlanTarget.Reset();
	TreasureGuardCandidateBatchCursor = 0;
	LastTreasureGuardTargetSeenTime = -BIG_NUMBER;
	SetTreasureGuardState(ETreasureGuardState::Guarding);

	GetWorld()->GetTimerManager().SetTimer(
		TreasureGuardDecisionTimer,
		this,
		&AShooterAIController::TickTreasureGuardBrain,
		TreasureGuardDecisionInterval,
		true,
		0.0f);
}

void AShooterAIController::StopTreasureGuardBrain()
{
	const bool bOwnedDedicatedBehaviour = bUseTreasureGuardBrain
		|| ActiveTreasureGuardMoveId.IsValid()
		|| TreasureGuardDecisionTimer.IsValid();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TreasureGuardDecisionTimer);
	}
	if (bOwnedDedicatedBehaviour)
	{
		StopTreasureGuardCombat();
		StopActiveTreasureGuardMove();
		ClearCurrentTarget();
	}
	bLeashed = false;
	bTreasureGuardRetreating = false;
	SetTreasureGuardState(ETreasureGuardState::Inactive);
}

void AShooterAIController::TickTreasureGuardBrain()
{
	AShooterNPC* NPC = Cast<AShooterNPC>(GetPawn());
	if (!HasAuthority() || !bUseTreasureGuardBrain || !IsValid(NPC)
		|| !IsValid(NPC->GuardedChest) || !NPC->GetHealthComponent()
		|| NPC->GetHealthComponent()->IsDead())
	{
		StopTreasureGuardBrain();
		return;
	}

	FVector GuardAnchor;
	FVector GuardHome;
	if (!GetTreasureGuardAnchor(GuardAnchor) || !GetGuardHomeLocation(GuardHome))
	{
		StopTreasureGuardCombat();
		return;
	}

	const ATacticalGameState* GameState = GetWorld()
		? GetWorld()->GetGameState<ATacticalGameState>()
		: nullptr;
	const bool bCombatActive = GameState && GameState->CurrentPhase == ERoundPhase::Combat;
	const float DistanceFromAnchor = FVector::Dist2D(NPC->GetActorLocation(), GuardAnchor);
	const float DistanceFromHome = FVector::Dist2D(NPC->GetActorLocation(), GuardHome);

	if (!bCombatActive)
	{
		ClearCurrentTarget();
		StopTreasureGuardCombat();
		bTreasureGuardRetreating = false;
		if (DistanceFromHome > TreasureGuardReturnCompleteRadius)
		{
			bLeashed = true;
			SetTreasureGuardState(ETreasureGuardState::ReturningHome);
			IssueTreasureGuardMove(GuardHome, ETreasureGuardMovePurpose::ReturnHome);
		}
		else
		{
			bLeashed = false;
			StopActiveTreasureGuardMove();
			SetTreasureGuardState(ETreasureGuardState::Guarding);
		}
		return;
	}

	const ATacticalGuardArena* Arena = ATacticalGuardArena::FindActive(GetWorld());
	if (Arena && !Arena->IsPointInsideWithInset(NPC->GetActorLocation(), 35.0f))
	{
		// A guard can be pushed through a player entrance even when every planned
		// path is internal. Do not allow one decision tick of combat from outside.
		StopTreasureGuardCombat();
		StopActiveTreasureGuardMove();
		ClearCurrentTarget();
		NPC->SetActorLocation(GuardHome, false, nullptr, ETeleportType::TeleportPhysics);
		SetTreasureGuardState(ETreasureGuardState::Guarding);
		UE_LOG(LogTemp, Warning, TEXT("Event=TreasureGuardArenaEscapeRecovered Guard=%s Home=%s"),
			*GetNameSafe(NPC), *GuardHome.ToCompactString());
		return;
	}

	if (!bLeashed && ((Arena && !Arena->IsPointInsideWithInset(NPC->GetActorLocation(), 70.0f))
		|| (!Arena && DistanceFromAnchor > TreasureGuardLeashBreakRadius)))
	{
		EnterTreasureGuardReturnHome();
	}

	if (bLeashed)
	{
		if (DistanceFromHome <= TreasureGuardReturnCompleteRadius)
		{
			StopActiveTreasureGuardMove();
			bLeashed = false;
			SetTreasureGuardState(ETreasureGuardState::Guarding);
		}
		else
		{
			IssueTreasureGuardMove(GuardHome, ETreasureGuardMovePurpose::ReturnHome);
		}
		return;
	}

	bool bTargetVisible = false;
	AActor* Target = SelectTreasureGuardTarget(bTargetVisible);
	if (!Target)
	{
		ClearCurrentTarget();
		StopTreasureGuardCombat();
		bTreasureGuardRetreating = false;
		if (ActiveTreasureGuardMoveId.IsValid()
			&& ActiveTreasureGuardMovePurpose == ETreasureGuardMovePurpose::Reposition)
		{
			// Finish the committed flank instead of cancelling it at the first
			// expired sight sample. The new position may reacquire the target.
			SetTreasureGuardState(ETreasureGuardState::Repositioning);
			return;
		}
		if (DistanceFromHome > TreasureGuardReturnCompleteRadius)
		{
			SetTreasureGuardState(ETreasureGuardState::Guarding);
			IssueTreasureGuardMove(GuardHome, ETreasureGuardMovePurpose::ReturnHome);
		}
		else
		{
			StopActiveTreasureGuardMove();
			SetTreasureGuardState(ETreasureGuardState::Guarding);
		}
		return;
	}

	SetCurrentTarget(Target);
	SetFocus(Target, EAIFocusPriority::Gameplay);
	const float TargetDistance = FVector::Dist2D(NPC->GetActorLocation(), Target->GetActorLocation());
	const TreasureGuardRules::ECombatDecision Decision = TreasureGuardRules::ChooseCombatDecision(
		TargetDistance,
		bTargetVisible,
		bTreasureGuardRetreating,
		TreasureGuardRetreatEnterDistance,
		TreasureGuardRetreatExitDistance,
		TreasureGuardMaxHoldAndFireDistance);

	const bool bMagazineEmpty = NPC->IsWeaponMagazineEmpty();
	if (bMagazineEmpty)
	{
		NPC->StopShooting();
		if (NPC->IsWeaponReloading() || NPC->RequestWeaponReload())
		{
			SetTreasureGuardState(ETreasureGuardState::Reloading);
		}
	}

	switch (Decision)
	{
	case TreasureGuardRules::ECombatDecision::Retreat:
	{
		bTreasureGuardRetreating = true;
		NPC->StopShooting();
		SetTreasureGuardState(NPC->IsWeaponReloading()
			? ETreasureGuardState::Reloading
			: ETreasureGuardState::Retreating);
		if (!CanPlanTreasureGuardMove(ETreasureGuardMovePurpose::Retreat))
		{
			break;
		}
		FVector RetreatDestination;
		if (FindTreasureGuardDestination(Target, true, RetreatDestination))
		{
			IssueTreasureGuardMove(RetreatDestination, ETreasureGuardMovePurpose::Retreat);
		}
		else if (!ActiveTreasureGuardMoveId.IsValid()
			|| ActiveTreasureGuardMovePurpose != ETreasureGuardMovePurpose::Retreat)
		{
			StopActiveTreasureGuardMove();
		}
		break;
	}

	case TreasureGuardRules::ECombatDecision::HoldAndFire:
		bTreasureGuardRetreating = false;
		StopActiveTreasureGuardMove();
		if (!bMagazineEmpty)
		{
			NPC->StartShooting(Target);
			SetTreasureGuardState(ETreasureGuardState::Firing);
		}
		else
		{
			SetTreasureGuardState(NPC->IsWeaponReloading()
				? ETreasureGuardState::Reloading
				: ETreasureGuardState::Guarding);
		}
		break;

	case TreasureGuardRules::ECombatDecision::Reposition:
	default:
	{
		bTreasureGuardRetreating = false;
		NPC->StopShooting();
		SetTreasureGuardState(NPC->IsWeaponReloading()
			? ETreasureGuardState::Reloading
			: ETreasureGuardState::Repositioning);
		if (!CanPlanTreasureGuardMove(ETreasureGuardMovePurpose::Reposition))
		{
			break;
		}
		FVector RepositionDestination;
		if (FindTreasureGuardDestination(Target, false, RepositionDestination))
		{
			IssueTreasureGuardMove(RepositionDestination, ETreasureGuardMovePurpose::Reposition);
		}
		else if (bTargetVisible
			&& (!ActiveTreasureGuardMoveId.IsValid()
				|| ActiveTreasureGuardMovePurpose != ETreasureGuardMovePurpose::Reposition))
		{
			StopActiveTreasureGuardMove();
			if (!bMagazineEmpty)
			{
				NPC->StartShooting(Target);
				SetTreasureGuardState(ETreasureGuardState::Firing);
			}
		}
		break;
	}
	}
}

AActor* AShooterAIController::SelectTreasureGuardTarget(bool& bOutVisible)
{
	bOutVisible = false;
	APawn* MyPawn = GetPawn();
	if (!MyPawn || !GetWorld())
	{
		return nullptr;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	AActor* CurrentTarget = GetCurrentTarget();
	if (IsValidGuardTarget(CurrentTarget) && IsInsideTreasureGuardThreatArea(CurrentTarget))
	{
		bOutVisible = IsTreasureGuardTargetVisible(CurrentTarget);
		if (bOutVisible)
		{
			LastTreasureGuardTargetSeenTime = Now;
			LastTreasureGuardTargetLocation = CurrentTarget->GetActorLocation();
			return CurrentTarget;
		}
		if (Now - LastTreasureGuardTargetSeenTime <= TreasureGuardTargetLostGrace)
		{
			return CurrentTarget;
		}
	}

	AActor* BestTarget = nullptr;
	float BestScore = BIG_NUMBER;
	FVector GuardAnchor;
	if (!GetTreasureGuardAnchor(GuardAnchor))
	{
		return nullptr;
	}

	auto ConsiderCandidate = [&](AActor* Candidate)
	{
		if (!IsValidGuardTarget(Candidate) || !IsInsideTreasureGuardThreatArea(Candidate)
			|| !HasClearShotFrom(MyPawn->GetPawnViewLocation(), Candidate))
		{
			return;
		}

		const float Score = FVector::DistSquared2D(GuardAnchor, Candidate->GetActorLocation())
			+ FVector::DistSquared2D(MyPawn->GetActorLocation(), Candidate->GetActorLocation()) * 0.1f;
		if (Score < BestScore)
		{
			BestScore = Score;
			BestTarget = Candidate;
		}
	};

	if (AIPerception)
	{
		TArray<AActor*> PerceivedActors;
		AIPerception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), PerceivedActors);
		for (AActor* Candidate : PerceivedActors)
		{
			ConsiderCandidate(Candidate);
		}
	}

	// Close-range awareness prevents circle-strafing through the rear half of the
	// sight cone from making a guard ignore a player touching its capsule.
	for (TActorIterator<ATacticalCharacter> It(GetWorld()); It; ++It)
	{
		ATacticalCharacter* Candidate = *It;
		if (Candidate && FVector::DistSquared2D(MyPawn->GetActorLocation(), Candidate->GetActorLocation())
			<= FMath::Square(TreasureGuardCloseAwarenessRadius))
		{
			ConsiderCandidate(Candidate);
		}
	}

	if (BestTarget)
	{
		SetCurrentTarget(BestTarget);
		LastTreasureGuardTargetSeenTime = Now;
		LastTreasureGuardTargetLocation = BestTarget->GetActorLocation();
		bOutVisible = true;
		return BestTarget;
	}

	ClearCurrentTarget();
	return nullptr;
}

bool AShooterAIController::IsTreasureGuardTargetVisible(const AActor* Target) const
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || !IsValidGuardTarget(Target)
		|| !HasClearShotFrom(MyPawn->GetPawnViewLocation(), Target))
	{
		return false;
	}

	if (FVector::DistSquared2D(MyPawn->GetActorLocation(), Target->GetActorLocation())
		<= FMath::Square(TreasureGuardCloseAwarenessRadius))
	{
		return true;
	}

	if (!AIPerception)
	{
		return false;
	}
	TArray<AActor*> PerceivedActors;
	AIPerception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), PerceivedActors);
	return PerceivedActors.Contains(const_cast<AActor*>(Target));
}

bool AShooterAIController::IsInsideTreasureGuardThreatArea(const AActor* Target) const
{
	if (const ATacticalGuardArena* Arena = ATacticalGuardArena::FindActive(GetWorld()))
	{
		return Target && Arena->IsPointInside(Target->GetActorLocation());
	}
	FVector GuardAnchor;
	return Target && GetTreasureGuardAnchor(GuardAnchor)
		&& FVector::DistSquared2D(GuardAnchor, Target->GetActorLocation())
			<= FMath::Square(TreasureGuardThreatRadius);
}

bool AShooterAIController::GetTreasureGuardAnchor(FVector& OutAnchor) const
{
	const AShooterNPC* NPC = Cast<AShooterNPC>(GetPawn());
	if (!NPC || !IsValid(NPC->GuardedChest))
	{
		return false;
	}
	OutAnchor = NPC->GuardedChest->GetActorLocation();
	return true;
}

bool AShooterAIController::HasClearShotFrom(const FVector& ViewLocation, const AActor* Target) const
{
	if (!Target || !GetWorld())
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TreasureGuardLineOfSight), false, GetPawn());
	QueryParams.AddIgnoredActor(GetPawn());
	FHitResult Hit;
	const FVector TargetPoint = Target->GetActorLocation() + FVector(0.0f, 0.0f, 20.0f);
	if (const ATacticalGuardArena* Arena = ATacticalGuardArena::FindActive(GetWorld());
		Arena && Arena->DoesSegmentCrossBarrier(ViewLocation, TargetPoint))
	{
		return false;
	}
	const bool bBlocked = GetWorld()->LineTraceSingleByChannel(
		Hit, ViewLocation, TargetPoint, ECC_Visibility, QueryParams);
	return !bBlocked || Hit.GetActor() == Target;
}

bool AShooterAIController::FindTreasureGuardDestination(
	AActor* Target, const bool bRetreat, FVector& OutDestination)
{
	const APawn* MyPawn = GetPawn();
	UNavigationSystemV1* NavigationSystem = GetWorld()
		? UNavigationSystemV1::GetCurrent(GetWorld())
		: nullptr;
	const ANavigationData* NavigationData = NavigationSystem
		? NavigationSystem->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)
		: nullptr;
	FVector GuardAnchor;
	FVector GuardHome;
	if (!MyPawn || !Target || !NavigationSystem || !NavigationData
		|| !GetTreasureGuardAnchor(GuardAnchor) || !GetGuardHomeLocation(GuardHome))
	{
		return false;
	}

	const FVector GuardLocation = MyPawn->GetActorLocation();
	const ATacticalGuardArena* Arena = ATacticalGuardArena::FindActive(GetWorld());
	const AShooterNPC* GuardPawn = Cast<AShooterNPC>(MyPawn);
	const float GuardGroundZ = GuardHome.Z - (GuardPawn && GuardPawn->GetCapsuleComponent()
		? GuardPawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f);
	const FVector LiveTargetLocation = Target->GetActorLocation();
	const FVector TacticalTargetLocation = IsTreasureGuardTargetVisible(Target)
		? LiveTargetLocation
		: LastTreasureGuardTargetLocation;
	const FVector RetreatDirection = TreasureGuardRules::MakeRetreatDirection2D(
		GuardLocation, TacticalTargetLocation, MyPawn->GetActorForwardVector());
	FVector SameSideDirection = GuardLocation - TacticalTargetLocation;
	SameSideDirection.Z = 0.0f;
	if (!SameSideDirection.Normalize())
	{
		SameSideDirection = RetreatDirection;
	}

	TArray<FVector> Candidates;
	if (bRetreat)
	{
		for (const float Angle : { 0.0f, 35.0f, -35.0f, 70.0f, -70.0f, 100.0f, -100.0f })
		{
			const FVector Direction = RetreatDirection.RotateAngleAxis(Angle, FVector::UpVector);
			Candidates.Add(GuardLocation + Direction * TreasureGuardRetreatStep);
		}
	}
	else
	{
		for (const float Radius : {
			TreasureGuardPreferredCombatDistance,
			TreasureGuardPreferredCombatDistance - 200.0f,
			TreasureGuardPreferredCombatDistance + 250.0f })
		{
			for (const float Angle : { 0.0f, 35.0f, -35.0f, 70.0f, -70.0f, 105.0f, -105.0f, 145.0f, -145.0f })
			{
				const FVector Direction = SameSideDirection.RotateAngleAxis(Angle, FVector::UpVector);
				Candidates.Add(TacticalTargetLocation + Direction * FMath::Max(250.0f, Radius));
			}
		}
		Candidates.Add(GuardHome);
	}

	const float CurrentTargetDistance = FVector::Dist2D(GuardLocation, TacticalTargetLocation);
	const float ViewHeight = FMath::Clamp(
		MyPawn->GetPawnViewLocation().Z - GuardLocation.Z, 50.0f, 200.0f);
	struct FScoredGuardPoint
	{
		FVector Location = FVector::ZeroVector;
		float Score = BIG_NUMBER;
	};
	TArray<FScoredGuardPoint> ScoredPoints;
	ScoredPoints.Reserve(Candidates.Num());
	TArray<FVector> UniqueProjectedPoints;

	for (const FVector& Candidate : Candidates)
	{
		const FVector BoundedCandidate = Arena ? Candidate
			: TreasureGuardRules::ClampPointToRadius2D(
				Candidate, GuardAnchor, TreasureGuardMovementRadius);
		FNavLocation ProjectedLocation;
		if (!NavigationSystem->ProjectPointToNavigation(
			BoundedCandidate, ProjectedLocation, FVector(160.0f, 160.0f, 250.0f), NavigationData))
		{
			continue;
		}
		if ((Arena && !Arena->IsPointInsideWithInset(ProjectedLocation.Location, 70.0f))
			|| FMath::Abs(ProjectedLocation.Location.Z - GuardGroundZ) > 80.0f
			|| (!Arena && FVector::DistSquared2D(ProjectedLocation.Location, GuardAnchor)
				> FMath::Square(TreasureGuardMovementRadius + 50.0f)))
		{
			continue;
		}

		bool bDuplicate = false;
		for (const FVector& Existing : UniqueProjectedPoints)
		{
			if (FVector::DistSquared2D(Existing, ProjectedLocation.Location) < FMath::Square(50.0f))
			{
				bDuplicate = true;
				break;
			}
		}
		if (bDuplicate)
		{
			continue;
		}
		UniqueProjectedPoints.Add(ProjectedLocation.Location);

		const float CandidateTargetDistance = FVector::Dist2D(
			ProjectedLocation.Location, TacticalTargetLocation);
		if (bRetreat && CandidateTargetDistance < CurrentTargetDistance + 75.0f)
		{
			continue;
		}
		if (!bRetreat && CandidateTargetDistance < TreasureGuardRetreatExitDistance)
		{
			continue;
		}

		const bool bCandidateHasShot = HasClearShotFrom(
			ProjectedLocation.Location + FVector(0.0f, 0.0f, ViewHeight), Target);
		FVector CandidateSide = ProjectedLocation.Location - TacticalTargetLocation;
		CandidateSide.Z = 0.0f;
		CandidateSide.Normalize();
		const float CrossingPenalty = FVector::DotProduct(CandidateSide, SameSideDirection) < 0.0f
			? 1200.0f
			: 0.0f;
		const float TooClosePenalty = CandidateTargetDistance < TreasureGuardRetreatExitDistance
			? (TreasureGuardRetreatExitDistance - CandidateTargetDistance) * 20.0f
			: 0.0f;
		const float Score = (bCandidateHasShot ? 0.0f : 5000.0f)
			+ FMath::Abs(CandidateTargetDistance - TreasureGuardPreferredCombatDistance) * 2.0f
			+ FVector::Dist2D(GuardLocation, ProjectedLocation.Location) * 0.35f
			+ FVector::Dist2D(GuardAnchor, ProjectedLocation.Location) * 0.1f
			+ CrossingPenalty + TooClosePenalty;
		FScoredGuardPoint& ScoredPoint = ScoredPoints.AddDefaulted_GetRef();
		ScoredPoint.Location = ProjectedLocation.Location;
		ScoredPoint.Score = Score;
	}

	ScoredPoints.Sort([](const FScoredGuardPoint& Left, const FScoredGuardPoint& Right)
	{
		return Left.Score < Right.Score;
	});

	if (ScoredPoints.IsEmpty())
	{
		TreasureGuardCandidateBatchCursor = 0;
		return false;
	}

	// Validate a fixed-size ranked batch per plan. If it is blocked, the cursor
	// rotates into the next batch on the following plan instead of repeating the
	// same candidates or synchronously pathfinding every point in one frame.
	constexpr int32 MaxPathCandidatesPerPlan = 6;
	const int32 BatchStart = TreasureGuardCandidateBatchCursor % ScoredPoints.Num();
	const int32 BatchCount = FMath::Min(MaxPathCandidatesPerPlan, ScoredPoints.Num());
	for (int32 BatchOffset = 0; BatchOffset < BatchCount; ++BatchOffset)
	{
		const int32 CandidateIndex = (BatchStart + BatchOffset) % ScoredPoints.Num();
		const FVector& CandidateLocation = ScoredPoints[CandidateIndex].Location;
		UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
			GetWorld(), GuardLocation, CandidateLocation, const_cast<APawn*>(MyPawn));
		if (!Path || !Path->IsValid() || Path->IsPartial())
		{
			continue;
		}

		bool bPathApproachesTarget = false;
		bool bPathLeavesGuardArea = false;
		FVector FlatTargetLocation = TacticalTargetLocation;
		FlatTargetLocation.Z = 0.0f;
		for (const FVector& PathPoint : Path->PathPoints)
		{
			if ((Arena && !Arena->IsPointInsideWithInset(PathPoint, 35.0f))
				|| FMath::Abs(PathPoint.Z - GuardGroundZ) > 80.0f
				|| (!Arena && FVector::DistSquared2D(PathPoint, GuardAnchor)
					> FMath::Square(TreasureGuardMovementRadius + 75.0f)))
			{
				bPathLeavesGuardArea = true;
				break;
			}
		}
		if (bPathLeavesGuardArea)
		{
			continue;
		}
		for (int32 PointIndex = 1; PointIndex < Path->PathPoints.Num(); ++PointIndex)
		{
			FVector SegmentStart = Path->PathPoints[PointIndex - 1];
			FVector SegmentEnd = Path->PathPoints[PointIndex];
			SegmentStart.Z = 0.0f;
			SegmentEnd.Z = 0.0f;
			const float SegmentTargetDistance = FMath::PointDistToSegment(
				FlatTargetLocation, SegmentStart, SegmentEnd);
			if ((!bRetreat && SegmentTargetDistance < TreasureGuardRetreatEnterDistance)
				|| (bRetreat && SegmentTargetDistance + 50.0f < CurrentTargetDistance))
			{
				bPathApproachesTarget = true;
				break;
			}
		}
		if (bRetreat && Path->PathPoints.Num() > 1)
		{
			FVector FirstPathDirection = Path->PathPoints[1] - Path->PathPoints[0];
			FirstPathDirection.Z = 0.0f;
			if (FirstPathDirection.Normalize()
				&& FVector::DotProduct(FirstPathDirection, RetreatDirection) < 0.0f)
			{
				bPathApproachesTarget = true;
			}
		}
		if (bPathApproachesTarget)
		{
			continue;
		}

		OutDestination = CandidateLocation;
		TreasureGuardCandidateBatchCursor = 0;
		return true;
	}

	TreasureGuardCandidateBatchCursor = (BatchStart + BatchCount) % ScoredPoints.Num();
	return false;
}

bool AShooterAIController::CanPlanTreasureGuardMove(
	const ETreasureGuardMovePurpose Purpose)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const float Now = World->GetTimeSeconds();
	const bool bHasActiveMove = ActiveTreasureGuardMoveId.IsValid();
	if (!TreasureGuardRules::ShouldPlanMove(
		bHasActiveMove,
		bHasActiveMove && ActiveTreasureGuardMovePurpose != Purpose,
		Now - LastTreasureGuardPlanTime,
		TreasureGuardMoveRequestInterval))
	{
		return false;
	}

	AActor* Target = GetCurrentTarget();
	if (Purpose != LastTreasureGuardPlanPurpose
		|| Target != LastTreasureGuardPlanTarget.Get())
	{
		TreasureGuardCandidateBatchCursor = 0;
	}
	LastTreasureGuardPlanTime = Now;
	LastTreasureGuardPlanPurpose = Purpose;
	LastTreasureGuardPlanTarget = Target;
	return true;
}

bool AShooterAIController::IssueTreasureGuardMove(
	const FVector& Destination, const ETreasureGuardMovePurpose Purpose)
{
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavigationSystem = World
		? UNavigationSystemV1::GetCurrent(World)
		: nullptr;
	const ANavigationData* NavigationData = NavigationSystem
		? NavigationSystem->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)
		: nullptr;
	if (!MyPawn || !World || !NavigationSystem || !NavigationData)
	{
		return false;
	}

	FNavLocation ProjectedDestination;
	if (!NavigationSystem->ProjectPointToNavigation(
		Destination, ProjectedDestination, FVector(160.0f, 160.0f, 250.0f), NavigationData))
	{
		return false;
	}
	if (Purpose == ETreasureGuardMovePurpose::ReturnHome
		&& (FVector::DistSquared2D(ProjectedDestination.Location, Destination) > FMath::Square(100.0f)
			|| FMath::Abs(ProjectedDestination.Location.Z - Destination.Z) > 150.0f))
	{
		// Return home must stay on the authored ground polygon, not another floor.
		return false;
	}
	if (const AShooterNPC* GuardPawn = Cast<AShooterNPC>(MyPawn))
	{
		FVector GuardHome;
		if (GetGuardHomeLocation(GuardHome))
		{
			const float GroundZ = GuardHome.Z - (GuardPawn->GetCapsuleComponent()
				? GuardPawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f);
			if (FMath::Abs(ProjectedDestination.Location.Z - GroundZ) > 80.0f)
			{
				return false;
			}
		}
	}
	if (const ATacticalGuardArena* Arena = ATacticalGuardArena::FindActive(World))
	{
		if (!Arena->IsPointInsideWithInset(ProjectedDestination.Location, 35.0f))
		{
			return false;
		}
		UNavigationPath* SafePath = UNavigationSystemV1::FindPathToLocationSynchronously(
			World, MyPawn->GetActorLocation(), ProjectedDestination.Location, MyPawn);
		if (!SafePath || !SafePath->IsValid() || SafePath->IsPartial())
		{
			return false;
		}
		for (const FVector& Point : SafePath->PathPoints)
		{
			if (!Arena->IsPointInsideWithInset(Point, 30.0f))
			{
				return false;
			}
			FVector Home;
			if (const AShooterNPC* Guard = Cast<AShooterNPC>(MyPawn);
				Guard && GetGuardHomeLocation(Home))
			{
				const float GroundZ = Home.Z - (Guard->GetCapsuleComponent()
					? Guard->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0f);
				if (FMath::Abs(Point.Z - GroundZ) > 80.0f)
				{
					return false;
				}
			}
		}
	}

	const float Now = World->GetTimeSeconds();
	const bool bHasActiveMove = ActiveTreasureGuardMoveId.IsValid();
	const bool bPurposeChanged = bHasActiveMove && Purpose != ActiveTreasureGuardMovePurpose;
	const AActor* Target = GetCurrentTarget();
	const FVector MoveTargetLocation = Target ? Target->GetActorLocation() : FVector::ZeroVector;
	if (!TreasureGuardRules::ShouldIssueMove(
		bHasActiveMove,
		bPurposeChanged,
		FVector::DistSquared2D(ActiveTreasureGuardDestination, ProjectedDestination.Location),
		FVector::DistSquared2D(LastTreasureGuardMoveTargetLocation, MoveTargetLocation),
		Now - LastTreasureGuardMoveRequestTime,
		TreasureGuardMoveRequestInterval,
		TreasureGuardMoveReplanDistance))
	{
		return bHasActiveMove;
	}

	StopActiveTreasureGuardMove();
	LastTreasureGuardMoveRequestTime = Now;
	LastTreasureGuardMoveTargetLocation = MoveTargetLocation;

	FAIMoveRequest MoveRequest;
	MoveRequest.SetGoalLocation(ProjectedDestination.Location);
	MoveRequest.SetUsePathfinding(true);
	MoveRequest.SetAllowPartialPath(false);
	MoveRequest.SetProjectGoalLocation(false);
	MoveRequest.SetRequireNavigableEndLocation(true);
	MoveRequest.SetCanStrafe(true);
	MoveRequest.SetAcceptanceRadius(TreasureGuardMoveAcceptanceRadius);
	MoveRequest.SetReachTestIncludesAgentRadius(
		Purpose != ETreasureGuardMovePurpose::ReturnHome);
	MoveRequest.SetReachTestIncludesGoalRadius(false);

	const FPathFollowingRequestResult MoveResult = MoveTo(MoveRequest);
	if (MoveResult.Code == EPathFollowingRequestResult::RequestSuccessful)
	{
		ActiveTreasureGuardMoveId = MoveResult.MoveId;
		ActiveTreasureGuardMovePurpose = Purpose;
		ActiveTreasureGuardDestination = ProjectedDestination.Location;
		return true;
	}
	if (MoveResult.Code == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		if (Purpose == ETreasureGuardMovePurpose::ReturnHome)
		{
			FVector HomeLocation;
			if (GetGuardHomeLocation(HomeLocation)
				&& FVector::Dist2D(MyPawn->GetActorLocation(), HomeLocation)
					<= TreasureGuardReturnCompleteRadius)
			{
				bLeashed = false;
				SetTreasureGuardState(ETreasureGuardState::Guarding);
				return true;
			}
			return false;
		}
		return true;
	}

	if (bEnableDiagnostics)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[AI-DEBUG] TreasureGuard MoveTo rejected Purpose=%d Destination=%s"),
			static_cast<int32>(Purpose), *ProjectedDestination.Location.ToCompactString());
	}
	return false;
}

void AShooterAIController::StopActiveTreasureGuardMove()
{
	const bool bHadMove = ActiveTreasureGuardMoveId.IsValid()
		|| GetMoveStatus() == EPathFollowingStatus::Moving
		|| GetMoveStatus() == EPathFollowingStatus::Waiting;
	ActiveTreasureGuardMoveId = FAIRequestID::InvalidRequest;
	ActiveTreasureGuardMovePurpose = ETreasureGuardMovePurpose::None;
	ActiveTreasureGuardDestination = FVector::ZeroVector;
	if (bHadMove)
	{
		StopMovement();
	}
}

void AShooterAIController::EnterTreasureGuardReturnHome()
{
	bLeashed = true;
	bTreasureGuardRetreating = false;
	ClearCurrentTarget();
	StopTreasureGuardCombat();
	StopActiveTreasureGuardMove();
	SetTreasureGuardState(ETreasureGuardState::ReturningHome);
}

void AShooterAIController::SetTreasureGuardState(const ETreasureGuardState NewState)
{
	if (TreasureGuardState == NewState)
	{
		return;
	}
	if (bEnableDiagnostics)
	{
		UE_LOG(LogTemp, Display, TEXT("[AI-DEBUG] TreasureGuard State %d -> %d Pawn=%s"),
			static_cast<int32>(TreasureGuardState), static_cast<int32>(NewState),
			*GetNameSafe(GetPawn()));
	}
	TreasureGuardState = NewState;
}

void AShooterAIController::StopTreasureGuardCombat()
{
	if (AShooterNPC* NPC = Cast<AShooterNPC>(GetPawn()))
	{
		NPC->StopShooting();
	}
	ClearFocus(EAIFocusPriority::Gameplay);
}

// ---- Leash ----

void AShooterAIController::StartLeashCheck()
{
	FTimerManager& TimerManager = GetWorld()->GetTimerManager();
	TimerManager.SetTimer(LeashTimer, this, &AShooterAIController::CheckLeash, LeashCheckInterval, true);
}

void AShooterAIController::ClearLeashCheck()
{
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(LeashTimer);
	}
}

void AShooterAIController::CheckLeash()
{
	if (bLeashed)
	{
		// Already returning home — nothing to do until OnMoveCompleted fires
		return;
	}

	FVector HomeLocation;
	if (!GetGuardHomeLocation(HomeLocation))
	{
		return;
	}

	APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return;
	}

	const float DistSq = FVector::DistSquared2D(MyPawn->GetActorLocation(), HomeLocation);
	if (DistSq > LeashRadius * LeashRadius)
	{
		// Leash broken — disengage and return home.
		if (bEnableDiagnostics)
		{
			UE_LOG(LogTemp, Display, TEXT("[AI-DEBUG] LEASH BREAK: Dist=%.0f LeashRadius=%.0f Home=%s PawnPos=%s"),
				FMath::Sqrt(DistSq), LeashRadius, *HomeLocation.ToString(), *MyPawn->GetActorLocation().ToString());
		}
		bLeashed = true;
		ClearCurrentTarget();

		// Stop shooting
		if (AShooterNPC* NPC = Cast<AShooterNPC>(MyPawn))
		{
			NPC->StopShooting();
		}

		// Force navigation back to the guard point
		MoveToLocation(HomeLocation, -1.0f, true, true, false, false, nullptr, true);
	}
}

bool AShooterAIController::GetGuardHomeLocation(FVector& OutLocation) const
{
	const AShooterNPC* NPC = Cast<AShooterNPC>(GetPawn());
	if (!NPC || !NPC->GuardedChest)
	{
		return false;
	}

	OutLocation = NPC->GuardHomeLocation;
	return true;
}

// ---- Diagnostics ----

void AShooterAIController::StartDiagnostics()
{
	if (!bEnableDiagnostics)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(DiagnosticTimer, this, &AShooterAIController::DiagnosticTick, 0.5f, true);
	}
}

void AShooterAIController::ClearDiagnostics()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DiagnosticTimer);
	}
}

void AShooterAIController::DiagnosticTick()
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn || !StateTreeAI)
	{
		return;
	}

	const EPathFollowingStatus::Type MoveStatus = GetMoveStatus();
	const FVector Velocity = MyPawn->GetVelocity();
	const FVector Loc = MyPawn->GetActorLocation();
	const FString StatusDesc = GetPathFollowingComponent() ? GetPathFollowingComponent()->GetStatusDesc() : TEXT("N/A");

	// UE exposes active StateTree names only when gameplay-debugger support is
	// compiled. Keep diagnostics useful there without breaking Shipping targets.
	FString ActiveStatesStr = TEXT("Unavailable");
#if WITH_GAMEPLAY_DEBUGGER
	ActiveStatesStr.Reset();
	const TArray<FName> ActiveStates = StateTreeAI->GetActiveStateNames();
	for (const FName& ActiveStateName : ActiveStates)
	{
		if (!ActiveStatesStr.IsEmpty()) { ActiveStatesStr += TEXT(" > "); }
		ActiveStatesStr += ActiveStateName.ToString();
	}
#endif

	// RunStatus: 0=Running 1=Succeeded 2=Failed 3=Stopped — lets us see if the tree died.
	const EStateTreeRunStatus RunStatus = StateTreeAI->GetStateTreeRunStatus();

	UE_LOG(LogTemp, Warning, TEXT("[AI-DEBUG] STATE: Target=%s MoveStatus=%s Velocity=%.0f Pos=(%.0f,%.0f,%.0f) TreeRunning=%d RunStatus=%d ActiveStates=%s"),
		*GetNameSafe(GetCurrentTarget()),
		*StatusDesc,
		Velocity.Size(),
		Loc.X, Loc.Y, Loc.Z,
		StateTreeAI->IsRunning(),
		(int32)RunStatus,
		*ActiveStatesStr);

	// Health/death state of the NPC — checks whether a 'Dead' transition condition could be firing.
	if (const AShooterNPC* NPC = Cast<AShooterNPC>(MyPawn))
	{
		if (const UHealthComponent* HealthComp = NPC->GetHealthComponent())
		{
			UE_LOG(LogTemp, Warning, TEXT("[AI-DEBUG]   Health: HP=%.0f IsDead=%d HasDeadTag=%d"),
				HealthComp->CurrentHP, HealthComp->IsDead(), NPC->ActorHasTag(FName("Dead")));
		}
	}

	// Log whenever the path-following status transitions (Idle/Waiting/Paused/Moving)
	if (MoveStatus != LastMoveStatus)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AI-DEBUG] MOVE STATUS CHANGED: %d -> %d (0=Idle 1=Waiting 2=Paused 3=Moving)"),
			(int32)LastMoveStatus, (int32)MoveStatus);
		LastMoveStatus = MoveStatus;
	}
}
