// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Economy/EconomyTypes.h"

/**
 * Pure tactical match rules shared by runtime code and automation tests.
 * Keeping these decisions independent from UWorld makes the locked design
 * rules cheap to verify without pretending to test networking or replication.
 */
namespace TacticalRules
{
	enum class ERoundEconomyOutcome : uint8
	{
		Win,
		Loss,
		Draw
	};

	struct FRoundResolution
	{
		bool bShouldEndRound = false;
		uint8 WinningTeam = 255;
		ERoundEndReason Reason = ERoundEndReason::Unknown;
	};

	struct FMatchForfeitResolution
	{
		bool bShouldEndMatch = false;
		uint8 WinningTeam = 255;
		EMatchEndReason Reason = EMatchEndReason::Unknown;
	};

	/** One-shot threshold checks used by the authoritative treasure timeline. */
	inline bool ShouldAnnounceTreasure(
		bool bEnabled,
		bool bHasSelection,
		bool bAlreadyAnnounced,
		ERoundPhase Phase,
		float PhaseTimeRemaining)
	{
		return bEnabled && bHasSelection && !bAlreadyAnnounced
			&& Phase == ERoundPhase::Combat && PhaseTimeRemaining > 0.0f
			&& PhaseTimeRemaining <= TacticalEconomy::TreasureAnnouncementTimeRemaining;
	}

	inline bool ShouldActivateTreasure(
		bool bEnabled,
		bool bHasSelection,
		bool bAnnounced,
		bool bAlreadyActivated,
		ERoundPhase Phase,
		float PhaseTimeRemaining)
	{
		return bEnabled && bHasSelection && bAnnounced && !bAlreadyActivated
			&& Phase == ERoundPhase::Combat && PhaseTimeRemaining > 0.0f
			&& PhaseTimeRemaining <= TacticalEconomy::TreasureActivationTimeRemaining;
	}

	inline bool CanClaimTreasure(
		bool bChestActive,
		bool bChestUnlocked,
		bool bAlreadyLooted,
		bool bClaimantAlive,
		bool bInInteractionRange,
		bool bActiveParticipant,
		uint8 Team,
		ERoundPhase Phase,
		int32 PreparedRound,
		int32 CurrentRound)
	{
		return bChestActive && bChestUnlocked && !bAlreadyLooted
			&& bClaimantAlive && bInInteractionRange && bActiveParticipant
			&& Team <= 1 && Phase == ERoundPhase::Combat
			&& PreparedRound > 0 && PreparedRound == CurrentRound;
	}

	inline bool IsTreasureRewardEligible(
		bool bRosterSlotReserved,
		uint8 ParticipantTeam,
		uint8 ClaimingTeam,
		int32 EnteredRound,
		int32 CurrentRound,
		bool bAbandoned)
	{
		return bRosterSlotReserved && ParticipantTeam == ClaimingTeam
			&& ClaimingTeam <= 1 && EnteredRound == CurrentRound && !bAbandoned;
	}

	inline int32 ApplyTreasureMoneyReward(int32 Money)
	{
		return FMath::Clamp(
			Money + TacticalEconomy::ChestTeamReward, 0, TacticalEconomy::MaxMoney);
	}

	inline bool CanStartMatch(int32 TeamAPlayers, int32 TeamBPlayers, int32 LobbyPlayers,
		int32 RequiredPlayers, bool bAllLobbyPlayersSelectedAndReady)
	{
		const int32 SelectedPlayers = TeamAPlayers + TeamBPlayers;
		return TeamAPlayers > 0
			&& TeamBPlayers > 0
			&& TeamAPlayers <= TacticalEconomy::MaxPlayersPerTeam
			&& TeamBPlayers <= TacticalEconomy::MaxPlayersPerTeam
			&& LobbyPlayers <= TacticalEconomy::MaxPlayersPerTeam * 2
			&& LobbyPlayers >= RequiredPlayers
			&& SelectedPlayers == LobbyPlayers
			&& bAllLobbyPlayersSelectedAndReady;
	}

	/** Inactive/disconnected PlayerStates may retain a team for reconnect restoration. */
	inline bool RequiresActiveRosterMembership(bool bIsActiveParticipant, uint8 Team)
	{
		return bIsActiveParticipant && Team <= 1;
	}

	/** A combat disconnect may require an alive-count refresh; it is not itself a death. */
	inline bool ShouldEvaluateDisconnectForElimination(bool bWorldTearingDown, ERoundPhase Phase,
		bool bWasActiveParticipant, uint8 PreviousTeam)
	{
		return !bWorldTearingDown
			&& Phase == ERoundPhase::Combat
			&& bWasActiveParticipant
			&& PreviousTeam <= 1;
	}

	/** Buy/Combat disconnects have entered the current round and receive exactly one settlement. */
	inline bool ShouldMarkDisconnectForSettlement(bool bWorldTearingDown, bool bMatchStarted,
		ERoundPhase Phase, bool bWasActiveParticipant, uint8 PreviousTeam)
	{
		return !bWorldTearingDown
			&& bMatchStarted
			&& (Phase == ERoundPhase::Buy || Phase == ERoundPhase::Combat)
			&& bWasActiveParticipant
			&& PreviousTeam <= 1;
	}

	/** Once a tactical match starts, only GameMode's round boundary may respawn players. */
	inline bool AllowsClientRequestedRestart(bool bMatchStarted)
	{
		return !bMatchStarted;
	}

	/** Lobby leave frees capacity; an in-progress match keeps its locked roster slot. */
	inline bool ShouldRetainAbandonedReservation(bool bMatchStarted)
	{
		return bMatchStarted;
	}

	/** A pawnless lobby candidate keeps its timed grace; only in-match absence downgrades immediately. */
	inline bool ShouldDowngradeDisconnectedWithoutPawn(
		bool bLobbyCandidate,
		bool bRetainedPawnAlive)
	{
		return !bLobbyCandidate && !bRetainedPawnAlive;
	}

	/**
	 * A living retained body belongs to the round, not to the public 30-second
	 * reconnect countdown. Expiring that countdown must not eliminate it.
	 */
	inline bool ShouldRetainDisconnectedPawnForCurrentRound(
		bool bMatchStarted,
		bool bRetainedPawnAlive,
		bool bEnteredCurrentRound,
		ERoundPhase Phase)
	{
		return bMatchStarted
			&& bRetainedPawnAlive
			&& bEnteredCurrentRound
			&& (Phase == ERoundPhase::Buy || Phase == ERoundPhase::Combat);
	}

	/** Same-round body recovery remains legal after the public countdown expires. */
	inline bool CanReclaimRetainedPawn(
		bool bRetainedBodyState,
		bool bRetainedPawnAlive,
		bool bEnteredCurrentRound,
		ERoundPhase Phase)
	{
		return bRetainedBodyState
			&& ShouldRetainDisconnectedPawnForCurrentRound(
				true, bRetainedPawnAlive, bEnteredCurrentRound, Phase);
	}

	/** Derives a client display value from a server-authored absolute deadline. */
	inline float GetReconnectGraceTimeRemaining(float GraceEndServerTime, float ServerWorldTime)
	{
		return GraceEndServerTime > 0.0f
			? FMath::Max(0.0f, GraceEndServerTime - ServerWorldTime)
			: 0.0f;
	}

	/** Keeps the HUD stable while still showing the full final partial second. */
	inline int32 GetReconnectGraceDisplaySeconds(float GraceEndServerTime, float ServerWorldTime)
	{
		return FMath::CeilToInt(GetReconnectGraceTimeRemaining(
			GraceEndServerTime, ServerWorldTime));
	}

	/** Identity reuse never authorizes an old controller to mutate a new roster generation. */
	inline bool IsSameParticipantGeneration(
		const FGuid& ExpectedParticipantId,
		const FGuid& CurrentParticipantId)
	{
		return ExpectedParticipantId.IsValid()
			&& CurrentParticipantId.IsValid()
			&& ExpectedParticipantId == CurrentParticipantId;
	}

	/**
	 * Information gate for the owner-only spectator snapshot. An inactive player
	 * with no pawn is intentionally allowed because that is the valid reconnect
	 * state between PostLogin and the next round boundary.
	 */
	inline bool CanReceiveTeammateSpectatorData(
		uint8 ViewerTeam,
		bool bViewerHasPawn,
		bool bViewerPawnIsDead,
		uint8 TargetTeam,
		bool bTargetIsActiveParticipant,
		bool bDifferentPlayer,
		bool bTargetPawnMatchesPlayerState,
		bool bTargetIsAlive)
	{
		return ViewerTeam <= 1
			&& TargetTeam == ViewerTeam
			&& bTargetIsActiveParticipant
			&& bDifferentPlayer
			&& bTargetPawnMatchesPlayerState
			&& bTargetIsAlive
			&& (!bViewerHasPawn || bViewerPawnIsDead);
	}

	/** A team that qualified at match start remains eligible for elimination while its roster is empty. */
	inline bool IsQualifiedTeamEliminated(bool bQualifiedForMatch, int32 LivingPlayers)
	{
		return bQualifiedForMatch && LivingPlayers <= 0;
	}

	/**
	 * Do not manufacture additional rounds for an empty locked roster. The
	 * current round settles normally; if that settlement has not already locked
	 * a score result, a wholly disconnected qualified team forfeits before the
	 * next round. A connected restricted spectator still counts because it owns
	 * a valid team controller and can enter next round.
	 */
	inline FMatchForfeitResolution ResolveWholeTeamDisconnectForfeit(
		bool bTeamAQualified,
		int32 ConnectedTeamAPlayers,
		bool bTeamBQualified,
		int32 ConnectedTeamBPlayers)
	{
		const bool bTeamAAbsent = bTeamAQualified && ConnectedTeamAPlayers <= 0;
		const bool bTeamBAbsent = bTeamBQualified && ConnectedTeamBPlayers <= 0;
		if (!bTeamAAbsent && !bTeamBAbsent)
		{
			return {};
		}
		if (bTeamAAbsent && bTeamBAbsent)
		{
			return { true, 255, EMatchEndReason::AllTeamsDisconnectedAbort };
		}
		return { true, bTeamAAbsent ? uint8{1} : uint8{0},
			EMatchEndReason::TeamDisconnectForfeit };
	}

	/**
	 * A round that has already locked the normal score result owns the match
	 * outcome. Disconnect state is consulted only when another round would
	 * otherwise start, so leaving during the RoundEnd presentation cannot reverse
	 * a victory that was earned by the completed round.
	 */
	inline FMatchForfeitResolution ResolveRoundEndMatchTermination(
		bool bScoreResultLocked,
		uint8 ScoreWinner,
		bool bTeamAQualified,
		int32 ConnectedTeamAPlayers,
		bool bTeamBQualified,
		int32 ConnectedTeamBPlayers)
	{
		if (bScoreResultLocked)
		{
			return { true, ScoreWinner, ScoreWinner <= 1
				? EMatchEndReason::ScoreVictory
				: EMatchEndReason::ScoreDraw };
		}

		return ResolveWholeTeamDisconnectForfeit(
			bTeamAQualified, ConnectedTeamAPlayers,
			bTeamBQualified, ConnectedTeamBPlayers);
	}

	/** One source of truth for connected and disconnected round-economy settlement. */
	inline void ApplyRoundEconomy(ERoundEconomyOutcome Outcome, int32& Money,
		int32& LossStreak, int32& RoundsWon)
	{
		Money = FMath::Clamp(Money, 0, TacticalEconomy::MaxMoney);
		LossStreak = FMath::Clamp(LossStreak, 0, TacticalEconomy::MaxLossStreak);
		RoundsWon = FMath::Max(0, RoundsWon);

		switch (Outcome)
		{
		case ERoundEconomyOutcome::Win:
			++RoundsWon;
			LossStreak = 0;
			Money += TacticalEconomy::WinRoundReward;
			break;

		case ERoundEconomyOutcome::Loss:
			LossStreak = FMath::Clamp(LossStreak + 1, 1, TacticalEconomy::MaxLossStreak);
			Money += TacticalEconomy::LossStreakBonus[LossStreak - 1];
			break;

		case ERoundEconomyOutcome::Draw:
			Money += TacticalEconomy::DrawRoundReward;
			break;
		}

		Money = FMath::Clamp(Money, 0, TacticalEconomy::MaxMoney);
	}

	inline FRoundResolution ResolveElimination(bool bTeamAEliminated, bool bTeamBEliminated)
	{
		if (bTeamAEliminated && bTeamBEliminated)
		{
			return { true, 255, ERoundEndReason::SimultaneousEliminationDraw };
		}
		if (bTeamAEliminated)
		{
			return { true, 1, ERoundEndReason::TeamEliminated };
		}
		if (bTeamBEliminated)
		{
			return { true, 0, ERoundEndReason::TeamEliminated };
		}
		return {};
	}

	inline FRoundResolution ResolveCombatTimeout(bool bTeamAEliminated, bool bTeamBEliminated)
	{
		FRoundResolution Resolution = ResolveElimination(bTeamAEliminated, bTeamBEliminated);
		if (!Resolution.bShouldEndRound)
		{
			Resolution = { true, 255, ERoundEndReason::TimeExpiredDraw };
		}
		return Resolution;
	}

	inline bool IsMatchOver(int32 TeamAScore, int32 TeamBScore, int32 CompletedRound, int32 MaximumRounds)
	{
		return TeamAScore >= TacticalEconomy::RoundsToWin
			|| TeamBScore >= TacticalEconomy::RoundsToWin
			|| CompletedRound >= MaximumRounds;
	}

	inline uint8 GetMatchWinner(int32 TeamAScore, int32 TeamBScore)
	{
		if (TeamAScore > TeamBScore)
		{
			return 0;
		}
		if (TeamBScore > TeamAScore)
		{
			return 1;
		}
		return 255;
	}
}
