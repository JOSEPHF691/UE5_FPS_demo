// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/Texture2D.h"
#include "Variant_Shooter/ShooterPlayerController.h"
#include "Variant_Shooter/UI/ShooterCombatFeedbackUI.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalCombatFeedbackStyleTest,
	"Tcamp.Tactical.UI.CombatFeedbackStyle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalCombatFeedbackStyleTest::RunTest(const FString& Parameters)
{
	using namespace ShooterCombatFeedbackStyle;
	const UTexture2D* SightTexture = LoadObject<UTexture2D>(
		nullptr, CrosshairTexturePath);
	const UTexture2D* HitTexture = LoadObject<UTexture2D>(
		nullptr, HitMarkerTexturePath);
	TestNotNull(TEXT("The RAR sight reticle texture loads"), SightTexture);
	TestNotNull(TEXT("The RAR hit marker texture loads"), HitTexture);
	TestTrue(TEXT("The sight texture has a usable canvas size"),
		CrosshairTextureCanvasSize > 0.0f);
	TestTrue(TEXT("The hit marker texture has a usable canvas size"),
		HitMarkerTextureCanvasSize > 0.0f);

	TestTrue(TEXT("The crosshair leaves a visible open center"),
		CrosshairCenterGap > CrosshairLineThickness * 0.5f);
	TestTrue(TEXT("Each crosshair arm has visible length"),
		CrosshairArmLength > CrosshairLineThickness);

	const float HorizontalLeftInnerX = -CrosshairCenterGap;
	const float HorizontalRightInnerX = CrosshairCenterGap;
	const float VerticalTopInnerY = -CrosshairCenterGap;
	const float VerticalBottomInnerY = CrosshairCenterGap;
	TestTrue(TEXT("The horizontal arms stop on opposite sides of the center"),
		HorizontalLeftInnerX < 0.0f && HorizontalRightInnerX > 0.0f);
	TestTrue(TEXT("The vertical arms stop on opposite sides of the center"),
		VerticalTopInnerY < 0.0f && VerticalBottomInnerY > 0.0f);

	TestTrue(TEXT("The hit marker remains outside the crosshair center gap"),
		HitMarkerInnerRadius > CrosshairCenterGap);
	TestTrue(TEXT("The hit marker strokes extend outward"),
		HitMarkerOuterRadius > HitMarkerInnerRadius);

	TestTrue(TEXT("The hit-confirmation duration is positive"), HitMarkerDuration > 0.0f);
	TestTrue(TEXT("The damage-feedback duration is positive"), DamageFeedbackDuration > 0.0f);
	TestTrue(TEXT("The damage vignette has a visible fade-in"),
		DamageFeedbackFadeInDuration > 0.0f);
	TestTrue(TEXT("The damage vignette has a visible hold"),
		DamageFeedbackHoldDuration > 0.0f);
	TestTrue(TEXT("The damage vignette has a visible fade-out"),
		DamageFeedbackFadeOutDuration > 0.0f);
	TestTrue(TEXT("Damage phase durations compose the full display duration"),
		FMath::IsNearlyEqual(
			DamageFeedbackDuration,
			DamageFeedbackFadeInDuration + DamageFeedbackHoldDuration
				+ DamageFeedbackFadeOutDuration));
	TestTrue(TEXT("Hit confirmation is briefer than the damage vignette"),
		HitMarkerDuration < DamageFeedbackDuration);
	TestTrue(TEXT("Damage edge falloff has stronger-than-linear contrast"),
		DamageEdgeContrastPower > 1.0f);
	TestTrue(TEXT("Damage edge arc has a valid horizontal radius"),
		DamageEdgeArcHalfWidth > UE_KINDA_SMALL_NUMBER);
	TestTrue(TEXT("Damage edge arc has a valid inward radius"),
		DamageEdgeArcDepth > UE_KINDA_SMALL_NUMBER);
	TestTrue(TEXT("Damage edge arc strength is normalized"),
		DamageEdgeArcStrength > 0.0f && DamageEdgeArcStrength <= 1.0f);
	TestTrue(TEXT("Damage overlay peak opacity is strong but not opaque"),
		DamageOverlayMaximumOpacity > 0.65f
			&& DamageOverlayMaximumOpacity < 1.0f);
	TestTrue(TEXT("Damage overlay uses a dark red tint"),
		DamageOverlayTintRed > DamageOverlayTintGreen
			&& DamageOverlayTintGreen >= DamageOverlayTintBlue
			&& DamageOverlayTintRed < 0.7f);

	auto VerifyFade = [this](const TCHAR* Label, const float Duration)
	{
		TestEqual(
			FString::Printf(TEXT("%s starts fully visible"), Label),
			ComputeFadeAlpha(Duration, Duration), 1.0f);
		TestTrue(
			FString::Printf(TEXT("%s uses the intended quadratic midpoint"), Label),
			FMath::IsNearlyEqual(ComputeFadeAlpha(Duration * 0.5f, Duration), 0.25f));
		TestEqual(
			FString::Printf(TEXT("%s ends fully transparent"), Label),
			ComputeFadeAlpha(0.0f, Duration), 0.0f);
		TestEqual(
			FString::Printf(TEXT("%s clamps expired time"), Label),
			ComputeFadeAlpha(-Duration, Duration), 0.0f);
		TestEqual(
			FString::Printf(TEXT("%s clamps excess remaining time"), Label),
			ComputeFadeAlpha(Duration * 2.0f, Duration), 1.0f);

		float PreviousAlpha = ComputeFadeAlpha(Duration, Duration);
		for (int32 Step = 1; Step <= 10; ++Step)
		{
			const float RemainingTime = Duration * (1.0f - static_cast<float>(Step) / 10.0f);
			const float CurrentAlpha = ComputeFadeAlpha(RemainingTime, Duration);
			TestTrue(
				FString::Printf(TEXT("%s fade is monotonic at step %d"), Label, Step),
				CurrentAlpha <= PreviousAlpha + UE_KINDA_SMALL_NUMBER);
			PreviousAlpha = CurrentAlpha;
		}
	};

	VerifyFade(TEXT("Hit confirmation"), HitMarkerDuration);
	TestTrue(TEXT("The regular reticle is visible without a hit"),
		ShouldDrawCrosshair(true, 0.0f));
	TestFalse(TEXT("A confirmed hit replaces the regular reticle"),
		ShouldDrawCrosshair(true, HitMarkerDuration));
	TestFalse(TEXT("The regular reticle stays hidden while the hit fades"),
		ShouldDrawCrosshair(true, HitMarkerDuration * 0.5f));
	TestFalse(TEXT("The regular reticle stays hidden until the timer reaches zero"),
		ShouldDrawCrosshair(true, UE_KINDA_SMALL_NUMBER * 0.1f));
	TestTrue(TEXT("The regular reticle returns when the hit ends"),
		ShouldDrawCrosshair(true, 0.0f));
	TestFalse(TEXT("Spectating still hides the regular reticle"),
		ShouldDrawCrosshair(false, 0.0f));

	TestEqual(TEXT("Damage vignette starts transparent"),
		ComputeDamageAlpha(DamageFeedbackDuration, 0.8f), 0.0f);
	TestTrue(TEXT("Damage vignette fades in"),
		FMath::IsNearlyEqual(
			ComputeDamageAlpha(
				DamageFeedbackDuration - DamageFeedbackFadeInDuration * 0.5f,
				0.8f),
			0.4f,
			UE_KINDA_SMALL_NUMBER));
	TestEqual(TEXT("Damage vignette reaches requested strength after fade-in"),
		ComputeDamageAlpha(
			DamageFeedbackDuration - DamageFeedbackFadeInDuration,
			0.8f),
		0.8f);
	TestEqual(TEXT("Damage vignette remains at requested strength while held"),
		ComputeDamageAlpha(
			DamageFeedbackFadeOutDuration + DamageFeedbackHoldDuration * 0.5f,
			0.8f),
		0.8f);
	TestTrue(TEXT("Damage vignette fades out"),
		FMath::IsNearlyEqual(
			ComputeDamageAlpha(DamageFeedbackFadeOutDuration * 0.5f, 0.8f),
			0.4f,
			UE_KINDA_SMALL_NUMBER));
	TestEqual(TEXT("Damage vignette turns off when its display window ends"),
		ComputeDamageAlpha(0.0f, 0.8f), 0.0f);
	TestEqual(TEXT("Damage vignette clamps excessive strength"),
		ComputeDamageAlpha(
			DamageFeedbackDuration - DamageFeedbackFadeInDuration,
			2.0f),
		1.0f);
	TestTrue(TEXT("Repeated damage resumes from the current opacity"),
		FMath::IsNearlyEqual(
			ComputeDamageAlpha(DamageFeedbackDuration, 0.8f, 0.5f),
			0.4f,
			UE_KINDA_SMALL_NUMBER));

	TestEqual(TEXT("Damage spatial mask is transparent at center"),
		ComputeDamageSpatialAlpha(0.0f, 0.0f), 0.0f);
	TestEqual(TEXT("Damage spatial mask reaches full alpha at left edge"),
		ComputeDamageSpatialAlpha(-1.0f, 0.0f), 1.0f);
	TestEqual(TEXT("Damage spatial mask reaches full alpha at right edge"),
		ComputeDamageSpatialAlpha(1.0f, 0.0f), 1.0f);
	TestEqual(TEXT("Damage spatial mask reaches full alpha at top edge"),
		ComputeDamageSpatialAlpha(0.0f, -1.0f), 1.0f);
	TestEqual(TEXT("Damage spatial mask reaches full alpha at bottom edge"),
		ComputeDamageSpatialAlpha(0.0f, 1.0f), 1.0f);
	TestEqual(TEXT("Damage spatial mask clamps corners to full alpha"),
		ComputeDamageSpatialAlpha(1.0f, 1.0f), 1.0f);
	TestTrue(TEXT("Damage spatial mask increases continuously away from center"),
		ComputeDamageSpatialAlpha(0.75f, 0.0f)
			> ComputeDamageSpatialAlpha(0.5f, 0.0f));
	TestTrue(TEXT("Damage fallback is lighter near the center than its old linear mask"),
		ComputeDamageSpatialAlpha(0.5f, 0.0f) < 0.25f);
	TestTrue(TEXT("Damage fallback becomes stronger close to the edge"),
		ComputeDamageSpatialAlpha(0.9f, 0.0f) > 0.81f);
	TestEqual(TEXT("Damage spatial mask is horizontally symmetric"),
		ComputeDamageSpatialAlpha(-0.6f, 0.2f),
		ComputeDamageSpatialAlpha(0.6f, 0.2f));

	auto VerifyScreenDirection = [this](
		const TCHAR* Label,
		const FVector& WorldDirection,
		float ViewYawDegrees,
		const FVector2D& ExpectedDirection)
	{
		const FVector2D ActualDirection = ComputeDamageScreenDirection(
			WorldDirection,
			ViewYawDegrees);
		TestTrue(
			FString::Printf(TEXT("%s X maps to the expected screen side"), Label),
			FMath::IsNearlyEqual(
				ActualDirection.X,
				ExpectedDirection.X,
				UE_KINDA_SMALL_NUMBER));
		TestTrue(
			FString::Printf(TEXT("%s Y maps to the expected screen side"), Label),
			FMath::IsNearlyEqual(
				ActualDirection.Y,
				ExpectedDirection.Y,
				UE_KINDA_SMALL_NUMBER));
	};

	VerifyScreenDirection(
		TEXT("Front damage"), FVector::ForwardVector, 0.0f,
		FVector2D(0.0f, -1.0f));
	VerifyScreenDirection(
		TEXT("Back damage"), -FVector::ForwardVector, 0.0f,
		FVector2D(0.0f, 1.0f));
	VerifyScreenDirection(
		TEXT("Left damage"), -FVector::RightVector, 0.0f,
		FVector2D(-1.0f, 0.0f));
	VerifyScreenDirection(
		TEXT("Right damage"), FVector::RightVector, 0.0f,
		FVector2D(1.0f, 0.0f));
	VerifyScreenDirection(
		TEXT("Left-front damage"), FVector(1.0f, -1.0f, 0.0f), 0.0f,
		FVector2D(-InvSqrtTwo, -InvSqrtTwo));
	VerifyScreenDirection(
		TEXT("Right-front damage"), FVector(1.0f, 1.0f, 0.0f), 0.0f,
		FVector2D(InvSqrtTwo, -InvSqrtTwo));
	VerifyScreenDirection(
		TEXT("Left-back damage"), FVector(-1.0f, -1.0f, 0.0f), 0.0f,
		FVector2D(-InvSqrtTwo, InvSqrtTwo));
	VerifyScreenDirection(
		TEXT("Right-back damage"), FVector(-1.0f, 1.0f, 0.0f), 0.0f,
		FVector2D(InvSqrtTwo, InvSqrtTwo));
	VerifyScreenDirection(
		TEXT("Rotated-view front damage"), FVector::RightVector, 90.0f,
		FVector2D(0.0f, -1.0f));
	TestTrue(TEXT("Vertical-only damage has no horizontal direction"),
		ComputeDamageScreenDirection(FVector::UpVector, 0.0f).IsNearlyZero());

	TestEqual(TEXT("Front damage selects the top sector"),
		QuantizeDamageScreenDirection(FVector2D(0.0f, -1.0f)), 0);
	TestEqual(TEXT("Right damage selects the right sector"),
		QuantizeDamageScreenDirection(FVector2D(1.0f, 0.0f)), 2);
	TestEqual(TEXT("Back damage selects the bottom sector"),
		QuantizeDamageScreenDirection(FVector2D(0.0f, 1.0f)), 4);
	TestEqual(TEXT("Left damage selects the left sector"),
		QuantizeDamageScreenDirection(FVector2D(-1.0f, 0.0f)), 6);
	TestEqual(TEXT("Left-front damage selects the top-left sector"),
		QuantizeDamageScreenDirection(FVector2D(-1.0f, -1.0f)), 7);
	TestEqual(TEXT("Missing direction selects the omnidirectional fallback"),
		QuantizeDamageScreenDirection(FVector2D::ZeroVector), INDEX_NONE);

	TestEqual(TEXT("Front mask is strongest at the top center"),
		ComputeDirectionalDamageSpatialAlpha(0.0f, -1.0f, 0), 1.0f);
	TestEqual(TEXT("Front mask excludes the bottom edge"),
		ComputeDirectionalDamageSpatialAlpha(0.0f, 1.0f, 0), 0.0f);
	TestEqual(TEXT("Front mask covers the complete top-left corner"),
		ComputeDirectionalDamageSpatialAlpha(-1.0f, -1.0f, 0), 1.0f);
	TestEqual(TEXT("Front mask covers the complete top-right corner"),
		ComputeDirectionalDamageSpatialAlpha(1.0f, -1.0f, 0), 1.0f);
	const float FrontNearEdge =
		ComputeDirectionalDamageSpatialAlpha(0.0f, -0.8f, 0);
	const float FrontMidDepth =
		ComputeDirectionalDamageSpatialAlpha(0.0f, -0.5f, 0);
	const float FrontNearCenter =
		ComputeDirectionalDamageSpatialAlpha(0.0f, -0.2f, 0);
	TestTrue(TEXT("Front mask is strongly weighted toward the screen edge"),
		FrontNearEdge > 0.85f);
	TestTrue(TEXT("Front mask becomes substantially lighter at mid depth"),
		FrontMidDepth > 0.35f && FrontMidDepth < 0.60f);
	TestTrue(TEXT("Front mask is faint close to the screen center"),
		FrontNearCenter < 0.08f);
	TestTrue(TEXT("Front mask contrast decreases monotonically toward center"),
		1.0f > FrontNearEdge
			&& FrontNearEdge > FrontMidDepth
			&& FrontMidDepth > FrontNearCenter);
	TestTrue(TEXT("Front mask has a semi-elliptical inward bulge"),
		FrontMidDepth
			> ComputeDirectionalDamageSpatialAlpha(1.0f, -0.5f, 0) + 0.15f);
	TestEqual(TEXT("Front mask stays transparent at screen center"),
		ComputeDirectionalDamageSpatialAlpha(0.0f, 0.0f, 0), 0.0f);

	const float TopLeftAtSharedCorner =
		ComputeDirectionalDamageSpatialAlpha(-1.0f, -1.0f, 7);
	const float TopLeftAtTopCenter =
		ComputeDirectionalDamageSpatialAlpha(0.0f, -1.0f, 7);
	const float TopLeftAtFarTopCorner =
		ComputeDirectionalDamageSpatialAlpha(1.0f, -1.0f, 7);
	TestTrue(TEXT("Top-left mask darkens the top edge toward the left"),
		TopLeftAtSharedCorner > TopLeftAtTopCenter
			&& TopLeftAtTopCenter > TopLeftAtFarTopCorner);
	TestTrue(TEXT("Top-left mask keeps the complete top edge visible"),
		TopLeftAtFarTopCorner
			>= DamageEdgeMinimumDirectionalAlpha - UE_KINDA_SMALL_NUMBER);

	const float TopLeftAtLeftCenter =
		ComputeDirectionalDamageSpatialAlpha(-1.0f, 0.0f, 7);
	const float TopLeftAtFarLeftCorner =
		ComputeDirectionalDamageSpatialAlpha(-1.0f, 1.0f, 7);
	TestTrue(TEXT("Top-left mask darkens the left edge toward the top"),
		TopLeftAtSharedCorner > TopLeftAtLeftCenter
			&& TopLeftAtLeftCenter > TopLeftAtFarLeftCorner);
	TestTrue(TEXT("Top-left mask keeps the complete left edge visible"),
		TopLeftAtFarLeftCorner
			>= DamageEdgeMinimumDirectionalAlpha - UE_KINDA_SMALL_NUMBER);
	TestEqual(TEXT("Top-left mask stays transparent at screen center"),
		ComputeDirectionalDamageSpatialAlpha(0.0f, 0.0f, 7), 0.0f);
	TestEqual(TEXT("Top-left mask excludes the opposite right edge center"),
		ComputeDirectionalDamageSpatialAlpha(1.0f, 0.0f, 7), 0.0f);
	TestEqual(TEXT("Top-left mask excludes the opposite bottom edge center"),
		ComputeDirectionalDamageSpatialAlpha(0.0f, 1.0f, 7), 0.0f);
	TestEqual(TEXT("Invalid directional mask index stays transparent"),
		ComputeDirectionalDamageSpatialAlpha(0.0f, -1.0f, INDEX_NONE), 0.0f);

	constexpr float AlongEdgeSamples[] = { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f };
	constexpr float InwardDepthSamples[] = { 0.25f, 0.5f, 0.75f, 1.0f };
	for (const float AlongEdge : AlongEdgeSamples)
	{
		for (const float InwardDepth : InwardDepthSamples)
		{
			const float FrontAlpha = ComputeDirectionalDamageSpatialAlpha(
				AlongEdge, -InwardDepth, 0);
			TestTrue(TEXT("Bottom edge uses the same full-width inward gradient as front"),
				FMath::IsNearlyEqual(
					ComputeDirectionalDamageSpatialAlpha(
						AlongEdge, InwardDepth, 4),
					FrontAlpha));
			TestTrue(TEXT("Left edge uses the same full-width inward gradient as front"),
				FMath::IsNearlyEqual(
					ComputeDirectionalDamageSpatialAlpha(
						-InwardDepth, AlongEdge, 6),
					FrontAlpha));
			TestTrue(TEXT("Right edge uses the same full-width inward gradient as front"),
				FMath::IsNearlyEqual(
					ComputeDirectionalDamageSpatialAlpha(
						InwardDepth, AlongEdge, 2),
					FrontAlpha));
		}
	}

	const float CenterTexelOffset = 1.0f / DamageGradientTextureSize;
	constexpr float CenterTexelSigns[] = { -1.0f, 1.0f };
	for (int32 DirectionIndex = 0;
		DirectionIndex < DamageDirectionCount;
		++DirectionIndex)
	{
		const FVector2D SectorDirection =
			GetDamageSectorDirection(DirectionIndex);
		TestEqual(
			FString::Printf(TEXT("Sector %d quantizes to itself"), DirectionIndex),
			QuantizeDamageScreenDirection(SectorDirection),
			DirectionIndex);

		const float DominantComponent = FMath::Max(
			FMath::Abs(SectorDirection.X),
			FMath::Abs(SectorDirection.Y));
		const FVector2D TargetEdge = SectorDirection / DominantComponent;
		TestTrue(
			FString::Printf(TEXT("Sector %d illuminates its target edge"), DirectionIndex),
			ComputeDirectionalDamageSpatialAlpha(
				TargetEdge.X, TargetEdge.Y, DirectionIndex) > 0.25f);
		TestEqual(
			FString::Printf(TEXT("Sector %d excludes the opposite edge"), DirectionIndex),
			ComputeDirectionalDamageSpatialAlpha(
				-TargetEdge.X, -TargetEdge.Y, DirectionIndex),
			0.0f);
		for (const float CenterXSign : CenterTexelSigns)
		{
			for (const float CenterYSign : CenterTexelSigns)
			{
				TestEqual(
					FString::Printf(
						TEXT("Sector %d keeps generated center texel %.0f,%.0f transparent"),
						DirectionIndex, CenterXSign, CenterYSign),
					FMath::RoundToInt(
						ComputeDirectionalDamageSpatialAlpha(
							CenterTexelOffset * CenterXSign,
							CenterTexelOffset * CenterYSign,
							DirectionIndex) * 255.0f),
					0);
			}
		}
	}

	const UFunction* ConfirmedHitRPC = AShooterPlayerController::StaticClass()->FindFunctionByName(
		TEXT("ClientNotifyEnemyHitConfirmed"));
	TestNotNull(TEXT("The confirmed-hit client RPC is reflected"), ConfirmedHitRPC);
	if (ConfirmedHitRPC)
	{
		TestTrue(TEXT("The confirmed-hit notification is networked"),
			ConfirmedHitRPC->HasAnyFunctionFlags(FUNC_Net));
		TestTrue(TEXT("The confirmed-hit notification targets the owning client"),
			ConfirmedHitRPC->HasAnyFunctionFlags(FUNC_NetClient));
		TestFalse(TEXT("The confirmed-hit notification is not multicast"),
			ConfirmedHitRPC->HasAnyFunctionFlags(FUNC_NetMulticast));
	}

	const UFunction* DamageDirectionRPC =
		AShooterPlayerController::StaticClass()->FindFunctionByName(
			TEXT("ClientNotifyDamageReceived"));
	TestNotNull(TEXT("The damage-direction client RPC is reflected"), DamageDirectionRPC);
	if (DamageDirectionRPC)
	{
		TestTrue(TEXT("The damage-direction notification is networked"),
			DamageDirectionRPC->HasAnyFunctionFlags(FUNC_Net));
		TestTrue(TEXT("The damage-direction notification targets the owning client"),
			DamageDirectionRPC->HasAnyFunctionFlags(FUNC_NetClient));
		TestFalse(TEXT("The damage-direction notification is not multicast"),
			DamageDirectionRPC->HasAnyFunctionFlags(FUNC_NetMulticast));
		TestFalse(TEXT("The cosmetic damage-direction notification is unreliable"),
			DamageDirectionRPC->HasAnyFunctionFlags(FUNC_NetReliable));
	}

	return true;
}

#endif
