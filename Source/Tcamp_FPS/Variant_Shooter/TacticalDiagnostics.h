// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Economy/EconomyTypes.h"

/** Dedicated, searchable category for low-frequency tactical flow events. */
DECLARE_LOG_CATEGORY_EXTERN(LogTacticalFlow, Log, All);

namespace TacticalDiagnostics
{
	inline const TCHAR* GetPhaseName(ERoundPhase Phase)
	{
		switch (Phase)
		{
		case ERoundPhase::PreRound: return TEXT("PreRound");
		case ERoundPhase::Buy: return TEXT("Buy");
		case ERoundPhase::Combat: return TEXT("Combat");
		case ERoundPhase::Overtime: return TEXT("OvertimeLegacy");
		case ERoundPhase::RoundEnd: return TEXT("RoundEnd");
		case ERoundPhase::MatchEnd: return TEXT("MatchEnd");
		default: return TEXT("Unknown");
		}
	}

	inline const TCHAR* GetRoundEndReasonName(ERoundEndReason Reason)
	{
		switch (Reason)
		{
		case ERoundEndReason::TeamEliminated: return TEXT("TeamEliminated");
		case ERoundEndReason::TimeExpiredDraw: return TEXT("TimeExpiredDraw");
		case ERoundEndReason::SimultaneousEliminationDraw: return TEXT("SimultaneousEliminationDraw");
		case ERoundEndReason::TimeAliveAdvantage: return TEXT("TimeAliveAdvantageLegacy");
		case ERoundEndReason::OvertimeDraw: return TEXT("OvertimeDrawLegacy");
		default: return TEXT("Unknown");
		}
	}

	inline const TCHAR* GetMatchEndReasonName(EMatchEndReason Reason)
	{
		switch (Reason)
		{
		case EMatchEndReason::ScoreVictory: return TEXT("ScoreVictory");
		case EMatchEndReason::ScoreDraw: return TEXT("ScoreDraw");
		case EMatchEndReason::TeamDisconnectForfeit: return TEXT("TeamDisconnectForfeit");
		case EMatchEndReason::AllTeamsDisconnectedAbort: return TEXT("AllTeamsDisconnectedAbort");
		case EMatchEndReason::HostLost: return TEXT("HostLost");
		default: return TEXT("Unknown");
		}
	}

	inline const TCHAR* GetTeamName(uint8 Team)
	{
		return Team == 0 ? TEXT("RED") : Team == 1 ? TEXT("BLUE") : TEXT("NONE");
	}

	inline const TCHAR* GetOutcomeName(uint8 WinningTeam)
	{
		return WinningTeam <= 1 ? GetTeamName(WinningTeam) : TEXT("DRAW");
	}

	inline const TCHAR* GetMatchOutcomeName(uint8 WinningTeam, EMatchEndReason Reason)
	{
		return Reason == EMatchEndReason::AllTeamsDisconnectedAbort
			|| Reason == EMatchEndReason::HostLost
			? TEXT("NO_RESULT")
			: GetOutcomeName(WinningTeam);
	}

	inline const TCHAR* GetNetModeName(ENetMode NetMode)
	{
		switch (NetMode)
		{
		case NM_Standalone: return TEXT("Standalone");
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		case NM_ListenServer: return TEXT("ListenServer");
		case NM_Client: return TEXT("Client");
		default: return TEXT("Unknown");
		}
	}

	inline const TCHAR* GetWeaponTierName(EWeaponTier Tier)
	{
		switch (Tier)
		{
		case EWeaponTier::DefaultPistol: return TEXT("DefaultPistol");
		case EWeaponTier::UpgradedPistol: return TEXT("UpgradedPistol");
		case EWeaponTier::Rifle: return TEXT("Rifle");
		case EWeaponTier::Sniper: return TEXT("Sniper");
		default: return TEXT("Unknown");
		}
	}

	inline const TCHAR* GetArmorTypeName(EArmorType Type)
	{
		switch (Type)
		{
		case EArmorType::None: return TEXT("None");
		case EArmorType::Light: return TEXT("Light");
		case EArmorType::Heavy: return TEXT("Heavy");
		default: return TEXT("Unknown");
		}
	}
}
