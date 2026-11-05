// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/TacticalUI.h"
#include "TacticalRosterStatusUI.generated.h"

class UBorder;
class UHorizontalBox;
class USizeBox;
class UTextBlock;

/**
 * Native, Blueprint-independent Tab scoreboard for the fixed 3v3 roster.
 * It displays only privacy-safe replicated state.
 */
UCLASS()
class TCAMP_FPS_API UTacticalRosterStatusUI final : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetRosterDisplay(
		const TArray<FTeamAvatarStatus>& RedPlayers,
		const TArray<FTeamAvatarStatus>& BluePlayers,
		int32 RedScore,
		int32 BlueScore,
		int32 RoundNumber,
		int32 MaxRounds);

	void ShowScoreboard();
	void HideScoreboard();
	bool IsScoreboardVisible() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

private:
	void BuildNativeWidgetTree();
	void BuildTeamColumn(
		UHorizontalBox* Parent,
		uint8 TeamByte,
		TObjectPtr<UTextBlock>& OutHeaderText,
		TArray<TObjectPtr<USizeBox>>& OutRowContainers,
		TArray<TObjectPtr<UBorder>>& OutRowBorders,
		TArray<TObjectPtr<UTextBlock>>& OutNameTexts,
		TArray<TObjectPtr<UTextBlock>>& OutStateTexts);
	void ApplyTeamRows(
		const TArray<FTeamAvatarStatus>& Players,
		const TArray<TObjectPtr<USizeBox>>& RowContainers,
		const TArray<TObjectPtr<UBorder>>& RowBorders,
		const TArray<TObjectPtr<UTextBlock>>& NameTexts,
		const TArray<TObjectPtr<UTextBlock>>& StateTexts);

	UPROPERTY()
	TObjectPtr<UTextBlock> MatchSummaryText;

	UPROPERTY()
	TObjectPtr<UTextBlock> RedHeaderText;

	UPROPERTY()
	TObjectPtr<UTextBlock> BlueHeaderText;

	UPROPERTY()
	TArray<TObjectPtr<USizeBox>> RedRowContainers;

	UPROPERTY()
	TArray<TObjectPtr<UBorder>> RedRowBorders;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> RedNameTexts;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> RedStateTexts;

	UPROPERTY()
	TArray<TObjectPtr<USizeBox>> BlueRowContainers;

	UPROPERTY()
	TArray<TObjectPtr<UBorder>> BlueRowBorders;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> BlueNameTexts;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> BlueStateTexts;
};
