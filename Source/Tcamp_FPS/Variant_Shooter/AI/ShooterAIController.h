// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "ShooterAIController.generated.h"

class UStateTreeAIComponent;
class UAIPerceptionComponent;
class AShooterNPC;
struct FAIStimulus;

DECLARE_DELEGATE_TwoParams(FShooterPerceptionUpdatedDelegate, AActor*, const FAIStimulus&);
DECLARE_DELEGATE_OneParam(FShooterPerceptionForgottenDelegate, AActor*);

/**
 *  AI Controller for shooter NPCs. Generic NPCs use StateTree behaviour;
 *  treasure guards use a dedicated server-authoritative combat brain so only
 *  one system owns their movement and return-home requests.
 */
UCLASS(abstract)
class TCAMP_FPS_API AShooterAIController : public AAIController
{
	GENERATED_BODY()

	/** Runs the behavior StateTree for this NPC */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UStateTreeAIComponent* StateTreeAI;

	/** Detects other actors through sight, hearing and other senses */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UAIPerceptionComponent* AIPerception;

protected:

	/** Team tag for pawn friend or foe identification */
	UPROPERTY(EditAnywhere, Category="Shooter")
	FName TeamTag = FName("Enemy");

	/** Enemy currently being targeted */
	TWeakObjectPtr<AActor> TargetEnemy;

	// ---- Dedicated treasure guard behaviour ----

	enum class ETreasureGuardState : uint8
	{
		Inactive,
		Guarding,
		Retreating,
		Repositioning,
		Firing,
		Reloading,
		ReturningHome
	};

	enum class ETreasureGuardMovePurpose : uint8
	{
		None,
		Retreat,
		Reposition,
		ReturnHome
	};

	/** Server decision cadence. Movement requests are throttled separately. */
	UPROPERTY(EditDefaultsOnly, Category="Treasure Guard|Decision", meta=(ClampMin=0.05f, ClampMax=1.0f))
	float TreasureGuardDecisionInterval = 0.1f;

	/** Players inside this radius of the chest may be selected as threats. */
	UPROPERTY(EditDefaultsOnly, Category="Treasure Guard|Perception", meta=(ClampMin=500.0f, ClampMax=10000.0f))
	float TreasureGuardThreatRadius = 2200.0f;

	/** Nearby visible players are noticed even while circling behind the guard. */
	UPROPERTY(EditDefaultsOnly, Category="Treasure Guard|Perception", meta=(ClampMin=100.0f, ClampMax=2000.0f))
	float TreasureGuardCloseAwarenessRadius = 650.0f;

	/** Brief grace before a temporarily occluded current target is discarded. */
	UPROPERTY(EditDefaultsOnly, Category="Treasure Guard|Perception", meta=(ClampMin=0.0f, ClampMax=5.0f))
	float TreasureGuardTargetLostGrace = 2.5f;

	/** Enter retreat below this range. */
	UPROPERTY(EditDefaultsOnly, Category="Treasure Guard|Combat", meta=(ClampMin=100.0f, ClampMax=1500.0f))
	float TreasureGuardRetreatEnterDistance = 450.0f;

	/** Leave retreat only after opening this much space, preventing range jitter. */
	UPROPERTY(EditDefaultsOnly, Category="Treasure Guard|Combat", meta=(ClampMin=100.0f, ClampMax=2000.0f))
	float TreasureGuardRetreatExitDistance = 650.0f;

	/** With clear sight, stop and fire at targets up to this range. */
	UPROPERTY(EditDefaultsOnly, Category="Treasure Guard|Combat", meta=(ClampMin=300.0f, ClampMax=5000.0f))
	float TreasureGuardMaxHoldAndFireDistance = 1600.0f;

	/** Desired range used to score alternative firing positions. */
	UPROPERTY(EditDefaultsOnly, Category="Treasure Guard|Combat", meta=(ClampMin=200.0f, ClampMax=3000.0f))
	float TreasureGuardPreferredCombatDistance = 900.0f;

	/** Distance added per close-range retreat request. */
	UPROPERTY(EditDefaultsOnly, Category="Treasure Guard|Movement", meta=(ClampMin=100.0f, ClampMax=1500.0f))
	float TreasureGuardRetreatStep = 550.0f;

	/** Hard movement boundary around the shared chest anchor. */
	UPROPERTY(EditDefaultsOnly, Category="Treasure Guard|Movement", meta=(ClampMin=200.0f, ClampMax=3000.0f))
	float TreasureGuardMovementRadius = 700.0f;

	/** Crossing this radius forces an exclusive return to the individual post. */
	UPROPERTY(EditDefaultsOnly, Category="Treasure Guard|Movement", meta=(ClampMin=300.0f, ClampMax=5000.0f))
	float TreasureGuardLeashBreakRadius = 900.0f;

	UPROPERTY(EditDefaultsOnly, Category="Treasure Guard|Movement", meta=(ClampMin=25.0f, ClampMax=500.0f))
	float TreasureGuardMoveAcceptanceRadius = 110.0f;

	UPROPERTY(EditDefaultsOnly, Category="Treasure Guard|Movement", meta=(ClampMin=25.0f, ClampMax=500.0f))
	float TreasureGuardReturnCompleteRadius = 125.0f;

	/** Minimum delay between equivalent path requests. */
	UPROPERTY(EditDefaultsOnly, Category="Treasure Guard|Movement", meta=(ClampMin=0.1f, ClampMax=5.0f))
	float TreasureGuardMoveRequestInterval = 1.25f;

	/** Target/destination drift required before an active path may be replaced. */
	UPROPERTY(EditDefaultsOnly, Category="Treasure Guard|Movement", meta=(ClampMin=25.0f, ClampMax=1000.0f))
	float TreasureGuardMoveReplanDistance = 200.0f;

	bool bUseTreasureGuardBrain = false;
	bool bTreasureGuardRetreating = false;
	ETreasureGuardState TreasureGuardState = ETreasureGuardState::Inactive;
	ETreasureGuardMovePurpose ActiveTreasureGuardMovePurpose = ETreasureGuardMovePurpose::None;
	ETreasureGuardMovePurpose LastTreasureGuardPlanPurpose = ETreasureGuardMovePurpose::None;
	FAIRequestID ActiveTreasureGuardMoveId = FAIRequestID::InvalidRequest;
	FVector ActiveTreasureGuardDestination = FVector::ZeroVector;
	FVector LastTreasureGuardTargetLocation = FVector::ZeroVector;
	FVector LastTreasureGuardMoveTargetLocation = FVector::ZeroVector;
	TWeakObjectPtr<AActor> LastTreasureGuardPlanTarget;
	float LastTreasureGuardTargetSeenTime = -BIG_NUMBER;
	float LastTreasureGuardMoveRequestTime = -BIG_NUMBER;
	float LastTreasureGuardPlanTime = -BIG_NUMBER;
	int32 TreasureGuardCandidateBatchCursor = 0;
	FTimerHandle TreasureGuardDecisionTimer;

	// ---- Leash behaviour ----

	/** Max distance the guard will travel from its GuardHomeLocation before disengaging (in UE units = cm) */
	UPROPERTY(EditAnywhere, Category="Leash", meta=(ClampMin=100.0f, ClampMax=10000.0f))
	float LeashRadius = 800.0f;

	/** How often the leash distance is checked (seconds) */
	UPROPERTY(EditAnywhere, Category="Leash", meta=(ClampMin=0.1f, ClampMax=2.0f))
	float LeashCheckInterval = 0.25f;

	/** True while the guard is returning home after a leash break */
	bool bLeashed = false;

	FTimerHandle LeashTimer;

	// ---- Diagnostics ----

	/** Periodic heartbeat that logs the AI's target/movement state to debug 'AI won't move' */
	FTimerHandle DiagnosticTimer;

	/** Opt-in only: periodic logging is too expensive for normal multiplayer PIE. */
	UPROPERTY(EditAnywhere, Category = "Diagnostics")
	bool bEnableDiagnostics = false;

	/** Last path-following status seen, used to detect status transitions */
	EPathFollowingStatus::Type LastMoveStatus = EPathFollowingStatus::Idle;

	void StartDiagnostics();
	void ClearDiagnostics();
	void DiagnosticTick();

public:

	/** Called when an AI perception has been updated. StateTree task delegate hook */
	FShooterPerceptionUpdatedDelegate OnShooterPerceptionUpdated;

	/** Called when an AI perception has been forgotten. StateTree task delegate hook */
	FShooterPerceptionForgottenDelegate OnShooterPerceptionForgotten;

	/** Constructor */
	AShooterAIController();

protected:

	/** Pawn initialization */
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Called when the possessed pawn dies */
	UFUNCTION()
	void OnPawnDeath();

	/** Called when a MoveTo issued by CheckLeash completes */
	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;

public:

	/** Sets the targeted enemy */
	void SetCurrentTarget(AActor* Target);

	/** Clears the targeted enemy */
	void ClearCurrentTarget();

	/** Returns the targeted enemy */
	AActor* GetCurrentTarget() const { return TargetEnemy.Get(); };

	/** Neutral guards may target either team, but never corpses or spectators. */
	static bool IsValidGuardTarget(const AActor* Candidate);

protected:

	/** Called when the AI perception component updates a perception on a given actor */
	UFUNCTION()
	void OnPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	/** Called when the AI perception component forgets a given actor */
	UFUNCTION()
	void OnPerceptionForgotten(AActor* Actor);

	void ConfigurePerception(bool bForTreasureGuard);
	void StartTreasureGuardBrain();
	void StopTreasureGuardBrain();
	void TickTreasureGuardBrain();
	AActor* SelectTreasureGuardTarget(bool& bOutVisible);
	bool IsTreasureGuardTargetVisible(const AActor* Target) const;
	bool IsInsideTreasureGuardThreatArea(const AActor* Target) const;
	bool GetTreasureGuardAnchor(FVector& OutAnchor) const;
	bool HasClearShotFrom(const FVector& ViewLocation, const AActor* Target) const;
	bool FindTreasureGuardDestination(AActor* Target, bool bRetreat, FVector& OutDestination);
	bool CanPlanTreasureGuardMove(ETreasureGuardMovePurpose Purpose);
	bool IssueTreasureGuardMove(const FVector& Destination, ETreasureGuardMovePurpose Purpose);
	void StopActiveTreasureGuardMove();
	void EnterTreasureGuardReturnHome();
	void SetTreasureGuardState(ETreasureGuardState NewState);
	void StopTreasureGuardCombat();

	// ---- Leash helpers ----

	void StartLeashCheck();
	void ClearLeashCheck();
	void CheckLeash();

	/** Read GuardHomeLocation from the possessed NPC. Returns false if no valid NPC. */
	bool GetGuardHomeLocation(FVector& OutLocation) const;
};
