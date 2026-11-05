// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Online/TacticalSessionSubsystem.h"
#include "TacticalSessionResultRowUI.generated.h"

class UButton;
class UTextBlock;

DECLARE_DELEGATE_OneParam(FTacticalSessionResultSelected, int32 /* ResultHandle */);

/** One native-only, generation-scoped server-browser result. */
UCLASS()
class TCAMP_FPS_API UTacticalSessionResultRowUI final : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeResult(
		const FTacticalSessionSearchEntry& InEntry,
		FTacticalSessionResultSelected InSelectedCallback,
		bool bInteractionEnabled);

	void SetInteractionEnabled(bool bEnabled);

protected:
	virtual void NativeOnInitialized() override;

private:
	void BuildNativeWidgetTree();
	void RefreshPresentation();

	UFUNCTION()
	void HandleRowClicked();

	FTacticalSessionSearchEntry Entry;
	FTacticalSessionResultSelected SelectedCallback;
	bool bAllowInteraction = true;

	UPROPERTY()
	TObjectPtr<UButton> RowButton;

	UPROPERTY()
	TObjectPtr<UTextBlock> ServerText;

	UPROPERTY()
	TObjectPtr<UTextBlock> OwnerText;

	UPROPERTY()
	TObjectPtr<UTextBlock> PlayerCountText;

	UPROPERTY()
	TObjectPtr<UTextBlock> PingText;

	UPROPERTY()
	TObjectPtr<UTextBlock> StateText;
};

