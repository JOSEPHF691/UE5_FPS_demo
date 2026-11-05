// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Materials/Material.h"
#include "Weapons/TacticalBulletTracer.h"
#include "Weapons/ShooterWeapon.h"
#include "Weapons/TacticalTracerRules.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalTracerPathTest,
	"Tcamp.Tactical.PlayerPresentation.TracerPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalTracerPathTest::RunTest(const FString& Parameters)
{
	const FVector Start(100.0, 200.0, 300.0);
	const FVector Direction = FVector(1.0, 2.0, 0.5).GetSafeNormal();
	const FVector MissEnd = TacticalTracerRules::ChooseVisualEnd(
		Start, Direction, false, FVector::ZeroVector);
	TestTrue(TEXT("a miss has a bounded finite endpoint"),
		!MissEnd.ContainsNaN()
		&& FMath::IsNearlyEqual(FVector::Distance(Start, MissEnd),
			TacticalTracerRules::MaxVisualDistanceCm, 0.01f));
	TestTrue(TEXT("a miss follows the spread-adjusted server direction"),
		(MissEnd - Start).GetSafeNormal().Equals(Direction, 0.0001f));

	const FVector WallImpact = Start + Direction * 850.0f;
	TestTrue(TEXT("a stopping wall or character hit ends at its impact"),
		TacticalTracerRules::ChooseVisualEnd(Start, Direction, true, WallImpact)
			.Equals(WallImpact, 0.01f));

	const FVector LastPenetrationImpact = Start + Direction * 2200.0f;
	TestTrue(TEXT("the last stopping penetration impact wins over earlier hits"),
		TacticalTracerRules::ChooseVisualEnd(
			Start, Direction, true, LastPenetrationImpact)
			.Equals(LastPenetrationImpact, 0.01f));
	TestTrue(TEXT("a penetrated target followed by a miss continues to bounded range"),
		TacticalTracerRules::ChooseVisualEnd(
			Start, Direction, false, LastPenetrationImpact).Equals(MissEnd, 0.01f));

	const FVector TooDistantImpact = Start + Direction * 90000.0f;
	TestTrue(TEXT("even an extremely distant hit is presentation-capped"),
		TacticalTracerRules::ChooseVisualEnd(
			Start, Direction, true, TooDistantImpact).Equals(MissEnd, 0.01f));
	TestTrue(TEXT("a degenerate direction creates no visual path"),
		TacticalTracerRules::ChooseVisualEnd(
			Start, FVector::ZeroVector, false, FVector::ZeroVector).Equals(Start));
	TestTrue(TEXT("a malformed stop cannot draw through a nearby surface"),
		TacticalTracerRules::ChooseVisualEnd(
			Start, Direction, true, Start - Direction * 20.0f).Equals(Start));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalTracerNetworkTest,
	"Tcamp.Tactical.PlayerPresentation.TracerNetworkContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalTracerNetworkTest::RunTest(const FString& Parameters)
{
	const UMaterial* TracerMaterial = LoadObject<UMaterial>(nullptr,
		TEXT("/Game/Tactical/FX/M_TacticalTracer.M_TacticalTracer"));
	TestNotNull(TEXT("standalone cooked tracer material exists"), TracerMaterial);
	if (TracerMaterial)
	{
		TestEqual(TEXT("tracer uses additive blending"),
			TracerMaterial->GetBlendMode(), BLEND_Additive);
	}
	TestFalse(TEXT("tracer actor never replicates"),
		ATacticalBulletTracer::StaticClass()->GetDefaultObject<ATacticalBulletTracer>()
			->GetIsReplicated());

	const UFunction* Event = AShooterWeapon::StaticClass()->FindFunctionByName(
		TEXT("MulticastHitscanResult"));
	TestNotNull(TEXT("hitscan impact and tracer share one RPC"), Event);
	if (Event)
	{
		TestTrue(TEXT("shot event is an unreliable multicast"),
			Event->HasAnyFunctionFlags(FUNC_NetMulticast)
			&& !Event->HasAnyFunctionFlags(FUNC_NetReliable));
	}
	TestTrue(TEXT("every-shot setting displays the first shot"),
		TacticalTracerRules::ShouldDisplayShot(1, 1));
	TestTrue(TEXT("every-second-shot setting follows server shot identity"),
		TacticalTracerRules::ShouldDisplayShot(1, 2)
		&& !TacticalTracerRules::ShouldDisplayShot(2, 2)
		&& TacticalTracerRules::ShouldDisplayShot(3, 2));
	return true;
}

#endif
