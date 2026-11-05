// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ReconnectStatusUI.h"

#include "TacticalGameInstance.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace TacticalReconnectUI
{
	constexpr float PanelWidth = 660.0f;
	constexpr float PanelHeight = 360.0f;

	const FLinearColor BackdropColor(0.005f, 0.008f, 0.014f, 0.86f);
	const FLinearColor PanelColor(0.018f, 0.025f, 0.042f, 0.98f);
	const FLinearColor PanelOutline(0.20f, 0.48f, 0.82f, 0.9f);
	const FLinearColor AccentBlue(0.18f, 0.62f, 1.0f, 1.0f);
	const FLinearColor SecondaryText(0.72f, 0.78f, 0.86f, 1.0f);
	const FLinearColor RetryNormal(0.045f, 0.32f, 0.62f, 1.0f);
	const FLinearColor RetryHover(0.07f, 0.46f, 0.84f, 1.0f);
	const FLinearColor LeaveNormal(0.30f, 0.08f, 0.09f, 1.0f);
	const FLinearColor LeaveHover(0.50f, 0.10f, 0.12f, 1.0f);

	FSlateFontInfo FontWithSize(const UTextBlock* TextBlock, int32 Size)
	{
		FSlateFontInfo Font = TextBlock->GetFont();
		Font.Size = Size;
		return Font;
	}

	void StyleText(UTextBlock* TextBlock, int32 Size,
		const FLinearColor& Color = FLinearColor::White)
	{
		TextBlock->SetFont(FontWithSize(TextBlock, Size));
		TextBlock->SetColorAndOpacity(FSlateColor(Color));
		TextBlock->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
		TextBlock->SetShadowOffset(FVector2D(1.0f, 1.0f));
	}

	void StyleButton(UButton* Button, const FLinearColor& NormalColor,
		const FLinearColor& HoverColor)
	{
		FButtonStyle Style = Button->GetStyle();
		Style.SetNormal(FSlateRoundedBoxBrush(NormalColor, 6.0f));
		Style.SetHovered(FSlateRoundedBoxBrush(HoverColor, 6.0f));
		Style.SetPressed(FSlateRoundedBoxBrush(HoverColor * 0.78f, 6.0f));
		Style.SetDisabled(FSlateRoundedBoxBrush(NormalColor * 0.45f, 6.0f));
		Button->SetStyle(Style);
	}
}

void UReconnectStatusUI::InitializeReconnectOwner(UTacticalGameInstance* InOwner)
{
	ReconnectOwner = InOwner;
}

void UReconnectStatusUI::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildNativeWidgetTree();
	SetIsFocusable(true);
	// This is one-time widget initialization. During a network failure Unreal can
	// remove every viewport widget while loading the local fallback world, then
	// the GameInstance reconnect guard adds this same instance back. NativeConstruct
	// runs again on that re-add, so resetting the actions there would hide the
	// already-available Retry/Leave buttons after ShowReconnectFailure.
	SetActionsVisible(false);
	SetOverlayVisible(false);
}

void UReconnectStatusUI::NativeConstruct()
{
	Super::NativeConstruct();
}

void UReconnectStatusUI::BuildNativeWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("ReconnectRoot"));
	Root->SetVisibility(ESlateVisibility::Visible);
	WidgetTree->RootWidget = Root;

	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ReconnectBackdrop"));
	Backdrop->SetBrushColor(TacticalReconnectUI::BackdropColor);
	if (UOverlaySlot* BackdropSlot = Root->AddChildToOverlay(Backdrop))
	{
		BackdropSlot->SetHorizontalAlignment(HAlign_Fill);
		BackdropSlot->SetVerticalAlignment(VAlign_Fill);
	}

	USizeBox* PanelSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ReconnectPanelSize"));
	PanelSize->SetWidthOverride(TacticalReconnectUI::PanelWidth);
	PanelSize->SetHeightOverride(TacticalReconnectUI::PanelHeight);
	if (UOverlaySlot* PanelSizeSlot = Root->AddChildToOverlay(PanelSize))
	{
		PanelSizeSlot->SetHorizontalAlignment(HAlign_Center);
		PanelSizeSlot->SetVerticalAlignment(VAlign_Center);
	}

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ReconnectPanel"));
	Panel->SetPadding(FMargin(34.0f, 27.0f));
	Panel->SetBrush(FSlateRoundedBoxBrush(
		TacticalReconnectUI::PanelColor,
		10.0f,
		TacticalReconnectUI::PanelOutline,
		1.5f));
	PanelSize->SetContent(Panel);

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ReconnectContent"));
	Panel->SetContent(Content);

	UTextBlock* TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ReconnectTitle"));
	TitleText->SetText(NSLOCTEXT("ReconnectStatusUI", "Title", "连接中断 / CONNECTION LOST"));
	TitleText->SetJustification(ETextJustify::Center);
	TacticalReconnectUI::StyleText(TitleText, 25, TacticalReconnectUI::AccentBlue);
	if (UVerticalBoxSlot* TitleSlot = Content->AddChildToVerticalBox(TitleText))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Fill);
		TitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 14.0f));
	}

	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ReconnectStatus"));
	StatusText->SetJustification(ETextJustify::Center);
	StatusText->SetAutoWrapText(true);
	TacticalReconnectUI::StyleText(StatusText, 19);
	if (UVerticalBoxSlot* StatusSlot = Content->AddChildToVerticalBox(StatusText))
	{
		StatusSlot->SetHorizontalAlignment(HAlign_Fill);
		StatusSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 9.0f));
	}

	ServerStatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ReconnectServerStatus"));
	ServerStatusText->SetJustification(ETextJustify::Center);
	TacticalReconnectUI::StyleText(ServerStatusText, 15, TacticalReconnectUI::SecondaryText);
	if (UVerticalBoxSlot* ServerSlot = Content->AddChildToVerticalBox(ServerStatusText))
	{
		ServerSlot->SetHorizontalAlignment(HAlign_Fill);
		ServerSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 7.0f));
	}

	AttemptText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ReconnectAttempt"));
	AttemptText->SetJustification(ETextJustify::Center);
	TacticalReconnectUI::StyleText(AttemptText, 16, FLinearColor(1.0f, 0.84f, 0.30f, 1.0f));
	if (UVerticalBoxSlot* AttemptSlot = Content->AddChildToVerticalBox(AttemptText))
	{
		AttemptSlot->SetHorizontalAlignment(HAlign_Fill);
		AttemptSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 13.0f));
	}

	DetailText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ReconnectDetail"));
	DetailText->SetText(NSLOCTEXT(
		"ReconnectStatusUI", "GraceDetail",
		"服务器会将存活角色保留到本回合结束。角色仍存活时可恢复原角色；自动重试停止后仍可手动重试。"));
	DetailText->SetJustification(ETextJustify::Center);
	DetailText->SetAutoWrapText(true);
	TacticalReconnectUI::StyleText(DetailText, 13, TacticalReconnectUI::SecondaryText);
	if (UVerticalBoxSlot* DetailSlot = Content->AddChildToVerticalBox(DetailText))
	{
		DetailSlot->SetHorizontalAlignment(HAlign_Fill);
		DetailSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		DetailSlot->SetVerticalAlignment(VAlign_Center);
		DetailSlot->SetPadding(FMargin(12.0f, 0.0f, 12.0f, 12.0f));
	}

	ActionRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ReconnectActions"));
	if (UVerticalBoxSlot* ActionSlot = Content->AddChildToVerticalBox(ActionRow))
	{
		ActionSlot->SetHorizontalAlignment(HAlign_Center);
		ActionSlot->SetVerticalAlignment(VAlign_Bottom);
	}

	auto AddActionButton = [this](const FName SizeName, const FName ButtonName, const FName TextName,
		const FText& Label, const FLinearColor& NormalColor, const FLinearColor& HoverColor,
		TObjectPtr<UButton>& OutButton)
	{
		USizeBox* ButtonSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), SizeName);
		ButtonSize->SetWidthOverride(238.0f);
		ButtonSize->SetHeightOverride(48.0f);
		if (UHorizontalBoxSlot* SizeSlot = ActionRow->AddChildToHorizontalBox(ButtonSize))
		{
			SizeSlot->SetHorizontalAlignment(HAlign_Center);
			SizeSlot->SetVerticalAlignment(VAlign_Center);
		}

		OutButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), ButtonName);
		TacticalReconnectUI::StyleButton(OutButton, NormalColor, HoverColor);
		ButtonSize->SetContent(OutButton);

		UTextBlock* LabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TextName);
		LabelText->SetText(Label);
		LabelText->SetJustification(ETextJustify::Center);
		TacticalReconnectUI::StyleText(LabelText, 16);
		OutButton->SetContent(LabelText);
	};

	AddActionButton(TEXT("RetryButtonSize"), TEXT("ManualRetryButton"), TEXT("ManualRetryText"),
		NSLOCTEXT("ReconnectStatusUI", "ManualRetry", "手动重试 / RETRY"),
		TacticalReconnectUI::RetryNormal, TacticalReconnectUI::RetryHover, ManualRetryButton);

	USpacer* ActionSpacer = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("ActionSpacer"));
	ActionSpacer->SetSize(FVector2D(18.0f, 1.0f));
	ActionRow->AddChildToHorizontalBox(ActionSpacer);

	AddActionButton(TEXT("LeaveButtonSize"), TEXT("LeaveMatchButton"), TEXT("LeaveMatchText"),
		NSLOCTEXT("ReconnectStatusUI", "LeaveMatch", "离开对局 / LEAVE MATCH"),
		TacticalReconnectUI::LeaveNormal, TacticalReconnectUI::LeaveHover, LeaveMatchButton);

	ManualRetryButton->OnClicked.AddDynamic(this, &UReconnectStatusUI::HandleManualRetryClicked);
	LeaveMatchButton->OnClicked.AddDynamic(this, &UReconnectStatusUI::HandleLeaveMatchClicked);
}

void UReconnectStatusUI::ShowAutomaticRetry(int32 NextAttempt, int32 MaxAttempts, float DelaySeconds)
{
	SetOverlayVisible(true);
	SetActionsVisible(false);
	if (DetailText)
	{
		DetailText->SetText(NSLOCTEXT(
			"ReconnectStatusUI", "GraceDetail",
			"服务器会将存活角色保留到本回合结束。角色仍存活时可恢复原角色；自动重试停止后仍可手动重试。"));
	}
	if (StatusText)
	{
		StatusText->SetText(DelaySeconds > 0.05f
			? NSLOCTEXT("ReconnectStatusUI", "WaitingToRetry", "网络连接已断开，正在等待下一次重连…")
			: NSLOCTEXT("ReconnectStatusUI", "StartingRetry", "网络连接已断开，即将尝试重连…"));
	}
	if (ServerStatusText)
	{
		ServerStatusText->SetText(NSLOCTEXT("ReconnectStatusUI", "ServerWaiting", "服务器状态：等待响应"));
	}
	if (AttemptText)
	{
		AttemptText->SetText(DelaySeconds > 0.05f
			? FText::Format(
				NSLOCTEXT("ReconnectStatusUI", "RetryWithDelay", "下一次尝试 {0} / {1}  ·  {2} 秒后"),
				FText::AsNumber(NextAttempt), FText::AsNumber(MaxAttempts),
				FText::AsNumber(FMath::CeilToInt(DelaySeconds)))
			: FText::Format(
				NSLOCTEXT("ReconnectStatusUI", "RetryImmediately", "下一次尝试 {0} / {1}"),
				FText::AsNumber(NextAttempt), FText::AsNumber(MaxAttempts)));
	}
}

void UReconnectStatusUI::ShowAttemptInProgress(int32 CurrentAttempt, int32 MaxAttempts)
{
	SetOverlayVisible(true);
	SetActionsVisible(false);
	if (StatusText)
	{
		StatusText->SetText(NSLOCTEXT("ReconnectStatusUI", "Connecting", "正在重新连接对局…"));
	}
	if (ServerStatusText)
	{
		ServerStatusText->SetText(NSLOCTEXT("ReconnectStatusUI", "ServerConnecting", "服务器状态：正在连接"));
	}
	if (AttemptText)
	{
		AttemptText->SetText(FText::Format(
			NSLOCTEXT("ReconnectStatusUI", "CurrentAttempt", "当前尝试 {0} / {1}"),
			FText::AsNumber(CurrentAttempt), FText::AsNumber(MaxAttempts)));
	}
}

void UReconnectStatusUI::ShowAwaitingServerConfirmation(int32 CurrentAttempt, int32 MaxAttempts)
{
	SetOverlayVisible(true);
	SetActionsVisible(false);
	if (StatusText)
	{
		StatusText->SetText(NSLOCTEXT("ReconnectStatusUI", "RestoringPlayer", "连接已建立，正在恢复你的对局状态…"));
	}
	if (ServerStatusText)
	{
		ServerStatusText->SetText(NSLOCTEXT("ReconnectStatusUI", "ServerConnected", "服务器状态：已连接，等待玩家状态确认"));
	}
	if (AttemptText)
	{
		AttemptText->SetText(FText::Format(
			NSLOCTEXT("ReconnectStatusUI", "ConnectedAttempt", "连接尝试 {0} / {1} 已响应"),
			FText::AsNumber(FMath::Max(1, CurrentAttempt)), FText::AsNumber(MaxAttempts)));
	}
}

void UReconnectStatusUI::ShowReconnectFailure(const FText& FailureReason, int32 AttemptsMade, int32 MaxAttempts)
{
	SetOverlayVisible(true);
	SetActionsVisible(true);
	if (StatusText)
	{
		StatusText->SetText(NSLOCTEXT("ReconnectStatusUI", "AutomaticRetryStopped", "自动重连未成功"));
	}
	if (ServerStatusText)
	{
		ServerStatusText->SetText(FailureReason.IsEmpty()
			? NSLOCTEXT("ReconnectStatusUI", "ServerUnavailable", "服务器状态：当前不可用")
			: FailureReason);
	}
	if (AttemptText)
	{
		AttemptText->SetText(FText::Format(
			NSLOCTEXT("ReconnectStatusUI", "AttemptsStopped", "已完成 {0} / {1} 次自动尝试"),
			FText::AsNumber(AttemptsMade), FText::AsNumber(MaxAttempts)));
	}
	if (DetailText)
	{
		DetailText->SetText(NSLOCTEXT(
			"ReconnectStatusUI", "FailureActions",
			"你可以重新启动一轮重连尝试，或离开本次对局。离开后不会再自动连接这个服务器。"));
	}
	// Do not force keyboard focus here. In single-process multi-window PIE the
	// disconnected client's Slate user can be shared with the host window, so an
	// asynchronous failure transition would otherwise take focus from the host.
	// The reconnect overlay is already in UI-only mode and this button remains
	// available to normal mouse navigation.
}

void UReconnectStatusUI::HideReconnectOverlay()
{
	SetActionsVisible(false);
	SetOverlayVisible(false);
}

void UReconnectStatusUI::SetOverlayVisible(bool bVisible)
{
	SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UReconnectStatusUI::SetActionsVisible(bool bVisible)
{
	if (ActionRow)
	{
		ActionRow->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UReconnectStatusUI::HandleManualRetryClicked()
{
	if (UTacticalGameInstance* Owner = ReconnectOwner.Get())
	{
		Owner->ManualRetryReconnect();
	}
}

void UReconnectStatusUI::HandleLeaveMatchClicked()
{
	if (UTacticalGameInstance* Owner = ReconnectOwner.Get())
	{
		Owner->LeaveMatch();
	}
}
