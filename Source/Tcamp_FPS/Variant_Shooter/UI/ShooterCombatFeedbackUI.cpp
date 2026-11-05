// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ShooterCombatFeedbackUI.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/CanvasPanel.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "UObject/ConstructorHelpers.h"
#include "Variant_Shooter/TacticalCharacter.h"
#include "Variant_Shooter/Weapons/TacticalWeapon.h"
#include "Components/WeaponInventoryComponent.h"

UShooterCombatFeedbackUI::UShooterCombatFeedbackUI(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UTexture2D> CrosshairAsset(
		ShooterCombatFeedbackStyle::CrosshairTexturePath);
	CrosshairTexture = CrosshairAsset.Object;

	static ConstructorHelpers::FObjectFinder<UTexture2D> HitMarkerAsset(
		ShooterCombatFeedbackStyle::HitMarkerTexturePath);
	HitMarkerTexture = HitMarkerAsset.Object;
}

void UShooterCombatFeedbackUI::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildNativeWidgetTree();
	BuildVisualAssetBrushes();
	BuildDamageGradientTextures();
	BuildSniperScopeMaskTexture();
}

void UShooterCombatFeedbackUI::BuildVisualAssetBrushes()
{
	SniperScopeBlackBrush = FSlateColorBrush(FLinearColor::Black);
	CrosshairBrush = FSlateBrush();
	CrosshairBrush.DrawAs = ESlateBrushDrawType::Image;
	CrosshairBrush.ImageSize = FVector2D(
		ShooterCombatFeedbackStyle::CrosshairTextureCanvasSize,
		ShooterCombatFeedbackStyle::CrosshairTextureCanvasSize);
	CrosshairBrush.SetResourceObject(CrosshairTexture);

	HitMarkerBrush = FSlateBrush();
	HitMarkerBrush.DrawAs = ESlateBrushDrawType::Image;
	HitMarkerBrush.ImageSize = FVector2D(
		ShooterCombatFeedbackStyle::HitMarkerTextureCanvasSize,
		ShooterCombatFeedbackStyle::HitMarkerTextureCanvasSize);
	HitMarkerBrush.SetResourceObject(HitMarkerTexture);
}

void UShooterCombatFeedbackUI::BuildNativeWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("CombatFeedbackRoot"));
	Root->SetVisibility(ESlateVisibility::HitTestInvisible);
	WidgetTree->RootWidget = Root;
}

UTexture2D* UShooterCombatFeedbackUI::CreateDamageGradientTexture(
	int32 DirectionIndex) const
{
	const int32 TextureSize = ShooterCombatFeedbackStyle::DamageGradientTextureSize;
	UTexture2D* Texture = UTexture2D::CreateTransient(
		// NAME_None lets the engine assign a unique object name for every local
		// viewport. This matters when multiple PIE clients share one process.
		TextureSize, TextureSize, PF_B8G8R8A8, NAME_None);
	if (!Texture || !Texture->GetPlatformData()
		|| Texture->GetPlatformData()->Mips.IsEmpty())
	{
		return nullptr;
	}

	Texture->SRGB = false;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->LODGroup = TEXTUREGROUP_UI;
	Texture->NeverStream = true;

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(TextureSize * TextureSize);
	for (int32 Y = 0; Y < TextureSize; ++Y)
	{
		const float NormalizedY =
			((static_cast<float>(Y) + 0.5f) / static_cast<float>(TextureSize)) * 2.0f - 1.0f;
		for (int32 X = 0; X < TextureSize; ++X)
		{
			const float NormalizedX =
				((static_cast<float>(X) + 0.5f) / static_cast<float>(TextureSize)) * 2.0f - 1.0f;
			const float SpatialAlpha = DirectionIndex == INDEX_NONE
				? ShooterCombatFeedbackStyle::ComputeDamageSpatialAlpha(
					NormalizedX, NormalizedY)
				: ShooterCombatFeedbackStyle::ComputeDirectionalDamageSpatialAlpha(
					NormalizedX, NormalizedY, DirectionIndex);
			Pixels[Y * TextureSize + X] = FColor(
				255, 255, 255, FMath::RoundToInt(SpatialAlpha * 255.0f));
		}
	}

	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	return Texture;
}

void UShooterCombatFeedbackUI::BuildDamageGradientTextures()
{
	bool bHasCompleteDirectionalTextures =
		DirectionalDamageGradientTextures.Num()
			== ShooterCombatFeedbackStyle::DamageDirectionCount;
	if (bHasCompleteDirectionalTextures)
	{
		for (const TObjectPtr<UTexture2D>& Texture
			: DirectionalDamageGradientTextures)
		{
			if (!Texture)
			{
				bHasCompleteDirectionalTextures = false;
				break;
			}
		}
	}

	bool bHasCompleteDirectionalBrushes =
		DirectionalDamageGradientBrushes.Num()
			== ShooterCombatFeedbackStyle::DamageDirectionCount;
	if (bHasCompleteDirectionalBrushes && bHasCompleteDirectionalTextures)
	{
		for (int32 DirectionIndex = 0;
			DirectionIndex < ShooterCombatFeedbackStyle::DamageDirectionCount;
			++DirectionIndex)
		{
			if (DirectionalDamageGradientBrushes[DirectionIndex].GetResourceObject()
				!= DirectionalDamageGradientTextures[DirectionIndex])
			{
				bHasCompleteDirectionalBrushes = false;
				break;
			}
		}
	}

	if (DamageGradientTexture && bHasCompleteDirectionalTextures
		&& bHasCompleteDirectionalBrushes
		&& DamageGradientBrush.GetResourceObject() == DamageGradientTexture)
	{
		return;
	}

	DamageGradientTexture = CreateDamageGradientTexture(INDEX_NONE);

	DamageGradientBrush = FSlateBrush();
	DamageGradientBrush.DrawAs = ESlateBrushDrawType::Image;
	DamageGradientBrush.ImageSize = FVector2D(
		ShooterCombatFeedbackStyle::DamageGradientTextureSize,
		ShooterCombatFeedbackStyle::DamageGradientTextureSize);
	DamageGradientBrush.SetResourceObject(DamageGradientTexture);

	DirectionalDamageGradientTextures.SetNum(
		ShooterCombatFeedbackStyle::DamageDirectionCount);
	DirectionalDamageGradientBrushes.SetNum(
		ShooterCombatFeedbackStyle::DamageDirectionCount);
	for (int32 DirectionIndex = 0;
		DirectionIndex < ShooterCombatFeedbackStyle::DamageDirectionCount;
		++DirectionIndex)
	{
		DirectionalDamageGradientTextures[DirectionIndex] =
			CreateDamageGradientTexture(DirectionIndex);
		FSlateBrush& Brush = DirectionalDamageGradientBrushes[DirectionIndex];
		Brush = FSlateBrush();
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.ImageSize = FVector2D(
			ShooterCombatFeedbackStyle::DamageGradientTextureSize,
			ShooterCombatFeedbackStyle::DamageGradientTextureSize);
		Brush.SetResourceObject(
			DirectionalDamageGradientTextures[DirectionIndex]);
	}
}

void UShooterCombatFeedbackUI::BuildSniperScopeMaskTexture()
{
	if (SniperScopeMaskTexture
		&& SniperScopeMaskBrush.GetResourceObject() == SniperScopeMaskTexture)
	{
		return;
	}

	const int32 TextureSize = ShooterCombatFeedbackStyle::SniperScopeMaskTextureSize;
	SniperScopeMaskTexture = UTexture2D::CreateTransient(
		TextureSize, TextureSize, PF_B8G8R8A8, NAME_None);
	if (!SniperScopeMaskTexture || !SniperScopeMaskTexture->GetPlatformData()
		|| SniperScopeMaskTexture->GetPlatformData()->Mips.IsEmpty())
	{
		SniperScopeMaskTexture = nullptr;
		return;
	}

	SniperScopeMaskTexture->SRGB = false;
	SniperScopeMaskTexture->Filter = TF_Bilinear;
	SniperScopeMaskTexture->AddressX = TA_Clamp;
	SniperScopeMaskTexture->AddressY = TA_Clamp;
	SniperScopeMaskTexture->LODGroup = TEXTUREGROUP_UI;
	SniperScopeMaskTexture->NeverStream = true;

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(TextureSize * TextureSize);
	for (int32 Y = 0; Y < TextureSize; ++Y)
	{
		const float NormalizedY =
			((static_cast<float>(Y) + 0.5f) / TextureSize) * 2.0f - 1.0f;
		for (int32 X = 0; X < TextureSize; ++X)
		{
			const float NormalizedX =
				((static_cast<float>(X) + 0.5f) / TextureSize) * 2.0f - 1.0f;
			const float Radius = FMath::Sqrt(
				NormalizedX * NormalizedX + NormalizedY * NormalizedY);
			const float EdgeAlpha = ShooterCombatFeedbackStyle::SmoothStep01(
				(Radius - 0.955f) / 0.02f);
			Pixels[Y * TextureSize + X] = FColor(
				0, 0, 0, FMath::RoundToInt(EdgeAlpha * 255.0f));
		}
	}

	FTexture2DMipMap& Mip = SniperScopeMaskTexture->GetPlatformData()->Mips[0];
	void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	SniperScopeMaskTexture->UpdateResource();

	SniperScopeMaskBrush = FSlateBrush();
	SniperScopeMaskBrush.DrawAs = ESlateBrushDrawType::Image;
	SniperScopeMaskBrush.ImageSize = FVector2D(TextureSize, TextureSize);
	SniperScopeMaskBrush.SetResourceObject(SniperScopeMaskTexture);
}

void UShooterCombatFeedbackUI::ShowHitConfirmation()
{
	// A delayed owner RPC must not flash over the spectator or hidden-HUD view.
	if (!bCrosshairVisible)
	{
		return;
	}

	HitFeedbackRemaining = ShooterCombatFeedbackStyle::HitMarkerDuration;
	Invalidate(EInvalidateWidgetReason::Paint);
}

float UShooterCombatFeedbackUI::GetDamageFeedbackStateAlpha(
	const FDamageFeedbackState& FeedbackState)
{
	return ShooterCombatFeedbackStyle::ComputeDamageAlpha(
		FeedbackState.Remaining,
		FeedbackState.Strength,
		FeedbackState.FadeInStartAlpha);
}

void UShooterCombatFeedbackUI::RestartDamageFeedbackState(
	FDamageFeedbackState& FeedbackState,
	float Strength)
{
	const float ExistingOpacity = GetDamageFeedbackStateAlpha(FeedbackState);
	const float RequestedStrength = FMath::Clamp(Strength, 0.25f, 1.0f);
	const float NewStrength = FeedbackState.Remaining > UE_KINDA_SMALL_NUMBER
		? FMath::Max(FeedbackState.Strength, RequestedStrength)
		: RequestedStrength;

	// Restart the time envelope from the opacity currently on screen. This makes
	// automatic-fire and damage-over-time refresh the vignette without a flash.
	FeedbackState.FadeInStartAlpha = NewStrength > UE_KINDA_SMALL_NUMBER
		? FMath::Clamp(ExistingOpacity / NewStrength, 0.0f, 1.0f)
		: 0.0f;
	FeedbackState.Remaining = ShooterCombatFeedbackStyle::DamageFeedbackDuration;
	FeedbackState.Strength = NewStrength;
}

bool UShooterCombatFeedbackUI::TickDamageFeedbackState(
	FDamageFeedbackState& FeedbackState,
	float DeltaTime)
{
	const float PreviousRemaining = FeedbackState.Remaining;
	FeedbackState.Remaining = FMath::Max(
		0.0f,
		FeedbackState.Remaining - DeltaTime);
	return !FMath::IsNearlyEqual(PreviousRemaining, FeedbackState.Remaining);
}

void UShooterCombatFeedbackUI::ShowDamageFeedback(float Strength)
{
	RestartDamageFeedbackState(OmnidirectionalDamageFeedback, Strength);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UShooterCombatFeedbackUI::ShowDirectionalDamageFeedback(
	const FVector2D& ScreenDirection,
	float Strength)
{
	const int32 DirectionIndex =
		ShooterCombatFeedbackStyle::QuantizeDamageScreenDirection(ScreenDirection);
	if (DirectionIndex < 0
		|| DirectionIndex >= ShooterCombatFeedbackStyle::DamageDirectionCount)
	{
		ShowDamageFeedback(Strength);
		return;
	}

	RestartDamageFeedbackState(
		DirectionalDamageFeedback[DirectionIndex],
		Strength);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UShooterCombatFeedbackUI::ResetTransientFeedback()
{
	HitFeedbackRemaining = 0.0f;
	OmnidirectionalDamageFeedback = FDamageFeedbackState();
	for (FDamageFeedbackState& FeedbackState : DirectionalDamageFeedback)
	{
		FeedbackState = FDamageFeedbackState();
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

void UShooterCombatFeedbackUI::SetCrosshairVisible(bool bVisible)
{
	if (bCrosshairVisible == bVisible)
	{
		return;
	}

	bCrosshairVisible = bVisible;
	if (!bVisible)
	{
		HitFeedbackRemaining = 0.0f;
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

bool UShooterCombatFeedbackUI::ShouldHideCrosshairForIronSight() const
{
	const ATacticalCharacter* Character =
		Cast<ATacticalCharacter>(GetOwningPlayerPawn());
	if (!Character || !Character->HasUsableFirstPersonIronSight())
	{
		return false;
	}
	const UWeaponInventoryComponent* Inventory = Character->GetWeaponInventory();
	const ATacticalWeapon* Weapon = Inventory
		? Cast<ATacticalWeapon>(Inventory->CurrentWeapon.Get()) : nullptr;
	return IsValid(Weapon) && Weapon->IsAiming();
}

bool UShooterCombatFeedbackUI::ShouldDrawSniperScope() const
{
	const APlayerController* Owner = GetOwningPlayer();
	if (!bCrosshairVisible || !Owner || !Owner->IsLocalController())
	{
		return false;
	}

	const ATacticalCharacter* Character =
		Cast<ATacticalCharacter>(GetOwningPlayerPawn());
	const UWeaponInventoryComponent* Inventory = Character
		? Character->GetWeaponInventory() : nullptr;
	const ATacticalWeapon* Weapon = Inventory
		? Cast<ATacticalWeapon>(Inventory->CurrentWeapon.Get()) : nullptr;
	return IsValid(Weapon)
		&& Weapon->GetWeaponTierEnum() == EWeaponTier::Sniper
		&& Weapon->GetAimBlendAlpha()
			>= ShooterCombatFeedbackStyle::SniperScopeActivationAlpha;
}

void UShooterCombatFeedbackUI::NativeTick(
	const FGeometry& MyGeometry,
	float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const float PreviousHitTime = HitFeedbackRemaining;
	const bool bIronSightHidden = ShouldHideCrosshairForIronSight();
	const bool bSniperScopeVisible = ShouldDrawSniperScope();
	const bool bIronSightVisibilityChanged =
		bLastIronSightHidden != bIronSightHidden;
	const bool bSniperScopeVisibilityChanged =
		bLastSniperScopeVisible != bSniperScopeVisible;
	bLastIronSightHidden = bIronSightHidden;
	bLastSniperScopeVisible = bSniperScopeVisible;
	HitFeedbackRemaining = FMath::Max(0.0f, HitFeedbackRemaining - InDeltaTime);
	bool bFeedbackChanged = TickDamageFeedbackState(
		OmnidirectionalDamageFeedback,
		InDeltaTime);
	for (FDamageFeedbackState& FeedbackState : DirectionalDamageFeedback)
	{
		bFeedbackChanged |= TickDamageFeedbackState(FeedbackState, InDeltaTime);
	}
	if (bIronSightVisibilityChanged || bSniperScopeVisibilityChanged
		|| !FMath::IsNearlyEqual(PreviousHitTime, HitFeedbackRemaining)
		|| bFeedbackChanged)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

int32 UShooterCombatFeedbackUI::NativePaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	int32 MaxLayer = Super::NativePaint(
		Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);

	const FVector2f LocalSize(AllottedGeometry.GetLocalSize());
	if (LocalSize.X <= 1.0f || LocalSize.Y <= 1.0f)
	{
		return MaxLayer;
	}

	auto DrawDamageFeedback = [&](const FDamageFeedbackState& FeedbackState,
		const FSlateBrush& Brush, const UTexture2D* Texture)
	{
		const float DamageAlpha = GetDamageFeedbackStateAlpha(FeedbackState);
		if (DamageAlpha <= UE_KINDA_SMALL_NUMBER || !Texture)
		{
			return;
		}

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			++MaxLayer,
			AllottedGeometry.ToPaintGeometry(),
			&Brush,
			ESlateDrawEffect::NoGamma,
			FLinearColor(
				ShooterCombatFeedbackStyle::DamageOverlayTintRed,
				ShooterCombatFeedbackStyle::DamageOverlayTintGreen,
				ShooterCombatFeedbackStyle::DamageOverlayTintBlue,
				ShooterCombatFeedbackStyle::DamageOverlayMaximumOpacity
					* DamageAlpha));
	};

	// Every mask is a single sampled texture. Diagonal feedback soft-unions two
	// complete neighboring edges inside that mask, so there are no quadrant seams.
	DrawDamageFeedback(
		OmnidirectionalDamageFeedback,
		DamageGradientBrush,
		DamageGradientTexture);
	for (int32 DirectionIndex = 0;
		DirectionIndex < ShooterCombatFeedbackStyle::DamageDirectionCount;
		++DirectionIndex)
	{
		if (DirectionalDamageGradientTextures.IsValidIndex(DirectionIndex)
			&& DirectionalDamageGradientBrushes.IsValidIndex(DirectionIndex))
		{
			DrawDamageFeedback(
				DirectionalDamageFeedback[DirectionIndex],
				DirectionalDamageGradientBrushes[DirectionIndex],
				DirectionalDamageGradientTextures[DirectionIndex]);
		}
	}

	const FVector2f Center = LocalSize * 0.5f;
	auto DrawCenteredTexture = [&](const FSlateBrush& Brush, float CanvasSize,
		const FLinearColor& Tint)
	{
		const FVector2f Extent(CanvasSize, CanvasSize);
		const FSlateLayoutTransform Layout(Center - Extent * 0.5f);
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			++MaxLayer,
			AllottedGeometry.ToPaintGeometry(Extent, Layout),
			&Brush,
			ESlateDrawEffect::NoGamma,
			Tint);
	};
	auto DrawSegments = [&](const TArray<TPair<FVector2f, FVector2f>>& Segments,
		const FLinearColor& Color, float Thickness, int32 DrawLayer)
	{
		for (const TPair<FVector2f, FVector2f>& Segment : Segments)
		{
			TArray<FVector2f> Points;
			Points.Reserve(2);
			Points.Add(Segment.Key);
			Points.Add(Segment.Value);
			FSlateDrawElement::MakeLines(
				OutDrawElements,
				DrawLayer,
				AllottedGeometry.ToPaintGeometry(),
				MoveTemp(Points),
				ESlateDrawEffect::None,
				Color,
				true,
				Thickness);
		}
	};
	const bool bSniperScopeVisible = ShouldDrawSniperScope();
	if (bSniperScopeVisible)
	{
		const float ScopeSize = FMath::Min(LocalSize.X, LocalSize.Y);
		const FVector2f ScopeTopLeft = Center - FVector2f(ScopeSize * 0.5f);
		auto DrawBlackBar = [&](const FVector2f& TopLeft,
			const FVector2f& Size)
		{
			if (Size.X > 0.0f && Size.Y > 0.0f)
			{
				FSlateDrawElement::MakeBox(
					OutDrawElements,
					++MaxLayer,
					AllottedGeometry.ToPaintGeometry(
						Size, FSlateLayoutTransform(TopLeft)),
					&SniperScopeBlackBrush,
					ESlateDrawEffect::None,
					FLinearColor::Black);
			}
		};

		// The circular mask is square; opaque bars cover the unused aspect ratio.
		DrawBlackBar(FVector2f::ZeroVector,
			FVector2f(ScopeTopLeft.X, LocalSize.Y));
		DrawBlackBar(FVector2f(ScopeTopLeft.X + ScopeSize, 0.0f),
			FVector2f(ScopeTopLeft.X, LocalSize.Y));
		DrawBlackBar(FVector2f::ZeroVector,
			FVector2f(LocalSize.X, ScopeTopLeft.Y));
		DrawBlackBar(FVector2f(0.0f, ScopeTopLeft.Y + ScopeSize),
			FVector2f(LocalSize.X, ScopeTopLeft.Y));
		if (SniperScopeMaskTexture)
		{
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				++MaxLayer,
				AllottedGeometry.ToPaintGeometry(
					FVector2f(ScopeSize),
					FSlateLayoutTransform(ScopeTopLeft)),
				&SniperScopeMaskBrush,
				ESlateDrawEffect::NoGamma,
				FLinearColor::White);
		}

		// The hit marker temporarily replaces the scope reticle, but the optic
		// mask stays in place for the whole fully-aimed interval.
		if (HitFeedbackRemaining <= 0.0f)
		{
			const float Outer = ScopeSize
				* ShooterCombatFeedbackStyle::SniperScopeReticleOuterRadiusFraction;
			const float Gap = ShooterCombatFeedbackStyle::SniperScopeReticleCenterGap;
			const TArray<TPair<FVector2f, FVector2f>> ScopeSegments = {
				{ Center + FVector2f(-Outer, 0.0f), Center + FVector2f(-Gap, 0.0f) },
				{ Center + FVector2f(Gap, 0.0f), Center + FVector2f(Outer, 0.0f) },
				{ Center + FVector2f(0.0f, -Outer), Center + FVector2f(0.0f, -Gap) },
				{ Center + FVector2f(0.0f, Gap), Center + FVector2f(0.0f, Outer) }
			};
			DrawSegments(ScopeSegments, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f),
				3.0f, ++MaxLayer);
			DrawSegments(ScopeSegments, FLinearColor(0.85f, 0.88f, 0.88f, 0.78f),
				1.0f, ++MaxLayer);
		}
	}

	const float HitAlpha = ShooterCombatFeedbackStyle::ComputeFadeAlpha(
		HitFeedbackRemaining,
		ShooterCombatFeedbackStyle::HitMarkerDuration);
	if (ShooterCombatFeedbackStyle::ShouldDrawCrosshair(
		bCrosshairVisible && !bSniperScopeVisible
			&& !ShouldHideCrosshairForIronSight(),
		HitFeedbackRemaining))
	{
		if (CrosshairTexture)
		{
			DrawCenteredTexture(
				CrosshairBrush,
				ShooterCombatFeedbackStyle::CrosshairTextureCanvasSize + 4.0f,
				FLinearColor(0.0f, 0.0f, 0.0f, 0.82f));
			DrawCenteredTexture(
				CrosshairBrush,
				ShooterCombatFeedbackStyle::CrosshairTextureCanvasSize,
				FLinearColor(1.0f, 1.0f, 1.0f, 0.96f));
		}
		else
		{
			const float Gap = ShooterCombatFeedbackStyle::CrosshairCenterGap;
			const float Outer = Gap + ShooterCombatFeedbackStyle::CrosshairArmLength;
			const TArray<TPair<FVector2f, FVector2f>> CrosshairSegments = {
				{ Center + FVector2f(-Outer, 0.0f), Center + FVector2f(-Gap, 0.0f) },
				{ Center + FVector2f(Gap, 0.0f), Center + FVector2f(Outer, 0.0f) },
				{ Center + FVector2f(0.0f, -Outer), Center + FVector2f(0.0f, -Gap) },
				{ Center + FVector2f(0.0f, Gap), Center + FVector2f(0.0f, Outer) }
			};
			DrawSegments(CrosshairSegments, FLinearColor(0.0f, 0.0f, 0.0f, 0.82f),
				ShooterCombatFeedbackStyle::CrosshairLineThickness + 2.0f, ++MaxLayer);
			DrawSegments(CrosshairSegments, FLinearColor(1.0f, 1.0f, 1.0f, 0.96f),
				ShooterCombatFeedbackStyle::CrosshairLineThickness, ++MaxLayer);
		}
	}

	if (HitAlpha > UE_KINDA_SMALL_NUMBER)
	{
		if (HitMarkerTexture)
		{
			DrawCenteredTexture(
				HitMarkerBrush,
				ShooterCombatFeedbackStyle::HitMarkerTextureCanvasSize + 3.0f,
				FLinearColor(0.0f, 0.0f, 0.0f, 0.88f * HitAlpha));
			DrawCenteredTexture(
				HitMarkerBrush,
				ShooterCombatFeedbackStyle::HitMarkerTextureCanvasSize,
				FLinearColor(1.0f, 0.18f, 0.06f, HitAlpha));
		}
		else
		{
			const float Inner = ShooterCombatFeedbackStyle::HitMarkerInnerRadius;
			const float Outer = ShooterCombatFeedbackStyle::HitMarkerOuterRadius;
			const TArray<TPair<FVector2f, FVector2f>> HitSegments = {
				{ Center + FVector2f(-Inner, -Inner), Center + FVector2f(-Outer, -Outer) },
				{ Center + FVector2f(Inner, -Inner), Center + FVector2f(Outer, -Outer) },
				{ Center + FVector2f(-Inner, Inner), Center + FVector2f(-Outer, Outer) },
				{ Center + FVector2f(Inner, Inner), Center + FVector2f(Outer, Outer) }
			};
			DrawSegments(HitSegments, FLinearColor(0.0f, 0.0f, 0.0f, 0.88f * HitAlpha),
				4.0f, ++MaxLayer);
			DrawSegments(HitSegments, FLinearColor(1.0f, 0.18f, 0.06f, HitAlpha),
				2.0f, ++MaxLayer);
		}
	}

	return MaxLayer;
}
