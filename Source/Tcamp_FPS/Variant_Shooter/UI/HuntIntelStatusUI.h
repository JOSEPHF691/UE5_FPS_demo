// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HuntIntelStatusUI.generated.h"

class UBorder;
class UCanvasPanel;
class UTextBlock;

/**
 * Native, Blueprint-independent presentation for late-round hunt intelligence.
 * Server-authored approximate world locations are projected into this owning
 * player's viewport and intentionally expire after a short pulse.
 */
UCLASS()
class TCAMP_FPS_API UHuntIntelStatusUI : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Shows or hides the persistent late-round warning. */
	void SetHuntIntelState(bool bActive, bool bEscalated);

	/** Presents one short-lived pulse of already-obfuscated enemy positions. */
	void ShowPulse(const TArray<FVector>& ApproximateEnemyLocations, bool bEscalated);

	/** Clears only position markers while retaining the current warning state. */
	void ClearPulseMarkers();

	/** Clears both the warning and every position marker. */
	void ClearHuntIntel();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildNativeWidgetTree();
	void UpdateWarningPresentation();
	void UpdateRootVisibility();
	FVector2D GetClampedMarkerPosition(
		const FGeometry& MyGeometry,
		const FVector& WorldLocation,
		bool& bOutProjectedOnScreen) const;

	static constexpr int32 MaxMarkerCount = 3;

	UPROPERTY()
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY()
	TObjectPtr<UBorder> WarningPanel;

	UPROPERTY()
	TObjectPtr<UTextBlock> WarningText;

	UPROPERTY()
	TArray<TObjectPtr<UBorder>> MarkerPanels;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> MarkerTexts;

	TArray<FVector> MarkerWorldLocations;
	float PulseTimeRemaining = 0.0f;
	float PulseDuration = 0.0f;
	float PulseElapsed = 0.0f;
	float StateAnnouncementTimeRemaining = 0.0f;
	bool bHuntIntelActive = false;
	bool bHuntIntelEscalated = false;
};
