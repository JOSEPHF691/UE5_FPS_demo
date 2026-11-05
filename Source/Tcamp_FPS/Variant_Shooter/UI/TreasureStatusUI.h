// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Economy/EconomyTypes.h"
#include "TreasureStatusUI.generated.h"

class UBorder;
class UCanvasPanel;
class UProgressBar;
class UTextBlock;

/**
 * Blueprint-independent presentation for the single server-authored treasure
 * objective. The widget consumes only a replicated snapshot; it never searches
 * the world for a chest or guard and never receives player input.
 */
UCLASS()
class TCAMP_FPS_API UTreasureStatusUI : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Applies one atomic treasure snapshot received from the GameState. */
	void SetTreasureState(const FTreasureRoundSnapshot& InSnapshot);

	/** Removes all treasure presentation and transient progress. */
	void ClearTreasure();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildNativeWidgetTree();
	void UpdateStaticPresentation();
	void UpdateTemporalPresentation(float ServerTimeSeconds, bool bForceTextRefresh);
	void UpdateMarkerPresentation(const FGeometry& MyGeometry);
	void UpdateRootVisibility();
	float GetSynchronizedServerTime() const;
	bool ShouldShowObjective() const;
	bool ShouldShowWorldMarker() const;
	FVector2D GetClampedMarkerPosition(
		const FGeometry& MyGeometry,
		const FVector& WorldLocation,
		bool& bOutProjectedOnScreen) const;

	UPROPERTY()
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY()
	TObjectPtr<UBorder> StatusPanel;

	UPROPERTY()
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY()
	TObjectPtr<UProgressBar> ClaimProgressBar;

	UPROPERTY()
	TObjectPtr<UBorder> MarkerPanel;

	UPROPERTY()
	TObjectPtr<UTextBlock> MarkerText;

	FTreasureRoundSnapshot Snapshot;
	int32 CachedActivationSeconds = INDEX_NONE;
	int32 CachedClaimPercent = INDEX_NONE;
	int32 CachedDistanceMeters = INDEX_NONE;
	bool bPresentationActive = false;
};
