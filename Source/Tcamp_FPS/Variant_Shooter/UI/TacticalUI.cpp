// Copyright Epic Games, Inc. All Rights Reserved.

#include "TacticalUI.h"
#include "TacticalGameState.h"
#include "TacticalPlayerState.h"
#include "ShooterCharacter.h"
#include "GameFramework/PlayerState.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "TacticalDiagnostics.h"

void UTacticalUI::SetAuthoredCrosshairSuppressed(bool bSuppressed)
{
	bAuthoredCrosshairSuppressed = bSuppressed;
	ApplyAuthoredCrosshairSuppression();
}

void UTacticalUI::ApplyAuthoredCrosshairSuppression()
{
	UWidget* Crosshair = GetWidgetFromName(TEXT("Crosshair"));
	if (!Crosshair)
	{
		return;
	}

	if (bAuthoredCrosshairSuppressed)
	{
		if (!bHasCachedAuthoredCrosshairVisibility)
		{
			CachedAuthoredCrosshairVisibility = Crosshair->GetVisibility();
			bHasCachedAuthoredCrosshairVisibility = true;
		}
		Crosshair->SetVisibility(ESlateVisibility::Collapsed);
	}
	else if (bHasCachedAuthoredCrosshairVisibility)
	{
		Crosshair->SetVisibility(CachedAuthoredCrosshairVisibility);
		bHasCachedAuthoredCrosshairVisibility = false;
	}
}

void UTacticalUI::SetPreRoundCrosshairOnly(bool bEnabled)
{
	if (!bEnabled)
	{
		if (!bPreRoundCrosshairOnly)
		{
			return;
		}

		for (const TPair<TWeakObjectPtr<UWidget>, ESlateVisibility>& Entry : PreRoundHiddenBranches)
		{
			if (UWidget* Branch = Entry.Key.Get())
			{
				Branch->SetVisibility(Entry.Value);
			}
		}
		PreRoundHiddenBranches.Reset();
		bPreRoundCrosshairOnly = false;
		ApplyAuthoredCrosshairSuppression();
		return;
	}

	if (bPreRoundCrosshairOnly)
	{
		// Blueprint update events may run while the lobby is open. Reassert only
		// the branch roots we own without disturbing the preserved crosshair path.
		for (const TPair<TWeakObjectPtr<UWidget>, ESlateVisibility>& Entry : PreRoundHiddenBranches)
		{
			if (UWidget* Branch = Entry.Key.Get())
			{
				Branch->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
		ApplyAuthoredCrosshairSuppression();
		return;
	}

	UWidget* Crosshair = GetWidgetFromName(TEXT("Crosshair"));
	if (!Crosshair)
	{
		// Prefer leaving the authored HUD visible over making the ready targets
		// unusable if a future replacement widget renames the required reticle.
		UE_LOG(LogTacticalFlow, Warning,
			TEXT("Event=PreRoundCrosshairIsolationFailed Widget=\"%s\" Reason=CrosshairNotFound"),
			*GetName());
		return;
	}

	PreRoundHiddenBranches.Reset();
	UWidget* PreservedChild = Crosshair;
	UPanelWidget* Parent = Crosshair->GetParent();
	while (Parent)
	{
		for (int32 ChildIndex = 0; ChildIndex < Parent->GetChildrenCount(); ++ChildIndex)
		{
			UWidget* SiblingBranch = Parent->GetChildAt(ChildIndex);
			if (!SiblingBranch || SiblingBranch == PreservedChild)
			{
				continue;
			}

			PreRoundHiddenBranches.Add(SiblingBranch, SiblingBranch->GetVisibility());
			SiblingBranch->SetVisibility(ESlateVisibility::Collapsed);
		}

		PreservedChild = Parent;
		Parent = Parent->GetParent();
	}

	bPreRoundCrosshairOnly = true;
	ApplyAuthoredCrosshairSuppression();
	UE_LOG(LogTacticalFlow, Verbose,
		TEXT("Event=PreRoundCrosshairIsolated Widget=\"%s\" HiddenBranches=%d"),
		*GetName(), PreRoundHiddenBranches.Num());
}

FText UTacticalUI::GetRoundEndReasonText(uint8 WinningTeam, ERoundEndReason Reason)
{
	const FText TeamText = WinningTeam <= 1
		? FText::Format(NSLOCTEXT("TacticalUI", "RoundWinningTeam", "Team {0} wins"), FText::AsNumber(WinningTeam + 1))
		: NSLOCTEXT("TacticalUI", "RoundDraw", "Round draw");

	switch (Reason)
	{
	case ERoundEndReason::TeamEliminated:
		return FText::Format(NSLOCTEXT("TacticalUI", "RoundEndElimination", "{0}: enemy team eliminated"), TeamText);
	case ERoundEndReason::TimeExpiredDraw:
		return NSLOCTEXT("TacticalUI", "RoundEndTimeExpiredDraw", "Time expired: round draw");
	case ERoundEndReason::SimultaneousEliminationDraw:
		return NSLOCTEXT("TacticalUI", "RoundEndSimultaneousElimination", "Both teams eliminated: round draw");
	default:
		return TeamText;
	}
}

FText UTacticalUI::GetMatchEndReasonText(uint8 WinningTeam, EMatchEndReason Reason)
{
	const FText WinningTeamText = WinningTeam == 0
		? NSLOCTEXT("TacticalUI", "RedTeam", "红队")
		: WinningTeam == 1
			? NSLOCTEXT("TacticalUI", "BlueTeam", "蓝队")
			: NSLOCTEXT("TacticalUI", "NoWinningTeam", "无胜方");

	switch (Reason)
	{
	case EMatchEndReason::ScoreVictory:
		return FText::Format(
			NSLOCTEXT("TacticalUI", "MatchScoreVictory", "{0}赢得比赛"),
			WinningTeamText);
	case EMatchEndReason::ScoreDraw:
		return NSLOCTEXT("TacticalUI", "MatchScoreDraw", "比赛平局");
	case EMatchEndReason::TeamDisconnectForfeit:
		return FText::Format(
			NSLOCTEXT("TacticalUI", "MatchDisconnectForfeit", "{0}因对方整队断线弃权获胜"),
			WinningTeamText);
	case EMatchEndReason::AllTeamsDisconnectedAbort:
		return NSLOCTEXT("TacticalUI", "MatchAllDisconnectedAbort", "双方均已断线，比赛中止（无结果）");
	case EMatchEndReason::HostLost:
		return NSLOCTEXT("TacticalUI", "MatchHostLost", "主机连接丢失，比赛中止（无结果）");
	default:
		return NSLOCTEXT("TacticalUI", "MatchResultUnknown", "比赛已结束");
	}
}

FText UTacticalUI::GetPhaseDisplayText(ERoundPhase Phase)
{
	switch (Phase)
	{
	case ERoundPhase::Buy:
		return FText::FromString(TEXT("\u8d2d\u4e70\u9636\u6bb5"));
	case ERoundPhase::Combat:
		return FText::FromString(TEXT("\u6218\u6597\u9636\u6bb5"));
	case ERoundPhase::RoundEnd:
	case ERoundPhase::MatchEnd:
		return FText::FromString(TEXT("\u5373\u5c06\u8fdb\u5165\u4e0b\u4e00\u9636\u6bb5"));
	case ERoundPhase::PreRound:
	default:
		return FText::FromString(TEXT("\u51c6\u5907\u9636\u6bb5"));
	}
}
FText UTacticalUI::GetCountdownDisplayText(float TimeRemaining)
{
	const int32 TotalSeconds = FMath::CeilToInt(FMath::Max(0.0f, TimeRemaining));
	const int32 Minutes = TotalSeconds / 60;
	const int32 Seconds = TotalSeconds % 60;

	return FText::FromString(FString::Printf(TEXT("%d:%02d"), Minutes, Seconds));
}

FText UTacticalUI::GetScoreDisplayText(int32 TeamAScore, int32 TeamBScore)
{
	return FText::FromString(FString::Printf(TEXT("%d : %d"), TeamAScore, TeamBScore));
}

FText UTacticalUI::GetCurrentScoreDisplayText() const
{
	const ATacticalGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATacticalGameState>() : nullptr;
	return GS ? GetScoreDisplayText(GS->TeamScores[0], GS->TeamScores[1]) : GetScoreDisplayText(0, 0);
}

FText UTacticalUI::GetCurrentPhaseDisplayText() const
{
	const ATacticalGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATacticalGameState>() : nullptr;
	if (!GS)
	{
		return GetPhaseDisplayText(ERoundPhase::PreRound);
	}

	return GetPhaseDisplayText(GS->CurrentPhase);
}

FText UTacticalUI::GetCurrentCountdownDisplayText() const
{
	const ATacticalGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATacticalGameState>() : nullptr;
	if (!GS)
	{
		return GetCountdownDisplayText(0.0f);
	}

	return GetCountdownDisplayText(GS->GetPhaseTimeRemaining());
}

TArray<FTeamAvatarStatus> UTacticalUI::GetTeamAvatarStatuses(uint8 TeamByte) const
{
	const APlayerController* OwningController = GetOwningPlayer();
	return BuildTeamAvatarStatuses(
		GetWorld(), TeamByte, OwningController ? OwningController->PlayerState : nullptr);
}

TArray<FTeamAvatarStatus> UTacticalUI::BuildTeamAvatarStatuses(
	const UWorld* World,
	uint8 TeamByte,
	const APlayerState* LocalPlayerState)
{
	TArray<FTeamAvatarStatus> Statuses;

	const ATacticalGameState* GS = World ? World->GetGameState<ATacticalGameState>() : nullptr;
	if (!GS)
	{
		return Statuses;
	}

	for (APlayerState* PS : GS->PlayerArray)
	{
		const ATacticalPlayerState* TPS = Cast<ATacticalPlayerState>(PS);
		if (!TPS || TPS->TeamByte != TeamByte)
		{
			continue;
		}

		FTeamAvatarStatus Status;
		Status.TeamByte = TeamByte;
		Status.PlayerId = PS->GetPlayerId();
		Status.PlayerName = FText::FromString(PS->GetPlayerName());
		Status.bIsActiveParticipant = TPS->bIsActiveParticipant;
		Status.bIsLocalPlayer = PS == LocalPlayerState;
		Status.ConnectionStatus = TPS->ConnectionStatus;
		Status.ReconnectGraceTimeRemaining = TPS->GetReconnectGraceTimeRemaining();

		if (const APawn* Pawn = PS->GetPawn())
		{
			if (const AShooterCharacter* Character = Cast<AShooterCharacter>(Pawn))
			{
				Status.bIsDead = Character->IsDead();
				Status.bHasLivingPawn = !Status.bIsDead;
			}
		}
		else
		{
			// A connected roster member without a Pawn is the normal restricted-
			// spectator state after reconnecting too late for the current round.
			// Only an actual replicated dead Pawn is authoritative evidence of an
			// elimination; inactivity alone also means "waiting for next round".
			Status.bIsDead = false;
		}

		Statuses.Add(Status);
	}

	Statuses.Sort([](const FTeamAvatarStatus& Left, const FTeamAvatarStatus& Right)
	{
		if (Left.PlayerId != Right.PlayerId)
		{
			return Left.PlayerId < Right.PlayerId;
		}
		return Left.PlayerName.ToString() < Right.PlayerName.ToString();
	});

	return Statuses;
}
