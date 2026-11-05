// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Weapons/TacticalBallisticRules.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalBallisticShotPathTest,
	"Tcamp.Tactical.Combat.BallisticShotPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalBallisticShotPathTest::RunTest(const FString& Parameters)
{
	using namespace TacticalBallisticRules;
	const FVector Eye(0.0f, 0.0f, 170.0f);
	const FVector Muzzle(35.0f, 12.0f, 140.0f);
	const FVector CameraTarget(1000.0f, 12.0f, 140.0f);
	const FVector DesiredSpawn = CalculateDesiredSpawn(
		Muzzle, CameraTarget, FVector::ForwardVector, 10.0f);
	TestTrue(TEXT("authored muzzle offset is applied only to the gameplay muzzle"),
		DesiredSpawn.Equals(Muzzle + FVector(10.0f, 0.0f, 0.0f), 0.01f));
	TestTrue(TEXT("negative offset cannot launch behind the gameplay muzzle"),
		CalculateDesiredSpawn(Muzzle, CameraTarget, FVector::ForwardVector, -10.0f)
			.Equals(Muzzle, 0.01f));
	TestTrue(TEXT("degenerate camera target uses a stable fallback direction"),
		CalculateDesiredSpawn(Muzzle, Muzzle, FVector::ForwardVector, 10.0f)
			.Equals(Muzzle + FVector(10.0f, 0.0f, 0.0f), 0.01f));

	const FHitResult NoHit;
	const FShotPath ClearPath = SelectShotPath(
		Eye, Muzzle, DesiredSpawn, CameraTarget, NoHit, NoHit);
	TestFalse(TEXT("clear sight and barrel path are not blocked"), ClearPath.IsBlocked());
	TestTrue(TEXT("clear shot launches from the offset gameplay muzzle"),
		ClearPath.Origin.Equals(DesiredSpawn, 0.01f)
		&& ClearPath.Target.Equals(CameraTarget, 0.01f));

	FHitResult EyeToMuzzleHit;
	EyeToMuzzleHit.bBlockingHit = true;
	EyeToMuzzleHit.ImpactPoint = FVector(20.0f, 6.0f, 155.0f);
	FHitResult MuzzleToSpawnHit;
	MuzzleToSpawnHit.bBlockingHit = true;
	MuzzleToSpawnHit.ImpactPoint = Muzzle + FVector(4.0f, 0.0f, 0.0f);

	const FShotPath EyeBlockedPath = SelectShotPath(
		Eye, Muzzle, DesiredSpawn, CameraTarget,
		EyeToMuzzleHit, MuzzleToSpawnHit);
	TestTrue(TEXT("eye-to-muzzle cover wins before any muzzle offset cover"),
		EyeBlockedPath.BlockedSegment == EBlockedSegment::EyeToMuzzle
		&& EyeBlockedPath.Origin.Equals(Eye, 0.01f)
		&& EyeBlockedPath.Target.Equals(EyeToMuzzleHit.ImpactPoint, 0.01f));

	const FShotPath OffsetBlockedPath = SelectShotPath(
		Eye, Muzzle, DesiredSpawn, CameraTarget,
		NoHit, MuzzleToSpawnHit);
	TestTrue(TEXT("thin wall inside muzzle offset intercepts the shot"),
		OffsetBlockedPath.BlockedSegment == EBlockedSegment::MuzzleToSpawn
		&& OffsetBlockedPath.Origin.Equals(Muzzle, 0.01f)
		&& OffsetBlockedPath.Target.Equals(MuzzleToSpawnHit.ImpactPoint, 0.01f));
	return true;
}

#endif
