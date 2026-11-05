// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Protocol constants and side-effect-free admission rules shared by the
 * server, client reconnect URL builder, and automation tests.
 */
namespace TacticalNetworkAdmission
{
	inline constexpr int32 ProtocolVersion = 1;
	inline constexpr TCHAR MatchIdOption[] = TEXT("TacticalMatchId");
	inline constexpr TCHAR ReconnectCredentialOption[] = TEXT("TacticalReconnectToken");
	inline constexpr TCHAR ProtocolVersionOption[] = TEXT("TacticalClientVersion");

	enum class EReconnectClaimShape : uint8
	{
		FreshJoin,
		CompleteReconnectClaim,
		MalformedPartialClaim
	};

	inline EReconnectClaimShape ClassifyClaim(
		bool bHasMatchId,
		bool bHasCredential,
		bool bHasProtocolVersion)
	{
		const int32 PresentFields = (bHasMatchId ? 1 : 0)
			+ (bHasCredential ? 1 : 0)
			+ (bHasProtocolVersion ? 1 : 0);
		if (PresentFields == 0)
		{
			return EReconnectClaimShape::FreshJoin;
		}
		return PresentFields == 3
			? EReconnectClaimShape::CompleteReconnectClaim
			: EReconnectClaimShape::MalformedPartialClaim;
	}

	inline bool IsCompatibleProtocolVersion(int32 CandidateVersion)
	{
		return CandidateVersion == ProtocolVersion;
	}

	inline bool IsTransientStaleConnectionRejection(const FString& ErrorString)
	{
		return ErrorString.Contains(
			TEXT("TACTICAL_DUPLICATE_ACTIVE_PARTICIPANT"),
			ESearchCase::IgnoreCase);
	}

	inline bool IsTacticalAdmissionRejection(const FString& ErrorString)
	{
		return ErrorString.Contains(TEXT("TACTICAL_"), ESearchCase::IgnoreCase);
	}

	/**
	 * A disconnected client may briefly load a Standalone fallback world. That
	 * local world's GameMode must never replace the admission for the remote
	 * match currently being recovered. Protection may outlive the visible retry
	 * overlay so a late fallback RPC also stays isolated after CancelReconnect.
	 */
	inline bool ShouldAcceptOwnerAdmission(
		bool bRecoveryAdmissionProtected,
		bool bSourceIsRemoteClient,
		bool bMatchesCachedMatch)
	{
		return !bRecoveryAdmissionProtected
			|| (bSourceIsRemoteClient && bMatchesCachedMatch);
	}

	inline bool CanUseUnacknowledgedFirstIssueRecovery(
		bool bHasAuthenticatedIdentity,
		bool bHasActiveConnection,
		bool bCredentialAcknowledged,
		bool bHasPreviousCredential,
		bool bParticipantAbandoned)
	{
		return bHasAuthenticatedIdentity
			&& !bHasActiveConnection
			&& !bCredentialAcknowledged
			// A previous bearer is created only by rotation. Credentialless
			// recovery is a one-shot escape hatch for the initial delivery gap;
			// every later reconnect must still prove the current or previous token.
			&& !bHasPreviousCredential
			&& !bParticipantAbandoned;
	}

	/** Compares opaque ASCII credentials without an early-out on content. */
	inline bool ConstantTimeCredentialEquals(const FString& Candidate, const FString& Expected)
	{
		const int32 MaximumLength = FMath::Max(Candidate.Len(), Expected.Len());
		uint32 Difference = static_cast<uint32>(Candidate.Len() ^ Expected.Len());
		for (int32 Index = 0; Index < MaximumLength; ++Index)
		{
			const TCHAR CandidateCharacter = Candidate.IsValidIndex(Index)
				? Candidate[Index]
				: 0;
			const TCHAR ExpectedCharacter = Expected.IsValidIndex(Index)
				? Expected[Index]
				: 0;
			Difference |= static_cast<uint32>(CandidateCharacter ^ ExpectedCharacter);
		}
		return Difference == 0;
	}
}
