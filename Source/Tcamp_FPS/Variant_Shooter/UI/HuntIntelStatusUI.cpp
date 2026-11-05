// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/HuntIntelStatusUI.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"

namespace HuntIntelHudStyle
{
	constexpr float WarningWidth = 560.0f;
	constexpr float WarningHeight = 52.0f;
	constexpr float WarningTop = 112.0f;
	constexpr float MarkerSize = 70.0f;
	constexpr float MarkerEdgePadding = 44.0f;
	constexpr float NormalPulseDuration = 3.0f;
	constexpr float EscalatedPulseDuration = 2.25f;
	constexpr float MarkerFadeDuration = 0.65f;
	constexpr float StateAnnouncementDuration = 2.5f;

	const FLinearColor NormalAccent(1.0f, 0.66f, 0.08f, 1.0f);
	const FLinearColor NormalPanel(0.12f, 0.075f, 0.008f, 0.90f);
	const FLinearColor EscalatedAccent(1.0f, 0.16f, 0.07f, 1.0f);
	const FLinearColor EscalatedPanel(0.16f, 0.012f, 0.008f, 0.93f);

	FSlateFontInfo FontWithSize(const UTextBlock* TextBlock, int32 Size)
	{
		FSlateFontInfo Font = TextBlock->GetFont();
		Font.Size = Size;
		return Font;
	}

	void StyleText(UTextBlock* TextBlock, int32 Size, const FLinearColor& Color)
	{
		TextBlock->SetFont(FontWithSize(TextBlock, Size));
		TextBlock->SetColorAndOpacity(FSlateColor(Color));
		TextBlock->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.95f));
		TextBlock->SetShadowOffset(FVector2D(1.0f, 1.0f));
	}
}

void UHuntIntelStatusUI::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildNativeWidgetTree();
	ClearHuntIntel();
}

void UHuntIntelStatusUI::NativeConstruct()
{
	Super::NativeConstruct();
}

void UHuntIntelStatusUI::BuildNativeWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("HuntIntelRoot"));
	RootCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = RootCanvas;

	WarningPanel = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("HuntIntelWarningPanel"));
	WarningPanel->SetPadding(FMargin(16.0f, 8.0f));
	WarningPanel->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* WarningSlot = RootCanvas->AddChildToCanvas(WarningPanel))
	{
		WarningSlot->SetAnchors(FAnchors(0.5f, 0.0f));
		WarningSlot->SetAlignment(FVector2D(0.5f, 0.0f));
		WarningSlot->SetPosition(FVector2D(0.0f, HuntIntelHudStyle::WarningTop));
		WarningSlot->SetSize(FVector2D(
			HuntIntelHudStyle::WarningWidth, HuntIntelHudStyle::WarningHeight));
		WarningSlot->SetZOrder(10);
	}

	WarningText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("HuntIntelWarningText"));
	WarningText->SetJustification(ETextJustify::Center);
	HuntIntelHudStyle::StyleText(WarningText, 18, HuntIntelHudStyle::NormalAccent);
	WarningPanel->SetContent(WarningText);

	MarkerPanels.Reserve(MaxMarkerCount);
	MarkerTexts.Reserve(MaxMarkerCount);
	for (int32 Index = 0; Index < MaxMarkerCount; ++Index)
	{
		const FName MarkerName(*FString::Printf(TEXT("HuntIntelMarker_%d"), Index));
		UBorder* MarkerPanel = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), MarkerName);
		MarkerPanel->SetPadding(FMargin(4.0f));
		MarkerPanel->SetVisibility(ESlateVisibility::Collapsed);
		MarkerPanel->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		if (UCanvasPanelSlot* MarkerSlot = RootCanvas->AddChildToCanvas(MarkerPanel))
		{
			MarkerSlot->SetAnchors(FAnchors(0.0f, 0.0f));
			MarkerSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			MarkerSlot->SetSize(FVector2D(
				HuntIntelHudStyle::MarkerSize, HuntIntelHudStyle::MarkerSize));
			MarkerSlot->SetZOrder(5);
		}

		UTextBlock* MarkerText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("HuntIntelMarkerText_%d"), Index)));
		MarkerText->SetJustification(ETextJustify::Center);
		MarkerText->SetAutoWrapText(false);
		HuntIntelHudStyle::StyleText(MarkerText, 14, FLinearColor::White);
		MarkerPanel->SetContent(MarkerText);

		MarkerPanels.Add(MarkerPanel);
		MarkerTexts.Add(MarkerText);
	}
}

void UHuntIntelStatusUI::SetHuntIntelState(bool bActive, bool bEscalated)
{
	const bool bNewEscalated = bActive && bEscalated;
	const bool bStateChanged = bHuntIntelActive != bActive
		|| bHuntIntelEscalated != bNewEscalated;
	bHuntIntelActive = bActive;
	bHuntIntelEscalated = bNewEscalated;

	if (!bHuntIntelActive)
	{
		ClearPulseMarkers();
		StateAnnouncementTimeRemaining = 0.0f;
	}
	else if (bStateChanged)
	{
		StateAnnouncementTimeRemaining = HuntIntelHudStyle::StateAnnouncementDuration;
	}

	UpdateWarningPresentation();
	if (WarningPanel && bHuntIntelActive)
	{
		WarningPanel->SetRenderOpacity(
			StateAnnouncementTimeRemaining > 0.0f ? 1.0f : 0.78f);
	}
	UpdateRootVisibility();
}

void UHuntIntelStatusUI::ShowPulse(
	const TArray<FVector>& ApproximateEnemyLocations,
	bool bEscalated)
{
	SetHuntIntelState(true, bEscalated);
	MarkerWorldLocations.Reset();
	const int32 MarkerCount = FMath::Min(ApproximateEnemyLocations.Num(), MaxMarkerCount);
	for (int32 Index = 0; Index < MarkerCount; ++Index)
	{
		MarkerWorldLocations.Add(ApproximateEnemyLocations[Index]);
	}

	PulseDuration = bHuntIntelEscalated
		? HuntIntelHudStyle::EscalatedPulseDuration
		: HuntIntelHudStyle::NormalPulseDuration;
	PulseTimeRemaining = MarkerWorldLocations.IsEmpty() ? 0.0f : PulseDuration;
	PulseElapsed = 0.0f;

	const FLinearColor Accent = bHuntIntelEscalated
		? HuntIntelHudStyle::EscalatedAccent
		: HuntIntelHudStyle::NormalAccent;
	const FLinearColor Panel = bHuntIntelEscalated
		? HuntIntelHudStyle::EscalatedPanel
		: HuntIntelHudStyle::NormalPanel;
	for (int32 Index = 0; Index < MarkerPanels.Num(); ++Index)
	{
		const bool bShowMarker = MarkerWorldLocations.IsValidIndex(Index);
		if (UBorder* MarkerPanel = MarkerPanels[Index])
		{
			MarkerPanel->SetVisibility(bShowMarker
				? ESlateVisibility::HitTestInvisible
				: ESlateVisibility::Collapsed);
			MarkerPanel->SetBrush(FSlateRoundedBoxBrush(
				Panel, HuntIntelHudStyle::MarkerSize * 0.5f, Accent, 2.0f));
			MarkerPanel->SetRenderOpacity(1.0f);
			MarkerPanel->SetRenderScale(FVector2D(1.0f, 1.0f));
		}
		if (MarkerTexts.IsValidIndex(Index) && MarkerTexts[Index])
		{
			MarkerTexts[Index]->SetText(bHuntIntelEscalated
				? NSLOCTEXT("HuntIntelStatusUI", "EscalatedMarker", "敌情\n高频")
				: NSLOCTEXT("HuntIntelStatusUI", "NormalMarker", "敌情\n近似"));
		}
	}

	UpdateRootVisibility();
}

void UHuntIntelStatusUI::ClearPulseMarkers()
{
	MarkerWorldLocations.Reset();
	PulseTimeRemaining = 0.0f;
	PulseDuration = 0.0f;
	PulseElapsed = 0.0f;
	for (UBorder* MarkerPanel : MarkerPanels)
	{
		if (MarkerPanel)
		{
			MarkerPanel->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	UpdateRootVisibility();
}

void UHuntIntelStatusUI::ClearHuntIntel()
{
	bHuntIntelActive = false;
	bHuntIntelEscalated = false;
	StateAnnouncementTimeRemaining = 0.0f;
	ClearPulseMarkers();
	UpdateWarningPresentation();
	UpdateRootVisibility();
}

void UHuntIntelStatusUI::UpdateWarningPresentation()
{
	if (!WarningPanel || !WarningText)
	{
		return;
	}

	WarningPanel->SetVisibility(bHuntIntelActive
		? ESlateVisibility::HitTestInvisible
		: ESlateVisibility::Collapsed);
	if (!bHuntIntelActive)
	{
		return;
	}

	const FLinearColor Accent = bHuntIntelEscalated
		? HuntIntelHudStyle::EscalatedAccent
		: HuntIntelHudStyle::NormalAccent;
	const FLinearColor Panel = bHuntIntelEscalated
		? HuntIntelHudStyle::EscalatedPanel
		: HuntIntelHudStyle::NormalPanel;
	WarningPanel->SetBrush(FSlateRoundedBoxBrush(Panel, 7.0f, Accent, 1.5f));
	WarningText->SetColorAndOpacity(FSlateColor(Accent));
	WarningText->SetText(bHuntIntelEscalated
		? NSLOCTEXT("HuntIntelStatusUI", "EscalatedWarning",
			"[!] 猎杀情报升级 / HIGH-FREQUENCY INTEL  ·  每 4 秒扫描")
		: NSLOCTEXT("HuntIntelStatusUI", "NormalWarning",
			"[!] 猎杀情报已启动 / HUNT INTEL ACTIVE  ·  每 8 秒扫描"));
}

void UHuntIntelStatusUI::UpdateRootVisibility()
{
	SetVisibility(bHuntIntelActive || PulseTimeRemaining > 0.0f
		? ESlateVisibility::HitTestInvisible
		: ESlateVisibility::Collapsed);
}

void UHuntIntelStatusUI::NativeTick(
	const FGeometry& MyGeometry,
	float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (StateAnnouncementTimeRemaining > 0.0f)
	{
		StateAnnouncementTimeRemaining = FMath::Max(
			0.0f, StateAnnouncementTimeRemaining - InDeltaTime);
		if (WarningPanel && StateAnnouncementTimeRemaining <= 0.0f)
		{
			// Set the persistent subdued state once instead of invalidating the
			// widget every frame for the rest of the 30-second hunt window.
			WarningPanel->SetRenderOpacity(0.78f);
		}
	}

	if (PulseTimeRemaining <= 0.0f || MarkerWorldLocations.IsEmpty())
	{
		return;
	}

	PulseElapsed += InDeltaTime;
	PulseTimeRemaining = FMath::Max(0.0f, PulseTimeRemaining - InDeltaTime);
	if (PulseTimeRemaining <= 0.0f)
	{
		ClearPulseMarkers();
		return;
	}

	const float FadeAlpha = PulseTimeRemaining < HuntIntelHudStyle::MarkerFadeDuration
		? PulseTimeRemaining / HuntIntelHudStyle::MarkerFadeDuration
		: 1.0f;
	const float PulseWave = 0.5f + 0.5f * FMath::Sin(PulseElapsed * 11.0f);
	const float MarkerScale = 0.92f + 0.13f * PulseWave;

	for (int32 Index = 0;
		Index < MarkerWorldLocations.Num() && MarkerPanels.IsValidIndex(Index);
		++Index)
	{
		UBorder* MarkerPanel = MarkerPanels[Index];
		UCanvasPanelSlot* MarkerSlot = MarkerPanel
			? Cast<UCanvasPanelSlot>(MarkerPanel->Slot)
			: nullptr;
		if (!MarkerPanel || !MarkerSlot)
		{
			continue;
		}

		bool bProjectedOnScreen = false;
		const FVector2D MarkerPosition = GetClampedMarkerPosition(
			MyGeometry, MarkerWorldLocations[Index], bProjectedOnScreen);
		MarkerSlot->SetPosition(MarkerPosition);
		MarkerPanel->SetRenderOpacity(FadeAlpha * (bProjectedOnScreen ? 1.0f : 0.82f));
		MarkerPanel->SetRenderScale(FVector2D(MarkerScale, MarkerScale));
	}
}

FVector2D UHuntIntelStatusUI::GetClampedMarkerPosition(
	const FGeometry& MyGeometry,
	const FVector& WorldLocation,
	bool& bOutProjectedOnScreen) const
{
	bOutProjectedOnScreen = false;
	const FVector2D LocalSize = MyGeometry.GetLocalSize();
	const FVector2D SafeSize(
		FMath::Max(LocalSize.X, HuntIntelHudStyle::MarkerSize * 2.0f),
		FMath::Max(LocalSize.Y, HuntIntelHudStyle::MarkerSize * 2.0f));
	const FVector2D Center = SafeSize * 0.5f;
	const float EdgePadding = HuntIntelHudStyle::MarkerEdgePadding;
	const auto ClampAndAvoidWarning = [Center, SafeSize, EdgePadding](FVector2D Position)
	{
		Position.X = FMath::Clamp(Position.X, EdgePadding, SafeSize.X - EdgePadding);
		Position.Y = FMath::Clamp(Position.Y, EdgePadding, SafeSize.Y - EdgePadding);

		const float MarkerHalfSize = HuntIntelHudStyle::MarkerSize * 0.5f;
		const float WarningLeft = Center.X - HuntIntelHudStyle::WarningWidth * 0.5f
			- MarkerHalfSize;
		const float WarningRight = Center.X + HuntIntelHudStyle::WarningWidth * 0.5f
			+ MarkerHalfSize;
		const float WarningTop = HuntIntelHudStyle::WarningTop - MarkerHalfSize;
		const float WarningBottom = HuntIntelHudStyle::WarningTop
			+ HuntIntelHudStyle::WarningHeight + MarkerHalfSize;
		if (Position.X >= WarningLeft && Position.X <= WarningRight
			&& Position.Y >= WarningTop && Position.Y <= WarningBottom)
		{
			Position.Y = FMath::Min(
				SafeSize.Y - EdgePadding, WarningBottom + 8.0f);
		}
		return Position;
	};

	APlayerController* PlayerController = GetOwningPlayer();
	FVector2D ProjectedPosition = Center;
	const bool bProjected = PlayerController
		&& UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
			PlayerController,
			WorldLocation + FVector(0.0f, 0.0f, 90.0f),
			ProjectedPosition,
			true);
	if (bProjected)
	{
		bOutProjectedOnScreen = ProjectedPosition.X >= EdgePadding
			&& ProjectedPosition.X <= SafeSize.X - EdgePadding
			&& ProjectedPosition.Y >= EdgePadding
			&& ProjectedPosition.Y <= SafeSize.Y - EdgePadding;
		return ClampAndAvoidWarning(ProjectedPosition);
	}

	if (!PlayerController)
	{
		return ClampAndAvoidWarning(Center);
	}

	FVector CameraLocation;
	FRotator CameraRotation;
	PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
	const FVector Direction = (WorldLocation - CameraLocation).GetSafeNormal();
	FVector2D EdgeDirection(
		FVector::DotProduct(Direction, CameraRotation.RotateVector(FVector::RightVector)),
		-FVector::DotProduct(Direction, CameraRotation.RotateVector(FVector::UpVector)));
	if (EdgeDirection.IsNearlyZero())
	{
		EdgeDirection = FVector2D(0.0f, 1.0f);
	}
	EdgeDirection.Normalize();

	const FVector2D HalfExtent(
		FMath::Max(1.0f, Center.X - EdgePadding),
		FMath::Max(1.0f, Center.Y - EdgePadding));
	const float HorizontalScale = FMath::Abs(EdgeDirection.X) > KINDA_SMALL_NUMBER
		? HalfExtent.X / FMath::Abs(EdgeDirection.X)
		: MAX_flt;
	const float VerticalScale = FMath::Abs(EdgeDirection.Y) > KINDA_SMALL_NUMBER
		? HalfExtent.Y / FMath::Abs(EdgeDirection.Y)
		: MAX_flt;
	return ClampAndAvoidWarning(
		Center + EdgeDirection * FMath::Min(HorizontalScale, VerticalScale));
}
