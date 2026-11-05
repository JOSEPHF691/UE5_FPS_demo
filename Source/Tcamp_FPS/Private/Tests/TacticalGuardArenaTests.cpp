// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Variant_Shooter/AI/TacticalGuardArena.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalGuardArenaBoundaryTest,
	"Tcamp.Tactical.Treasure.GuardArenaBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalGuardArenaBoundaryTest::RunTest(const FString& Parameters)
{
	ATacticalGuardArena* AuthoredArena = NewObject<ATacticalGuardArena>();
	TestTrue(TEXT("ground navigation remains inside with side-wall clearance"),
		AuthoredArena->IsPointInsideWithInset(FVector(0.0f, 0.0f, -400.0f), 100.0f));
	TestFalse(TEXT("side-wall clearance still rejects near-wall navigation"),
		AuthoredArena->IsPointInsideWithInset(FVector(1650.0f, 0.0f, 0.0f), 100.0f));

	const FTransform Box(FRotator::ZeroRotator, FVector::ZeroVector);
	const FVector Extent(1000.0f, 800.0f, 400.0f);
	FVector Hit = FVector::ZeroVector;
	FVector Normal = FVector::ZeroVector;
	const auto Crosses = [&](const FTransform& Transform, const FVector& Start,
		const FVector& End)
	{
		return ATacticalGuardArena::FindBoxBoundaryIntersection(
			Transform, Extent, Start, End, Hit, Normal);
	};

	TestFalse(TEXT("two points inside do not cross the boundary"),
		Crosses(Box, FVector(-300.0f, 0.0f, 0.0f), FVector(300.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("inside-to-outside is blocked"),
		Crosses(Box, FVector::ZeroVector, FVector(1500.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("inside-to-outside stops at the first wall"),
		Hit.Equals(FVector(1000.0f, 0.0f, 0.0f), 0.01f)
		&& Normal.Equals(FVector::ForwardVector, 0.01f));
	TestTrue(TEXT("outside-to-inside is blocked"),
		Crosses(Box, FVector(-1500.0f, 0.0f, 0.0f), FVector::ZeroVector));
	TestTrue(TEXT("outside-to-outside through the arena is blocked"),
		Crosses(Box, FVector(-1500.0f, 0.0f, 0.0f), FVector(1500.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("outside-to-outside stops at entry, not exit"),
		Hit.Equals(FVector(-1000.0f, 0.0f, 0.0f), 0.01f));
	TestFalse(TEXT("two outside points on the same side may shoot each other"),
		Crosses(Box, FVector(-1500.0f, 0.0f, 0.0f), FVector(-1200.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("a shot grazing the wall is conservatively blocked"),
		Crosses(Box, FVector(-1500.0f, 800.0f, 0.0f), FVector(1500.0f, 800.0f, 0.0f)));
	TestFalse(TEXT("a line above the open top does not touch a side panel"),
		Crosses(Box, FVector(-1500.0f, 0.0f, 450.0f), FVector(1500.0f, 0.0f, 450.0f)));
	TestFalse(TEXT("a line entering through the open top is allowed"),
		Crosses(Box, FVector(0.0f, 0.0f, 600.0f), FVector(0.0f, 0.0f, 0.0f)));
	TestFalse(TEXT("a line leaving through the open bottom is allowed"),
		Crosses(Box, FVector(0.0f, 0.0f, 0.0f), FVector(0.0f, 0.0f, -600.0f)));
	TestFalse(TEXT("a line crosses the side plane above the wall height"),
		Crosses(Box, FVector(-1500.0f, 0.0f, 450.0f), FVector(0.0f, 0.0f, 400.0f)));
	TestTrue(TEXT("a shot through the open top into a side panel is blocked"),
		Crosses(Box, FVector(0.0f, 0.0f, 600.0f), FVector(1500.0f, 0.0f, 0.0f)));

	const FTransform Rotated(FRotator(0.0f, 90.0f, 0.0f), FVector(50.0f, -75.0f, 200.0f));
	TestTrue(TEXT("rotating the authored arena rotates its blocking boundary"),
		Crosses(Rotated, FVector(50.0f, -1500.0f, 200.0f),
			FVector(50.0f, 1500.0f, 200.0f)));
	TestTrue(TEXT("rotated boundary reports world-space first contact"),
		Hit.Equals(FVector(50.0f, -1075.0f, 200.0f), 0.01f));
	return true;
}

#endif
