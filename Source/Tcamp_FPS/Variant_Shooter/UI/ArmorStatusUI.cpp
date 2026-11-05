// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ArmorStatusUI.h"
#include "UI/TacticalHudLayout.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"

namespace ArmorHudLayout
{
	const FLinearColor EmptyColor(1.0f, 1.0f, 1.0f, 0.22f);
	const FLinearColor ArmorBlue(0.05f, 0.55f, 1.0f, 1.0f);
	const FLinearColor OutlineBlue(0.12f, 0.62f, 1.0f, 0.85f);
}

void UArmorStatusUI::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildNativeWidgetTree();
}

void UArmorStatusUI::BuildNativeWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ArmorStatusRoot"));
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Root;

	ArmorPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ArmorPanel"));
	// One vertical pixel keeps the compact bar while leaving enough room for the 10 px value text at high DPI.
	ArmorPanel->SetPadding(FMargin(0.0f, 1.0f));
	ArmorPanel->SetBrush(FSlateRoundedBoxBrush(
		FLinearColor::Transparent,
		ArmorHudLayout::OutlineBlue,
		1.25f,
		FVector2D(TacticalHudLayout::ArmorWidth, TacticalHudLayout::ArmorHeight)));
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(ArmorPanel))
	{
		PanelSlot->SetAnchors(FAnchors(0.0f, 0.0f));
		PanelSlot->SetAlignment(FVector2D::ZeroVector);
		PanelSlot->SetPosition(FVector2D(TacticalHudLayout::Left, TacticalHudLayout::ArmorTop));
		PanelSlot->SetSize(FVector2D(TacticalHudLayout::ArmorWidth, TacticalHudLayout::ArmorHeight));
	}

	UOverlay* Content = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("ArmorContent"));
	ArmorPanel->SetContent(Content);

	ArmorBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("ArmorBar"));
	FProgressBarStyle BarStyle = ArmorBar->GetWidgetStyle();
	BarStyle.SetBackgroundImage(FSlateRoundedBoxBrush(ArmorHudLayout::EmptyColor, FVector2D(32.0f, 32.0f)));
	BarStyle.SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White, FVector2D(32.0f, 32.0f)));
	ArmorBar->SetWidgetStyle(BarStyle);
	ArmorBar->SetBarFillStyle(EProgressBarFillStyle::Scale);
	ArmorBar->SetFillColorAndOpacity(ArmorHudLayout::ArmorBlue);
	if (UOverlaySlot* BarSlot = Content->AddChildToOverlay(ArmorBar))
	{
		BarSlot->SetHorizontalAlignment(HAlign_Fill);
		BarSlot->SetVerticalAlignment(VAlign_Fill);
	}

	ArmorText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ArmorText"));
	ArmorText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	ArmorText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
	ArmorText->SetShadowOffset(FVector2D(1.0f, 1.0f));
	FSlateFontInfo ArmorFont = ArmorText->GetFont();
	ArmorFont.Size = 10;
	ArmorText->SetFont(ArmorFont);
	if (UOverlaySlot* TextSlot = Content->AddChildToOverlay(ArmorText))
	{
		TextSlot->SetHorizontalAlignment(HAlign_Center);
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}
}

void UArmorStatusUI::NativeConstruct()
{
	Super::NativeConstruct();
	SetArmorDisplay(EArmorType::None, 0.0f, 0.0f);
}

void UArmorStatusUI::SetArmorDisplay(EArmorType Type, float CurrentDurability, float MaxDurability)
{
	const float SafeMax = FMath::Max(0.0f, MaxDurability);
	const float SafeCurrent = FMath::Clamp(CurrentDurability, 0.0f, SafeMax);
	const bool bHasArmor = Type != EArmorType::None && SafeMax > 0.0f && SafeCurrent > 0.0f;
	const float Percent = bHasArmor ? SafeCurrent / SafeMax : 0.0f;

	if (ArmorPanel)
	{
		ArmorPanel->SetVisibility(bHasArmor
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed);
	}

	if (ArmorText)
	{
		if (bHasArmor)
		{
			const FText TypeText = Type == EArmorType::Heavy
				? NSLOCTEXT("ArmorStatusUI", "HeavyArmorShort", "重甲")
				: NSLOCTEXT("ArmorStatusUI", "LightArmorShort", "轻甲");
			ArmorText->SetText(FText::Format(
				NSLOCTEXT("ArmorStatusUI", "ArmorValueCompact", "{0}  {1} / {2}"),
				TypeText,
				FText::AsNumber(FMath::RoundToInt(SafeCurrent)),
				FText::AsNumber(FMath::RoundToInt(SafeMax))));
		}
		else
		{
			ArmorText->SetText(FText::GetEmpty());
		}
	}

	if (ArmorBar)
	{
		ArmorBar->SetPercent(Percent);
		ArmorBar->SetFillColorAndOpacity(ArmorHudLayout::ArmorBlue);
	}
}
