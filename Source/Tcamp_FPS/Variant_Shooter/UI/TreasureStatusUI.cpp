// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/TreasureStatusUI.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace TreasureHudStyle
{
	constexpr float StatusWidth = 620.0f;
	constexpr float StatusHeight = 78.0f;
	constexpr float StatusTop = 176.0f;
	constexpr float MarkerSize = 88.0f;
	constexpr float MarkerEdgePadding = 52.0f;
	constexpr float MarkerWorldHeight = 90.0f;
	constexpr float ReservedHuntWidth = 560.0f;
	constexpr float ReservedHuntHeight = 52.0f;
	constexpr float ReservedHuntTop = 112.0f;

	const FLinearColor AnnouncedAccent(1.0f, 0.73f, 0.12f, 1.0f);
	const FLinearColor AnnouncedPanel(0.14f, 0.085f, 0.01f, 0.92f);
	const FLinearColor GuardedAccent(1.0f, 0.34f, 0.08f, 1.0f);
	const FLinearColor GuardedPanel(0.16f, 0.025f, 0.008f, 0.93f);
	const FLinearColor UnlockedAccent(0.16f, 0.92f, 0.62f, 1.0f);
	const FLinearColor UnlockedPanel(0.008f, 0.13f, 0.075f, 0.93f);
	const FLinearColor ClaimingAccent(0.78f, 0.48f, 1.0f, 1.0f);
	const FLinearColor ClaimingPanel(0.075f, 0.02f, 0.14f, 0.94f);
	const FLinearColor ClaimedAccent(0.83f, 0.86f, 0.90f, 1.0f);
	const FLinearColor ClaimedPanel(0.045f, 0.055f, 0.07f, 0.92f);
	const FLinearColor RedTeam(1.0f, 0.18f, 0.12f, 1.0f);
	const FLinearColor BlueTeam(0.16f, 0.52f, 1.0f, 1.0f);

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

	FString TeamName(uint8 Team)
	{
		switch (Team)
		{
		case 0: return TEXT("红队");
		case 1: return TEXT("蓝队");
		default: return TEXT("未知队伍");
		}
	}

	FLinearColor TeamColor(uint8 Team)
	{
		return Team == 0 ? RedTeam : Team == 1 ? BlueTeam : ClaimedAccent;
	}
}

void UTreasureStatusUI::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(false);
	BuildNativeWidgetTree();
	ClearTreasure();
}

void UTreasureStatusUI::NativeConstruct()
{
	Super::NativeConstruct();
	UpdateStaticPresentation();
}

void UTreasureStatusUI::BuildNativeWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("TreasureStatusRoot"));
	RootCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = RootCanvas;

	StatusPanel = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("TreasureStatusPanel"));
	StatusPanel->SetPadding(FMargin(18.0f, 8.0f));
	StatusPanel->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* StatusSlot = RootCanvas->AddChildToCanvas(StatusPanel))
	{
		StatusSlot->SetAnchors(FAnchors(0.5f, 0.0f));
		StatusSlot->SetAlignment(FVector2D(0.5f, 0.0f));
		StatusSlot->SetPosition(FVector2D(0.0f, TreasureHudStyle::StatusTop));
		StatusSlot->SetSize(FVector2D(
			TreasureHudStyle::StatusWidth, TreasureHudStyle::StatusHeight));
		StatusSlot->SetZOrder(10);
	}

	UVerticalBox* StatusContents = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("TreasureStatusContents"));
	StatusContents->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	StatusPanel->SetContent(StatusContents);

	StatusText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("TreasureStatusText"));
	StatusText->SetJustification(ETextJustify::Center);
	StatusText->SetAutoWrapText(false);
	TreasureHudStyle::StyleText(StatusText, 18, TreasureHudStyle::AnnouncedAccent);
	if (UVerticalBoxSlot* TextSlot = StatusContents->AddChildToVerticalBox(StatusText))
	{
		TextSlot->SetHorizontalAlignment(HAlign_Fill);
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}

	USizeBox* ProgressHeight = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(), TEXT("TreasureClaimProgressHeight"));
	ProgressHeight->SetHeightOverride(10.0f);
	ProgressHeight->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* ProgressSlot = StatusContents->AddChildToVerticalBox(ProgressHeight))
	{
		ProgressSlot->SetPadding(FMargin(0.0f, 7.0f, 0.0f, 0.0f));
		ProgressSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	ClaimProgressBar = WidgetTree->ConstructWidget<UProgressBar>(
		UProgressBar::StaticClass(), TEXT("TreasureClaimProgress"));
	ClaimProgressBar->SetPercent(0.0f);
	ClaimProgressBar->SetFillColorAndOpacity(TreasureHudStyle::ClaimingAccent);
	ClaimProgressBar->SetVisibility(ESlateVisibility::HitTestInvisible);
	ProgressHeight->SetContent(ClaimProgressBar);

	MarkerPanel = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("TreasureWorldMarker"));
	MarkerPanel->SetPadding(FMargin(5.0f));
	MarkerPanel->SetVisibility(ESlateVisibility::Collapsed);
	MarkerPanel->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	if (UCanvasPanelSlot* MarkerSlot = RootCanvas->AddChildToCanvas(MarkerPanel))
	{
		MarkerSlot->SetAnchors(FAnchors(0.0f, 0.0f));
		MarkerSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		MarkerSlot->SetSize(FVector2D(
			TreasureHudStyle::MarkerSize, TreasureHudStyle::MarkerSize));
		MarkerSlot->SetZOrder(5);
	}

	MarkerText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("TreasureWorldMarkerText"));
	MarkerText->SetJustification(ETextJustify::Center);
	MarkerText->SetAutoWrapText(false);
	TreasureHudStyle::StyleText(MarkerText, 14, FLinearColor::White);
	MarkerPanel->SetContent(MarkerText);
}

void UTreasureStatusUI::SetTreasureState(
	const FTreasureRoundSnapshot& InSnapshot)
{
	Snapshot = InSnapshot;
	CachedActivationSeconds = INDEX_NONE;
	CachedClaimPercent = INDEX_NONE;
	CachedDistanceMeters = INDEX_NONE;
	bPresentationActive = ShouldShowObjective();
	UpdateStaticPresentation();
	UpdateTemporalPresentation(GetSynchronizedServerTime(), true);
	UpdateRootVisibility();
}

void UTreasureStatusUI::ClearTreasure()
{
	Snapshot = FTreasureRoundSnapshot();
	bPresentationActive = false;
	CachedActivationSeconds = INDEX_NONE;
	CachedClaimPercent = INDEX_NONE;
	CachedDistanceMeters = INDEX_NONE;

	if (StatusPanel)
	{
		StatusPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (MarkerPanel)
	{
		MarkerPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (ClaimProgressBar)
	{
		ClaimProgressBar->SetPercent(0.0f);
		if (UWidget* ProgressContainer = ClaimProgressBar->GetParent())
		{
			ProgressContainer->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	UpdateRootVisibility();
}

void UTreasureStatusUI::UpdateStaticPresentation()
{
	if (!StatusPanel || !StatusText || !MarkerPanel || !MarkerText || !ClaimProgressBar)
	{
		return;
	}

	bPresentationActive = ShouldShowObjective();
	StatusPanel->SetVisibility(bPresentationActive
		? ESlateVisibility::HitTestInvisible
		: ESlateVisibility::Collapsed);
	MarkerPanel->SetVisibility(ShouldShowWorldMarker()
		? ESlateVisibility::HitTestInvisible
		: ESlateVisibility::Collapsed);

	FLinearColor Accent = TreasureHudStyle::AnnouncedAccent;
	FLinearColor Panel = TreasureHudStyle::AnnouncedPanel;
	switch (Snapshot.State)
	{
	case ETreasureRoundState::Guarded:
		Accent = TreasureHudStyle::GuardedAccent;
		Panel = TreasureHudStyle::GuardedPanel;
		break;
	case ETreasureRoundState::Unlocked:
		Accent = TreasureHudStyle::UnlockedAccent;
		Panel = TreasureHudStyle::UnlockedPanel;
		break;
	case ETreasureRoundState::Claiming:
		Accent = TreasureHudStyle::ClaimingAccent;
		Panel = TreasureHudStyle::ClaimingPanel;
		break;
	case ETreasureRoundState::Claimed:
		Accent = TreasureHudStyle::TeamColor(Snapshot.ClaimedTeam);
		Panel = TreasureHudStyle::ClaimedPanel;
		break;
	default:
		break;
	}

	StatusPanel->SetBrush(FSlateRoundedBoxBrush(Panel, 7.0f, Accent, 1.5f));
	MarkerPanel->SetBrush(FSlateRoundedBoxBrush(
		Panel, TreasureHudStyle::MarkerSize * 0.5f, Accent, 2.0f));
	StatusText->SetColorAndOpacity(FSlateColor(Accent));
	ClaimProgressBar->SetFillColorAndOpacity(Accent);
	if (UWidget* ProgressContainer = ClaimProgressBar->GetParent())
	{
		ProgressContainer->SetVisibility(
			Snapshot.State == ETreasureRoundState::Claiming
				? ESlateVisibility::HitTestInvisible
				: ESlateVisibility::Collapsed);
	}

	switch (Snapshot.State)
	{
	case ETreasureRoundState::Guarded:
		StatusText->SetText(FText::FromString(FString::Printf(
			TEXT("宝箱已激活 · 剩余守卫 %d"),
			static_cast<int32>(Snapshot.RemainingGuards))));
		break;
	case ETreasureRoundState::Unlocked:
		StatusText->SetText(NSLOCTEXT(
			"TreasureStatusUI", "Unlocked", "宝箱已解锁 · 靠近并按住互动键 2 秒"));
		break;
	case ETreasureRoundState::Claimed:
		StatusText->SetText(FText::FromString(FString::Printf(
			TEXT("宝箱已被%s夺取"),
			*TreasureHudStyle::TeamName(Snapshot.ClaimedTeam))));
		break;
	default:
		break;
	}

	UpdateRootVisibility();
}

void UTreasureStatusUI::UpdateTemporalPresentation(
	float ServerTimeSeconds,
	bool bForceTextRefresh)
{
	if (!StatusText || !ClaimProgressBar)
	{
		return;
	}

	if (Snapshot.State == ETreasureRoundState::Announced)
	{
		const float Remaining = FMath::Max(
			0.0f, Snapshot.ActivationServerTime - ServerTimeSeconds);
		const int32 DisplaySeconds = FMath::CeilToInt(Remaining);
		if (bForceTextRefresh || CachedActivationSeconds != DisplaySeconds)
		{
			CachedActivationSeconds = DisplaySeconds;
			StatusText->SetText(DisplaySeconds > 0
				? FText::FromString(FString::Printf(
					TEXT("宝箱位置已公布 · %d 秒后激活"), DisplaySeconds))
				: NSLOCTEXT(
					"TreasureStatusUI", "Activating", "宝箱即将激活 · 前往标记位置"));
		}
		return;
	}

	if (Snapshot.State == ETreasureRoundState::Claiming)
	{
		const float Remaining = Snapshot.ClaimEndServerTime > 0.0f
			? FMath::Max(0.0f, Snapshot.ClaimEndServerTime - ServerTimeSeconds)
			: TacticalEconomy::TreasureClaimDuration;
		const float Progress = FMath::Clamp(
			1.0f - Remaining / TacticalEconomy::TreasureClaimDuration,
			0.0f, 1.0f);
		ClaimProgressBar->SetPercent(Progress);
		const int32 DisplayPercent = FMath::RoundToInt(Progress * 100.0f);
		if (bForceTextRefresh || CachedClaimPercent != DisplayPercent)
		{
			CachedClaimPercent = DisplayPercent;
			FString PlayerName = Snapshot.ClaimingPlayerName.IsEmpty()
				? TEXT("未知玩家")
				: Snapshot.ClaimingPlayerName;
			constexpr int32 MaxDisplayedNameLength = 18;
			if (PlayerName.Len() > MaxDisplayedNameLength)
			{
				PlayerName = PlayerName.Left(MaxDisplayedNameLength - 1) + TEXT("…");
			}
			StatusText->SetText(FText::FromString(FString::Printf(
				TEXT("%s · %s正在开启宝箱 · %d%%"),
				*TreasureHudStyle::TeamName(Snapshot.ClaimingTeam),
				*PlayerName,
				DisplayPercent)));
		}
	}
}

void UTreasureStatusUI::UpdateMarkerPresentation(const FGeometry& MyGeometry)
{
	if (!ShouldShowWorldMarker() || !MarkerPanel || !MarkerText)
	{
		return;
	}

	const FVector SiteLocation(Snapshot.SiteLocation);
	bool bProjectedOnScreen = false;
	const FVector2D MarkerPosition = GetClampedMarkerPosition(
		MyGeometry, SiteLocation, bProjectedOnScreen);
	if (UCanvasPanelSlot* MarkerSlot = Cast<UCanvasPanelSlot>(MarkerPanel->Slot))
	{
		MarkerSlot->SetPosition(MarkerPosition);
	}
	MarkerPanel->SetRenderOpacity(bProjectedOnScreen ? 1.0f : 0.84f);

	FVector ObserverLocation = FVector::ZeroVector;
	bool bHasObserverLocation = false;
	if (const APlayerController* PlayerController = GetOwningPlayer())
	{
		if (const APawn* Pawn = PlayerController->GetPawn())
		{
			ObserverLocation = Pawn->GetActorLocation();
			bHasObserverLocation = true;
		}
		else
		{
			FRotator ViewRotation;
			PlayerController->GetPlayerViewPoint(ObserverLocation, ViewRotation);
			bHasObserverLocation = true;
		}
	}

	const int32 DistanceMeters = bHasObserverLocation
		? FMath::Max(0, FMath::RoundToInt(FVector::Distance(
			ObserverLocation, SiteLocation) / 100.0f))
		: INDEX_NONE;
	if (CachedDistanceMeters != DistanceMeters)
	{
		CachedDistanceMeters = DistanceMeters;
		MarkerText->SetText(DistanceMeters >= 0
			? FText::FromString(FString::Printf(TEXT("宝箱\n%d m"), DistanceMeters))
			: NSLOCTEXT("TreasureStatusUI", "Marker", "宝箱"));
	}
}

void UTreasureStatusUI::NativeTick(
	const FGeometry& MyGeometry,
	float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bPresentationActive)
	{
		return;
	}

	if (Snapshot.State == ETreasureRoundState::Announced
		|| Snapshot.State == ETreasureRoundState::Claiming)
	{
		UpdateTemporalPresentation(GetSynchronizedServerTime(), false);
	}
	if (ShouldShowWorldMarker())
	{
		UpdateMarkerPresentation(MyGeometry);
	}
}

void UTreasureStatusUI::UpdateRootVisibility()
{
	SetVisibility(bPresentationActive
		? ESlateVisibility::HitTestInvisible
		: ESlateVisibility::Collapsed);
}

float UTreasureStatusUI::GetSynchronizedServerTime() const
{
	const UWorld* World = GetWorld();
	if (const AGameStateBase* GameState = World ? World->GetGameState() : nullptr)
	{
		return GameState->GetServerWorldTimeSeconds();
	}
	return World ? World->GetTimeSeconds() : 0.0f;
}

bool UTreasureStatusUI::ShouldShowObjective() const
{
	return Snapshot.State == ETreasureRoundState::Announced
		|| Snapshot.State == ETreasureRoundState::Guarded
		|| Snapshot.State == ETreasureRoundState::Unlocked
		|| Snapshot.State == ETreasureRoundState::Claiming
		|| Snapshot.State == ETreasureRoundState::Claimed;
}

bool UTreasureStatusUI::ShouldShowWorldMarker() const
{
	return ShouldShowObjective() && Snapshot.SiteIndex != 255;
}

FVector2D UTreasureStatusUI::GetClampedMarkerPosition(
	const FGeometry& MyGeometry,
	const FVector& WorldLocation,
	bool& bOutProjectedOnScreen) const
{
	bOutProjectedOnScreen = false;
	const FVector2D LocalSize = MyGeometry.GetLocalSize();
	const FVector2D SafeSize(
		FMath::Max(LocalSize.X, TreasureHudStyle::MarkerSize * 2.0f),
		FMath::Max(LocalSize.Y, TreasureHudStyle::MarkerSize * 2.0f));
	const FVector2D Center = SafeSize * 0.5f;
	const float EdgePadding = TreasureHudStyle::MarkerEdgePadding;
	const auto ClampAndAvoidStatus = [Center, SafeSize, EdgePadding](FVector2D Position)
	{
		Position.X = FMath::Clamp(Position.X, EdgePadding, SafeSize.X - EdgePadding);
		Position.Y = FMath::Clamp(Position.Y, EdgePadding, SafeSize.Y - EdgePadding);

		const float MarkerHalfSize = TreasureHudStyle::MarkerSize * 0.5f;
		const float HuntLeft = Center.X - TreasureHudStyle::ReservedHuntWidth * 0.5f
			- MarkerHalfSize;
		const float HuntRight = Center.X + TreasureHudStyle::ReservedHuntWidth * 0.5f
			+ MarkerHalfSize;
		const float HuntTop = TreasureHudStyle::ReservedHuntTop - MarkerHalfSize;
		const float HuntBottom = TreasureHudStyle::ReservedHuntTop
			+ TreasureHudStyle::ReservedHuntHeight + MarkerHalfSize;
		const float StatusLeft = Center.X - TreasureHudStyle::StatusWidth * 0.5f
			- MarkerHalfSize;
		const float StatusRight = Center.X + TreasureHudStyle::StatusWidth * 0.5f
			+ MarkerHalfSize;
		const float StatusTop = TreasureHudStyle::StatusTop - MarkerHalfSize;
		const float StatusBottom = TreasureHudStyle::StatusTop
			+ TreasureHudStyle::StatusHeight + MarkerHalfSize;
		const bool bOverlapsHunt = Position.X >= HuntLeft && Position.X <= HuntRight
			&& Position.Y >= HuntTop && Position.Y <= HuntBottom;
		const bool bOverlapsStatus = Position.X >= StatusLeft && Position.X <= StatusRight
			&& Position.Y >= StatusTop && Position.Y <= StatusBottom;
		if (bOverlapsHunt || bOverlapsStatus)
		{
			Position.Y = FMath::Min(SafeSize.Y - EdgePadding, StatusBottom + 8.0f);
		}
		return Position;
	};

	APlayerController* PlayerController = GetOwningPlayer();
	FVector2D ProjectedPosition = Center;
	const bool bProjected = PlayerController
		&& UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
			PlayerController,
			WorldLocation + FVector(0.0f, 0.0f, TreasureHudStyle::MarkerWorldHeight),
			ProjectedPosition,
			true);
	if (bProjected)
	{
		bOutProjectedOnScreen = ProjectedPosition.X >= EdgePadding
			&& ProjectedPosition.X <= SafeSize.X - EdgePadding
			&& ProjectedPosition.Y >= EdgePadding
			&& ProjectedPosition.Y <= SafeSize.Y - EdgePadding;
		return ClampAndAvoidStatus(ProjectedPosition);
	}

	if (!PlayerController)
	{
		return ClampAndAvoidStatus(Center);
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
	return ClampAndAvoidStatus(
		Center + EdgeDirection * FMath::Min(HorizontalScale, VerticalScale));
}
