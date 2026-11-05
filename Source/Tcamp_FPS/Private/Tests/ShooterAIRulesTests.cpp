// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Variant_Shooter/AI/TreasureGuardRules.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTreasureGuardCombatDecisionTest,
	"Tcamp.Tactical.AI.TreasureGuardCombatDecision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTreasureGuardCombatDecisionTest::RunTest(const FString& Parameters)
{
	using TreasureGuardRules::ECombatDecision;
	using TreasureGuardRules::ChooseCombatDecision;

	TestEqual(TEXT("A newly close target triggers retreat"),
		static_cast<uint8>(ChooseCombatDecision(449.0f, true, false, 450.0f, 650.0f, 1600.0f)),
		static_cast<uint8>(ECombatDecision::Retreat));
	TestEqual(TEXT("Retreat hysteresis remains active below its exit distance"),
		static_cast<uint8>(ChooseCombatDecision(600.0f, true, true, 450.0f, 650.0f, 1600.0f)),
		static_cast<uint8>(ECombatDecision::Retreat));
	TestEqual(TEXT("Retreat hysteresis exits at its configured boundary"),
		static_cast<uint8>(ChooseCombatDecision(650.0f, true, true, 450.0f, 650.0f, 1600.0f)),
		static_cast<uint8>(ECombatDecision::HoldAndFire));
	TestEqual(TEXT("Clear line of sight in the firing band holds position"),
		static_cast<uint8>(ChooseCombatDecision(900.0f, true, false, 450.0f, 650.0f, 1600.0f)),
		static_cast<uint8>(ECombatDecision::HoldAndFire));
	TestEqual(TEXT("Lost line of sight requests a reposition"),
		static_cast<uint8>(ChooseCombatDecision(900.0f, false, false, 450.0f, 650.0f, 1600.0f)),
		static_cast<uint8>(ECombatDecision::Reposition));
	TestEqual(TEXT("A visible target beyond the hold band requests a reposition"),
		static_cast<uint8>(ChooseCombatDecision(1601.0f, true, false, 450.0f, 650.0f, 1600.0f)),
		static_cast<uint8>(ECombatDecision::Reposition));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTreasureGuardMoveThrottleTest,
	"Tcamp.Tactical.AI.TreasureGuardMoveThrottle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTreasureGuardMoveThrottleTest::RunTest(const FString& Parameters)
{
	using TreasureGuardRules::ShouldIssueMove;
	using TreasureGuardRules::ShouldPlanMove;

	TestTrue(TEXT("An idle guard may issue its first move"),
		ShouldIssueMove(false, false, 0.0f, 0.0f, 100.0f, 1.25f, 200.0f));
	TestFalse(TEXT("A failed request observes backoff before retrying"),
		ShouldIssueMove(false, false, 0.0f, 0.0f, 0.1f, 1.25f, 200.0f));
	TestTrue(TEXT("A changed move purpose may replace the active request"),
		ShouldIssueMove(true, true, 0.0f, 0.0f, 0.1f, 1.25f, 200.0f));
	TestFalse(TEXT("An unchanged request cannot be reissued during cooldown"),
		ShouldIssueMove(true, false, FMath::Square(500.0f), FMath::Square(500.0f),
			0.1f, 1.25f, 200.0f));
	TestFalse(TEXT("Small destination drift does not restart MoveTo"),
		ShouldIssueMove(true, false, FMath::Square(50.0f), FMath::Square(50.0f),
			2.0f, 1.25f, 200.0f));
	TestTrue(TEXT("Material target movement may trigger a bounded replan"),
		ShouldIssueMove(true, false, 0.0f, FMath::Square(250.0f),
			2.0f, 1.25f, 200.0f));
	TestTrue(TEXT("Material destination movement may trigger a bounded replan"),
		ShouldIssueMove(true, false, FMath::Square(250.0f), 0.0f,
			2.0f, 1.25f, 200.0f));
	TestTrue(TEXT("The exact cooldown boundary permits reconsideration when idle"),
		ShouldIssueMove(false, false, 0.0f, 0.0f, 1.25f, 1.25f, 200.0f));
	TestFalse(TEXT("The exact replan-distance boundary does not restart MoveTo"),
		ShouldIssueMove(true, false, FMath::Square(200.0f), 0.0f,
			2.0f, 1.25f, 200.0f));
	TestFalse(TEXT("Candidate path planning has its own cooldown"),
		ShouldPlanMove(true, false, 0.1f, 1.25f));
	TestTrue(TEXT("Candidate path planning resumes at its cooldown boundary"),
		ShouldPlanMove(true, false, 1.25f, 1.25f));
	TestTrue(TEXT("A changed active purpose may plan immediately"),
		ShouldPlanMove(true, true, 0.1f, 1.25f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTreasureGuardSpatialRulesTest,
	"Tcamp.Tactical.AI.TreasureGuardSpatialRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTreasureGuardSpatialRulesTest::RunTest(const FString& Parameters)
{
	const FVector RetreatDirection = TreasureGuardRules::MakeRetreatDirection2D(
		FVector(100.0f, 0.0f, 50.0f), FVector::ZeroVector, FVector::ForwardVector);
	TestTrue(TEXT("Retreat points away from the target"),
		FVector::DotProduct(RetreatDirection, FVector::ForwardVector) > 0.99f);
	TestTrue(TEXT("Retreat remains horizontal"), FMath::IsNearlyZero(RetreatDirection.Z));
	const FVector FallbackRetreatDirection = TreasureGuardRules::MakeRetreatDirection2D(
		FVector::ZeroVector, FVector::ZeroVector, FVector::ForwardVector);
	TestTrue(TEXT("A coincident target uses the opposite facing as a retreat fallback"),
		FVector::DotProduct(FallbackRetreatDirection, FVector::BackwardVector) > 0.99f);

	const FVector Clamped = TreasureGuardRules::ClampPointToRadius2D(
		FVector(1000.0f, 0.0f, 75.0f), FVector::ZeroVector, 700.0f);
	TestTrue(TEXT("Movement points stay inside the guard area"),
		FMath::IsNearlyEqual(FVector::Dist2D(Clamped, FVector::ZeroVector), 700.0));
	TestTrue(TEXT("Clamping preserves the candidate height"),
		FMath::IsNearlyEqual(Clamped.Z, 75.0));

	TestFalse(TEXT("An unrelated aborted move cannot finish return"),
		TreasureGuardRules::CanCompleteReturn(false, 0.0f, 125.0f));
	TestFalse(TEXT("A failed return outside home remains in return state"),
		TreasureGuardRules::CanCompleteReturn(true, 300.0f, 125.0f));
	TestTrue(TEXT("Physical arrival can finish return even if path reports failure"),
		TreasureGuardRules::CanCompleteReturn(true, 100.0f, 125.0f));
	TestFalse(TEXT("Path success alone cannot finish far from the individual post"),
		TreasureGuardRules::CanCompleteReturn(true, 300.0f, 125.0f));
	TestTrue(TEXT("The exact physical completion boundary is accepted"),
		TreasureGuardRules::CanCompleteReturn(true, 125.0f, 125.0f));
	return true;
}

#endif
