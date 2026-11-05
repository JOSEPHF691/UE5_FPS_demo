// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/TacticalSessionResultRowUI.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace TacticalSessionRowUI
{
	const FLinearColor NormalColor(0.035f, 0.052f, 0.080f, 0.98f);
	const FLinearColor HoverColor(0.065f, 0.15f, 0.24f, 1.0f);
	const FLinearColor DisabledColor(0.025f, 0.031f, 0.043f, 0.82f);
	const FLinearColor PrimaryText(0.95f, 0.97f, 1.0f, 1.0f);
	const FLinearColor SecondaryText(0.57f, 0.65f, 0.74f, 1.0f);

	void StyleText(UTextBlock* TextBlock, int32 Size, const FLinearColor& Color)
	{
		FSlateFontInfo Font = TextBlock->GetFont();
		Font.Size = Size;
		TextBlock->SetFont(Font);
		TextBlock->SetColorAndOpacity(FSlateColor(Color));
		TextBlock->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
		TextBlock->SetShadowOffset(FVector2D(1.0f, 1.0f));
	}
}

void UTacticalSessionResultRowUI::InitializeResult(
	const FTacticalSessionSearchEntry& InEntry,
	FTacticalSessionResultSelected InSelectedCallback,
	bool bInteractionEnabled)
{
	Entry = InEntry;
	SelectedCallback = MoveTemp(InSelectedCallback);
	bAllowInteraction = bInteractionEnabled;
	RefreshPresentation();
}

void UTacticalSessionResultRowUI::SetInteractionEnabled(bool bEnabled)
{
	bAllowInteraction = bEnabled;
	RefreshPresentation();
}

void UTacticalSessionResultRowUI::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildNativeWidgetTree();
	RefreshPresentation();
}

void UTacticalSessionResultRowUI::BuildNativeWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	USizeBox* RootSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ResultRootSize"));
	RootSize->SetHeightOverride(70.0f);
	WidgetTree->RootWidget = RootSize;

	RowButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ResultButton"));
	FButtonStyle ButtonStyle = RowButton->GetStyle();
	ButtonStyle.SetNormal(FSlateRoundedBoxBrush(TacticalSessionRowUI::NormalColor, 6.0f));
	ButtonStyle.SetHovered(FSlateRoundedBoxBrush(TacticalSessionRowUI::HoverColor, 6.0f));
	ButtonStyle.SetPressed(FSlateRoundedBoxBrush(TacticalSessionRowUI::HoverColor * 0.78f, 6.0f));
	ButtonStyle.SetDisabled(FSlateRoundedBoxBrush(TacticalSessionRowUI::DisabledColor, 6.0f));
	RowButton->SetStyle(ButtonStyle);
	RowButton->OnClicked.AddDynamic(this, &UTacticalSessionResultRowUI::HandleRowClicked);
	RootSize->SetContent(RowButton);

	UHorizontalBox* Content = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ResultContent"));
	RowButton->SetContent(Content);

	UVerticalBox* ServerColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ServerColumn"));
	if (UHorizontalBoxSlot* ServerSlot = Content->AddChildToHorizontalBox(ServerColumn))
	{
		ServerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ServerSlot->SetVerticalAlignment(VAlign_Center);
	}

	ServerText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ServerText"));
	TacticalSessionRowUI::StyleText(ServerText, 16, TacticalSessionRowUI::PrimaryText);
	ServerColumn->AddChildToVerticalBox(ServerText);

	OwnerText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("OwnerText"));
	TacticalSessionRowUI::StyleText(OwnerText, 12, TacticalSessionRowUI::SecondaryText);
	ServerColumn->AddChildToVerticalBox(OwnerText);

	auto AddFixedText = [this, Content](const FName SizeName, const FName TextName,
		float Width, TObjectPtr<UTextBlock>& OutText)
	{
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), SizeName);
		Size->SetWidthOverride(Width);
		if (UHorizontalBoxSlot* TextSlot = Content->AddChildToHorizontalBox(Size))
		{
			TextSlot->SetVerticalAlignment(VAlign_Center);
		}
		OutText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TextName);
		OutText->SetJustification(ETextJustify::Center);
		TacticalSessionRowUI::StyleText(OutText, 13, TacticalSessionRowUI::SecondaryText);
		Size->SetContent(OutText);
	};

	AddFixedText(TEXT("PlayerCountSize"), TEXT("PlayerCountText"), 88.0f, PlayerCountText);
	AddFixedText(TEXT("PingSize"), TEXT("PingText"), 82.0f, PingText);
	AddFixedText(TEXT("StateSize"), TEXT("StateText"), 124.0f, StateText);
}

void UTacticalSessionResultRowUI::RefreshPresentation()
{
	if (!RowButton)
	{
		return;
	}

	const bool bCanJoin = bAllowInteraction && Entry.bCompatible && Entry.bJoinable;
	RowButton->SetIsEnabled(bCanJoin);

	if (ServerText)
	{
		ServerText->SetText(Entry.ServerName.IsEmpty()
			? NSLOCTEXT("TacticalSessionResultRowUI", "UnnamedServer", "未命名房间")
			: FText::FromString(Entry.ServerName));
	}
	if (OwnerText)
	{
		OwnerText->SetText(FText::Format(
			NSLOCTEXT("TacticalSessionResultRowUI", "Owner", "房主：{0}"),
			FText::FromString(Entry.OwnerName.IsEmpty() ? TEXT("-") : Entry.OwnerName)));
	}
	if (PlayerCountText)
	{
		PlayerCountText->SetText(FText::Format(
			NSLOCTEXT("TacticalSessionResultRowUI", "Players", "{0} / {1}"),
			FText::AsNumber(Entry.CurrentPlayers),
			FText::AsNumber(Entry.MaxPlayers)));
	}
	if (PingText)
	{
		PingText->SetText(FText::Format(
			NSLOCTEXT("TacticalSessionResultRowUI", "Ping", "{0} ms"),
			FText::AsNumber(FMath::Max(0, Entry.PingMs))));
	}
	if (StateText)
	{
		FText State = NSLOCTEXT("TacticalSessionResultRowUI", "Join", "点击加入");
		FLinearColor StateColor(0.26f, 0.86f, 0.48f, 1.0f);
		if (!Entry.bCompatible)
		{
			State = NSLOCTEXT("TacticalSessionResultRowUI", "Incompatible", "版本不兼容");
			StateColor = FLinearColor(0.95f, 0.36f, 0.30f, 1.0f);
		}
		else if (!Entry.bJoinable)
		{
			State = Entry.bMatchInProgress
				? NSLOCTEXT("TacticalSessionResultRowUI", "InProgress", "对局进行中")
				: NSLOCTEXT("TacticalSessionResultRowUI", "Unavailable", "暂不可加入");
			StateColor = FLinearColor(0.92f, 0.68f, 0.24f, 1.0f);
		}
		StateText->SetText(State);
		StateText->SetColorAndOpacity(FSlateColor(StateColor));
	}
}

void UTacticalSessionResultRowUI::HandleRowClicked()
{
	if (bAllowInteraction && Entry.bCompatible && Entry.bJoinable && Entry.ResultHandle != INDEX_NONE)
	{
		SelectedCallback.ExecuteIfBound(Entry.ResultHandle);
	}
}
