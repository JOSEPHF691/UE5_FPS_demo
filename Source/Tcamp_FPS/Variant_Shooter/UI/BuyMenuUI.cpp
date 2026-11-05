// Copyright Epic Games, Inc. All Rights Reserved.

#include "BuyMenuUI.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "InputCoreTypes.h"
#include "TacticalShopItemIconUI.h"
#include "UObject/ConstructorHelpers.h"
#include "Variant_Shooter/Economy/EconomyTypes.h"
#include "Variant_Shooter/TacticalPlayerController.h"
#include "Variant_Shooter/TacticalPlayerState.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"

namespace BuyCatalog
{
	constexpr float Width = 1600.0f;
	constexpr float Height = 900.0f;
	constexpr float ColumnWidth = 371.0f;
	constexpr float ColumnGap = 12.0f;
	constexpr float ColumnLeft = 40.0f;
	constexpr float CardTop = 143.0f;
	constexpr float CardHeight = 677.0f;

	const FLinearColor Backdrop(0.008f, 0.018f, 0.029f, 0.96f);
	const FLinearColor Panel(0.025f, 0.054f, 0.075f, 0.98f);
	const FLinearColor ImagePanel(0.012f, 0.035f, 0.052f, 1.0f);
	const FLinearColor Teal(0.18f, 0.86f, 0.88f, 1.0f);
	const FLinearColor Gold(1.0f, 0.72f, 0.28f, 1.0f);
	const FLinearColor White(0.90f, 0.95f, 0.97f, 1.0f);
	const FLinearColor Muted(0.52f, 0.65f, 0.72f, 1.0f);

	UCanvasPanelSlot* Place(UCanvasPanel* Parent, UWidget* Child,
		float X, float Y, float W, float H)
	{
		UCanvasPanelSlot* Slot = Parent->AddChildToCanvas(Child);
		Slot->SetPosition(FVector2D(X, Y));
		Slot->SetSize(FVector2D(W, H));
		return Slot;
	}

	UBorder* MakePanel(UWidgetTree* Tree, UCanvasPanel* Canvas,
		float X, float Y, float W, float H, const FLinearColor& Color)
	{
		UBorder* Border = Tree->ConstructWidget<UBorder>();
		Border->SetBrushColor(Color);
		Border->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(Canvas, Border, X, Y, W, H);
		return Border;
	}

	UTextBlock* MakeText(UWidgetTree* Tree, UCanvasPanel* Canvas,
		const FString& Value, float X, float Y, float W, float H,
		int32 FontSize, const FLinearColor& Color)
	{
		UTextBlock* Label = Tree->ConstructWidget<UTextBlock>();
		Label->SetText(FText::FromString(Value));
		Label->SetColorAndOpacity(FSlateColor(Color));
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = FontSize;
		Label->SetFont(Font);
		Label->SetAutoWrapText(true);
		Label->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(Canvas, Label, X, Y, W, H);
		return Label;
	}

	void StyleButton(UButton* Button, const FLinearColor& Base)
	{
		FButtonStyle Style;
		Style.Normal = FSlateColorBrush(Base);
		Style.Hovered = FSlateColorBrush(Base * 1.25f);
		Style.Pressed = FSlateColorBrush(Base * 0.78f);
		Style.Disabled = FSlateColorBrush(FLinearColor(0.12f, 0.19f, 0.23f, 1.0f));
		Style.NormalPadding = FMargin(0.0f);
		Style.PressedPadding = FMargin(0.0f);
		Button->SetStyle(Style);
	}

	UTextBlock* AdoptButton(UWidgetTree* Tree, UCanvasPanel* Canvas,
		UButton* Button, float X, float Y, float W, float H,
		const FString& Caption, const FLinearColor& Base, int32 FontSize)
	{
		Button->RemoveFromParent();
		StyleButton(Button, Base);
		UTextBlock* Label = Tree->ConstructWidget<UTextBlock>();
		Label->SetText(FText::FromString(Caption));
		Label->SetJustification(ETextJustify::Center);
		Label->SetColorAndOpacity(FSlateColor(White));
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = FontSize;
		Label->SetFont(Font);
		Button->SetContent(Label);
		Place(Canvas, Button, X, Y, W, H);
		return Label;
	}
}

UBuyMenuUI::UBuyMenuUI(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UTexture2D> RifleIcon(
		TEXT("/Game/RAR/Content/InfimaGames/RealisticAssaultRifle/Art/Weapon/Icons/T_RAR_HUD_AssaultRifle_Body_D.T_RAR_HUD_AssaultRifle_Body_D"));
	if (RifleIcon.Succeeded())
	{
		DefaultRifleIcon = RifleIcon.Object;
	}
}

void UBuyMenuUI::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	bNativeCatalogReady = BuildNativeCatalog();
	if (bNativeCatalogReady)
	{
		RefreshCatalog();
	}
}

void UBuyMenuUI::NativeConstruct()
{
	Super::NativeConstruct();
	if (!bNativeCatalogReady)
	{
		return;
	}

	// Blueprint's old OnClicked handlers can be installed during its Construct.
	// Clear them after Super, so one click can never issue two purchase RPCs.
	PistolButton->OnClicked.Clear();
	RifleButton->OnClicked.Clear();
	SniperButton->OnClicked.Clear();
	LightArmorButton->OnClicked.Clear();
	HeavyArmorButton->OnClicked.Clear();
	CloseButton->OnClicked.Clear();
	PistolButton->OnClicked.AddDynamic(this, &UBuyMenuUI::BuyPistol);
	RifleButton->OnClicked.AddDynamic(this, &UBuyMenuUI::BuyRifle);
	SniperButton->OnClicked.AddDynamic(this, &UBuyMenuUI::BuySniper);
	LightArmorButton->OnClicked.AddDynamic(this, &UBuyMenuUI::BuyLightArmor);
	HeavyArmorButton->OnClicked.AddDynamic(this, &UBuyMenuUI::BuyHeavyArmor);
	CloseButton->OnClicked.AddDynamic(this, &UBuyMenuUI::CloseCatalog);
	RefreshCatalog();
}

bool UBuyMenuUI::BuildNativeCatalog()
{
	UCanvasPanel* RootCanvas = WidgetTree ? Cast<UCanvasPanel>(WidgetTree->RootWidget) : nullptr;
	if (!RootCanvas)
	{
		UE_LOG(LogTemp, Warning, TEXT("Event=NativeBuyCatalog Ready=0 Reason=CanvasRootMissing"));
		return false;
	}

	PistolButton = Cast<UButton>(GetWidgetFromName(TEXT("Btn_BuyPistol")));
	RifleButton = Cast<UButton>(GetWidgetFromName(TEXT("Btn_BuyRifle")));
	SniperButton = Cast<UButton>(GetWidgetFromName(TEXT("Btn_BuySniper")));
	LightArmorButton = Cast<UButton>(GetWidgetFromName(TEXT("Btn_BuyLightArmor")));
	HeavyArmorButton = Cast<UButton>(GetWidgetFromName(TEXT("Btn_BuyHeavyArmor")));
	CloseButton = Cast<UButton>(GetWidgetFromName(TEXT("Btn_Close")));
	if (!PistolButton || !RifleButton || !SniperButton || !LightArmorButton || !HeavyArmorButton || !CloseButton)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Event=NativeBuyCatalog Ready=0 Buttons=P%d,R%d,S%d,L%d,H%d,C%d"),
			PistolButton != nullptr, RifleButton != nullptr, SniperButton != nullptr,
			LightArmorButton != nullptr, HeavyArmorButton != nullptr, CloseButton != nullptr);
		return false;
	}

	// Keep the original Blueprint widget objects, including its six buttons, but
	// replace only the visual hierarchy. No Blueprint asset is changed or saved.
	PistolButton->RemoveFromParent();
	RifleButton->RemoveFromParent();
	SniperButton->RemoveFromParent();
	LightArmorButton->RemoveFromParent();
	HeavyArmorButton->RemoveFromParent();
	CloseButton->RemoveFromParent();
	RootCanvas->ClearChildren();
	RootCanvas->SetVisibility(ESlateVisibility::Visible);
	RootCanvas->SetRenderOpacity(1.0f);
	SetRenderOpacity(1.0f);

	UBorder* FullscreenDim = WidgetTree->ConstructWidget<UBorder>();
	FullscreenDim->SetBrushColor(BuyCatalog::Backdrop);
	FullscreenDim->SetVisibility(ESlateVisibility::HitTestInvisible);
	UCanvasPanelSlot* DimSlot = RootCanvas->AddChildToCanvas(FullscreenDim);
	DimSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
	DimSlot->SetOffsets(FMargin(0.0f));

	UScaleBox* Scaler = WidgetTree->ConstructWidget<UScaleBox>();
	Scaler->SetStretch(EStretch::ScaleToFit);
	Scaler->SetStretchDirection(EStretchDirection::Both);
	UCanvasPanelSlot* ScalerSlot = RootCanvas->AddChildToCanvas(Scaler);
	ScalerSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
	ScalerSlot->SetOffsets(FMargin(0.0f));

	USizeBox* DesignSize = WidgetTree->ConstructWidget<USizeBox>();
	DesignSize->SetWidthOverride(BuyCatalog::Width);
	DesignSize->SetHeightOverride(BuyCatalog::Height);
	Scaler->SetContent(DesignSize);
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	DesignSize->SetContent(Canvas);

	BuyCatalog::MakePanel(WidgetTree, Canvas, 40.0f, 31.0f, 1520.0f, 91.0f, BuyCatalog::Panel);
	BuyCatalog::MakePanel(WidgetTree, Canvas, 40.0f, 31.0f, 6.0f, 91.0f, BuyCatalog::Teal);
	BuyCatalog::MakeText(WidgetTree, Canvas, TEXT("战术军械库"),
		68.0f, 45.0f, 425.0f, 45.0f, 32, BuyCatalog::White);
	BuyCatalog::MakeText(WidgetTree, Canvas, TEXT("TACTICAL LOADOUT  /  BUY PHASE"),
		68.0f, 91.0f, 525.0f, 23.0f, 14, BuyCatalog::Muted);
	MoneyLabel = BuyCatalog::MakeText(WidgetTree, Canvas, TEXT("资金  $0"),
		1030.0f, 55.0f, 250.0f, 48.0f, 28, BuyCatalog::Gold);
	TimerLabel = BuyCatalog::MakeText(WidgetTree, Canvas, TEXT("购买剩余  -- 秒"),
		1280.0f, 62.0f, 225.0f, 38.0f, 20, BuyCatalog::White);
	BuyCatalog::AdoptButton(WidgetTree, Canvas, CloseButton,
		1511.0f, 52.0f, 41.0f, 40.0f, TEXT("×"), FLinearColor(0.28f, 0.12f, 0.16f), 29);

	PriceLabels.SetNum(5);
	DetailLabels.SetNum(5);
	SubtitleLabels.SetNum(5);
	ActionLabels.SetNum(5);
	CardImages.SetNum(5);
	FallbackIcons.SetNum(5);
	BuildWeaponCard(Canvas, 0, BuyCatalog::ColumnLeft, PistolButton);
	BuildWeaponCard(Canvas, 1, BuyCatalog::ColumnLeft + BuyCatalog::ColumnWidth + BuyCatalog::ColumnGap, RifleButton);
	BuildWeaponCard(Canvas, 2, BuyCatalog::ColumnLeft + 2.0f * (BuyCatalog::ColumnWidth + BuyCatalog::ColumnGap), SniperButton);
	const float ArmorX = BuyCatalog::ColumnLeft + 3.0f * (BuyCatalog::ColumnWidth + BuyCatalog::ColumnGap);
	BuildArmorCard(Canvas, 3, ArmorX, BuyCatalog::CardTop, LightArmorButton);
	BuildArmorCard(Canvas, 4, ArmorX, BuyCatalog::CardTop + 344.0f, HeavyArmorButton);

	BuyCatalog::MakePanel(WidgetTree, Canvas, 40.0f, 836.0f, 1520.0f, 36.0f,
		FLinearColor(0.017f, 0.043f, 0.057f, 1.0f));
	ResultLabel = BuyCatalog::MakeText(WidgetTree, Canvas,
		TEXT("选择装备后点击购买  ·  按 Tab 查看战绩"),
		58.0f, 843.0f, 1440.0f, 28.0f, 16, BuyCatalog::Muted);
	UE_LOG(LogTemp, Log, TEXT("Event=NativeBuyCatalog Ready=1 Buttons=P1,R1,S1,L1,H1,C1 Cards=5"));
	return true;
}

void UBuyMenuUI::BuildWeaponCard(UCanvasPanel* Canvas, int32 Index, float X,
	UButton* PurchaseButton)
{
	const TCHAR* Names[] = { TEXT("强化手枪"), TEXT("步枪"), TEXT("狙击枪") };
	const TCHAR* Tags[] = { TEXT("01   /   SIDEARM"), TEXT("02   /   ASSAULT"), TEXT("03   /   PRECISION") };
	const ETacticalShopItemIconStyle IconStyles[] = {
		ETacticalShopItemIconStyle::UpgradedPistol,
		ETacticalShopItemIconStyle::Rifle,
		ETacticalShopItemIconStyle::Sniper
	};
	BuyCatalog::MakePanel(WidgetTree, Canvas, X, BuyCatalog::CardTop,
		BuyCatalog::ColumnWidth, BuyCatalog::CardHeight, BuyCatalog::Panel);
	BuyCatalog::MakePanel(WidgetTree, Canvas, X, BuyCatalog::CardTop,
		BuyCatalog::ColumnWidth, 5.0f, BuyCatalog::Teal);
	BuyCatalog::MakeText(WidgetTree, Canvas, Tags[Index],
		X + 22.0f, 171.0f, 320.0f, 26.0f, 14, BuyCatalog::Teal);
	BuyCatalog::MakeText(WidgetTree, Canvas, Names[Index],
		X + 22.0f, 202.0f, 322.0f, 48.0f, 31, BuyCatalog::White);
	SubtitleLabels[Index] = BuyCatalog::MakeText(WidgetTree, Canvas, TEXT(""),
		X + 22.0f, 252.0f, 322.0f, 28.0f, 15, BuyCatalog::Muted);
	PriceLabels[Index] = BuyCatalog::MakeText(WidgetTree, Canvas, TEXT("$ --"),
		X + 22.0f, 289.0f, 322.0f, 46.0f, 29, BuyCatalog::Gold);
	BuyCatalog::MakePanel(WidgetTree, Canvas, X + 18.0f, 342.0f,
		BuyCatalog::ColumnWidth - 36.0f, 210.0f, BuyCatalog::ImagePanel);
	UImage* Image = WidgetTree->ConstructWidget<UImage>();
	Image->SetVisibility(ESlateVisibility::Collapsed);
	BuyCatalog::Place(Canvas, Image, X + 32.0f, 361.0f,
		BuyCatalog::ColumnWidth - 64.0f, 171.0f);
	CardImages[Index] = Image;
	UTacticalShopItemIconUI* Fallback = CreateWidget<UTacticalShopItemIconUI>(
		GetOwningPlayer(), UTacticalShopItemIconUI::StaticClass());
	if (Fallback)
	{
		Fallback->SetIconStyle(IconStyles[Index]);
		Fallback->SetAccentColor(BuyCatalog::Teal);
		BuyCatalog::Place(Canvas, Fallback, X + 32.0f, 361.0f,
			BuyCatalog::ColumnWidth - 64.0f, 171.0f);
	}
	FallbackIcons[Index] = Fallback;
	BuyCatalog::MakeText(WidgetTree, Canvas, TEXT("作战参数  /  PERFORMANCE"),
		X + 22.0f, 573.0f, 323.0f, 25.0f, 14, BuyCatalog::Teal);
	DetailLabels[Index] = BuyCatalog::MakeText(WidgetTree, Canvas, TEXT("数据载入中..."),
		X + 22.0f, 609.0f, 323.0f, 112.0f, 18, BuyCatalog::White);
	ActionLabels[Index] = BuyCatalog::AdoptButton(WidgetTree, Canvas, PurchaseButton,
		X + 22.0f, 746.0f, BuyCatalog::ColumnWidth - 44.0f, 51.0f,
		TEXT("购买装备"), FLinearColor(0.047f, 0.38f, 0.45f), 20);
}

void UBuyMenuUI::BuildArmorCard(UCanvasPanel* Canvas, int32 Index, float X,
	float Y, UButton* PurchaseButton)
{
	const bool bHeavy = Index == 4;
	const FLinearColor Accent = bHeavy ? BuyCatalog::Gold : BuyCatalog::Teal;
	BuyCatalog::MakePanel(WidgetTree, Canvas, X, Y,
		BuyCatalog::ColumnWidth, 333.0f, BuyCatalog::Panel);
	BuyCatalog::MakePanel(WidgetTree, Canvas, X, Y,
		BuyCatalog::ColumnWidth, 5.0f, Accent);
	BuyCatalog::MakeText(WidgetTree, Canvas,
		bHeavy ? TEXT("05   /   HEAVY DEFENSE") : TEXT("04   /   LIGHT DEFENSE"),
		X + 22.0f, Y + 17.0f, 323.0f, 25.0f, 13, Accent);
	BuyCatalog::MakeText(WidgetTree, Canvas, bHeavy ? TEXT("重甲") : TEXT("轻甲"),
		X + 22.0f, Y + 45.0f, 155.0f, 42.0f, 27, BuyCatalog::White);
	SubtitleLabels[Index] = BuyCatalog::MakeText(WidgetTree, Canvas, TEXT(""),
		X + 22.0f, Y + 88.0f, 194.0f, 24.0f, 13, BuyCatalog::Muted);
	PriceLabels[Index] = BuyCatalog::MakeText(WidgetTree, Canvas, TEXT("$ --"),
		X + 224.0f, Y + 52.0f, 130.0f, 37.0f, 22, BuyCatalog::Gold);
	BuyCatalog::MakePanel(WidgetTree, Canvas, X + 18.0f, Y + 120.0f,
		104.0f, 119.0f, BuyCatalog::ImagePanel);
	UImage* Image = WidgetTree->ConstructWidget<UImage>();
	Image->SetVisibility(ESlateVisibility::Collapsed);
	BuyCatalog::Place(Canvas, Image, X + 26.0f, Y + 128.0f, 88.0f, 103.0f);
	CardImages[Index] = Image;
	UTacticalShopItemIconUI* Fallback = CreateWidget<UTacticalShopItemIconUI>(
		GetOwningPlayer(), UTacticalShopItemIconUI::StaticClass());
	if (Fallback)
	{
		Fallback->SetIconStyle(bHeavy
			? ETacticalShopItemIconStyle::HeavyArmor
			: ETacticalShopItemIconStyle::LightArmor);
		Fallback->SetAccentColor(Accent);
		BuyCatalog::Place(Canvas, Fallback, X + 26.0f, Y + 128.0f, 88.0f, 103.0f);
	}
	FallbackIcons[Index] = Fallback;
	DetailLabels[Index] = BuyCatalog::MakeText(WidgetTree, Canvas, TEXT("数据载入中..."),
		X + 138.0f, Y + 132.0f, 214.0f, 107.0f, 17, BuyCatalog::White);
	ActionLabels[Index] = BuyCatalog::AdoptButton(WidgetTree, Canvas, PurchaseButton,
		X + 22.0f, Y + 260.0f, BuyCatalog::ColumnWidth - 44.0f, 50.0f,
		TEXT("购买护甲"), bHeavy ? FLinearColor(0.39f, 0.27f, 0.08f)
			: FLinearColor(0.047f, 0.38f, 0.45f), 19);
}

void UBuyMenuUI::SetCardIcon(int32 Index, UTexture2D* Texture)
{
	if (!CardImages.IsValidIndex(Index) || !FallbackIcons.IsValidIndex(Index))
	{
		return;
	}
	if (UImage* Image = CardImages[Index])
	{
		if (Texture)
		{
			Image->SetBrushFromTexture(Texture, true);
		}
		Image->SetVisibility(Texture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (UTacticalShopItemIconUI* Fallback = FallbackIcons[Index])
	{
		Fallback->SetVisibility(Texture ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

void UBuyMenuUI::SetCatalogVisible(bool bVisible)
{
	if (bNativeCatalogReady)
	{
		const bool bWasVisible = IsVisible();
		SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (bVisible)
		{
			if (!bWasVisible && ResultLabel)
			{
				ResultLabel->SetColorAndOpacity(FSlateColor(BuyCatalog::Muted));
				ResultLabel->SetText(FText::FromString(
					TEXT("选择装备后点击购买  ·  按 Tab 查看战绩")));
			}
			RefreshCatalog();
		}
	}
}

void UBuyMenuUI::UpdateCatalogMoney(int32 Money)
{
	if (!bNativeCatalogReady || !MoneyLabel || CachedMoney == Money)
	{
		return;
	}
	CachedMoney = Money;
	MoneyLabel->SetText(FText::FromString(FString::Printf(TEXT("资金  $%d"), Money)));
}

void UBuyMenuUI::UpdateCatalogTimer(float TimeRemaining)
{
	if (!bNativeCatalogReady || !TimerLabel)
	{
		return;
	}
	const int32 Seconds = FMath::CeilToInt(FMath::Max(0.0f, TimeRemaining));
	if (Seconds == CachedTimerSecond)
	{
		return;
	}
	CachedTimerSecond = Seconds;
	TimerLabel->SetText(FText::FromString(
		FString::Printf(TEXT("购买剩余  %02d 秒"), Seconds)));
}

void UBuyMenuUI::ShowCatalogPurchaseResult(bool bSuccess, const FText& Message)
{
	if (!bNativeCatalogReady || !ResultLabel)
	{
		return;
	}
	ResultLabel->SetColorAndOpacity(FSlateColor(bSuccess ? BuyCatalog::Teal
		: FLinearColor(1.0f, 0.38f, 0.36f, 1.0f)));
	ResultLabel->SetText(FText::FromString(FString::Printf(TEXT("%s  ·  %s"),
		bSuccess ? TEXT("购买成功") : TEXT("购买失败"), *Message.ToString())));
}

void UBuyMenuUI::RefreshCatalog()
{
	if (!bNativeCatalogReady)
	{
		return;
	}
	ATacticalPlayerController* Controller = Cast<ATacticalPlayerController>(GetOwningPlayer());
	if (!Controller)
	{
		return;
	}
	const ATacticalPlayerState* State = Controller->GetPlayerState<ATacticalPlayerState>();
	const int32 Money = State ? State->Money : 0;
	UpdateCatalogMoney(Money);

	const EWeaponTier WeaponTiers[] = {
		EWeaponTier::UpgradedPistol, EWeaponTier::Rifle, EWeaponTier::Sniper
	};
	UButton* WeaponButtons[] = { PistolButton, RifleButton, SniperButton };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FWeaponTierRow Data;
		const EWeaponTier Tier = WeaponTiers[Index];
		const bool bHasData = Controller->GetWeaponDataForTier(Tier, Data);
		const int32 Cost = bHasData && Data.Cost > 0
			? Data.Cost : ATacticalPlayerState::GetWeaponCost(Tier);
		const bool bConfigured = Controller->GetWeaponClassForTier(Tier) != nullptr;
		const bool bEquipped = State && (Tier == EWeaponTier::UpgradedPistol
			? State->SavedPistolTier == Tier : State->SavedPrimaryTier == Tier);
		const bool bAffordable = State && Cost > 0 && Money >= Cost;
		PriceLabels[Index]->SetText(FText::FromString(
			FString::Printf(TEXT("$ %d"), Cost)));
		SubtitleLabels[Index]->SetText(bHasData && !Data.DisplayName.IsEmpty()
			? Data.DisplayName : FText::GetEmpty());
		if (bHasData)
		{
			const float HeadDamage = Data.BodyDamage * Data.HeadshotMultiplier;
			const float RPM = Data.RefireRate > KINDA_SMALL_NUMBER ? 60.0f / Data.RefireRate : 0.0f;
			DetailLabels[Index]->SetText(FText::FromString(FString::Printf(
				TEXT("身体 %.0f   /   爆头 %.0f\n弹匣 %d   /   备弹 %d\n射速 %.0f 发/分"),
				Data.BodyDamage, HeadDamage, Data.MagazineSize, Data.ReserveAmmo, RPM)));
		}
		else
		{
			DetailLabels[Index]->SetText(FText::FromString(TEXT("武器数据未配置")));
		}
		UTexture2D* Icon = bHasData ? Data.Icon.Get() : nullptr;
		if (!Icon && Tier == EWeaponTier::Rifle)
		{
			Icon = DefaultRifleIcon;
		}
		SetCardIcon(Index, Icon);
		WeaponButtons[Index]->SetIsEnabled(bConfigured && !bEquipped && bAffordable);
		const TCHAR* Action = !bConfigured ? TEXT("暂不可购买")
			: bEquipped ? TEXT("当前装备已购买")
			: !bAffordable ? TEXT("余额不足") : TEXT("购买武器");
		ActionLabels[Index]->SetText(FText::FromString(Action));
	}

	const EArmorType ArmorTypes[] = { EArmorType::Light, EArmorType::Heavy };
	UButton* ArmorButtons[] = { LightArmorButton, HeavyArmorButton };
	for (int32 ArmorIndex = 0; ArmorIndex < 2; ++ArmorIndex)
	{
		const int32 Index = ArmorIndex + 3;
		const EArmorType Type = ArmorTypes[ArmorIndex];
		FArmorDataRow Data;
		const bool bHasData = Controller->GetArmorDataForType(Type, Data);
		const int32 Cost = bHasData && Data.Cost > 0
			? Data.Cost : ATacticalPlayerState::GetArmorCost(Type);
		const float DefaultDurability = Type == EArmorType::Light
			? TacticalEconomy::LightArmorMaxDurability : TacticalEconomy::HeavyArmorMaxDurability;
		const float Durability = bHasData && Data.MaxDurability > 0.0f
			? Data.MaxDurability : DefaultDurability;
		const float DefaultBody = Type == EArmorType::Light
			? TacticalEconomy::LightArmorBodyReduction : TacticalEconomy::HeavyArmorBodyReduction;
		const float DefaultHead = Type == EArmorType::Light
			? TacticalEconomy::LightArmorHeadReduction : TacticalEconomy::HeavyArmorHeadReduction;
		// ArmorComponent intentionally ignores the current all-zero table reductions.
		const bool bUseRowReductions = bHasData
			&& (Data.BodyDamageReduction > 0.0f || Data.HeadDamageReduction > 0.0f);
		const float BodyReduction = bUseRowReductions
			? FMath::Clamp(Data.BodyDamageReduction, 0.0f, 1.0f) : DefaultBody;
		const float HeadReduction = bUseRowReductions
			? FMath::Clamp(Data.HeadDamageReduction, 0.0f, 1.0f) : DefaultHead;
		PriceLabels[Index]->SetText(FText::FromString(FString::Printf(TEXT("$ %d"), Cost)));
		SubtitleLabels[Index]->SetText(bHasData && !Data.DisplayName.IsEmpty()
			? Data.DisplayName : FText::GetEmpty());
		DetailLabels[Index]->SetText(FText::FromString(FString::Printf(
			TEXT("耐久  %.0f\n身体减伤  %.0f%%\n头部减伤  %.0f%%"),
			Durability, BodyReduction * 100.0f, HeadReduction * 100.0f)));
		SetCardIcon(Index, bHasData ? Data.Icon.Get() : nullptr);
		// Armor can be repurchased to restore durability; owning armor is not a disable reason.
		const bool bAffordable = State && Money >= Cost && Cost > 0;
		ArmorButtons[ArmorIndex]->SetIsEnabled(bAffordable);
		ActionLabels[Index]->SetText(FText::FromString(
			bAffordable ? TEXT("购买护甲 / 补足耐久") : TEXT("余额不足")));
	}
}

void UBuyMenuUI::BuyPistol()
{
	if (ATacticalPlayerController* Controller = Cast<ATacticalPlayerController>(GetOwningPlayer()))
	{
		Controller->ServerRequestPurchaseWeapon(EWeaponTier::UpgradedPistol);
	}
}

void UBuyMenuUI::BuyRifle()
{
	if (ATacticalPlayerController* Controller = Cast<ATacticalPlayerController>(GetOwningPlayer()))
	{
		Controller->ServerRequestPurchaseWeapon(EWeaponTier::Rifle);
	}
}

void UBuyMenuUI::BuySniper()
{
	if (ATacticalPlayerController* Controller = Cast<ATacticalPlayerController>(GetOwningPlayer()))
	{
		Controller->ServerRequestPurchaseWeapon(EWeaponTier::Sniper);
	}
}

void UBuyMenuUI::BuyLightArmor()
{
	if (ATacticalPlayerController* Controller = Cast<ATacticalPlayerController>(GetOwningPlayer()))
	{
		Controller->ServerRequestPurchaseArmor(EArmorType::Light);
	}
}

void UBuyMenuUI::BuyHeavyArmor()
{
	if (ATacticalPlayerController* Controller = Cast<ATacticalPlayerController>(GetOwningPlayer()))
	{
		Controller->ServerRequestPurchaseArmor(EArmorType::Heavy);
	}
}

void UBuyMenuUI::CloseCatalog()
{
	if (ATacticalPlayerController* Controller = Cast<ATacticalPlayerController>(GetOwningPlayer()))
	{
		Controller->CloseBuyMenu();
	}
}

FReply UBuyMenuUI::NativeOnPreviewKeyDown(
	const FGeometry& InGeometry,
	const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Tab)
	{
		if (ATacticalPlayerController* TacticalController =
			Cast<ATacticalPlayerController>(GetOwningPlayer()))
		{
			TacticalController->SetRosterScoreboardVisible(true);
			return FReply::Handled();
		}
	}

	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

FReply UBuyMenuUI::NativeOnKeyUp(
	const FGeometry& InGeometry,
	const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Tab)
	{
		if (ATacticalPlayerController* TacticalController =
			Cast<ATacticalPlayerController>(GetOwningPlayer()))
		{
			TacticalController->SetRosterScoreboardVisible(false);
			return FReply::Handled();
		}
	}

	return Super::NativeOnKeyUp(InGeometry, InKeyEvent);
}
