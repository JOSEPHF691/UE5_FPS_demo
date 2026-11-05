// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/TacticalRosterStatusUI.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace TacticalRosterStyle
{
	constexpr int32 TeamCapacity = 3;
	constexpr float PanelWidth = 900.0f;
	constexpr float PanelHeight = 352.0f;
	constexpr float RowHeight = 48.0f;

	const FLinearColor PanelColor(0.012f, 0.017f, 0.026f, 0.94f);
	const FLinearColor PanelOutline(0.36f, 0.42f, 0.50f, 0.75f);
	const FLinearColor RedTeam(0.68f, 0.025f, 0.035f, 0.96f);
	const FLinearColor BlueTeam(0.025f, 0.14f, 0.68f, 0.96f);
	const FLinearColor ConnectedRow(0.04f, 0.12f, 0.08f, 0.92f);
	const FLinearColor ReconnectingRow(0.25f, 0.15f, 0.025f, 0.94f);
	const FLinearColor DisconnectedRow(0.18f, 0.045f, 0.045f, 0.94f);
	const FLinearColor InactiveRow(0.075f, 0.085f, 0.10f, 0.94f);
	const FLinearColor ConnectedText(0.38f, 1.0f, 0.58f, 1.0f);
	const FLinearColor ReconnectingText(1.0f, 0.76f, 0.23f, 1.0f);
	const FLinearColor DisconnectedText(1.0f, 0.38f, 0.38f, 1.0f);
	const FLinearColor InactiveText(0.68f, 0.72f, 0.78f, 1.0f);

	void StyleText(UTextBlock* TextBlock, int32 Size,
		const FLinearColor& Color = FLinearColor::White)
	{
		if (!TextBlock)
		{
			return;
		}
		FSlateFontInfo Font = TextBlock->GetFont();
		Font.Size = Size;
		TextBlock->SetFont(Font);
		TextBlock->SetColorAndOpacity(FSlateColor(Color));
		TextBlock->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
		TextBlock->SetShadowOffset(FVector2D(1.0f, 1.0f));
	}
}

void UTacticalRosterStatusUI::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildNativeWidgetTree();
	SetVisibility(ESlateVisibility::Collapsed);
}

void UTacticalRosterStatusUI::NativeConstruct()
{
	Super::NativeConstruct();
	HideScoreboard();
}

void UTacticalRosterStatusUI::BuildNativeWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("RosterStatusRoot"));
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Root;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("RosterStatusPanel"));
	Panel->SetPadding(FMargin(18.0f, 14.0f));
	Panel->SetBrush(FSlateRoundedBoxBrush(
		TacticalRosterStyle::PanelColor,
		9.0f,
		TacticalRosterStyle::PanelOutline,
		1.5f));
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel))
	{
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		PanelSlot->SetPosition(FVector2D::ZeroVector);
		PanelSlot->SetSize(FVector2D(
			TacticalRosterStyle::PanelWidth,
			TacticalRosterStyle::PanelHeight));
	}

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("RosterStatusContent"));
	Panel->SetContent(Content);

	UTextBlock* TitleText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("RosterStatusTitle"));
	TitleText->SetText(NSLOCTEXT("TacticalRosterStatusUI", "Title", "对局名单 / MATCH ROSTER"));
	TitleText->SetJustification(ETextJustify::Center);
	TacticalRosterStyle::StyleText(TitleText, 22, FLinearColor(1.0f, 0.86f, 0.35f, 1.0f));
	if (UVerticalBoxSlot* TitleSlot = Content->AddChildToVerticalBox(TitleText))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Fill);
		TitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
	}

	MatchSummaryText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("RosterMatchSummary"));
	MatchSummaryText->SetJustification(ETextJustify::Center);
	TacticalRosterStyle::StyleText(MatchSummaryText, 15, FLinearColor(0.78f, 0.82f, 0.88f, 1.0f));
	if (UVerticalBoxSlot* SummarySlot = Content->AddChildToVerticalBox(MatchSummaryText))
	{
		SummarySlot->SetHorizontalAlignment(HAlign_Fill);
		SummarySlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 11.0f));
	}

	UHorizontalBox* Teams = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("RosterTeams"));
	if (UVerticalBoxSlot* TeamsSlot = Content->AddChildToVerticalBox(Teams))
	{
		TeamsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TeamsSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	BuildTeamColumn(Teams, 0, RedHeaderText, RedRowContainers, RedRowBorders,
		RedNameTexts, RedStateTexts);
	BuildTeamColumn(Teams, 1, BlueHeaderText, BlueRowContainers, BlueRowBorders,
		BlueNameTexts, BlueStateTexts);

	UTextBlock* FooterText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("RosterStatusFooter"));
	FooterText->SetText(NSLOCTEXT(
		"TacticalRosterStatusUI", "Footer", "松开 TAB 关闭 / RELEASE TAB TO CLOSE"));
	FooterText->SetJustification(ETextJustify::Center);
	TacticalRosterStyle::StyleText(FooterText, 12, FLinearColor(0.60f, 0.64f, 0.70f, 1.0f));
	if (UVerticalBoxSlot* FooterSlot = Content->AddChildToVerticalBox(FooterText))
	{
		FooterSlot->SetHorizontalAlignment(HAlign_Fill);
		FooterSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
	}
}

void UTacticalRosterStatusUI::BuildTeamColumn(
	UHorizontalBox* Parent,
	uint8 TeamByte,
	TObjectPtr<UTextBlock>& OutHeaderText,
	TArray<TObjectPtr<USizeBox>>& OutRowContainers,
	TArray<TObjectPtr<UBorder>>& OutRowBorders,
	TArray<TObjectPtr<UTextBlock>>& OutNameTexts,
	TArray<TObjectPtr<UTextBlock>>& OutStateTexts)
{
	if (!WidgetTree || !Parent)
	{
		return;
	}

	UBorder* TeamPanel = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TeamByte == 0 ? TEXT("RedTeamPanel") : TEXT("BlueTeamPanel"));
	TeamPanel->SetPadding(FMargin(8.0f));
	TeamPanel->SetBrush(FSlateRoundedBoxBrush(
		FLinearColor(0.025f, 0.032f, 0.045f, 0.94f), 6.0f));
	if (UHorizontalBoxSlot* TeamSlot = Parent->AddChildToHorizontalBox(TeamPanel))
	{
		TeamSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TeamSlot->SetPadding(TeamByte == 0
			? FMargin(0.0f, 0.0f, 6.0f, 0.0f)
			: FMargin(6.0f, 0.0f, 0.0f, 0.0f));
	}

	UVerticalBox* TeamContent = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TeamByte == 0 ? TEXT("RedTeamContent") : TEXT("BlueTeamContent"));
	TeamPanel->SetContent(TeamContent);

	UBorder* Header = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TeamByte == 0 ? TEXT("RedTeamHeader") : TEXT("BlueTeamHeader"));
	Header->SetPadding(FMargin(8.0f, 5.0f));
	Header->SetBrush(FSlateRoundedBoxBrush(
		TeamByte == 0 ? TacticalRosterStyle::RedTeam : TacticalRosterStyle::BlueTeam,
		4.0f));
	if (UVerticalBoxSlot* HeaderSlot = TeamContent->AddChildToVerticalBox(Header))
	{
		HeaderSlot->SetHorizontalAlignment(HAlign_Fill);
		HeaderSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 7.0f));
	}

	OutHeaderText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TeamByte == 0 ? TEXT("RedHeaderText") : TEXT("BlueHeaderText"));
	OutHeaderText->SetJustification(ETextJustify::Center);
	TacticalRosterStyle::StyleText(OutHeaderText, 17);
	Header->SetContent(OutHeaderText);

	for (int32 Index = 0; Index < TacticalRosterStyle::TeamCapacity; ++Index)
	{
		const FString Prefix = TeamByte == 0 ? TEXT("Red") : TEXT("Blue");
		USizeBox* RowContainer = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), *FString::Printf(TEXT("%sRowContainer%d"), *Prefix, Index));
		RowContainer->SetHeightOverride(TacticalRosterStyle::RowHeight);
		if (UVerticalBoxSlot* RowSlot = TeamContent->AddChildToVerticalBox(RowContainer))
		{
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
			RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));
		}

		UBorder* RowBorder = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), *FString::Printf(TEXT("%sRowBorder%d"), *Prefix, Index));
		RowBorder->SetPadding(FMargin(10.0f, 5.0f));
		// Keep the rounded brush neutral: UBorder::BrushColor is a multiplier, so
		// baking the inactive tint into the brush would darken every later state.
		RowBorder->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, 4.0f));
		RowBorder->SetBrushColor(TacticalRosterStyle::InactiveRow);
		RowContainer->SetContent(RowBorder);

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(), *FString::Printf(TEXT("%sRow%d"), *Prefix, Index));
		RowBorder->SetContent(Row);

		UTextBlock* NameText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), *FString::Printf(TEXT("%sPlayerName%d"), *Prefix, Index));
		TacticalRosterStyle::StyleText(NameText, 14);
		if (UHorizontalBoxSlot* NameSlot = Row->AddChildToHorizontalBox(NameText))
		{
			NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			NameSlot->SetHorizontalAlignment(HAlign_Left);
			NameSlot->SetVerticalAlignment(VAlign_Center);
		}

		UTextBlock* StateText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), *FString::Printf(TEXT("%sPlayerState%d"), *Prefix, Index));
		StateText->SetJustification(ETextJustify::Right);
		TacticalRosterStyle::StyleText(StateText, 13, TacticalRosterStyle::InactiveText);
		if (UHorizontalBoxSlot* StateSlot = Row->AddChildToHorizontalBox(StateText))
		{
			StateSlot->SetHorizontalAlignment(HAlign_Right);
			StateSlot->SetVerticalAlignment(VAlign_Center);
		}

		OutRowContainers.Add(RowContainer);
		OutRowBorders.Add(RowBorder);
		OutNameTexts.Add(NameText);
		OutStateTexts.Add(StateText);
	}
}

void UTacticalRosterStatusUI::ApplyTeamRows(
	const TArray<FTeamAvatarStatus>& Players,
	const TArray<TObjectPtr<USizeBox>>& RowContainers,
	const TArray<TObjectPtr<UBorder>>& RowBorders,
	const TArray<TObjectPtr<UTextBlock>>& NameTexts,
	const TArray<TObjectPtr<UTextBlock>>& StateTexts)
{
	const int32 RowCount = FMath::Min(
		FMath::Min(RowContainers.Num(), RowBorders.Num()),
		FMath::Min(NameTexts.Num(), StateTexts.Num()));
	for (int32 Index = 0; Index < RowCount; ++Index)
	{
		USizeBox* Container = RowContainers[Index];
		if (Index >= Players.Num())
		{
			if (Container)
			{
				Container->SetVisibility(ESlateVisibility::Collapsed);
			}
			continue;
		}

		if (Container)
		{
			Container->SetVisibility(ESlateVisibility::HitTestInvisible);
		}

		const FTeamAvatarStatus& Player = Players[Index];
		const FString SafeName = Player.PlayerName.IsEmpty()
			? TEXT("未知玩家 / UNKNOWN")
			: Player.PlayerName.ToString();
		if (UTextBlock* NameText = NameTexts[Index])
		{
			NameText->SetText(FText::FromString(Player.bIsLocalPlayer
				? FString::Printf(TEXT("● %s  (你 / YOU)"), *SafeName)
				: SafeName));
		}

		FText StateLabel;
		FLinearColor RowColor = TacticalRosterStyle::InactiveRow;
		FLinearColor StateColor = TacticalRosterStyle::InactiveText;
		switch (Player.ConnectionStatus)
		{
		case ETacticalPublicConnectionStatus::Reconnecting:
		{
			const int32 Seconds = FMath::CeilToInt(
				FMath::Max(0.0f, Player.ReconnectGraceTimeRemaining));
			StateLabel = FText::Format(
				NSLOCTEXT("TacticalRosterStatusUI", "Reconnecting", "重连中 {0}s / RECONNECTING"),
				FText::AsNumber(Seconds));
			RowColor = TacticalRosterStyle::ReconnectingRow;
			StateColor = TacticalRosterStyle::ReconnectingText;
			break;
		}
		case ETacticalPublicConnectionStatus::Disconnected:
			StateLabel = NSLOCTEXT(
				"TacticalRosterStatusUI", "Disconnected", "已断线 / DISCONNECTED");
			RowColor = TacticalRosterStyle::DisconnectedRow;
			StateColor = TacticalRosterStyle::DisconnectedText;
			break;
		case ETacticalPublicConnectionStatus::Abandoned:
			StateLabel = NSLOCTEXT(
				"TacticalRosterStatusUI", "Abandoned", "已离开 / LEFT");
			RowColor = TacticalRosterStyle::InactiveRow;
			StateColor = TacticalRosterStyle::InactiveText;
			break;
		case ETacticalPublicConnectionStatus::Connected:
		default:
			if (Player.bHasLivingPawn)
			{
				StateLabel = NSLOCTEXT(
					"TacticalRosterStatusUI", "Alive", "存活 / ALIVE");
				RowColor = TacticalRosterStyle::ConnectedRow;
				StateColor = TacticalRosterStyle::ConnectedText;
			}
			else if (Player.bIsDead)
			{
				StateLabel = NSLOCTEXT(
					"TacticalRosterStatusUI", "Eliminated", "阵亡 / ELIMINATED");
			}
			else
			{
				StateLabel = NSLOCTEXT(
					"TacticalRosterStatusUI", "Waiting", "等待 / WAITING");
			}
			break;
		}

		if (UBorder* RowBorder = RowBorders[Index])
		{
			RowBorder->SetBrushColor(RowColor);
		}
		if (UTextBlock* StateText = StateTexts[Index])
		{
			StateText->SetText(StateLabel);
			StateText->SetColorAndOpacity(FSlateColor(StateColor));
		}
	}
}

void UTacticalRosterStatusUI::SetRosterDisplay(
	const TArray<FTeamAvatarStatus>& RedPlayers,
	const TArray<FTeamAvatarStatus>& BluePlayers,
	int32 RedScore,
	int32 BlueScore,
	int32 RoundNumber,
	int32 MaxRounds)
{
	if (MatchSummaryText)
	{
		MatchSummaryText->SetText(FText::Format(
			NSLOCTEXT("TacticalRosterStatusUI", "MatchSummary", "回合 {0} / {1}    ·    比分 {2} : {3}"),
			FText::AsNumber(FMath::Max(0, RoundNumber)),
			FText::AsNumber(FMath::Max(0, MaxRounds)),
			FText::AsNumber(FMath::Max(0, RedScore)),
			FText::AsNumber(FMath::Max(0, BlueScore))));
	}
	if (RedHeaderText)
	{
		RedHeaderText->SetText(FText::Format(
			NSLOCTEXT("TacticalRosterStatusUI", "RedHeader", "红队 / RED    {0}"),
			FText::AsNumber(FMath::Max(0, RedScore))));
	}
	if (BlueHeaderText)
	{
		BlueHeaderText->SetText(FText::Format(
			NSLOCTEXT("TacticalRosterStatusUI", "BlueHeader", "蓝队 / BLUE    {0}"),
			FText::AsNumber(FMath::Max(0, BlueScore))));
	}

	ApplyTeamRows(RedPlayers, RedRowContainers, RedRowBorders, RedNameTexts, RedStateTexts);
	ApplyTeamRows(BluePlayers, BlueRowContainers, BlueRowBorders, BlueNameTexts, BlueStateTexts);
}

void UTacticalRosterStatusUI::ShowScoreboard()
{
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UTacticalRosterStatusUI::HideScoreboard()
{
	SetVisibility(ESlateVisibility::Collapsed);
}

bool UTacticalRosterStatusUI::IsScoreboardVisible() const
{
	return GetVisibility() != ESlateVisibility::Collapsed
		&& GetVisibility() != ESlateVisibility::Hidden;
}
