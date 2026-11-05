// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/MatchResultStatusUI.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace TacticalMatchResultStyle
{
	const FLinearColor ForfeitRed(0.55f, 0.035f, 0.035f, 0.96f);
	const FLinearColor ForfeitBlue(0.035f, 0.13f, 0.58f, 0.96f);
	const FLinearColor Aborted(0.11f, 0.12f, 0.15f, 0.96f);
}

void UMatchResultStatusUI::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildNativeWidgetTree();
}

void UMatchResultStatusUI::NativeConstruct()
{
	Super::NativeConstruct();
	HideResult();
}

void UMatchResultStatusUI::BuildNativeWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("MatchResultRoot"));
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Root;

	ResultPanel = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("SpecialMatchResultPanel"));
	ResultPanel->SetPadding(FMargin(24.0f, 13.0f));
	ResultPanel->SetHorizontalAlignment(HAlign_Fill);
	ResultPanel->SetVerticalAlignment(VAlign_Center);
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(ResultPanel))
	{
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.0f));
		PanelSlot->SetAlignment(FVector2D(0.5f, 0.0f));
		PanelSlot->SetPosition(FVector2D(0.0f, 76.0f));
		PanelSlot->SetSize(FVector2D(760.0f, 100.0f));
	}

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("SpecialMatchResultContent"));
	ResultPanel->SetContent(Content);

	ResultTitleText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("SpecialMatchResultTitle"));
	ResultTitleText->SetJustification(ETextJustify::Center);
	ResultTitleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	FSlateFontInfo TitleFont = ResultTitleText->GetFont();
	TitleFont.Size = 24;
	ResultTitleText->SetFont(TitleFont);
	if (UVerticalBoxSlot* TitleSlot = Content->AddChildToVerticalBox(ResultTitleText))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Fill);
		TitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));
	}

	ResultDetailText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("SpecialMatchResultDetail"));
	ResultDetailText->SetJustification(ETextJustify::Center);
	ResultDetailText->SetColorAndOpacity(FSlateColor(
		FLinearColor(0.83f, 0.86f, 0.91f, 1.0f)));
	FSlateFontInfo DetailFont = ResultDetailText->GetFont();
	DetailFont.Size = 15;
	ResultDetailText->SetFont(DetailFont);
	if (UVerticalBoxSlot* DetailSlot = Content->AddChildToVerticalBox(ResultDetailText))
	{
		DetailSlot->SetHorizontalAlignment(HAlign_Fill);
	}
}

void UMatchResultStatusUI::ShowSpecialResult(
	uint8 WinningTeam,
	EMatchEndReason Reason,
	int32 TeamAScore,
	int32 TeamBScore)
{
	if (!ResultPanel || !ResultTitleText || !ResultDetailText)
	{
		return;
	}

	if (Reason != EMatchEndReason::TeamDisconnectForfeit
		&& Reason != EMatchEndReason::AllTeamsDisconnectedAbort
		&& Reason != EMatchEndReason::HostLost)
	{
		HideResult();
		return;
	}

	if (Reason == EMatchEndReason::TeamDisconnectForfeit && WinningTeam <= 1)
	{
		const bool bRedWon = WinningTeam == 0;
		ResultPanel->SetBrushColor(bRedWon
			? TacticalMatchResultStyle::ForfeitRed
			: TacticalMatchResultStyle::ForfeitBlue);
		ResultTitleText->SetText(bRedWon
			? NSLOCTEXT("MatchResultStatusUI", "RedForfeitWin", "红队获胜 · 对方整队断线弃权")
			: NSLOCTEXT("MatchResultStatusUI", "BlueForfeitWin", "蓝队获胜 · 对方整队断线弃权"));
		ResultDetailText->SetText(FText::Format(
			NSLOCTEXT("MatchResultStatusUI", "ForfeitScore", "最终比分 {0} : {1} · 弃权不会额外增加回合分"),
			FText::AsNumber(TeamAScore), FText::AsNumber(TeamBScore)));
	}
	else
	{
		ResultPanel->SetBrushColor(TacticalMatchResultStyle::Aborted);
		ResultTitleText->SetText(Reason == EMatchEndReason::HostLost
			? NSLOCTEXT("MatchResultStatusUI", "HostLost", "主机连接丢失 · 比赛中止")
			: NSLOCTEXT("MatchResultStatusUI", "AllOffline", "双方均已断线 · 比赛中止"));
		ResultDetailText->SetText(
			NSLOCTEXT("MatchResultStatusUI", "NoContest", "本场无胜方、无结果，不记作比赛平局"));
	}

	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMatchResultStatusUI::HideResult()
{
	SetVisibility(ESlateVisibility::Collapsed);
}
