// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TacticalSessionSubsystem.generated.h"

class IOnlineSubsystem;

UENUM(BlueprintType)
enum class ETacticalSessionOperation : uint8
{
	Idle,
	Destroying,
	Creating,
	Finding,
	Joining,
	Traveling,
	Leaving,
	Failed
};

USTRUCT(BlueprintType)
struct FTacticalSessionSearchEntry
{
	GENERATED_BODY()

	/** Stable only for the current search generation. */
	UPROPERTY(BlueprintReadOnly)
	int32 ResultHandle = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly)
	FString ServerName;

	UPROPERTY(BlueprintReadOnly)
	FString OwnerName;

	UPROPERTY(BlueprintReadOnly)
	int32 CurrentPlayers = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 MaxPlayers = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 PingMs = 0;

	UPROPERTY(BlueprintReadOnly)
	bool bCompatible = false;

	UPROPERTY(BlueprintReadOnly)
	bool bJoinable = false;

	UPROPERTY(BlueprintReadOnly)
	bool bMatchInProgress = false;
};

DECLARE_MULTICAST_DELEGATE_ThreeParams(
	FOnTacticalSessionStatusChanged,
	ETacticalSessionOperation,
	const FText&,
	bool /* bBusy */);
DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnTacticalSessionResultsChanged,
	const TArray<FTacticalSessionSearchEntry>&);
DECLARE_DELEGATE_TwoParams(
	FOnTacticalReconnectTargetResolved,
	bool /* bSuccess */,
	const FString& /* ConnectString */);
DECLARE_DELEGATE(FOnTacticalSessionCleanupComplete);

/**
 * Single owner for the legacy OnlineSubsystem Session lifecycle.
 *
 * UI and gameplay never retain raw OSS search results or delegate handles.
 * Every async operation is fenced by a generation so a late callback cannot
 * travel after Leave, a newer search, or subsystem shutdown.
 */
UCLASS()
class TCAMP_FPS_API UTacticalSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxActivePlayers = 6;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	bool HostSession(const FString& RequestedServerName = FString());
	bool FindSessions();
	bool JoinSessionByHandle(int32 ResultHandle);
	bool ShowInviteUI();

	/** Destroy/leave the local named session, then invoke Completion once. */
	void CleanupSession(FOnTacticalSessionCleanupComplete Completion = {});

	/**
	 * Resolve the current joined session for a reconnect. If the local named
	 * session was lost, the immutable last joined search result is rejoined.
	 */
	void ResolveReconnectTarget(FOnTacticalReconnectTargetResolved Completion);

	/** Host-only publication hook called by the authoritative match flow. */
	void SetGameplayMatchInProgress(bool bInProgress);
	/** Publish a fail-closed CLOSING state before a listen host destroys its room. */
	void PrepareHostSessionShutdown(FOnTacticalSessionCleanupComplete Completion);
	/** Fence a timed-out destroy callback so the frontend cannot remain permanently busy. */
	void ForceResetAfterCleanupTimeout(const FText& FrontendNotice);

	/** Rebind world-aware OSS delegates after map travel. */
	void RefreshOnlineBindings();
	/** Mark the host's post-match ServerTravel so travel failure is recoverable. */
	void NotifyHostRestartTravelStarted();
	/** Mark a server-directed client travel without changing reconnect ownership. */
	void NotifyClientServerTravelStarted();
	void NotifyTravelComplete();
	void PublishFrontendNotice(const FText& Notice);

	bool HasNamedSession() const;
	bool HasReconnectTarget() const;
	bool IsSessionHost() const { return bHostingSession; }
	bool IsSteamSubsystemActive() const;
	bool IsBusy() const;
	ETacticalSessionOperation GetOperation() const { return Operation; }
	const FText& GetStatusText() const { return StatusText; }
	const TArray<FTacticalSessionSearchEntry>& GetSearchEntries() const
	{
		return SearchEntries;
	}
	FString GetOnlineIdentityLabel() const;

	FOnTacticalSessionStatusChanged OnStatusChanged;
	FOnTacticalSessionResultsChanged OnResultsChanged;

private:
	enum class EPostDestroyAction : uint8
	{
		None,
		Create,
		Join,
		Cleanup,
		ReconnectJoin
	};

	enum class ETimedOperation : uint8
	{
		None,
		Create,
		Find,
		Join,
		ReconnectJoin,
		Destroy,
		MatchState
	};

	IOnlineSubsystem* GetOnlineSubsystem() const;
	IOnlineSessionPtr GetSessionInterface() const;
	void BindGlobalDelegates(const IOnlineSessionPtr& Sessions);
	void ClearGlobalDelegates();
	void ClearOperationDelegates();
	void CancelSearchIfNeeded();
	void ArmOperationTimeout(
		ETimedOperation TimedOperation,
		uint64 Generation,
		float TimeoutSeconds = 25.0f);
	void CancelOperationTimeout();
	bool HandleOperationTimeout(float DeltaTime);

	void SetStatus(ETacticalSessionOperation NewOperation, const FText& NewStatus);
	void FailOperation(const FText& FailureText);
	void BeginDestroySession(EPostDestroyAction Action);
	void ContinueAfterDestroy(EPostDestroyAction Action, bool bDestroySucceeded);
	void BeginCreate();
	void BeginJoin(const FOnlineSessionSearchResult& SearchResult, bool bReconnect);
	void BeginMatchStateTransition(bool bInProgress);
	void BeginMatchStateSettingsUpdate(bool bInProgress);
	void BeginMatchSessionStart();
	void FinishMatchStateTransition(bool bSuccess);
	bool IsMatchStatePublished(bool bInProgress) const;
	void ScheduleMatchStateRetry(bool bInProgress);
	void ScheduleMatchStateConvergencePoll(bool bInProgress);
	void CancelMatchStateRetry();
	bool HandleMatchStateRetry(float DeltaTime);
	void TravelHostToGameplay();
	bool ResolveNamedSessionConnectString(FString& OutConnectString) const;
	void TravelClientToSession(const FString& ConnectString);
	bool IsResultCompatible(const FOnlineSessionSearchResult& Result) const;
	FString BuildDefaultServerName() const;

	void HandleInviteAccepted(
		bool bWasSuccessful,
		int32 ControllerId,
		FUniqueNetIdPtr UserId,
		const FOnlineSessionSearchResult& InviteResult);
	void HandleSessionFailure(const FUniqueNetId& UserId, ESessionFailure::Type FailureType);
	void HandleDestroySessionRequested(int32 LocalUserNum, FName SessionName);

	ETacticalSessionOperation Operation = ETacticalSessionOperation::Idle;
	FText StatusText;
	uint64 OperationGeneration = 0;
	int32 SearchGeneration = 0;
	bool bHostingSession = false;
	bool bMatchStateTransitionInFlight = false;
	bool bMatchStateTransitionTarget = false;
	bool bHasQueuedMatchState = false;
	bool bQueuedMatchState = false;
	bool bMatchStateRetryTarget = false;
	int32 MatchStateRetryCount = 0;
	double MatchStateTransitionDeadlineSeconds = 0.0;

	FString PendingServerName;
	FOnlineSessionSearchResult PendingJoinResult;
	FOnlineSessionSearchResult LastJoinedSearchResult;
	TSharedPtr<FOnlineSessionSearch> ActiveSearch;
	TArray<FTacticalSessionSearchEntry> SearchEntries;

	EPostDestroyAction PostDestroyAction = EPostDestroyAction::None;
	FOnTacticalReconnectTargetResolved ReconnectCompletion;
	FOnTacticalSessionCleanupComplete CleanupCompletion;
	FOnTacticalSessionCleanupComplete HostShutdownPreparationCompletion;

	IOnlineSessionPtr BoundSessionInterface;
	IOnlineSessionPtr OperationSessionInterface;
	ETimedOperation ActiveTimedOperation = ETimedOperation::None;
	uint64 TimedOperationGeneration = 0;
	FTSTicker::FDelegateHandle OperationTimeoutTickerHandle;
	FTSTicker::FDelegateHandle MatchStateRetryTickerHandle;
	FDelegateHandle CreateDelegateHandle;
	FDelegateHandle FindDelegateHandle;
	FDelegateHandle JoinDelegateHandle;
	FDelegateHandle DestroyDelegateHandle;
	FDelegateHandle UpdateDelegateHandle;
	FDelegateHandle StartDelegateHandle;
	FDelegateHandle EndDelegateHandle;
	FDelegateHandle InviteAcceptedDelegateHandle;
	FDelegateHandle SessionFailureDelegateHandle;
	FDelegateHandle DestroyRequestedDelegateHandle;
};
