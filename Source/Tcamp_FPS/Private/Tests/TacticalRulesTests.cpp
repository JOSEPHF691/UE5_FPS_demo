// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/InputSettings.h"
#include "Engine/GameInstance.h"
#include "Variant_Shooter/TacticalPlayerController.h"
#include "Variant_Shooter/TacticalRules.h"
#include "Variant_Shooter/TacticalPlayerState.h"
#include "Variant_Shooter/Network/TacticalMatchRosterSubsystem.h"
#include "Variant_Shooter/Network/TacticalNetworkAdmission.h"
#include "Variant_Shooter/UI/TacticalHudLayout.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalMatchStartRulesTest,
	"Tcamp.Tactical.Rules.MatchStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalMatchStartRulesTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("One player cannot start"), TacticalRules::CanStartMatch(1, 0, 1, 2, true));
	TestFalse(TEXT("Both teams are required"), TacticalRules::CanStartMatch(2, 0, 2, 2, true));
	TestFalse(TEXT("Every lobby player must be ready"), TacticalRules::CanStartMatch(1, 1, 2, 2, false));
	TestFalse(TEXT("An unselected third lobby player blocks start"), TacticalRules::CanStartMatch(1, 1, 3, 2, false));
	TestFalse(TEXT("Selected count must match the lobby candidates"), TacticalRules::CanStartMatch(1, 1, 3, 2, true));
	TestTrue(TEXT("One ready player on each team can start"), TacticalRules::CanStartMatch(1, 1, 2, 2, true));
	TestFalse(TEXT("Five ready players do not satisfy a six-player gate"), TacticalRules::CanStartMatch(3, 2, 5, 6, true));
	TestTrue(TEXT("A full 3v3 roster can start"), TacticalRules::CanStartMatch(3, 3, 6, 2, true));
	TestTrue(TEXT("A full 3v3 roster satisfies a six-player gate"), TacticalRules::CanStartMatch(3, 3, 6, 6, true));
	TestFalse(TEXT("A team cannot exceed three players"), TacticalRules::CanStartMatch(4, 1, 5, 2, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalDisconnectRulesTest,
	"Tcamp.Tactical.Rules.Disconnect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalDisconnectRulesTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("An active assigned player must be present in its roster"),
		TacticalRules::RequiresActiveRosterMembership(true, 0));
	TestFalse(TEXT("An inactive disconnected player may retain its previous team"),
		TacticalRules::RequiresActiveRosterMembership(false, 0));
	TestFalse(TEXT("An unassigned lobby candidate is not yet in a team roster"),
		TacticalRules::RequiresActiveRosterMembership(true, 255));

	TestTrue(TEXT("A real active-player Combat disconnect refreshes alive-team evaluation"),
		TacticalRules::ShouldEvaluateDisconnectForElimination(false, ERoundPhase::Combat, true, 1));
	TestFalse(TEXT("World teardown never triggers elimination evaluation"),
		TacticalRules::ShouldEvaluateDisconnectForElimination(true, ERoundPhase::Combat, true, 1));
	TestFalse(TEXT("A Buy-phase disconnect does not resolve a Combat elimination"),
		TacticalRules::ShouldEvaluateDisconnectForElimination(false, ERoundPhase::Buy, true, 1));
	TestFalse(TEXT("An inactive spectator disconnect does not affect elimination"),
		TacticalRules::ShouldEvaluateDisconnectForElimination(false, ERoundPhase::Combat, false, 1));
	TestFalse(TEXT("An unassigned player disconnect does not affect elimination"),
		TacticalRules::ShouldEvaluateDisconnectForElimination(false, ERoundPhase::Combat, true, 255));

	TestTrue(TEXT("An active Buy player is owed this round's settlement"),
		TacticalRules::ShouldMarkDisconnectForSettlement(false, true, ERoundPhase::Buy, true, 0));
	TestTrue(TEXT("An active Combat player is owed this round's settlement"),
		TacticalRules::ShouldMarkDisconnectForSettlement(false, true, ERoundPhase::Combat, true, 1));
	TestFalse(TEXT("World teardown never creates pending economy"),
		TacticalRules::ShouldMarkDisconnectForSettlement(true, true, ERoundPhase::Combat, true, 0));
	TestFalse(TEXT("RoundEnd has already settled economy"),
		TacticalRules::ShouldMarkDisconnectForSettlement(false, true, ERoundPhase::RoundEnd, true, 0));
	TestFalse(TEXT("An inactive reconnect is not marked a second time"),
		TacticalRules::ShouldMarkDisconnectForSettlement(false, true, ERoundPhase::Combat, false, 0));
	TestFalse(TEXT("A pawnless lobby candidate keeps its bounded reconnect grace"),
		TacticalRules::ShouldDowngradeDisconnectedWithoutPawn(true, false));
	TestTrue(TEXT("A pawnless in-match participant downgrades to waiting"),
		TacticalRules::ShouldDowngradeDisconnectedWithoutPawn(false, false));
	TestFalse(TEXT("A retained living pawn remains in the current round"),
		TacticalRules::ShouldDowngradeDisconnectedWithoutPawn(false, true));
	TestTrue(TEXT("A living disconnected Buy pawn survives status-window expiry"),
		TacticalRules::ShouldRetainDisconnectedPawnForCurrentRound(
			true, true, true, ERoundPhase::Buy));
	TestTrue(TEXT("A living disconnected Combat pawn survives status-window expiry"),
		TacticalRules::ShouldRetainDisconnectedPawnForCurrentRound(
			true, true, true, ERoundPhase::Combat));
	TestFalse(TEXT("Status expiry never retains a dead pawn"),
		TacticalRules::ShouldRetainDisconnectedPawnForCurrentRound(
			true, false, true, ERoundPhase::Combat));
	TestFalse(TEXT("RoundEnd owns retained-body cleanup"),
		TacticalRules::ShouldRetainDisconnectedPawnForCurrentRound(
			true, true, true, ERoundPhase::RoundEnd));
	TestTrue(TEXT("A same-round living body remains reclaimable after countdown expiry"),
		TacticalRules::CanReclaimRetainedPawn(
			true, true, true, ERoundPhase::Combat));
	TestFalse(TEXT("A retained body from an older round cannot be reclaimed"),
		TacticalRules::CanReclaimRetainedPawn(
			true, true, false, ERoundPhase::Combat));
	TestFalse(TEXT("RoundEnd reconnect cannot repossess the completed round's body"),
		TacticalRules::CanReclaimRetainedPawn(
			true, true, true, ERoundPhase::RoundEnd));
	TestTrue(TEXT("Engine restart requests are allowed before the tactical match starts"),
		TacticalRules::AllowsClientRequestedRestart(false));
	TestFalse(TEXT("Engine restart requests cannot bypass the next-round reconnect gate"),
		TacticalRules::AllowsClientRequestedRestart(true));
	TestFalse(TEXT("Lobby abandonment releases its reservation"),
		TacticalRules::ShouldRetainAbandonedReservation(false));
	TestTrue(TEXT("An in-progress match keeps its abandoned roster reservation"),
		TacticalRules::ShouldRetainAbandonedReservation(true));
	const FGuid OriginalParticipantId(1, 2, 3, 4);
	const FGuid DifferentParticipantId(1, 2, 3, 5);
	TestTrue(TEXT("The same valid participant generation may complete deferred cleanup"),
		TacticalRules::IsSameParticipantGeneration(
			OriginalParticipantId, OriginalParticipantId));
	TestFalse(TEXT("An invalid old generation cannot mutate an identity's current record"),
		TacticalRules::IsSameParticipantGeneration(
			FGuid(), OriginalParticipantId));
	TestFalse(TEXT("A reused identity's new participant generation is protected"),
		TacticalRules::IsSameParticipantGeneration(
			OriginalParticipantId, DifferentParticipantId));
	TestTrue(TEXT("A qualified team with no living players is eliminated"),
		TacticalRules::IsQualifiedTeamEliminated(true, 0));
	TestFalse(TEXT("A qualified team with a living player remains in the round"),
		TacticalRules::IsQualifiedTeamEliminated(true, 1));
	TestFalse(TEXT("An unqualified empty team is not manufactured into a participant"),
		TacticalRules::IsQualifiedTeamEliminated(false, 0));

	const TacticalRules::FMatchForfeitResolution ConnectedTeams =
		TacticalRules::ResolveWholeTeamDisconnectForfeit(true, 1, true, 1);
	TestFalse(TEXT("Two connected qualified teams continue to the next round"),
		ConnectedTeams.bShouldEndMatch);
	const TacticalRules::FMatchForfeitResolution BlueForfeits =
		TacticalRules::ResolveWholeTeamDisconnectForfeit(true, 1, true, 0);
	TestTrue(TEXT("A wholly disconnected qualified team forfeits at the round boundary"),
		BlueForfeits.bShouldEndMatch);
	TestEqual(TEXT("The connected red team wins a blue disconnect forfeit"),
		BlueForfeits.WinningTeam, uint8{0});
	TestEqual(TEXT("A unilateral disconnect is explicitly a match forfeit"),
		static_cast<uint8>(BlueForfeits.Reason),
		static_cast<uint8>(EMatchEndReason::TeamDisconnectForfeit));
	const TacticalRules::FMatchForfeitResolution BothOffline =
		TacticalRules::ResolveWholeTeamDisconnectForfeit(true, 0, true, 0);
	TestTrue(TEXT("Two absent qualified teams terminate the match"),
		BothOffline.bShouldEndMatch);
	TestEqual(TEXT("Two absent qualified teams have no winning team"),
		BothOffline.WinningTeam, uint8{255});
	TestEqual(TEXT("Two absent qualified teams abort instead of drawing"),
		static_cast<uint8>(BothOffline.Reason),
		static_cast<uint8>(EMatchEndReason::AllTeamsDisconnectedAbort));
	const TacticalRules::FMatchForfeitResolution RedRecovered =
		TacticalRules::ResolveWholeTeamDisconnectForfeit(true, 1, true, 1);
	TestFalse(TEXT("One restored connection cancels a pending whole-team forfeit"),
		RedRecovered.bShouldEndMatch);
	const TacticalRules::FMatchForfeitResolution UnqualifiedEmptyTeam =
		TacticalRules::ResolveWholeTeamDisconnectForfeit(true, 1, false, 0);
	TestFalse(TEXT("An empty team that never qualified cannot cause a forfeit"),
		UnqualifiedEmptyTeam.bShouldEndMatch);
	const TacticalRules::FMatchForfeitResolution LockedRedVictory =
		TacticalRules::ResolveRoundEndMatchTermination(
			true, 0, true, 0, true, 1);
	TestEqual(TEXT("A completed score victory cannot be reversed by its winner disconnecting during RoundEnd"),
		LockedRedVictory.WinningTeam, uint8{0});
	TestEqual(TEXT("A locked score victory outranks disconnect forfeit evaluation"),
		static_cast<uint8>(LockedRedVictory.Reason),
		static_cast<uint8>(EMatchEndReason::ScoreVictory));
	const TacticalRules::FMatchForfeitResolution LockedScoreDraw =
		TacticalRules::ResolveRoundEndMatchTermination(
			true, 255, true, 1, true, 0);
	TestEqual(TEXT("A max-round score draw cannot be rewritten as a disconnect forfeit"),
		static_cast<uint8>(LockedScoreDraw.Reason),
		static_cast<uint8>(EMatchEndReason::ScoreDraw));
	const TacticalRules::FMatchForfeitResolution UnlockedBlueForfeit =
		TacticalRules::ResolveRoundEndMatchTermination(
			false, 255, true, 1, true, 0);
	TestEqual(TEXT("Disconnect forfeit applies when the completed round did not end the match"),
		static_cast<uint8>(UnlockedBlueForfeit.Reason),
		static_cast<uint8>(EMatchEndReason::TeamDisconnectForfeit));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalReconnectGraceCountdownTest,
	"Tcamp.Tactical.Network.GraceCountdown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalReconnectGraceCountdownTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("A missing deadline has no reconnect grace"),
		TacticalRules::GetReconnectGraceTimeRemaining(0.0f, 100.0f), 0.0f);
	TestEqual(TEXT("Remaining grace uses the synchronized server clock"),
		TacticalRules::GetReconnectGraceTimeRemaining(130.5f, 100.0f), 30.5f);
	TestEqual(TEXT("A partial final second stays visible"),
		TacticalRules::GetReconnectGraceDisplaySeconds(100.01f, 100.0f), 1);
	TestEqual(TEXT("An expired deadline clamps to zero"),
		TacticalRules::GetReconnectGraceTimeRemaining(90.0f, 100.0f), 0.0f);
	TestEqual(TEXT("Expired display seconds never become negative"),
		TacticalRules::GetReconnectGraceDisplaySeconds(90.0f, 100.0f), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalStableRosterTest,
	"Tcamp.Tactical.Network.StableRoster",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalStableRosterTest::RunTest(const FString& Parameters)
{
	UGameInstance* OuterGameInstance = NewObject<UGameInstance>();
	UTacticalMatchRosterSubsystem* Roster =
		NewObject<UTacticalMatchRosterSubsystem>(OuterGameInstance);
	TestNotNull(TEXT("Stable roster subsystem can be constructed"), Roster);
	if (!Roster)
	{
		return false;
	}

	FTacticalMatchParticipantRecord& RedRecord =
		Roster->RegisterParticipant(TEXT("UID:RedA"), nullptr, nullptr);
	const FString FirstMatchId = Roster->GetMatchId();
	const FString FirstCredential = RedRecord.ReconnectCredential;
	TestFalse(TEXT("A match gets a non-empty opaque id"), FirstMatchId.IsEmpty());
	TestFalse(TEXT("A participant gets a non-empty reconnect credential"),
		FirstCredential.IsEmpty());
	TestFalse(TEXT("A first-issued credential starts unacknowledged"),
		RedRecord.bReconnectCredentialAcknowledged);
	RedRecord.TeamByte = 0;
	RedRecord.bRosterSlotReserved = true;
	RedRecord.ConnectionState = ETacticalParticipantConnectionState::DisconnectedGrace;
	const FGuid StableParticipantId = RedRecord.ParticipantId;

	FTacticalMatchParticipantRecord& SameRedRecord =
		Roster->RegisterParticipant(TEXT("UID:RedA"), nullptr, nullptr);
	TestEqual(TEXT("Reconnect keeps the opaque participant id"),
		SameRedRecord.ParticipantId, StableParticipantId);
	TestEqual(TEXT("Re-registering does not rotate a credential implicitly"),
		SameRedRecord.ReconnectCredential, FirstCredential);
	TestTrue(TEXT("The exact match/identity/credential tuple is admitted"),
		Roster->ValidateReconnectCredential(
			TEXT("UID:RedA"), FirstMatchId, FirstCredential));
	TestFalse(TEXT("A credential is scoped to its match"),
		Roster->ValidateReconnectCredential(
			TEXT("UID:RedA"), TEXT("WrongMatch"), FirstCredential));
	TestFalse(TEXT("A credential is scoped to its authenticated identity"),
		Roster->ValidateReconnectCredential(
			TEXT("UID:BlueA"), FirstMatchId, FirstCredential));
	const FString RotatedCredential =
		Roster->RotateReconnectCredential(SameRedRecord, FirstCredential);
	TestNotEqual(TEXT("Successful reconnect rotation replaces the old credential"),
		RotatedCredential, FirstCredential);
	TestTrue(TEXT("The client-known bearer overlaps until replacement ACK"),
		Roster->ValidateReconnectCredential(
			TEXT("UID:RedA"), FirstMatchId, FirstCredential));
	TestTrue(TEXT("The replacement credential is immediately valid"),
		Roster->ValidateReconnectCredential(
			TEXT("UID:RedA"), FirstMatchId, RotatedCredential));
	TestFalse(TEXT("A mismatched ACK cannot retire the overlap"),
		Roster->AcknowledgeReconnectCredential(
			SameRedRecord, FirstMatchId, TEXT("WrongCredential")));
	TestTrue(TEXT("The owning client's replacement ACK is accepted"),
		Roster->AcknowledgeReconnectCredential(
			SameRedRecord, FirstMatchId, RotatedCredential));
	TestTrue(TEXT("The registry records current credential acknowledgement"),
		SameRedRecord.bReconnectCredentialAcknowledged);
	TestFalse(TEXT("ACK retires the old bearer value"),
		Roster->ValidateReconnectCredential(
			TEXT("UID:RedA"), FirstMatchId, FirstCredential));
	const FString GuestIdentity = Roster->CreateGuestIdentityKey();
	TestTrue(TEXT("Provider-neutral identities are server-originated guest keys"),
		GuestIdentity.StartsWith(TEXT("GUEST:")));
	FTacticalMatchParticipantRecord& GuestRecord =
		Roster->RegisterParticipant(GuestIdentity, nullptr, nullptr);
	TestTrue(TEXT("A guest bearer resolves its private registry record"),
		Roster->FindByReconnectCredential(
			FirstMatchId, GuestRecord.ReconnectCredential) == &GuestRecord);
	TestNull(TEXT("A guest bearer cannot cross match boundaries"),
		Roster->FindByReconnectCredential(
			TEXT("WrongMatch"), GuestRecord.ReconnectCredential));
	TestEqual(TEXT("A disconnected participant still reserves its team slot"),
		Roster->CountReservedTeamSlots(0), 1);
	TestEqual(TEXT("The reservation never leaks into the opposite team"),
		Roster->CountReservedTeamSlots(1), 0);

	SameRedRecord.ConnectionState = ETacticalParticipantConnectionState::Abandoned;
	TestEqual(TEXT("An abandoned mid-match participant still reserves capacity"),
		Roster->CountReservedTeamSlots(0), 1);
	Roster->RemoveParticipant(TEXT("UID:RedA"));
	TestNull(TEXT("Removed identities cannot reclaim a record"),
		Roster->FindByIdentity(TEXT("UID:RedA")));
	Roster->ResetForNewMatch();
	TestNotEqual(TEXT("A new match rotates the match id"),
		Roster->GetMatchId(), FirstMatchId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalReconnectAdmissionRulesTest,
	"Tcamp.Tactical.Network.AdmissionRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalReconnectAdmissionRulesTest::RunTest(const FString& Parameters)
{
	using namespace TacticalNetworkAdmission;
	TestEqual(TEXT("No admission fields means a fresh join"),
		static_cast<uint8>(ClassifyClaim(false, false, false)),
		static_cast<uint8>(EReconnectClaimShape::FreshJoin));
	TestEqual(TEXT("All admission fields form a reconnect claim"),
		static_cast<uint8>(ClassifyClaim(true, true, true)),
		static_cast<uint8>(EReconnectClaimShape::CompleteReconnectClaim));
	TestEqual(TEXT("A partial claim is rejected as malformed"),
		static_cast<uint8>(ClassifyClaim(true, false, true)),
		static_cast<uint8>(EReconnectClaimShape::MalformedPartialClaim));
	TestTrue(TEXT("The current protocol version is compatible"),
		IsCompatibleProtocolVersion(ProtocolVersion));
	TestTrue(TEXT("Normal owner admission is accepted outside recovery"),
		ShouldAcceptOwnerAdmission(false, false, false));
	TestFalse(TEXT("A Standalone fallback cannot overwrite recovery admission"),
		ShouldAcceptOwnerAdmission(true, false, true));
	TestFalse(TEXT("A different match cannot replace recovery admission"),
		ShouldAcceptOwnerAdmission(true, true, false));
	TestTrue(TEXT("The recovered remote match may rotate its credential"),
		ShouldAcceptOwnerAdmission(true, true, true));
	TestFalse(TEXT("An older protocol version is incompatible"),
		IsCompatibleProtocolVersion(ProtocolVersion - 1));
	TestTrue(TEXT("A duplicate-active rejection is transient during reconnect"),
		IsTransientStaleConnectionRejection(
			TEXT("Failure Received: TACTICAL_DUPLICATE_ACTIVE_PARTICIPANT")));
	TestFalse(TEXT("A credential rejection is deterministic"),
		IsTransientStaleConnectionRejection(
			TEXT("TACTICAL_RECONNECT_ADMISSION_REJECTED")));
	TestTrue(TEXT("Project admission errors are recognized inside pending-connect failures"),
		IsTacticalAdmissionRejection(
			TEXT("NMT_Failure: TACTICAL_RECONNECT_VERSION_MISMATCH")));
	TestFalse(TEXT("A pure transport timeout is not an admission rejection"),
		IsTacticalAdmissionRejection(TEXT("Connection timed out")));
	TestTrue(TEXT("An offline UID may recover a first credential lost before ACK"),
		CanUseUnacknowledgedFirstIssueRecovery(true, false, false, false, false));
	TestFalse(TEXT("Guest bearers never use credentialless first-issue recovery"),
		CanUseUnacknowledgedFirstIssueRecovery(false, false, false, false, false));
	TestFalse(TEXT("An acknowledged participant must present its bearer"),
		CanUseUnacknowledgedFirstIssueRecovery(true, false, true, false, false));
	TestFalse(TEXT("A rotated credential must use its retained bearer overlap"),
		CanUseUnacknowledgedFirstIssueRecovery(true, false, false, true, false));
	TestFalse(TEXT("An abandoned participant cannot use first-issue recovery"),
		CanUseUnacknowledgedFirstIssueRecovery(true, false, false, false, true));
	TestTrue(TEXT("Opaque credential equality accepts identical values"),
		ConstantTimeCredentialEquals(TEXT("0123456789abcdef"),
			TEXT("0123456789abcdef")));
	TestFalse(TEXT("Opaque credential equality rejects content mismatch"),
		ConstantTimeCredentialEquals(TEXT("0123456789abcdef"),
			TEXT("0123456789abcdee")));
	TestFalse(TEXT("Opaque credential equality rejects length mismatch"),
		ConstantTimeCredentialEquals(TEXT("0123456789abcdef"),
			TEXT("0123456789abcde")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalRoundResolutionRulesTest,
	"Tcamp.Tactical.Rules.RoundResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalRoundResolutionRulesTest::RunTest(const FString& Parameters)
{
	const TacticalRules::FRoundResolution NoElimination = TacticalRules::ResolveElimination(false, false);
	TestFalse(TEXT("Living teams do not end the round before timeout"), NoElimination.bShouldEndRound);

	const TacticalRules::FRoundResolution RedEliminated = TacticalRules::ResolveElimination(true, false);
	TestTrue(TEXT("A single eliminated team ends the round"), RedEliminated.bShouldEndRound);
	TestEqual(TEXT("Blue wins when red is eliminated"), RedEliminated.WinningTeam, static_cast<uint8>(1));
	TestEqual(TEXT("Single-team elimination reason"), static_cast<uint8>(RedEliminated.Reason), static_cast<uint8>(ERoundEndReason::TeamEliminated));

	const TacticalRules::FRoundResolution BlueEliminated = TacticalRules::ResolveElimination(false, true);
	TestEqual(TEXT("Red wins when blue is eliminated"), BlueEliminated.WinningTeam, static_cast<uint8>(0));

	const TacticalRules::FRoundResolution Simultaneous = TacticalRules::ResolveElimination(true, true);
	TestEqual(TEXT("Simultaneous elimination has no winner"), Simultaneous.WinningTeam, static_cast<uint8>(255));
	TestEqual(TEXT("Simultaneous elimination reason"), static_cast<uint8>(Simultaneous.Reason), static_cast<uint8>(ERoundEndReason::SimultaneousEliminationDraw));

	const TacticalRules::FRoundResolution Timeout = TacticalRules::ResolveCombatTimeout(false, false);
	TestEqual(TEXT("Timeout has no winner"), Timeout.WinningTeam, static_cast<uint8>(255));
	TestEqual(TEXT("Timeout is an explicit draw"), static_cast<uint8>(Timeout.Reason), static_cast<uint8>(ERoundEndReason::TimeExpiredDraw));

	const TacticalRules::FRoundResolution RedDeadAtTimeout = TacticalRules::ResolveCombatTimeout(true, false);
	TestEqual(TEXT("Blue wins if red is eliminated at timeout"), RedDeadAtTimeout.WinningTeam, static_cast<uint8>(1));
	const TacticalRules::FRoundResolution BlueDeadAtTimeout = TacticalRules::ResolveCombatTimeout(false, true);
	TestEqual(TEXT("Red wins if blue is eliminated at timeout"), BlueDeadAtTimeout.WinningTeam, static_cast<uint8>(0));
	const TacticalRules::FRoundResolution BothDeadAtTimeout = TacticalRules::ResolveCombatTimeout(true, true);
	TestEqual(TEXT("Both eliminated at timeout is a draw"), BothDeadAtTimeout.WinningTeam, static_cast<uint8>(255));
	TestEqual(TEXT("Both eliminated keeps the simultaneous-elimination reason"),
		static_cast<uint8>(BothDeadAtTimeout.Reason), static_cast<uint8>(ERoundEndReason::SimultaneousEliminationDraw));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalMatchResolutionRulesTest,
	"Tcamp.Tactical.Rules.MatchResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalMatchResolutionRulesTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Match continues before target and round cap"), TacticalRules::IsMatchOver(4, 3, 8, 9));
	TestTrue(TEXT("Five wins ends the match immediately"), TacticalRules::IsMatchOver(5, 0, 5, 9));
	TestTrue(TEXT("The ninth completed round ends the match"), TacticalRules::IsMatchOver(4, 3, 9, 9));
	TestEqual(TEXT("Higher red score wins"), TacticalRules::GetMatchWinner(4, 3), static_cast<uint8>(0));
	TestEqual(TEXT("Higher blue score wins"), TacticalRules::GetMatchWinner(2, 5), static_cast<uint8>(1));
	TestEqual(TEXT("Equal score is a match draw"), TacticalRules::GetMatchWinner(4, 4), static_cast<uint8>(255));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalEconomyConstantsTest,
	"Tcamp.Tactical.Rules.EconomyConstants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalEconomyConstantsTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Starting money"), TacticalEconomy::StartingMoney, 1000);
	TestEqual(TEXT("Money cap"), TacticalEconomy::MaxMoney, 9000);
	TestEqual(TEXT("Enemy player kill reward"), TacticalEconomy::KillRewardPlayer, 200);
	TestEqual(TEXT("AI kill reward is disabled"), TacticalEconomy::KillRewardAI, 0);
	TestEqual(TEXT("Round win income"), TacticalEconomy::WinRoundReward, 3000);
	TestEqual(TEXT("Draw income"), TacticalEconomy::DrawRoundReward, 1700);
	TestEqual(TEXT("First loss income"), TacticalEconomy::LossStreakBonus[0], 1700);
	TestEqual(TEXT("Second loss income"), TacticalEconomy::LossStreakBonus[1], 2100);
	TestEqual(TEXT("Third-plus loss income"), TacticalEconomy::LossStreakBonus[2], 2500);

	TestEqual(TEXT("Default pistol price"), ATacticalPlayerState::GetWeaponCost(EWeaponTier::DefaultPistol), 0);
	TestEqual(TEXT("Upgraded pistol price"), ATacticalPlayerState::GetWeaponCost(EWeaponTier::UpgradedPistol), 500);
	TestEqual(TEXT("Rifle price"), ATacticalPlayerState::GetWeaponCost(EWeaponTier::Rifle), 2500);
	TestEqual(TEXT("Sniper price"), ATacticalPlayerState::GetWeaponCost(EWeaponTier::Sniper), 4000);
	TestEqual(TEXT("Light armor price"), ATacticalPlayerState::GetArmorCost(EArmorType::Light), 650);
	TestEqual(TEXT("Heavy armor price"), ATacticalPlayerState::GetArmorCost(EArmorType::Heavy), 1250);
	TestEqual(TEXT("Light armor durability"), ATacticalPlayerState::GetArmorMaxDurability(EArmorType::Light), 100.0f);
	TestEqual(TEXT("Heavy armor durability"), ATacticalPlayerState::GetArmorMaxDurability(EArmorType::Heavy), 200.0f);
	TestEqual(TEXT("Light armor body reduction"), TacticalEconomy::LightArmorBodyReduction, 0.5f);
	TestEqual(TEXT("Light armor has no head protection"), TacticalEconomy::LightArmorHeadReduction, 0.0f);
	TestEqual(TEXT("Heavy armor body reduction"), TacticalEconomy::HeavyArmorBodyReduction, 0.5f);
	TestEqual(TEXT("Heavy armor head reduction"), TacticalEconomy::HeavyArmorHeadReduction, 0.5f);

	int32 Money = 8500;
	int32 LossStreak = 2;
	int32 RoundsWon = 3;
	TacticalRules::ApplyRoundEconomy(TacticalRules::ERoundEconomyOutcome::Win,
		Money, LossStreak, RoundsWon);
	TestEqual(TEXT("Win settlement clamps money"), Money, TacticalEconomy::MaxMoney);
	TestEqual(TEXT("Win settlement clears loss streak"), LossStreak, 0);
	TestEqual(TEXT("Win settlement increments rounds won"), RoundsWon, 4);

	Money = 100;
	LossStreak = 0;
	TacticalRules::ApplyRoundEconomy(TacticalRules::ERoundEconomyOutcome::Loss,
		Money, LossStreak, RoundsWon);
	TestEqual(TEXT("First loss uses the first loss reward"), Money, 1800);
	TestEqual(TEXT("First loss advances the streak"), LossStreak, 1);

	const int32 RoundsBeforeDraw = RoundsWon;
	TacticalRules::ApplyRoundEconomy(TacticalRules::ERoundEconomyOutcome::Draw,
		Money, LossStreak, RoundsWon);
	TestEqual(TEXT("Draw adds draw income"), Money, 3500);
	TestEqual(TEXT("Draw does not change the loss streak"), LossStreak, 1);
	TestEqual(TEXT("Draw does not change rounds won"), RoundsWon, RoundsBeforeDraw);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalInputConfigurationTest,
	"Tcamp.Tactical.Input.Configuration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalInputConfigurationTest::RunTest(const FString& Parameters)
{
	const ATacticalPlayerController* ControllerDefaults = GetDefault<ATacticalPlayerController>();
	TestNotNull(TEXT("Tactical PlayerController defaults exist"), ControllerDefaults);
	if (ControllerDefaults)
	{
		TestTrue(TEXT("PlayerController input and rotation tick every frame"),
			FMath::IsNearlyZero(ControllerDefaults->PrimaryActorTick.TickInterval));
	}

	const UInputSettings* InputSettings = GetDefault<UInputSettings>();
	TestNotNull(TEXT("Input settings exist"), InputSettings);
	if (InputSettings)
	{
		TestFalse(TEXT("Mouse smoothing is disabled"), InputSettings->bEnableMouseSmoothing);
	}

	const IConsoleVariable* RawInputWorker = IConsoleManager::Get().FindConsoleVariable(
		TEXT("WindowsApplication.UseWorkerThreadForRawInput"));
	TestNotNull(TEXT("Raw-input worker CVar exists"), RawInputWorker);
	if (RawInputWorker)
	{
		TestEqual(TEXT("High-rate raw mouse input uses the worker thread"), RawInputWorker->GetInt(), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalSpectatorInformationGateTest,
	"Tcamp.Tactical.Rules.SpectatorInformationGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalSpectatorInformationGateTest::RunTest(const FString& Parameters)
{
	using TacticalRules::CanReceiveTeammateSpectatorData;

	TestTrue(TEXT("A dead player may observe an active living teammate"),
		CanReceiveTeammateSpectatorData(0, true, true, 0, true, true, true, true));
	TestTrue(TEXT("A pawnless reconnect may observe its preserved team"),
		CanReceiveTeammateSpectatorData(1, false, false, 1, true, true, true, true));
	TestFalse(TEXT("A living player cannot request teammate spectator data"),
		CanReceiveTeammateSpectatorData(0, true, false, 0, true, true, true, true));
	TestFalse(TEXT("An unassigned spectator has no trusted team context"),
		CanReceiveTeammateSpectatorData(255, false, false, 0, true, true, true, true));
	TestFalse(TEXT("Enemy state is never exposed through the teammate snapshot"),
		CanReceiveTeammateSpectatorData(0, false, false, 1, true, true, true, true));
	TestFalse(TEXT("An inactive target cannot be observed"),
		CanReceiveTeammateSpectatorData(0, false, false, 0, false, true, true, true));
	TestFalse(TEXT("The viewer cannot select its own PlayerState"),
		CanReceiveTeammateSpectatorData(0, true, true, 0, true, false, true, true));
	TestFalse(TEXT("A stale pawn association is rejected"),
		CanReceiveTeammateSpectatorData(0, true, true, 0, true, true, false, true));
	TestFalse(TEXT("A dead target cannot remain in the snapshot"),
		CanReceiveTeammateSpectatorData(0, true, true, 0, true, true, true, false));

	TestTrue(TEXT("Initial spectator acquisition retries independently of normal cycling"),
		TacticalSpectatorAcquisition::RetryIntervalSeconds >= 0.1f
			&& TacticalSpectatorAcquisition::RetryIntervalSeconds <= 0.2f
			&& TacticalSpectatorAcquisition::MaxRetryAttempts > 0);
	TestTrue(TEXT("An empty target list is deferred during initial acquisition"),
		TacticalSpectatorAcquisition::ShouldDeferEmptyTarget(true, 1));
	TestTrue(TEXT("An empty target list is deferred until the viewer team replicates"),
		TacticalSpectatorAcquisition::ShouldDeferEmptyTarget(false, 255));
	TestFalse(TEXT("A settled empty list for a known team may show no target"),
		TacticalSpectatorAcquisition::ShouldDeferEmptyTarget(false, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalHudLayoutTest,
	"Tcamp.Tactical.UI.TopLeftLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalHudLayoutTest::RunTest(const FString& Parameters)
{
	const float ArmorBottom = TacticalHudLayout::ArmorTop + TacticalHudLayout::ArmorHeight;
	const float HealthBottom = TacticalHudLayout::HealthTop + TacticalHudLayout::HealthHeight;

	TestTrue(TEXT("Armor bar ends above the health bar"), ArmorBottom <= TacticalHudLayout::HealthTop);
	TestTrue(TEXT("Team badge starts below the health bar"), HealthBottom <= TacticalHudLayout::TeamBadgeTop);
	TestEqual(TEXT("Armor and health share a left edge"), TacticalHudLayout::Left, 50.0f);
	TestTrue(TEXT("Armor bar is smaller than the health bar"),
		TacticalHudLayout::ArmorWidth < TacticalHudLayout::HealthWidth
			&& TacticalHudLayout::ArmorHeight < TacticalHudLayout::HealthHeight);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTacticalTreasureRulesTest,
	"Tcamp.Tactical.Rules.Treasure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTacticalTreasureRulesTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("The authored chest has one fixed site"),
		TacticalEconomy::TreasureSiteCount, 1);
	TestEqual(TEXT("Each active chest requires two guards"),
		TacticalEconomy::TreasureGuardCount, 2);
	TestEqual(TEXT("Treasure claim duration is two seconds"),
		TacticalEconomy::TreasureClaimDuration, 2.0f);
	TestEqual(TEXT("Treasure team reward is 500"),
		TacticalEconomy::ChestTeamReward, 500);
	TestEqual(TEXT("Claimant heal is capped by a 40-point grant"),
		TacticalEconomy::ChestClaimantHeal, 40.0f);

	TestFalse(TEXT("No announcement before the 75-second threshold"),
		TacticalRules::ShouldAnnounceTreasure(
			true, true, false, ERoundPhase::Combat, 75.01f));
	TestTrue(TEXT("Crossing 75 seconds announces exactly once"),
		TacticalRules::ShouldAnnounceTreasure(
			true, true, false, ERoundPhase::Combat, 74.8f));
	TestFalse(TEXT("An already-announced chest cannot announce again"),
		TacticalRules::ShouldAnnounceTreasure(
			true, true, true, ERoundPhase::Combat, 70.0f));
	TestFalse(TEXT("A chest cannot activate before 70 seconds"),
		TacticalRules::ShouldActivateTreasure(
			true, true, true, false, ERoundPhase::Combat, 70.01f));
	TestTrue(TEXT("Crossing 70 seconds activates exactly once"),
		TacticalRules::ShouldActivateTreasure(
			true, true, true, false, ERoundPhase::Combat, 69.8f));
	TestFalse(TEXT("Treasure never activates outside Combat"),
		TacticalRules::ShouldActivateTreasure(
			true, true, true, false, ERoundPhase::RoundEnd, 60.0f));

	TestTrue(TEXT("A living active player in range may claim an unlocked chest"),
		TacticalRules::CanClaimTreasure(
			true, true, false, true, true, true, 0,
			ERoundPhase::Combat, 3, 3));
	TestFalse(TEXT("A dead player cannot claim"),
		TacticalRules::CanClaimTreasure(
			true, true, false, false, true, true, 0,
			ERoundPhase::Combat, 3, 3));
	TestFalse(TEXT("A stale previous-round claim is rejected"),
		TacticalRules::CanClaimTreasure(
			true, true, false, true, true, true, 0,
			ERoundPhase::Combat, 2, 3));

	TestTrue(TEXT("A dead but retained current-round roster member remains reward eligible"),
		TacticalRules::IsTreasureRewardEligible(
			true, 1, 1, 4, 4, false));
	TestFalse(TEXT("A mid-round spectator who never entered the round is not rewarded"),
		TacticalRules::IsTreasureRewardEligible(
			true, 1, 1, 3, 4, false));
	TestFalse(TEXT("An abandoned participant is not rewarded"),
		TacticalRules::IsTreasureRewardEligible(
			true, 1, 1, 4, 4, true));
	TestEqual(TEXT("Treasure money clamps to the account maximum"),
		TacticalRules::ApplyTreasureMoneyReward(8800), TacticalEconomy::MaxMoney);
	return true;
}

#endif
