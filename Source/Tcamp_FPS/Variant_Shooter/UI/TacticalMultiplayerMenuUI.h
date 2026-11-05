// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Online/TacticalSessionSubsystem.h"
#include "TacticalMultiplayerMenuUI.generated.h"

class UButton;
class UBorder;
class UHorizontalBox;
class UPanelWidget;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UVerticalBox;
class UTacticalGameInstance;
class UTacticalSessionSubsystem;

/** Native frontend server browser and compact in-match session menu. */
UCLASS()
class TCAMP_FPS_API UTacticalMultiplayerMenuUI final : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Must be called before (or immediately after) AddToViewport. */
	void InitializeMenu(bool bInMatch);

	/** Unbinds async presentation callbacks and removes the widget. */
	void CloseMenu();

	bool IsInMatchMenu() const { return bInMatchMode; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(
		const FGeometry& InGeometry,
		const FKeyEvent& InKeyEvent) override;

private:
	void BuildNativeWidgetTree();
	void BindSessionSubsystem();
	void UnbindSessionSubsystem();
	void RefreshModePresentation();
	void RefreshButtons(bool bBusy);
	void RebuildResultRows(const TArray<FTacticalSessionSearchEntry>& Entries);
	UTacticalSessionSubsystem* GetSessionSubsystem() const;
	UTacticalGameInstance* GetTacticalGameInstance() const;

	void HandleSessionStatusChanged(
		ETacticalSessionOperation NewOperation,
		const FText& NewStatus,
		bool bBusy);
	void HandleSessionResultsChanged(const TArray<FTacticalSessionSearchEntry>& Entries);
	void HandleResultSelected(int32 ResultHandle);

	UFUNCTION()
	void HandleHostClicked();

	UFUNCTION()
	void HandleRefreshClicked();

	UFUNCTION()
	void HandleResumeClicked();

	UFUNCTION()
	void HandleInviteClicked();

	UFUNCTION()
	void HandleLeaveClicked();

	bool bInMatchMode = false;
	bool bSessionBusy = false;
	TWeakObjectPtr<UTacticalSessionSubsystem> BoundSessionSubsystem;
	FDelegateHandle StatusChangedHandle;
	FDelegateHandle ResultsChangedHandle;

	UPROPERTY()
	TObjectPtr<USizeBox> PanelSize;

	UPROPERTY()
	TObjectPtr<UBorder> Backdrop;

	UPROPERTY()
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY()
	TObjectPtr<UTextBlock> IdentityText;

	UPROPERTY()
	TObjectPtr<UHorizontalBox> FrontendActionRow;

	UPROPERTY()
	TObjectPtr<UVerticalBox> FrontendResultsSection;

	UPROPERTY()
	TObjectPtr<UScrollBox> ResultsScrollBox;

	UPROPERTY()
	TObjectPtr<UVerticalBox> MatchActionColumn;

	UPROPERTY()
	TObjectPtr<UButton> HostButton;

	UPROPERTY()
	TObjectPtr<UButton> RefreshButton;

	UPROPERTY()
	TObjectPtr<UButton> ResumeButton;

	UPROPERTY()
	TObjectPtr<UButton> InviteButton;

	UPROPERTY()
	TObjectPtr<UButton> LeaveButton;

	UPROPERTY()
	TObjectPtr<UTextBlock> LeaveButtonText;

	UPROPERTY()
	TObjectPtr<UTextBlock> StatusText;
};
