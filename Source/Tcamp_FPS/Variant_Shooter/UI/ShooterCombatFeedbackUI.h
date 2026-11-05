// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Containers/StaticArray.h"
#include "Styling/SlateBrush.h"
#include "ShooterCombatFeedbackUI.generated.h"

class UTexture2D;

namespace ShooterCombatFeedbackStyle
{
	inline constexpr TCHAR CrosshairTexturePath[] = TEXT(
		"/Game/RAR/Content/InfimaGames/ArtCore/Weapons/Guns/Textures/"
		"T_IG_ATT_Sight_Dot_03_D.T_IG_ATT_Sight_Dot_03_D");
	inline constexpr TCHAR HitMarkerTexturePath[] = TEXT(
		"/Game/RAR/Content/InfimaGames/ArtCore/Weapons/Guns/Textures/"
		"T_IG_UI_Hitmarker.T_IG_UI_Hitmarker");
	// Both source textures include generous transparent padding. These canvas
	// sizes make their visible marks roughly 32 pixels wide at 100% UI scale.
	inline constexpr float CrosshairTextureCanvasSize = 60.0f;
	inline constexpr float HitMarkerTextureCanvasSize = 48.0f;
	inline constexpr float CrosshairCenterGap = 5.0f;
	inline constexpr float CrosshairArmLength = 9.0f;
	inline constexpr float CrosshairLineThickness = 2.0f;
	inline constexpr float HitMarkerInnerRadius = 9.0f;
	inline constexpr float HitMarkerOuterRadius = 16.0f;
	inline constexpr float HitMarkerDuration = 0.16f;
	inline constexpr int32 SniperScopeMaskTextureSize = 512;
	inline constexpr float SniperScopeActivationAlpha = 0.9f;
	inline constexpr float SniperScopeReticleOuterRadiusFraction = 0.43f;
	inline constexpr float SniperScopeReticleCenterGap = 4.0f;
	inline constexpr float DamageFeedbackFadeInDuration = 0.08f;
	inline constexpr float DamageFeedbackHoldDuration = 0.10f;
	inline constexpr float DamageFeedbackFadeOutDuration = 0.32f;
	inline constexpr float DamageFeedbackDuration = DamageFeedbackFadeInDuration
		+ DamageFeedbackHoldDuration + DamageFeedbackFadeOutDuration;
	inline constexpr int32 DamageGradientTextureSize = 128;
	inline constexpr int32 DamageDirectionCount = 8;
	inline constexpr float InvSqrtTwo = 0.70710678118f;
	inline constexpr float DamageEdgeMinimumDirectionalAlpha = 0.32f;
	inline constexpr float DamageEdgeContrastPower = 2.0f;
	inline constexpr float DamageEdgeArcAnchor = 0.55f;
	inline constexpr float DamageEdgeArcHalfWidth = 0.95f;
	inline constexpr float DamageEdgeArcDepth = 0.90f;
	inline constexpr float DamageEdgeArcStrength = 0.65f;
	inline constexpr float DamageOverlayTintRed = 0.62f;
	inline constexpr float DamageOverlayTintGreen = 0.002f;
	inline constexpr float DamageOverlayTintBlue = 0.0f;
	inline constexpr float DamageOverlayMaximumOpacity = 0.70f;

	inline float ComputeFadeAlpha(float RemainingTime, float Duration)
	{
		const float LinearAlpha = Duration > UE_KINDA_SMALL_NUMBER
			? FMath::Clamp(RemainingTime / Duration, 0.0f, 1.0f)
			: 0.0f;
		return LinearAlpha * LinearAlpha;
	}

	/** Hit confirmation replaces, rather than overlays, the regular reticle. */
	inline bool ShouldDrawCrosshair(
		bool bCrosshairVisible, float HitFeedbackRemaining)
	{
		return bCrosshairVisible && HitFeedbackRemaining <= 0.0f;
	}

	inline float SmoothStep01(float Alpha)
	{
		const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
		return ClampedAlpha * ClampedAlpha * (3.0f - 2.0f * ClampedAlpha);
	}

	/**
	 * Time envelope for the damage vignette. FadeInStartAlpha keeps rapid,
	 * repeated hits continuous instead of restarting the overlay at zero.
	 */
	inline float ComputeDamageEnvelopeAlpha(
		float RemainingTime,
		float FadeInStartAlpha = 0.0f)
	{
		if (RemainingTime <= UE_KINDA_SMALL_NUMBER)
		{
			return 0.0f;
		}

		const float ElapsedTime = FMath::Clamp(
			DamageFeedbackDuration - RemainingTime,
			0.0f,
			DamageFeedbackDuration);
		if (ElapsedTime < DamageFeedbackFadeInDuration)
		{
			const float FadeProgress = DamageFeedbackFadeInDuration > UE_KINDA_SMALL_NUMBER
				? ElapsedTime / DamageFeedbackFadeInDuration
				: 1.0f;
			return FMath::Lerp(
				FMath::Clamp(FadeInStartAlpha, 0.0f, 1.0f),
				1.0f,
				SmoothStep01(FadeProgress));
		}

		const float FadeOutStartTime =
			DamageFeedbackFadeInDuration + DamageFeedbackHoldDuration;
		if (ElapsedTime < FadeOutStartTime)
		{
			return 1.0f;
		}

		const float FadeOutProgress = DamageFeedbackFadeOutDuration > UE_KINDA_SMALL_NUMBER
			? (ElapsedTime - FadeOutStartTime) / DamageFeedbackFadeOutDuration
			: 1.0f;
		return 1.0f - SmoothStep01(FadeOutProgress);
	}

	inline float ComputeDamageAlpha(
		float RemainingTime,
		float Strength,
		float FadeInStartAlpha = 0.0f)
	{
		return ComputeDamageEnvelopeAlpha(RemainingTime, FadeInStartAlpha)
			* FMath::Clamp(Strength, 0.0f, 1.0f);
	}

	/**
	 * Elliptical distance mask in normalized viewport space (-1..1).
	 * Zero at the center and one on every edge/corner, with no quadrant layering.
	 */
	inline float ComputeDamageSpatialAlpha(float NormalizedX, float NormalizedY)
	{
		const float RadiusSquared = FMath::Clamp(
			NormalizedX * NormalizedX + NormalizedY * NormalizedY,
			0.0f,
			1.0f);
		return SmoothStep01(RadiusSquared);
	}

	/**
	 * Converts a horizontal world direction from the victim towards the damage
	 * source into screen space: +X is right and -Y is the top of the viewport.
	 */
	inline FVector2D ComputeDamageScreenDirection(
		const FVector& WorldDirectionToSource,
		float ViewYawDegrees)
	{
		const FVector2D FlatDirection(
			WorldDirectionToSource.X,
			WorldDirectionToSource.Y);
		if (FlatDirection.IsNearlyZero())
		{
			return FVector2D::ZeroVector;
		}

		const FVector2D Direction = FlatDirection.GetSafeNormal();
		const float ViewYawRadians = FMath::DegreesToRadians(ViewYawDegrees);
		const FVector2D ViewForward(
			FMath::Cos(ViewYawRadians),
			FMath::Sin(ViewYawRadians));
		const FVector2D ViewRight(-ViewForward.Y, ViewForward.X);
		return FVector2D(
			FVector2D::DotProduct(Direction, ViewRight),
			-FVector2D::DotProduct(Direction, ViewForward)).GetSafeNormal();
	}

	/** Clockwise sectors: top, top-right, right, ... , top-left. */
	inline FVector2D GetDamageSectorDirection(int32 DirectionIndex)
	{
		switch ((DirectionIndex % DamageDirectionCount + DamageDirectionCount)
			% DamageDirectionCount)
		{
		case 0: return FVector2D(0.0f, -1.0f);
		case 1: return FVector2D(InvSqrtTwo, -InvSqrtTwo);
		case 2: return FVector2D(1.0f, 0.0f);
		case 3: return FVector2D(InvSqrtTwo, InvSqrtTwo);
		case 4: return FVector2D(0.0f, 1.0f);
		case 5: return FVector2D(-InvSqrtTwo, InvSqrtTwo);
		case 6: return FVector2D(-1.0f, 0.0f);
		default: return FVector2D(-InvSqrtTwo, -InvSqrtTwo);
		}
	}

	inline int32 QuantizeDamageScreenDirection(const FVector2D& ScreenDirection)
	{
		if (ScreenDirection.IsNearlyZero())
		{
			return INDEX_NONE;
		}

		const FVector2D Direction = ScreenDirection.GetSafeNormal();
		int32 BestDirectionIndex = 0;
		float BestAlignment = -1.0f;
		for (int32 DirectionIndex = 0;
			DirectionIndex < DamageDirectionCount;
			++DirectionIndex)
		{
			const float Alignment = FVector2D::DotProduct(
				Direction,
				GetDamageSectorDirection(DirectionIndex));
			if (Alignment > BestAlignment)
			{
				BestAlignment = Alignment;
				BestDirectionIndex = DirectionIndex;
			}
		}
		return BestDirectionIndex;
	}

	/**
	 * Keeps the complete selected edge visible while making the side closest to
	 * a diagonal source darker. Cardinal directions have no along-edge bias.
	 */
	inline float ComputeDamageEdgeDirectionalBias(
		float AlongEdgePosition,
		float AlongEdgeDirection)
	{
		if (FMath::IsNearlyZero(AlongEdgeDirection))
		{
			return 1.0f;
		}

		const float TowardSource = FMath::Clamp(
			0.5f + 0.5f * AlongEdgePosition * FMath::Sign(AlongEdgeDirection),
			0.0f,
			1.0f);
		return FMath::Lerp(
			DamageEdgeMinimumDirectionalAlpha,
			1.0f,
			SmoothStep01(TowardSource));
	}

	/**
	 * Combines a high-contrast full-edge falloff with a soft semi-elliptical
	 * bulge. The bulge is centered for cardinal damage and shifts toward the
	 * source side for diagonal damage.
	 */
	inline float ComputeDamageEdgeArcAlpha(
		float TowardEdgePosition,
		float AlongEdgePosition,
		float AlongEdgeDirection)
	{
		const float EdgeProgress = FMath::Clamp(TowardEdgePosition, 0.0f, 1.0f);
		const float EdgeAlpha = FMath::Pow(
			SmoothStep01(EdgeProgress),
			DamageEdgeContrastPower);

		const float ArcCenter = FMath::IsNearlyZero(AlongEdgeDirection)
			? 0.0f
			: FMath::Sign(AlongEdgeDirection) * DamageEdgeArcAnchor;
		const float AlongDistance =
			(AlongEdgePosition - ArcCenter) / DamageEdgeArcHalfWidth;
		const float InwardDistance =
			(1.0f - EdgeProgress) / DamageEdgeArcDepth;
		const float ArcDistance = FMath::Sqrt(
			AlongDistance * AlongDistance + InwardDistance * InwardDistance);
		const float ArcAlpha =
			(1.0f - SmoothStep01(ArcDistance)) * DamageEdgeArcStrength;

		return FMath::Clamp(
			1.0f - (1.0f - EdgeAlpha) * (1.0f - ArcAlpha),
			0.0f,
			1.0f);
	}

	/**
	 * Covers the full edge selected by the source direction. Diagonal hits join
	 * two complete neighboring edges and bias both gradients toward their shared
	 * corner, so direction is readable without returning to small isolated lobes.
	 */
	inline float ComputeDirectionalDamageSpatialAlpha(
		float NormalizedX,
		float NormalizedY,
		int32 DirectionIndex)
	{
		if (DirectionIndex < 0 || DirectionIndex >= DamageDirectionCount)
		{
			return 0.0f;
		}

		const FVector2D Direction = GetDamageSectorDirection(DirectionIndex);
		const float DominantMagnitude = FMath::Max(
			FMath::Abs(Direction.X),
			FMath::Abs(Direction.Y));
		if (DominantMagnitude <= UE_KINDA_SMALL_NUMBER)
		{
			return 0.0f;
		}

		const float EdgeX = Direction.X / DominantMagnitude;
		const float EdgeY = Direction.Y / DominantMagnitude;
		const float Top = FMath::Max(-EdgeY, 0.0f)
			* ComputeDamageEdgeArcAlpha(-NormalizedY, NormalizedX, EdgeX)
			* ComputeDamageEdgeDirectionalBias(NormalizedX, EdgeX);
		const float Bottom = FMath::Max(EdgeY, 0.0f)
			* ComputeDamageEdgeArcAlpha(NormalizedY, NormalizedX, EdgeX)
			* ComputeDamageEdgeDirectionalBias(NormalizedX, EdgeX);
		const float Left = FMath::Max(-EdgeX, 0.0f)
			* ComputeDamageEdgeArcAlpha(-NormalizedX, NormalizedY, EdgeY)
			* ComputeDamageEdgeDirectionalBias(NormalizedY, EdgeY);
		const float Right = FMath::Max(EdgeX, 0.0f)
			* ComputeDamageEdgeArcAlpha(NormalizedX, NormalizedY, EdgeY)
			* ComputeDamageEdgeDirectionalBias(NormalizedY, EdgeY);

		return FMath::Clamp(
			1.0f - (1.0f - Top) * (1.0f - Bottom)
				* (1.0f - Left) * (1.0f - Right),
			0.0f,
			1.0f);
	}
}

/**
 * Native combat feedback drawn over the owning viewport.
 *
 * The RAR sight texture and hit marker are used when available, with native
 * line art as a missing-asset fallback. Hit confirmation remains
 * server-authoritative and the damage gradient remains code-generated.
 */
UCLASS()
class TCAMP_FPS_API UShooterCombatFeedbackUI : public UUserWidget
{
	GENERATED_BODY()

public:
	UShooterCombatFeedbackUI(const FObjectInitializer& ObjectInitializer);

	void ShowHitConfirmation();
	/** Full-edge fallback used only when the source direction is unavailable. */
	void ShowDamageFeedback(float Strength = 1.0f);
	void ShowDirectionalDamageFeedback(
		const FVector2D& ScreenDirection,
		float Strength = 1.0f);
	void ResetTransientFeedback();
	void SetCrosshairVisible(bool bVisible);
	bool IsCrosshairVisible() const { return bCrosshairVisible; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	/** Reticle hides only after the owner's visible sight has reached the view axis. */
	bool ShouldHideCrosshairForIronSight() const;
	/** The Sniper uses a screen-space optic only after its local ADS transition. */
	bool ShouldDrawSniperScope() const;
	bool bLastIronSightHidden = false;
	bool bLastSniperScopeVisible = false;
	struct FDamageFeedbackState
	{
		float Remaining = 0.0f;
		float Strength = 0.0f;
		float FadeInStartAlpha = 0.0f;
	};

	void BuildNativeWidgetTree();
	void BuildVisualAssetBrushes();
	void BuildDamageGradientTextures();
	void BuildSniperScopeMaskTexture();
	UTexture2D* CreateDamageGradientTexture(int32 DirectionIndex) const;
	static void RestartDamageFeedbackState(
		FDamageFeedbackState& FeedbackState,
		float Strength);
	static bool TickDamageFeedbackState(
		FDamageFeedbackState& FeedbackState,
		float DeltaTime);
	static float GetDamageFeedbackStateAlpha(
		const FDamageFeedbackState& FeedbackState);

	// Serialized CDO references ensure both textures enter cooked packages.
	UPROPERTY()
	TObjectPtr<UTexture2D> CrosshairTexture;

	UPROPERTY()
	TObjectPtr<UTexture2D> HitMarkerTexture;

	FSlateBrush CrosshairBrush;
	FSlateBrush HitMarkerBrush;

	/** Generated once per owning viewport; transparent center, opaque black exterior. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> SniperScopeMaskTexture;

	FSlateBrush SniperScopeMaskBrush;
	FSlateBrush SniperScopeBlackBrush;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> DamageGradientTexture;

	FSlateBrush DamageGradientBrush;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> DirectionalDamageGradientTextures;

	TArray<FSlateBrush> DirectionalDamageGradientBrushes;

	float HitFeedbackRemaining = 0.0f;
	FDamageFeedbackState OmnidirectionalDamageFeedback;
	TStaticArray<FDamageFeedbackState,
		ShooterCombatFeedbackStyle::DamageDirectionCount> DirectionalDamageFeedback;
	bool bCrosshairVisible = true;
};
