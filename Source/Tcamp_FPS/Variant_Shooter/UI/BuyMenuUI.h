// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BuyMenuUI.generated.h"

class UButton;
class UCanvasPanel;
class UImage;
class UTextBlock;
class UTacticalShopItemIconUI;
class UTexture2D;

/** Runtime four-column catalog that reuses the existing Blueprint's purchase buttons. */
UCLASS(abstract)
class TCAMP_FPS_API UBuyMenuUI : public UUserWidget
{
	GENERATED_BODY()

public:
	UBuyMenuUI(const FObjectInitializer& ObjectInitializer);

	/** True when the native four-column catalog successfully adopted the BP buttons. */
	UFUNCTION(BlueprintPure, Category = "BuyMenu")
	bool HasNativeCatalog() const { return bNativeCatalogReady; }

	UFUNCTION(BlueprintCallable, Category = "BuyMenu")
	void SetCatalogVisible(bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "BuyMenu")
	void UpdateCatalogMoney(int32 Money);

	UFUNCTION(BlueprintCallable, Category = "BuyMenu")
	void UpdateCatalogTimer(float TimeRemaining);

	UFUNCTION(BlueprintCallable, Category = "BuyMenu")
	void ShowCatalogPurchaseResult(bool bSuccess, const FText& Message);

	/** Updates prices, effective combat stats and button availability without rebuilding widgets. */
	UFUNCTION(BlueprintCallable, Category = "BuyMenu")
	void RefreshCatalog();

	/** Show the buy menu */
	UFUNCTION(BlueprintImplementableEvent, Category = "BuyMenu", meta = (DisplayName = "Show"))
	void BP_Show();

	/** Hide the buy menu */
	UFUNCTION(BlueprintImplementableEvent, Category = "BuyMenu", meta = (DisplayName = "Hide"))
	void BP_Hide();

	/** Update displayed money */
	UFUNCTION(BlueprintImplementableEvent, Category = "BuyMenu", meta = (DisplayName = "Update Money"))
	void BP_UpdateMoney(int32 Money);

	/** Update the buy phase countdown timer */
	UFUNCTION(BlueprintImplementableEvent, Category = "BuyMenu", meta = (DisplayName = "Update Timer"))
	void BP_UpdateTimer(float TimeRemaining);

	/** Show purchase result feedback */
	UFUNCTION(BlueprintImplementableEvent, Category = "BuyMenu", meta = (DisplayName = "Purchase Result"))
	void BP_PurchaseResult(bool bSuccess, const FText& Message);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

	/** Keep Tab available for the roster when a purchase button owns UI focus. */
	virtual FReply NativeOnPreviewKeyDown(
		const FGeometry& InGeometry,
		const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyUp(
		const FGeometry& InGeometry,
		const FKeyEvent& InKeyEvent) override;

private:
	UFUNCTION() void BuyPistol();
	UFUNCTION() void BuyRifle();
	UFUNCTION() void BuySniper();
	UFUNCTION() void BuyLightArmor();
	UFUNCTION() void BuyHeavyArmor();
	UFUNCTION() void CloseCatalog();

	bool BuildNativeCatalog();
	void BuildWeaponCard(UCanvasPanel* Canvas, int32 Index, float X, UButton* PurchaseButton);
	void BuildArmorCard(UCanvasPanel* Canvas, int32 Index, float X, float Y, UButton* PurchaseButton);
	void SetCardIcon(int32 Index, UTexture2D* Texture);

	UPROPERTY(Transient) TObjectPtr<UButton> PistolButton;
	UPROPERTY(Transient) TObjectPtr<UButton> RifleButton;
	UPROPERTY(Transient) TObjectPtr<UButton> SniperButton;
	UPROPERTY(Transient) TObjectPtr<UButton> LightArmorButton;
	UPROPERTY(Transient) TObjectPtr<UButton> HeavyArmorButton;
	UPROPERTY(Transient) TObjectPtr<UButton> CloseButton;
	/** Hard reference so the bundled rifle HUD icon is included in packaged builds. */
	UPROPERTY() TObjectPtr<UTexture2D> DefaultRifleIcon;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MoneyLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TimerLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ResultLabel;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> PriceLabels;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> DetailLabels;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SubtitleLabels;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ActionLabels;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> CardImages;
	UPROPERTY(Transient) TArray<TObjectPtr<UTacticalShopItemIconUI>> FallbackIcons;

	bool bNativeCatalogReady = false;
	int32 CachedMoney = INDEX_NONE;
	int32 CachedTimerSecond = INDEX_NONE;
};
