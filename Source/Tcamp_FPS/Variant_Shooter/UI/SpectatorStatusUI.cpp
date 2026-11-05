// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SpectatorStatusUI.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace SpectatorHudStyle
{
	constexpr float PanelWidth = 620.0f;
	constexpr float PanelHeight = 218.0f;
	constexpr float BarHeight = 25.0f;

	const FLinearColor PanelColor(0.015f, 0.02f, 0.03f, 0.88f);
	const FLinearColor PanelOutline(0.42f, 0.48f, 0.56f, 0.65f);
	const FLinearColor EmptyBarColor(0.19f, 0.21f, 0.24f, 0.95f);
	const FLinearColor HealthGreen(0.08f, 0.72f, 0.25f, 1.0f);
	const FLinearColor ArmorBlue(0.05f, 0.52f, 1.0f, 1.0f);
	const FLinearColor NoArmorGray(0.37f, 0.39f, 0.43f, 1.0f);
	const FLinearColor RedTeam(0.72f, 0.035f, 0.035f, 0.96f);
	const FLinearColor BlueTeam(0.035f, 0.17f, 0.72f, 0.96f);
	const FLinearColor UnknownTeam(0.25f, 0.27f, 0.31f, 0.96f);

	FSlateFontInfo FontWithSize(const UTextBlock* TextBlock, int32 Size)
	{
		FSlateFontInfo Font = TextBlock->GetFont();
		Font.Size = Size;
		return Font;
	}

	void StyleReadableText(UTextBlock* TextBlock, int32 Size, const FLinearColor& Color = FLinearColor::White)
	{
		TextBlock->SetFont(FontWithSize(TextBlock, Size));
		TextBlock->SetColorAndOpacity(FSlateColor(Color));
		TextBlock->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
		TextBlock->SetShadowOffset(FVector2D(1.0f, 1.0f));
	}

	void StyleProgressBar(UProgressBar* ProgressBar)
	{
		FProgressBarStyle BarStyle = ProgressBar->GetWidgetStyle();
		BarStyle.SetBackgroundImage(FSlateRoundedBoxBrush(EmptyBarColor, FVector2D(24.0f, 24.0f)));
		BarStyle.SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White, FVector2D(24.0f, 24.0f)));
		ProgressBar->SetWidgetStyle(BarStyle);
		ProgressBar->SetBarFillStyle(EProgressBarFillStyle::Scale);
	}
}

void USpectatorStatusUI::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildNativeWidgetTree();
	SetVisibility(ESlateVisibility::Collapsed);
}

void USpectatorStatusUI::NativeConstruct()
{
	Super::NativeConstruct();
	ClearSpectatorDisplay();
}

void USpectatorStatusUI::BuildNativeWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("SpectatorStatusRoot"));
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Root;

	SpectatorPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SpectatorPanel"));
	SpectatorPanel->SetPadding(FMargin(16.0f, 11.0f));
	SpectatorPanel->SetBrush(FSlateRoundedBoxBrush(
		SpectatorHudStyle::PanelColor,
		8.0f,
		SpectatorHudStyle::PanelOutline,
		1.25f));
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(SpectatorPanel))
	{
		PanelSlot->SetAnchors(FAnchors(0.5f, 1.0f));
		PanelSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		PanelSlot->SetPosition(FVector2D(0.0f, -42.0f));
		PanelSlot->SetSize(FVector2D(SpectatorHudStyle::PanelWidth, SpectatorHudStyle::PanelHeight));
	}

	UVerticalBox* PanelContent = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PanelContent"));
	SpectatorPanel->SetContent(PanelContent);

	SpectatingTitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SpectatingTitleText"));
	SpectatingTitleText->SetText(NSLOCTEXT("SpectatorStatusUI", "SpectatingTitle", "正在观战 / SPECTATING"));
	SpectatingTitleText->SetJustification(ETextJustify::Center);
	SpectatorHudStyle::StyleReadableText(SpectatingTitleText, 20, FLinearColor(1.0f, 0.84f, 0.22f, 1.0f));
	if (UVerticalBoxSlot* TitleSlot = PanelContent->AddChildToVerticalBox(SpectatingTitleText))
	{
		TitleSlot->SetHorizontalAlignment(HAlign_Fill);
		TitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
	}

	UHorizontalBox* IdentityRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("IdentityRow"));
	if (UVerticalBoxSlot* IdentitySlot = PanelContent->AddChildToVerticalBox(IdentityRow))
	{
		IdentitySlot->SetHorizontalAlignment(HAlign_Fill);
		IdentitySlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 7.0f));
	}

	TeamBadge = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SpectatorTeamBadge"));
	TeamBadge->SetPadding(FMargin(9.0f, 3.0f));
	TeamBadge->SetBrush(FSlateRoundedBoxBrush(SpectatorHudStyle::UnknownTeam, 4.0f));
	TeamBadgeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SpectatorTeamBadgeText"));
	TeamBadgeText->SetText(NSLOCTEXT("SpectatorStatusUI", "UnknownTeam", "未知队伍"));
	TeamBadgeText->SetJustification(ETextJustify::Center);
	SpectatorHudStyle::StyleReadableText(TeamBadgeText, 13);
	TeamBadge->SetContent(TeamBadgeText);
	if (UHorizontalBoxSlot* BadgeSlot = IdentityRow->AddChildToHorizontalBox(TeamBadge))
	{
		BadgeSlot->SetHorizontalAlignment(HAlign_Left);
		BadgeSlot->SetVerticalAlignment(VAlign_Center);
		BadgeSlot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
	}

	TargetNameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TargetNameText"));
	SpectatorHudStyle::StyleReadableText(TargetNameText, 17);
	if (UHorizontalBoxSlot* NameSlot = IdentityRow->AddChildToHorizontalBox(TargetNameText))
	{
		NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		NameSlot->SetHorizontalAlignment(HAlign_Left);
		NameSlot->SetVerticalAlignment(VAlign_Center);
	}

	TargetCountText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TargetCountText"));
	TargetCountText->SetJustification(ETextJustify::Right);
	SpectatorHudStyle::StyleReadableText(TargetCountText, 14, FLinearColor(0.78f, 0.82f, 0.88f, 1.0f));
	if (UHorizontalBoxSlot* CountSlot = IdentityRow->AddChildToHorizontalBox(TargetCountText))
	{
		CountSlot->SetHorizontalAlignment(HAlign_Right);
		CountSlot->SetVerticalAlignment(VAlign_Center);
	}

	DetailPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SpectatorDetailPanel"));
	if (UVerticalBoxSlot* DetailsSlot = PanelContent->AddChildToVerticalBox(DetailPanel))
	{
		DetailsSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	auto AddBarRow = [this](UVerticalBox* Parent, const FName SizeBoxName, const FName OverlayName,
		const FName BarName, const FName TextName, TObjectPtr<UProgressBar>& OutBar, TObjectPtr<UTextBlock>& OutText)
	{
		USizeBox* BarSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), SizeBoxName);
		BarSize->SetHeightOverride(SpectatorHudStyle::BarHeight);
		if (UVerticalBoxSlot* SizeSlot = Parent->AddChildToVerticalBox(BarSize))
		{
			SizeSlot->SetHorizontalAlignment(HAlign_Fill);
			SizeSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));
		}

		UOverlay* BarOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), OverlayName);
		BarSize->SetContent(BarOverlay);

		OutBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), BarName);
		SpectatorHudStyle::StyleProgressBar(OutBar);
		if (UOverlaySlot* ProgressSlot = BarOverlay->AddChildToOverlay(OutBar))
		{
			ProgressSlot->SetHorizontalAlignment(HAlign_Fill);
			ProgressSlot->SetVerticalAlignment(VAlign_Fill);
		}

		OutText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TextName);
		OutText->SetJustification(ETextJustify::Center);
		SpectatorHudStyle::StyleReadableText(OutText, 13);
		if (UOverlaySlot* TextSlot = BarOverlay->AddChildToOverlay(OutText))
		{
			TextSlot->SetHorizontalAlignment(HAlign_Fill);
			TextSlot->SetVerticalAlignment(VAlign_Center);
		}
	};

	AddBarRow(DetailPanel, TEXT("HealthSize"), TEXT("HealthOverlay"), TEXT("SpectatorHealthBar"),
		TEXT("SpectatorHealthText"), HealthBar, HealthText);
	AddBarRow(DetailPanel, TEXT("ArmorSize"), TEXT("ArmorOverlay"), TEXT("SpectatorArmorBar"),
		TEXT("SpectatorArmorText"), ArmorBar, ArmorText);

	UHorizontalBox* EquipmentRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("EquipmentRow"));
	if (UVerticalBoxSlot* EquipmentSlot = DetailPanel->AddChildToVerticalBox(EquipmentRow))
	{
		EquipmentSlot->SetHorizontalAlignment(HAlign_Fill);
		EquipmentSlot->SetPadding(FMargin(2.0f, 1.0f, 2.0f, 0.0f));
	}

	WeaponText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SpectatorWeaponText"));
	SpectatorHudStyle::StyleReadableText(WeaponText, 14);
	if (UHorizontalBoxSlot* WeaponSlot = EquipmentRow->AddChildToHorizontalBox(WeaponText))
	{
		WeaponSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		WeaponSlot->SetHorizontalAlignment(HAlign_Left);
		WeaponSlot->SetVerticalAlignment(VAlign_Center);
	}

	AmmoText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SpectatorAmmoText"));
	AmmoText->SetJustification(ETextJustify::Right);
	SpectatorHudStyle::StyleReadableText(AmmoText, 14, FLinearColor(1.0f, 0.9f, 0.55f, 1.0f));
	if (UHorizontalBoxSlot* AmmoSlot = EquipmentRow->AddChildToHorizontalBox(AmmoText))
	{
		AmmoSlot->SetHorizontalAlignment(HAlign_Right);
		AmmoSlot->SetVerticalAlignment(VAlign_Center);
	}
}

void USpectatorStatusUI::ApplyTargetIdentity(const FString& TargetName, int32 TargetPlayerId, uint8 TeamByte,
	int32 TargetIndex, int32 TargetCount)
{
	const bool bRedTeam = TeamByte == 0;
	const bool bBlueTeam = TeamByte == 1;
	if (TeamBadge)
	{
		TeamBadge->SetBrushColor(bRedTeam
			? SpectatorHudStyle::RedTeam
			: bBlueTeam ? SpectatorHudStyle::BlueTeam : SpectatorHudStyle::UnknownTeam);
	}
	if (TeamBadgeText)
	{
		TeamBadgeText->SetText(bRedTeam
			? NSLOCTEXT("SpectatorStatusUI", "RedTeam", "红队 / RED")
			: bBlueTeam
				? NSLOCTEXT("SpectatorStatusUI", "BlueTeam", "蓝队 / BLUE")
				: NSLOCTEXT("SpectatorStatusUI", "UnknownTeam", "未知队伍"));
	}

	if (TargetNameText)
	{
		const FString SafeName = TargetName.IsEmpty() ? TEXT("未知玩家 / UNKNOWN") : TargetName;
		TargetNameText->SetText(TargetPlayerId == INDEX_NONE
			? FText::FromString(SafeName)
			: FText::Format(
				NSLOCTEXT("SpectatorStatusUI", "PlayerNameAndId", "{0}  #{1}"),
				FText::FromString(SafeName),
				FText::AsNumber(TargetPlayerId)));
	}

	if (TargetCountText)
	{
		const int32 SafeCount = FMath::Max(0, TargetCount);
		const int32 DisplayIndex = SafeCount > 0 ? FMath::Clamp(TargetIndex + 1, 1, SafeCount) : 0;
		TargetCountText->SetText(FText::Format(
			NSLOCTEXT("SpectatorStatusUI", "TargetPosition", "队友 {0} / {1}"),
			FText::AsNumber(DisplayIndex),
			FText::AsNumber(SafeCount)));
	}
}

void USpectatorStatusUI::SetSpectatorDisplay(const FSpectatorHudSnapshot& Snapshot)
{
	if (!Snapshot.bIsValid)
	{
		ShowNoTarget();
		return;
	}

	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (SpectatingTitleText)
	{
		SpectatingTitleText->SetText(NSLOCTEXT("SpectatorStatusUI", "SpectatingTitle", "正在观战 / SPECTATING"));
	}
	if (DetailPanel)
	{
		DetailPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	ApplyTargetIdentity(Snapshot.TargetPlayerName, Snapshot.TargetPlayerId, Snapshot.TeamByte,
		Snapshot.TargetIndex, Snapshot.TargetCount);

	const float SafeMaxHealth = FMath::Max(0.0f, Snapshot.MaxHealth);
	const float SafeHealth = FMath::Clamp(Snapshot.CurrentHealth, 0.0f, SafeMaxHealth);
	if (HealthBar)
	{
		HealthBar->SetPercent(SafeMaxHealth > UE_SMALL_NUMBER ? SafeHealth / SafeMaxHealth : 0.0f);
		HealthBar->SetFillColorAndOpacity(SpectatorHudStyle::HealthGreen);
	}
	if (HealthText)
	{
		HealthText->SetText(FText::Format(
			NSLOCTEXT("SpectatorStatusUI", "HealthValues", "生命 / HP    {0} / {1}"),
			FText::AsNumber(FMath::RoundToInt(SafeHealth)),
			FText::AsNumber(FMath::RoundToInt(SafeMaxHealth))));
	}

	const float SafeMaxArmor = FMath::Max(0.0f, Snapshot.MaxArmor);
	const float SafeArmor = FMath::Clamp(Snapshot.CurrentArmor, 0.0f, SafeMaxArmor);
	const bool bHasArmor = Snapshot.ArmorType != EArmorType::None && SafeMaxArmor > UE_SMALL_NUMBER && SafeArmor > 0.0f;
	if (ArmorBar)
	{
		// A player without armor still gets a full neutral bar, so the row remains
		// visually legible instead of looking like missing or uninitialized data.
		ArmorBar->SetPercent(bHasArmor ? SafeArmor / SafeMaxArmor : 1.0f);
		ArmorBar->SetFillColorAndOpacity(bHasArmor ? SpectatorHudStyle::ArmorBlue : SpectatorHudStyle::NoArmorGray);
	}
	if (ArmorText)
	{
		if (!bHasArmor)
		{
			ArmorText->SetText(NSLOCTEXT("SpectatorStatusUI", "NoArmor", "无护甲 / NO ARMOR"));
		}
		else
		{
			const FText ArmorName = Snapshot.ArmorType == EArmorType::Heavy
				? NSLOCTEXT("SpectatorStatusUI", "HeavyArmor", "重甲 / HEAVY")
				: NSLOCTEXT("SpectatorStatusUI", "LightArmor", "轻甲 / LIGHT");
			ArmorText->SetText(FText::Format(
				NSLOCTEXT("SpectatorStatusUI", "ArmorValues", "{0}    {1} / {2}"),
				ArmorName,
				FText::AsNumber(FMath::RoundToInt(SafeArmor)),
				FText::AsNumber(FMath::RoundToInt(SafeMaxArmor))));
		}
	}

	if (!Snapshot.bHasWeapon)
	{
		if (WeaponText)
		{
			WeaponText->SetText(NSLOCTEXT("SpectatorStatusUI", "NoWeapon", "武器：无 / WEAPON: NONE"));
		}
		if (AmmoText)
		{
			AmmoText->SetText(NSLOCTEXT("SpectatorStatusUI", "NoAmmo", "弹药 / AMMO  —"));
		}
		return;
	}

	FText WeaponName;
	switch (Snapshot.WeaponTier)
	{
	case EWeaponTier::UpgradedPistol:
		WeaponName = NSLOCTEXT("SpectatorStatusUI", "UpgradedPistol", "升级手枪 / UPGRADED PISTOL");
		break;
	case EWeaponTier::Rifle:
		WeaponName = NSLOCTEXT("SpectatorStatusUI", "Rifle", "步枪 / RIFLE");
		break;
	case EWeaponTier::Sniper:
		WeaponName = NSLOCTEXT("SpectatorStatusUI", "Sniper", "狙击步枪 / SNIPER");
		break;
	case EWeaponTier::DefaultPistol:
	default:
		WeaponName = NSLOCTEXT("SpectatorStatusUI", "DefaultPistol", "基础手枪 / DEFAULT PISTOL");
		break;
	}

	if (WeaponText)
	{
		WeaponText->SetText(FText::Format(
			NSLOCTEXT("SpectatorStatusUI", "WeaponName", "武器：{0}"),
			WeaponName));
	}
	if (AmmoText)
	{
		AmmoText->SetText(FText::Format(
			NSLOCTEXT("SpectatorStatusUI", "AmmoValues", "弹匣 {0} / {1}    备弹 {2}"),
			FText::AsNumber(FMath::Max(0, Snapshot.CurrentMagazineAmmo)),
			FText::AsNumber(FMath::Max(0, Snapshot.MagazineCapacity)),
			FText::AsNumber(FMath::Max(0, Snapshot.ReserveAmmo))));
	}
}

void USpectatorStatusUI::ShowPending(const FString& TargetName, uint8 TeamByte, int32 TargetIndex, int32 TargetCount)
{
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (SpectatingTitleText)
	{
		SpectatingTitleText->SetText(NSLOCTEXT("SpectatorStatusUI", "SpectatingPending", "正在观战 / SPECTATING  ·  同步状态…"));
	}
	if (DetailPanel)
	{
		DetailPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
	ApplyTargetIdentity(TargetName, INDEX_NONE, TeamByte, TargetIndex, TargetCount);
}

void USpectatorStatusUI::ShowNoTarget()
{
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (SpectatingTitleText)
	{
		SpectatingTitleText->SetText(NSLOCTEXT("SpectatorStatusUI", "SpectatingWaiting", "正在观战 / SPECTATING"));
	}
	if (TeamBadge)
	{
		TeamBadge->SetBrushColor(SpectatorHudStyle::UnknownTeam);
	}
	if (TeamBadgeText)
	{
		TeamBadgeText->SetText(NSLOCTEXT("SpectatorStatusUI", "WaitingBadge", "等待"));
	}
	if (TargetNameText)
	{
		TargetNameText->SetText(NSLOCTEXT("SpectatorStatusUI", "NoLivingTeammate", "暂无可观战的存活队友 / NO LIVING TEAMMATE"));
	}
	if (TargetCountText)
	{
		TargetCountText->SetText(FText::GetEmpty());
	}
	if (DetailPanel)
	{
		DetailPanel->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void USpectatorStatusUI::ClearSpectatorDisplay()
{
	SetVisibility(ESlateVisibility::Collapsed);
}
