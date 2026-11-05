// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/TacticalMultiplayerMenuUI.h"

#include "TacticalGameInstance.h"
#include "UI/TacticalSessionResultRowUI.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "InputCoreTypes.h"

namespace TacticalMultiplayerMenuStyle
{
	const FLinearColor FrontendBackdrop(0.004f, 0.007f, 0.012f, 1.0f);
	const FLinearColor MatchBackdrop(0.004f, 0.007f, 0.012f, 0.76f);
	const FLinearColor PanelColor(0.018f, 0.027f, 0.044f, 0.99f);
	const FLinearColor PanelOutline(0.15f, 0.50f, 0.86f, 0.92f);
	const FLinearColor Accent(0.22f, 0.67f, 1.0f, 1.0f);
	const FLinearColor SecondaryText(0.63f, 0.71f, 0.81f, 1.0f);
	const FLinearColor PrimaryNormal(0.04f, 0.34f, 0.68f, 1.0f);
	const FLinearColor PrimaryHover(0.06f, 0.49f, 0.92f, 1.0f);
	const FLinearColor NeutralNormal(0.11f, 0.15f, 0.21f, 1.0f);
	const FLinearColor NeutralHover(0.17f, 0.23f, 0.31f, 1.0f);
	const FLinearColor DangerNormal(0.38f, 0.075f, 0.09f, 1.0f);
	const FLinearColor DangerHover(0.62f, 0.09f, 0.12f, 1.0f);

	void StyleText(UTextBlock* TextBlock, int32 Size,
		const FLinearColor& Color = FLinearColor::White)
	{
		FSlateFontInfo Font = TextBlock->GetFont();
		Font.Size = Size;
		TextBlock->SetFont(Font);
		TextBlock->SetColorAndOpacity(FSlateColor(Color));
		TextBlock->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.88f));
		TextBlock->SetShadowOffset(FVector2D(1.0f, 1.0f));
	}

	void StyleButton(UButton* Button, const FLinearColor& Normal, const FLinearColor& Hover)
	{
		FButtonStyle Style = Button->GetStyle();
		Style.SetNormal(FSlateRoundedBoxBrush(Normal, 6.0f));
		Style.SetHovered(FSlateRoundedBoxBrush(Hover, 6.0f));
		Style.SetPressed(FSlateRoundedBoxBrush(Hover * 0.78f, 6.0f));
		Style.SetDisabled(FSlateRoundedBoxBrush(Normal * 0.36f, 6.0f));
		Button->SetStyle(Style);
	}
}

void UTacticalMultiplayerMenuUI::InitializeMenu(bool bInMatch)
{
	bInMatchMode = bInMatch;
	RefreshModePresentation();
}

void UTacticalMultiplayerMenuUI::CloseMenu()
{
	UnbindSessionSubsystem();
	RemoveFromParent();
}

void UTacticalMultiplayerMenuUI::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildNativeWidgetTree();
	SetIsFocusable(true);
}

void UTacticalMultiplayerMenuUI::NativeConstruct()
{
	Super::NativeConstruct();
	BindSessionSubsystem();
	RefreshModePresentation();
}

void UTacticalMultiplayerMenuUI::NativeDestruct()
{
	UnbindSessionSubsystem();
	Super::NativeDestruct();
}

FReply UTacticalMultiplayerMenuUI::NativeOnKeyDown(
	const FGeometry& InGeometry,
	const FKeyEvent& InKeyEvent)
{
	if (!bInMatchMode)
	{
		return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
	}

	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		if (UTacticalGameInstance* GameInstance = GetTacticalGameInstance())
		{
			if (!GameInstance->IsLeaveMatchPending())
			{
				GameInstance->HideMultiplayerMenu();
			}
		}
	}

	// While the in-match menu owns focus, gameplay keys must not leak through
	// during the frame in which the overlay is opened or closed.
	return FReply::Handled();
}

void UTacticalMultiplayerMenuUI::BuildNativeWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("MultiplayerMenuRoot"));
	Root->SetVisibility(ESlateVisibility::Visible);
	WidgetTree->RootWidget = Root;

	Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MenuBackdrop"));
	Backdrop->SetBrushColor(TacticalMultiplayerMenuStyle::FrontendBackdrop);
	if (UOverlaySlot* BackdropSlot = Root->AddChildToOverlay(Backdrop))
	{
		BackdropSlot->SetHorizontalAlignment(HAlign_Fill);
		BackdropSlot->SetVerticalAlignment(VAlign_Fill);
	}

	PanelSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("MenuPanelSize"));
	if (UOverlaySlot* PanelSlot = Root->AddChildToOverlay(PanelSize))
	{
		PanelSlot->SetHorizontalAlignment(HAlign_Center);
		PanelSlot->SetVerticalAlignment(VAlign_Center);
	}

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MenuPanel"));
	Panel->SetPadding(FMargin(30.0f, 24.0f));
	Panel->SetBrush(FSlateRoundedBoxBrush(
		TacticalMultiplayerMenuStyle::PanelColor,
		10.0f,
		TacticalMultiplayerMenuStyle::PanelOutline,
		1.5f));
	PanelSize->SetContent(Panel);

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MenuContent"));
	Panel->SetContent(Content);

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MenuTitle"));
	TitleText->SetJustification(ETextJustify::Center);
	TacticalMultiplayerMenuStyle::StyleText(TitleText, 26, TacticalMultiplayerMenuStyle::Accent);
	if (UVerticalBoxSlot* TitleSlot = Content->AddChildToVerticalBox(TitleText))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Fill);
		TitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
	}

	IdentityText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("IdentityText"));
	IdentityText->SetJustification(ETextJustify::Center);
	TacticalMultiplayerMenuStyle::StyleText(IdentityText, 13, TacticalMultiplayerMenuStyle::SecondaryText);
	if (UVerticalBoxSlot* IdentitySlot = Content->AddChildToVerticalBox(IdentityText))
	{
		IdentitySlot->SetHorizontalAlignment(HAlign_Fill);
		IdentitySlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 20.0f));
	}

	FrontendActionRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("FrontendActions"));
	if (UVerticalBoxSlot* ActionSlot = Content->AddChildToVerticalBox(FrontendActionRow))
	{
		ActionSlot->SetHorizontalAlignment(HAlign_Center);
		ActionSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 18.0f));
	}

	auto CreateButton = [this](UPanelWidget* Parent, const FName SizeName, const FName ButtonName,
		const FName LabelName, const FText& Label, const FLinearColor& Normal,
		const FLinearColor& Hover, TObjectPtr<UButton>& OutButton,
		TObjectPtr<UTextBlock>* OutLabel = nullptr)
	{
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), SizeName);
		Size->SetWidthOverride(220.0f);
		Size->SetHeightOverride(48.0f);
		Parent->AddChild(Size);

		OutButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), ButtonName);
		TacticalMultiplayerMenuStyle::StyleButton(OutButton, Normal, Hover);
		Size->SetContent(OutButton);

		UTextBlock* LabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), LabelName);
		LabelText->SetText(Label);
		LabelText->SetJustification(ETextJustify::Center);
		TacticalMultiplayerMenuStyle::StyleText(LabelText, 16);
		OutButton->SetContent(LabelText);
		if (OutLabel)
		{
			*OutLabel = LabelText;
		}
	};

	CreateButton(FrontendActionRow, TEXT("HostSize"), TEXT("HostButton"), TEXT("HostLabel"),
		NSLOCTEXT("TacticalMultiplayerMenuUI", "Host", "创建房间 / HOST"),
		TacticalMultiplayerMenuStyle::PrimaryNormal, TacticalMultiplayerMenuStyle::PrimaryHover, HostButton);

	USpacer* FrontendSpacer = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("FrontendActionSpacer"));
	FrontendSpacer->SetSize(FVector2D(16.0f, 1.0f));
	FrontendActionRow->AddChildToHorizontalBox(FrontendSpacer);

	CreateButton(FrontendActionRow, TEXT("RefreshSize"), TEXT("RefreshButton"), TEXT("RefreshLabel"),
		NSLOCTEXT("TacticalMultiplayerMenuUI", "Refresh", "刷新房间 / REFRESH"),
		TacticalMultiplayerMenuStyle::NeutralNormal, TacticalMultiplayerMenuStyle::NeutralHover, RefreshButton);

	FrontendResultsSection = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("FrontendResults"));
	if (UVerticalBoxSlot* ResultsSectionSlot = Content->AddChildToVerticalBox(FrontendResultsSection))
	{
		ResultsSectionSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	UTextBlock* ResultsHeader = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ResultsHeader"));
	ResultsHeader->SetText(NSLOCTEXT(
		"TacticalMultiplayerMenuUI", "ResultsHeader", "可用房间                         玩家      延迟       状态"));
	TacticalMultiplayerMenuStyle::StyleText(ResultsHeader, 13, TacticalMultiplayerMenuStyle::SecondaryText);
	if (UVerticalBoxSlot* HeaderSlot = FrontendResultsSection->AddChildToVerticalBox(ResultsHeader))
	{
		HeaderSlot->SetPadding(FMargin(12.0f, 0.0f, 12.0f, 7.0f));
	}

	ResultsScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ResultsScrollBox"));
	ResultsScrollBox->SetScrollBarVisibility(ESlateVisibility::Visible);
	if (UVerticalBoxSlot* ScrollSlot = FrontendResultsSection->AddChildToVerticalBox(ResultsScrollBox))
	{
		ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	MatchActionColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MatchActions"));
	if (UVerticalBoxSlot* MatchSlot = Content->AddChildToVerticalBox(MatchActionColumn))
	{
		MatchSlot->SetHorizontalAlignment(HAlign_Center);
		MatchSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	CreateButton(MatchActionColumn, TEXT("ResumeSize"), TEXT("ResumeButton"), TEXT("ResumeLabel"),
		NSLOCTEXT("TacticalMultiplayerMenuUI", "Resume", "继续游戏 / RESUME"),
		TacticalMultiplayerMenuStyle::PrimaryNormal, TacticalMultiplayerMenuStyle::PrimaryHover, ResumeButton);

	USpacer* MatchSpacerA = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("MatchSpacerA"));
	MatchSpacerA->SetSize(FVector2D(1.0f, 14.0f));
	MatchActionColumn->AddChildToVerticalBox(MatchSpacerA);

	CreateButton(MatchActionColumn, TEXT("InviteSize"), TEXT("InviteButton"), TEXT("InviteLabel"),
		NSLOCTEXT("TacticalMultiplayerMenuUI", "Invite", "邀请好友 / INVITE"),
		TacticalMultiplayerMenuStyle::NeutralNormal, TacticalMultiplayerMenuStyle::NeutralHover, InviteButton);

	USpacer* MatchSpacerB = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("MatchSpacerB"));
	MatchSpacerB->SetSize(FVector2D(1.0f, 14.0f));
	MatchActionColumn->AddChildToVerticalBox(MatchSpacerB);

	CreateButton(MatchActionColumn, TEXT("LeaveSize"), TEXT("LeaveButton"), TEXT("LeaveLabel"),
		NSLOCTEXT("TacticalMultiplayerMenuUI", "Leave", "离开对局 / LEAVE"),
		TacticalMultiplayerMenuStyle::DangerNormal, TacticalMultiplayerMenuStyle::DangerHover,
		LeaveButton, &LeaveButtonText);

	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MenuStatus"));
	StatusText->SetJustification(ETextJustify::Center);
	StatusText->SetAutoWrapText(true);
	TacticalMultiplayerMenuStyle::StyleText(StatusText, 13, TacticalMultiplayerMenuStyle::SecondaryText);
	if (UVerticalBoxSlot* StatusSlot = Content->AddChildToVerticalBox(StatusText))
	{
		StatusSlot->SetHorizontalAlignment(HAlign_Fill);
		StatusSlot->SetPadding(FMargin(8.0f, 16.0f, 8.0f, 0.0f));
	}

	HostButton->OnClicked.AddDynamic(this, &UTacticalMultiplayerMenuUI::HandleHostClicked);
	RefreshButton->OnClicked.AddDynamic(this, &UTacticalMultiplayerMenuUI::HandleRefreshClicked);
	ResumeButton->OnClicked.AddDynamic(this, &UTacticalMultiplayerMenuUI::HandleResumeClicked);
	InviteButton->OnClicked.AddDynamic(this, &UTacticalMultiplayerMenuUI::HandleInviteClicked);
	LeaveButton->OnClicked.AddDynamic(this, &UTacticalMultiplayerMenuUI::HandleLeaveClicked);
}

void UTacticalMultiplayerMenuUI::BindSessionSubsystem()
{
	UTacticalSessionSubsystem* Sessions = GetSessionSubsystem();
	if (!Sessions || BoundSessionSubsystem.Get() == Sessions)
	{
		return;
	}

	UnbindSessionSubsystem();
	BoundSessionSubsystem = Sessions;
	StatusChangedHandle = Sessions->OnStatusChanged.AddUObject(
		this, &UTacticalMultiplayerMenuUI::HandleSessionStatusChanged);
	ResultsChangedHandle = Sessions->OnResultsChanged.AddUObject(
		this, &UTacticalMultiplayerMenuUI::HandleSessionResultsChanged);
}

void UTacticalMultiplayerMenuUI::UnbindSessionSubsystem()
{
	if (UTacticalSessionSubsystem* Sessions = BoundSessionSubsystem.Get())
	{
		Sessions->OnStatusChanged.Remove(StatusChangedHandle);
		Sessions->OnResultsChanged.Remove(ResultsChangedHandle);
	}
	StatusChangedHandle.Reset();
	ResultsChangedHandle.Reset();
	BoundSessionSubsystem.Reset();
}

void UTacticalMultiplayerMenuUI::RefreshModePresentation()
{
	if (!PanelSize || !TitleText)
	{
		return;
	}

	PanelSize->SetWidthOverride(bInMatchMode ? 500.0f : 900.0f);
	PanelSize->SetHeightOverride(bInMatchMode ? 430.0f : 640.0f);
	if (Backdrop)
	{
		Backdrop->SetBrushColor(bInMatchMode
			? TacticalMultiplayerMenuStyle::MatchBackdrop
			: TacticalMultiplayerMenuStyle::FrontendBackdrop);
	}
	TitleText->SetText(bInMatchMode
		? NSLOCTEXT("TacticalMultiplayerMenuUI", "MatchTitle", "对局菜单 / MATCH MENU")
		: NSLOCTEXT("TacticalMultiplayerMenuUI", "FrontendTitle", "战术竞技 / TACTICAL FPS"));
	FrontendActionRow->SetVisibility(bInMatchMode ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	FrontendResultsSection->SetVisibility(bInMatchMode ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	MatchActionColumn->SetVisibility(bInMatchMode ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	if (UTacticalSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		IdentityText->SetText(FText::Format(
			NSLOCTEXT("TacticalMultiplayerMenuUI", "Identity", "在线身份：{0}"),
			FText::FromString(Sessions->GetOnlineIdentityLabel())));
		bSessionBusy = Sessions->IsBusy();
		if (StatusText)
		{
			StatusText->SetText(Sessions->GetStatusText());
		}
		RebuildResultRows(Sessions->GetSearchEntries());
	}
	else
	{
		IdentityText->SetText(NSLOCTEXT("TacticalMultiplayerMenuUI", "NoSubsystem", "在线服务尚未就绪"));
		if (StatusText)
		{
			StatusText->SetText(NSLOCTEXT("TacticalMultiplayerMenuUI", "NoSubsystemDetail", "无法访问联机会话服务。"));
		}
	}

	RefreshButtons(bSessionBusy);
}

void UTacticalMultiplayerMenuUI::RefreshButtons(bool bBusy)
{
	if (HostButton)
	{
		HostButton->SetIsEnabled(!bBusy && !bInMatchMode);
	}
	if (RefreshButton)
	{
		RefreshButton->SetIsEnabled(!bBusy && !bInMatchMode);
	}

	UTacticalSessionSubsystem* Sessions = GetSessionSubsystem();
	if (InviteButton)
	{
		InviteButton->SetIsEnabled(!bBusy && Sessions && Sessions->HasNamedSession());
	}
	if (ResumeButton)
	{
		ResumeButton->SetIsEnabled(bInMatchMode && !bBusy);
	}
	if (LeaveButton)
	{
		LeaveButton->SetIsEnabled(bInMatchMode && !bBusy);
	}
	if (LeaveButtonText && Sessions)
	{
		LeaveButtonText->SetText(Sessions->IsSessionHost()
			? NSLOCTEXT("TacticalMultiplayerMenuUI", "EndMatch", "结束房间 / END MATCH")
			: NSLOCTEXT("TacticalMultiplayerMenuUI", "Leave", "离开对局 / LEAVE"));
	}

	if (ResultsScrollBox)
	{
		for (UWidget* Child : ResultsScrollBox->GetAllChildren())
		{
			if (UTacticalSessionResultRowUI* Row = Cast<UTacticalSessionResultRowUI>(Child))
			{
				Row->SetInteractionEnabled(!bBusy);
			}
		}
	}
}

void UTacticalMultiplayerMenuUI::RebuildResultRows(const TArray<FTacticalSessionSearchEntry>& Entries)
{
	if (!ResultsScrollBox)
	{
		return;
	}

	ResultsScrollBox->ClearChildren();
	if (Entries.IsEmpty())
	{
		UTextBlock* EmptyText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("EmptyResultsText"));
		EmptyText->SetText(NSLOCTEXT(
			"TacticalMultiplayerMenuUI", "EmptyResults", "暂无房间。点击“刷新房间”重新搜索。"));
		EmptyText->SetJustification(ETextJustify::Center);
		TacticalMultiplayerMenuStyle::StyleText(EmptyText, 15, TacticalMultiplayerMenuStyle::SecondaryText);
		ResultsScrollBox->AddChild(EmptyText);
		return;
	}

	for (const FTacticalSessionSearchEntry& Entry : Entries)
	{
		UTacticalSessionResultRowUI* Row = CreateWidget<UTacticalSessionResultRowUI>(GetOwningPlayer());
		if (!Row)
		{
			continue;
		}

		Row->InitializeResult(
			Entry,
			FTacticalSessionResultSelected::CreateUObject(
				this, &UTacticalMultiplayerMenuUI::HandleResultSelected),
			!bSessionBusy);
		ResultsScrollBox->AddChild(Row);
	}
}

UTacticalSessionSubsystem* UTacticalMultiplayerMenuUI::GetSessionSubsystem() const
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		return GameInstance->GetSubsystem<UTacticalSessionSubsystem>();
	}
	return nullptr;
}

UTacticalGameInstance* UTacticalMultiplayerMenuUI::GetTacticalGameInstance() const
{
	return Cast<UTacticalGameInstance>(GetGameInstance());
}

void UTacticalMultiplayerMenuUI::HandleSessionStatusChanged(
	ETacticalSessionOperation NewOperation,
	const FText& NewStatus,
	bool bBusy)
{
	bSessionBusy = bBusy;
	if (StatusText)
	{
		StatusText->SetText(NewStatus);
		StatusText->SetColorAndOpacity(FSlateColor(NewOperation == ETacticalSessionOperation::Failed
			? FLinearColor(1.0f, 0.34f, 0.30f, 1.0f)
			: TacticalMultiplayerMenuStyle::SecondaryText));
	}
	RefreshButtons(bBusy);
}

void UTacticalMultiplayerMenuUI::HandleSessionResultsChanged(
	const TArray<FTacticalSessionSearchEntry>& Entries)
{
	RebuildResultRows(Entries);
}

void UTacticalMultiplayerMenuUI::HandleResultSelected(int32 ResultHandle)
{
	if (UTacticalSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->JoinSessionByHandle(ResultHandle);
	}
}

void UTacticalMultiplayerMenuUI::HandleHostClicked()
{
	if (UTacticalSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->HostSession();
	}
}

void UTacticalMultiplayerMenuUI::HandleRefreshClicked()
{
	if (UTacticalSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->FindSessions();
	}
}

void UTacticalMultiplayerMenuUI::HandleResumeClicked()
{
	if (UTacticalGameInstance* GameInstance = GetTacticalGameInstance())
	{
		GameInstance->HideMultiplayerMenu();
	}
}

void UTacticalMultiplayerMenuUI::HandleInviteClicked()
{
	if (UTacticalSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->ShowInviteUI();
	}
}

void UTacticalMultiplayerMenuUI::HandleLeaveClicked()
{
	// The authoritative abandon/host-shutdown acknowledgement is asynchronous.
	// Lock the menu immediately so a double-click cannot submit two teardown
	// paths before the Session subsystem enters its Leaving state.
	bSessionBusy = true;
	RefreshButtons(true);
	if (StatusText)
	{
		StatusText->SetText(bInMatchMode
			? NSLOCTEXT("TacticalMultiplayerMenuUI", "LeavingMatch", "正在结束当前连接……")
			: NSLOCTEXT("TacticalMultiplayerMenuUI", "LeavingRoom", "正在离开房间……"));
	}
	if (UTacticalGameInstance* GameInstance = GetTacticalGameInstance())
	{
		GameInstance->LeaveMatch();
	}
}
