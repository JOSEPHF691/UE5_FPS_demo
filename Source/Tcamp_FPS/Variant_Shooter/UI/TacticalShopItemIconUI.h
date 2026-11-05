// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TacticalShopItemIconUI.generated.h"

/** Lightweight fallbacks for shop items that do not have an authored thumbnail. */
UENUM(BlueprintType)
enum class ETacticalShopItemIconStyle : uint8
{
	UpgradedPistol UMETA(DisplayName = "Upgraded Pistol"),
	Rifle UMETA(DisplayName = "Rifle"),
	Sniper UMETA(DisplayName = "Sniper"),
	LightArmor UMETA(DisplayName = "Light Armor"),
	HeavyArmor UMETA(DisplayName = "Heavy Armor")
};

/** Scalable, transparent tactical line art. Has no textures, animations, or Tick. */
UCLASS()
class TCAMP_FPS_API UTacticalShopItemIconUI : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Shop|Icon")
	void SetIconStyle(ETacticalShopItemIconStyle InIconStyle);

	UFUNCTION(BlueprintCallable, Category = "Shop|Icon")
	void SetAccentColor(const FLinearColor& InAccentColor);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop|Icon")
	ETacticalShopItemIconStyle IconStyle = ETacticalShopItemIconStyle::UpgradedPistol;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop|Icon")
	FLinearColor AccentColor = FLinearColor(0.28f, 0.75f, 1.0f, 1.0f);

protected:
	virtual void NativeOnInitialized() override;
	virtual int32 NativePaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;
};
