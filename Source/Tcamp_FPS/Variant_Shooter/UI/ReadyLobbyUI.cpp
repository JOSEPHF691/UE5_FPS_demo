// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ReadyLobbyUI.h"
#include "UI/TacticalHudLayout.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"

void UReadyLobbyUI::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildNativeWidgetTree();
}

void UReadyLobbyUI::BuildNativeWidgetTree()
{
	// UUserWidget builds and caches its Slate tree before NativeConstruct runs.
	// A native-only widget must therefore create its UMG root during
	// initialization, otherwise RebuildWidget caches an empty SSpacer forever.
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ReadyRoot"));
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Root;

	TeamBadge = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TeamBadge"));
	TeamBadge->SetVisibility(ESlateVisibility::Collapsed);
	TeamBadge->SetPadding(FMargin(10.0f, 4.0f));
	TeamBadgeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TeamBadgeText"));
	TeamBadgeText->SetJustification(ETextJustify::Center);
	TeamBadgeText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	FSlateFontInfo BadgeFont = TeamBadgeText->GetFont();
	BadgeFont.Size = 14;
	TeamBadgeText->SetFont(BadgeFont);
	TeamBadge->SetContent(TeamBadgeText);
	if (UCanvasPanelSlot* BadgeSlot = Root->AddChildToCanvas(TeamBadge))
	{
		BadgeSlot->SetAnchors(FAnchors(0.0f, 0.0f));
		BadgeSlot->SetAlignment(FVector2D(0.0f, 0.0f));
		BadgeSlot->SetPosition(FVector2D(TacticalHudLayout::Left, TacticalHudLayout::TeamBadgeTop));
		BadgeSlot->SetSize(FVector2D(TacticalHudLayout::TeamBadgeWidth, TacticalHudLayout::TeamBadgeHeight));
	}

	ReadyStatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ReadyStatusText"));
	ReadyStatusText->SetVisibility(ESlateVisibility::HitTestInvisible);
	ReadyStatusText->SetJustification(ETextJustify::Center);
	ReadyStatusText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(ReadyStatusText))
	{
		PanelSlot->SetAnchors(FAnchors(0.5f, 1.0f));
		PanelSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		PanelSlot->SetPosition(FVector2D(0.0f, -36.0f));
		PanelSlot->SetSize(FVector2D(900.0f, 54.0f));
	}
}

void UReadyLobbyUI::NativeConstruct()
{
	Super::NativeConstruct();

	SetReadyDisplay(false, 0, 0, 0, 0, 0, 255, false, true);
}

void UReadyLobbyUI::SetReadyDisplay(bool bLocalPlayerReady, int32 ReadyPlayers, int32 SelectedPlayers,
	int32 LobbyPlayers, int32 RedPlayers, int32 BluePlayers, uint8 TeamByte,
	bool bLobbyCandidate, bool bShowReadyPrompt)
{
	const bool bHasTeam = bLobbyCandidate && TeamByte <= 1;
	const bool bRedTeam = TeamByte == 0;
	const FText TeamName = bRedTeam
		? NSLOCTEXT("ReadyLobbyUI", "RedTeam", "红队 / RED")
		: NSLOCTEXT("ReadyLobbyUI", "BlueTeam", "蓝队 / BLUE");

	if (TeamBadge)
	{
		TeamBadge->SetVisibility(bLobbyCandidate ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		TeamBadge->SetBrushColor(!bHasTeam
			? FLinearColor(0.12f, 0.12f, 0.12f, 0.92f)
			: bRedTeam
				? FLinearColor(0.65f, 0.025f, 0.025f, 0.92f)
				: FLinearColor(0.025f, 0.12f, 0.65f, 0.92f));
	}
	if (TeamBadgeText && bLobbyCandidate)
	{
		TeamBadgeText->SetText(!bHasTeam
			? NSLOCTEXT("ReadyLobbyUI", "NoTeam", "未选择队伍 / NO TEAM")
			: bLocalPlayerReady
				? FText::Format(NSLOCTEXT("ReadyLobbyUI", "JoinedReadyTeam", "✓ 已加入 {0}"), TeamName)
				: FText::Format(NSLOCTEXT("ReadyLobbyUI", "AssignedTeam", "你的队伍：{0}"), TeamName));
	}

	if (!ReadyStatusText)
	{
		return;
	}

	ReadyStatusText->SetVisibility(bLobbyCandidate && bShowReadyPrompt
		? ESlateVisibility::HitTestInvisible
		: ESlateVisibility::Collapsed);
	if (!bLobbyCandidate || !bShowReadyPrompt)
	{
		return;
	}

	if (!bHasTeam)
	{
		ReadyStatusText->SetText(FText::Format(
			NSLOCTEXT("ReadyLobbyUI", "ChooseTeam", "射击红色或蓝色旗帜选择队伍并准备  ·  红 {0}/3  蓝 {1}/3"),
			FText::AsNumber(RedPlayers),
			FText::AsNumber(BluePlayers)));
	}
	else if (bLocalPlayerReady && SelectedPlayers < LobbyPlayers)
	{
		ReadyStatusText->SetText(FText::Format(
			NSLOCTEXT("ReadyLobbyUI", "WaitingForSelection", "✓ 已加入 {0}并准备  ·  仍有玩家未选队 {1}/{2}  ·  射击另一旗帜可换队"),
			TeamName,
			FText::AsNumber(SelectedPlayers),
			FText::AsNumber(LobbyPlayers)));
	}
	else if (bLocalPlayerReady && (RedPlayers == 0 || BluePlayers == 0))
	{
		ReadyStatusText->SetText(FText::Format(
			NSLOCTEXT("ReadyLobbyUI", "WaitingForBothTeams", "✓ 已加入 {0}并准备  ·  红蓝双方都必须有人  ·  射击另一旗帜可换队"),
			TeamName));
	}
	else if (bLocalPlayerReady)
	{
		ReadyStatusText->SetText(FText::Format(
			NSLOCTEXT("ReadyLobbyUI", "LocalReady", "✓ 已准备 · 等待其他玩家  {0}/{1}  ·  射击另一旗帜可换队"),
			FText::AsNumber(ReadyPlayers),
			FText::AsNumber(LobbyPlayers)));
	}
	else
	{
		ReadyStatusText->SetText(FText::Format(
			NSLOCTEXT("ReadyLobbyUI", "ReadyCount", "射击 {0} 旗帜确认准备  ·  已准备 {1}/{2}"),
			TeamName,
			FText::AsNumber(ReadyPlayers),
			FText::AsNumber(LobbyPlayers)));
	}
}
