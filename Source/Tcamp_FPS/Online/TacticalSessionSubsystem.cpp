// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/TacticalSessionSubsystem.h"

#include "TacticalGameInstance.h"
#include "Variant_Shooter/Network/TacticalNetworkAdmission.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogTacticalSession, Log, All);

namespace TacticalSession
{
	const FName ProtocolSetting(TEXT("TACTICAL_PROTOCOL"));
	const FName ProductSetting(TEXT("TACTICAL_PRODUCT"));
	const FName ServerNameSetting(TEXT("TACTICAL_SERVER_NAME"));
	const FName MatchStateSetting(TEXT("TACTICAL_MATCH_STATE"));
	const FString ProductId(TEXT("TCAMP_FPS"));
	const FString InitializingState(TEXT("INITIALIZING"));
	const FString LobbyState(TEXT("LOBBY"));
	const FString InProgressState(TEXT("IN_PROGRESS"));
	const FString ClosingState(TEXT("CLOSING"));
	const FName SteamSubsystemName(TEXT("STEAM"));
	const FName NullSubsystemName(TEXT("NULL"));
	const FName GameplayMap(
		TEXT("/Game/FirstPerson/Lvl_FirstPerson"));
	constexpr int32 MaxSearchResults = 50;
	constexpr float AsyncOperationTimeoutSeconds = 25.0f;
	constexpr int32 MaxMatchStateRetries = 2;
	constexpr float MatchStateRetryDelaySeconds = 0.75f;
	constexpr float MatchStateConvergencePollSeconds = 0.50f;
	constexpr double MatchStateTransitionBudgetSeconds = 20.0;
	constexpr float DestroyOperationTimeoutSeconds = 12.0f;
	constexpr float MatchStateOperationTimeoutSeconds = 5.0f;
}

void UTacticalSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	StatusText = NSLOCTEXT(
		"TacticalSession", "InitialStatus", "选择创建房间或搜索房间");
}

void UTacticalSessionSubsystem::Deinitialize()
{
	++OperationGeneration;
	CancelMatchStateRetry();
	CancelSearchIfNeeded();
	ClearOperationDelegates();
	ClearGlobalDelegates();
	ReconnectCompletion.Unbind();
	CleanupCompletion.Unbind();
	HostShutdownPreparationCompletion.Unbind();
	ActiveSearch.Reset();
	SearchEntries.Reset();
	Super::Deinitialize();
}

IOnlineSubsystem* UTacticalSessionSubsystem::GetOnlineSubsystem() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

#if WITH_EDITOR
	// A Steam OSS instance is a process singleton, so it cannot represent
	// several PIE clients signed into distinct accounts. Always use OSS Null in
	// editor worlds; packaged Development/Shipping builds use Steam.
	if (GIsEditor)
	{
		IOnlineSubsystem* NullSubsystem =
			Online::GetSubsystem(World, TacticalSession::NullSubsystemName);
		return NullSubsystem && NullSubsystem->IsEnabled()
			? NullSubsystem : nullptr;
	}
#endif

	IOnlineSubsystem* Subsystem = Online::GetSubsystem(World);
#if !UE_BUILD_SHIPPING
	// Steam intentionally does not initialize its socket driver inside the
	// editor. Keep PIE automation and local multi-window work on OSS Null.
	if (!Subsystem || !Subsystem->IsEnabled())
	{
		Subsystem = Online::GetSubsystem(World, TacticalSession::NullSubsystemName);
	}
#endif
	if (!Subsystem || !Subsystem->IsEnabled())
	{
		return nullptr;
	}
#if UE_BUILD_SHIPPING
	// The release admission path requires a provider-qualified Steam identity.
	// Never make a Shipping build appear usable by silently falling back to the
	// unauthenticated Null subsystem when Steam initialization failed.
	if (Subsystem->GetSubsystemName() != TacticalSession::SteamSubsystemName)
	{
		return nullptr;
	}
#endif
	return Subsystem;
}

IOnlineSessionPtr UTacticalSessionSubsystem::GetSessionInterface() const
{
	IOnlineSubsystem* Subsystem = GetOnlineSubsystem();
	return Subsystem ? Subsystem->GetSessionInterface() : nullptr;
}

void UTacticalSessionSubsystem::RefreshOnlineBindings()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		ClearGlobalDelegates();
		return;
	}
	BindGlobalDelegates(Sessions);
}

void UTacticalSessionSubsystem::NotifyTravelComplete()
{
	RefreshOnlineBindings();
	if (Operation == ETacticalSessionOperation::Traveling)
	{
		SetStatus(ETacticalSessionOperation::Idle,
			NSLOCTEXT("TacticalSession", "TravelComplete", "已进入在线房间。"));
	}
}

void UTacticalSessionSubsystem::NotifyHostRestartTravelStarted()
{
	if (bHostingSession)
	{
		SetStatus(ETacticalSessionOperation::Traveling,
			NSLOCTEXT("TacticalSession", "RestartTravel",
				"正在为下一场比赛重载地图……"));
	}
}

void UTacticalSessionSubsystem::NotifyClientServerTravelStarted()
{
	if (!bHostingSession && HasNamedSession()
		&& (Operation == ETacticalSessionOperation::Idle
			|| Operation == ETacticalSessionOperation::Failed))
	{
		SetStatus(ETacticalSessionOperation::Traveling,
			NSLOCTEXT("TacticalSession", "ClientServerTravel",
				"房主正在切换地图……"));
	}
}

void UTacticalSessionSubsystem::PublishFrontendNotice(const FText& Notice)
{
	SetStatus(ETacticalSessionOperation::Idle, Notice);
}

void UTacticalSessionSubsystem::BindGlobalDelegates(const IOnlineSessionPtr& Sessions)
{
	if (BoundSessionInterface == Sessions && InviteAcceptedDelegateHandle.IsValid()
		&& SessionFailureDelegateHandle.IsValid()
		&& DestroyRequestedDelegateHandle.IsValid())
	{
		return;
	}

	ClearGlobalDelegates();
	BoundSessionInterface = Sessions;
	InviteAcceptedDelegateHandle = Sessions->AddOnSessionUserInviteAcceptedDelegate_Handle(
		FOnSessionUserInviteAcceptedDelegate::CreateUObject(
			this, &ThisClass::HandleInviteAccepted));
	SessionFailureDelegateHandle = Sessions->AddOnSessionFailureDelegate_Handle(
		FOnSessionFailureDelegate::CreateUObject(
			this, &ThisClass::HandleSessionFailure));
	DestroyRequestedDelegateHandle = Sessions->AddOnDestroySessionRequestedDelegate_Handle(
		FOnDestroySessionRequestedDelegate::CreateUObject(
			this, &ThisClass::HandleDestroySessionRequested));
}

void UTacticalSessionSubsystem::ClearGlobalDelegates()
{
	if (BoundSessionInterface.IsValid())
	{
		if (InviteAcceptedDelegateHandle.IsValid())
		{
			BoundSessionInterface->ClearOnSessionUserInviteAcceptedDelegate_Handle(
				InviteAcceptedDelegateHandle);
		}
		if (SessionFailureDelegateHandle.IsValid())
		{
			BoundSessionInterface->ClearOnSessionFailureDelegate_Handle(
				SessionFailureDelegateHandle);
		}
		if (DestroyRequestedDelegateHandle.IsValid())
		{
			BoundSessionInterface->ClearOnDestroySessionRequestedDelegate_Handle(
				DestroyRequestedDelegateHandle);
		}
	}
	InviteAcceptedDelegateHandle.Reset();
	SessionFailureDelegateHandle.Reset();
	DestroyRequestedDelegateHandle.Reset();
	BoundSessionInterface.Reset();
}

void UTacticalSessionSubsystem::ClearOperationDelegates()
{
	CancelOperationTimeout();
	CancelMatchStateRetry();
	// Delegate handles must be cleared on the same interface instance that
	// created them. World-aware OSS lookup can return a different instance across
	// hard travel (especially PIE), so retain the exact operation owner.
	const IOnlineSessionPtr Sessions = OperationSessionInterface.IsValid()
		? OperationSessionInterface
		: GetSessionInterface();
	if (Sessions.IsValid())
	{
		if (CreateDelegateHandle.IsValid())
		{
			Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateDelegateHandle);
		}
		if (FindDelegateHandle.IsValid())
		{
			Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindDelegateHandle);
		}
		if (JoinDelegateHandle.IsValid())
		{
			Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinDelegateHandle);
		}
		if (DestroyDelegateHandle.IsValid())
		{
			Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyDelegateHandle);
		}
		if (UpdateDelegateHandle.IsValid())
		{
			Sessions->ClearOnUpdateSessionCompleteDelegate_Handle(UpdateDelegateHandle);
		}
		if (StartDelegateHandle.IsValid())
		{
			Sessions->ClearOnStartSessionCompleteDelegate_Handle(StartDelegateHandle);
		}
		if (EndDelegateHandle.IsValid())
		{
			Sessions->ClearOnEndSessionCompleteDelegate_Handle(EndDelegateHandle);
		}
	}
	CreateDelegateHandle.Reset();
	FindDelegateHandle.Reset();
	JoinDelegateHandle.Reset();
	DestroyDelegateHandle.Reset();
	UpdateDelegateHandle.Reset();
	StartDelegateHandle.Reset();
	EndDelegateHandle.Reset();
	OperationSessionInterface.Reset();
}

void UTacticalSessionSubsystem::ArmOperationTimeout(
	ETimedOperation TimedOperation,
	uint64 Generation,
	float TimeoutSeconds)
{
	CancelOperationTimeout();
	ActiveTimedOperation = TimedOperation;
	TimedOperationGeneration = Generation;
	OperationTimeoutTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(
			this, &ThisClass::HandleOperationTimeout),
		FMath::Max(0.1f, TimeoutSeconds));
}

void UTacticalSessionSubsystem::CancelOperationTimeout()
{
	if (OperationTimeoutTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(OperationTimeoutTickerHandle);
		OperationTimeoutTickerHandle.Reset();
	}
	ActiveTimedOperation = ETimedOperation::None;
	TimedOperationGeneration = 0;
}

bool UTacticalSessionSubsystem::HandleOperationTimeout(float DeltaTime)
{
	OperationTimeoutTickerHandle.Reset();
	const ETimedOperation TimedOutOperation = ActiveTimedOperation;
	const uint64 TimedOutGeneration = TimedOperationGeneration;
	ActiveTimedOperation = ETimedOperation::None;
	TimedOperationGeneration = 0;

	if (TimedOutOperation == ETimedOperation::None
		|| TimedOutGeneration != OperationGeneration)
	{
		return false;
	}

	const IOnlineSessionPtr TimedOutSessions = OperationSessionInterface;
	const EPostDestroyAction TimedOutPostDestroyAction = PostDestroyAction;
	++OperationGeneration;
	if (TimedOutOperation == ETimedOperation::Find && TimedOutSessions.IsValid())
	{
		TimedOutSessions->CancelFindSessions();
	}
	ClearOperationDelegates();
	PostDestroyAction = EPostDestroyAction::None;
	if (TimedOutOperation == ETimedOperation::MatchState)
	{
		UE_LOG(LogTacticalSession, Warning,
			TEXT("Event=SessionMatchStateTimedOut Generation=%llu"),
			TimedOutGeneration);
		FinishMatchStateTransition(false);
		return false;
	}

	const bool bReconnect = TimedOutOperation == ETimedOperation::ReconnectJoin
		|| (TimedOutOperation == ETimedOperation::Destroy
			&& TimedOutPostDestroyAction == EPostDestroyAction::ReconnectJoin);
	const bool bCleanupDestroy = TimedOutOperation == ETimedOperation::Destroy
		&& TimedOutPostDestroyAction == EPostDestroyAction::Cleanup;
	if (bReconnect)
	{
		ReconnectCompletion.ExecuteIfBound(false, FString());
		ReconnectCompletion.Unbind();
	}
	else if (TimedOutOperation == ETimedOperation::Find)
	{
		ActiveSearch.Reset();
		SearchEntries.Reset();
		OnResultsChanged.Broadcast(SearchEntries);
	}
	if (bCleanupDestroy)
	{
		bHostingSession = false;
		SetStatus(ETacticalSessionOperation::Idle,
			NSLOCTEXT("TacticalSession", "CleanupTimedOut",
				"在线房间清理超时，已返回主菜单。"));
		CleanupCompletion.ExecuteIfBound();
		CleanupCompletion.Unbind();
		UE_LOG(LogTacticalSession, Warning,
			TEXT("Event=SessionOperationTimedOut Type=CleanupDestroy Generation=%llu"),
			TimedOutGeneration);
		return false;
	}

	// A provider can report neither completion nor immediate failure while it is
	// losing connectivity. Best-effort cleanup prevents a late Create/Join from
	// leaving a hidden local named session behind after the UI becomes usable.
	if (TimedOutSessions.IsValid()
		&& TimedOutOperation != ETimedOperation::Find
		&& TimedOutOperation != ETimedOperation::Destroy
		&& TimedOutSessions->GetNamedSession(NAME_GameSession))
	{
		TimedOutSessions->DestroySession(NAME_GameSession);
	}

	SetStatus(ETacticalSessionOperation::Failed,
		bReconnect
			? NSLOCTEXT("TacticalSession", "ReconnectJoinTimedOut",
				"重新加入 Steam 房间超时，将按重连规则继续尝试。")
			: TimedOutOperation == ETimedOperation::Create
				? NSLOCTEXT("TacticalSession", "CreateTimedOut",
					"创建 Steam 房间超时，请检查网络后重试。")
				: TimedOutOperation == ETimedOperation::Find
					? NSLOCTEXT("TacticalSession", "FindTimedOut",
						"搜索 Steam 房间超时，请检查网络后重试。")
					: TimedOutOperation == ETimedOperation::Destroy
						? NSLOCTEXT("TacticalSession", "DestroyTimedOut",
							"清理旧 Steam 房间超时，请稍后重试。")
						: NSLOCTEXT("TacticalSession", "JoinTimedOut",
							"加入 Steam 房间超时，请刷新房间后重试。"));
	UE_LOG(LogTacticalSession, Warning,
		TEXT("Event=SessionOperationTimedOut Type=%d Generation=%llu"),
		static_cast<int32>(TimedOutOperation), TimedOutGeneration);
	return false;
}

void UTacticalSessionSubsystem::CancelSearchIfNeeded()
{
	if (ActiveSearch.IsValid() && ActiveSearch->SearchState == EOnlineAsyncTaskState::InProgress)
	{
		const IOnlineSessionPtr Sessions = OperationSessionInterface.IsValid()
			? OperationSessionInterface
			: GetSessionInterface();
		if (Sessions.IsValid())
		{
			Sessions->CancelFindSessions();
		}
	}
}

bool UTacticalSessionSubsystem::IsBusy() const
{
	return bMatchStateTransitionInFlight
		|| MatchStateRetryTickerHandle.IsValid()
		|| (Operation != ETacticalSessionOperation::Idle
			&& Operation != ETacticalSessionOperation::Failed);
}

void UTacticalSessionSubsystem::SetStatus(
	ETacticalSessionOperation NewOperation,
	const FText& NewStatus)
{
	Operation = NewOperation;
	StatusText = NewStatus;
	OnStatusChanged.Broadcast(Operation, StatusText, IsBusy());
}

void UTacticalSessionSubsystem::FailOperation(const FText& FailureText)
{
	ClearOperationDelegates();
	PostDestroyAction = EPostDestroyAction::None;
	SetStatus(ETacticalSessionOperation::Failed, FailureText);
	UE_LOG(LogTacticalSession, Warning, TEXT("Event=SessionOperationFailed Message=\"%s\""),
		*FailureText.ToString());
}

bool UTacticalSessionSubsystem::HostSession(const FString& RequestedServerName)
{
	if (IsBusy())
	{
		return false;
	}

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		FailOperation(NSLOCTEXT(
			"TacticalSession", "OnlineUnavailableHost",
			"Steam 服务不可用。请确认 Steam 已启动并登录后重试。"));
		return false;
	}
	BindGlobalDelegates(Sessions);
	++OperationGeneration;
	ClearOperationDelegates();
	PendingServerName = RequestedServerName.TrimStartAndEnd();
	if (PendingServerName.IsEmpty())
	{
		PendingServerName = BuildDefaultServerName();
	}
	bHostingSession = false;
	bMatchStateTransitionInFlight = false;
	bHasQueuedMatchState = false;
	MatchStateRetryCount = 0;
	MatchStateTransitionDeadlineSeconds = 0.0;

	if (Sessions->GetNamedSession(NAME_GameSession))
	{
		BeginDestroySession(EPostDestroyAction::Create);
	}
	else
	{
		BeginCreate();
	}
	return true;
}

void UTacticalSessionSubsystem::BeginCreate()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	IOnlineSubsystem* Subsystem = GetOnlineSubsystem();
	if (!Sessions.IsValid() || !Subsystem)
	{
		FailOperation(NSLOCTEXT(
			"TacticalSession", "CreateUnavailable", "无法访问在线会话服务。"));
		return;
	}

	FOnlineSessionSettings Settings;
	const bool bSteam = Subsystem->GetSubsystemName() == TacticalSession::SteamSubsystemName;
	Settings.bIsLANMatch = !bSteam;
	Settings.NumPublicConnections = MaxActivePlayers;
	Settings.NumPrivateConnections = 0;
	// Create fail-closed. The lobby is published only after gameplay BeginPlay
	// confirms that the listen world and its net driver are ready.
	Settings.bShouldAdvertise = false;
	Settings.bAllowJoinInProgress = false;
	Settings.bAllowInvites = false;
	Settings.bUsesPresence = true;
	Settings.bAllowJoinViaPresence = false;
	Settings.bAllowJoinViaPresenceFriendsOnly = false;
	Settings.bUseLobbiesIfAvailable = true;
	Settings.bUseLobbiesVoiceChatIfAvailable = false;
	Settings.Set(SETTING_MAPNAME, TacticalSession::GameplayMap.ToString(),
		EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(TacticalSession::ProtocolSetting,
		TacticalNetworkAdmission::ProtocolVersion,
		EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(TacticalSession::ProductSetting, TacticalSession::ProductId,
		EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(TacticalSession::ServerNameSetting, PendingServerName,
		EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(TacticalSession::MatchStateSetting, TacticalSession::InitializingState,
		EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	OperationSessionInterface = Sessions;
	const uint64 Generation = OperationGeneration;
	CreateDelegateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateWeakLambda(
			this, [this, Generation](FName SessionName, bool bWasSuccessful)
			{
				if (Generation != OperationGeneration || SessionName != NAME_GameSession)
				{
					return;
				}
				CancelOperationTimeout();
				if (OperationSessionInterface.IsValid()
					&& CreateDelegateHandle.IsValid())
				{
					OperationSessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(
						CreateDelegateHandle);
				}
				CreateDelegateHandle.Reset();
				if (!bWasSuccessful)
				{
					FailOperation(NSLOCTEXT(
						"TacticalSession", "CreateFailed", "创建 Steam 房间失败。"));
					return;
				}
				bHostingSession = true;
				SetStatus(ETacticalSessionOperation::Traveling,
					NSLOCTEXT("TacticalSession", "HostTravel", "房间已创建，正在进入对局……"));
				TravelHostToGameplay();
			}));

	SetStatus(ETacticalSessionOperation::Creating,
		NSLOCTEXT("TacticalSession", "Creating", "正在创建最多 6 人的 Steam 房间……"));
	ArmOperationTimeout(ETimedOperation::Create, Generation);
	if (!Sessions->CreateSession(0, NAME_GameSession, Settings))
	{
		CancelOperationTimeout();
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateDelegateHandle);
		CreateDelegateHandle.Reset();
		FailOperation(NSLOCTEXT(
			"TacticalSession", "CreateRequestFailed", "无法提交创建房间请求。"));
	}
}

bool UTacticalSessionSubsystem::FindSessions()
{
	if (IsBusy())
	{
		return false;
	}
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	IOnlineSubsystem* Subsystem = GetOnlineSubsystem();
	if (!Sessions.IsValid() || !Subsystem)
	{
		FailOperation(NSLOCTEXT(
			"TacticalSession", "OnlineUnavailableFind",
			"Steam 服务不可用，无法搜索房间。"));
		return false;
	}
	BindGlobalDelegates(Sessions);
	++OperationGeneration;
	++SearchGeneration;
	ClearOperationDelegates();
	SearchEntries.Reset();
	OnResultsChanged.Broadcast(SearchEntries);

	ActiveSearch = MakeShared<FOnlineSessionSearch>();
	ActiveSearch->MaxSearchResults = TacticalSession::MaxSearchResults;
	ActiveSearch->bIsLanQuery =
		Subsystem->GetSubsystemName() != TacticalSession::SteamSubsystemName;
	ActiveSearch->QuerySettings.Set(
		SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
	ActiveSearch->QuerySettings.Set(
		TacticalSession::ProtocolSetting,
		TacticalNetworkAdmission::ProtocolVersion,
		EOnlineComparisonOp::Equals);
	ActiveSearch->QuerySettings.Set(
		TacticalSession::ProductSetting,
		TacticalSession::ProductId,
		EOnlineComparisonOp::Equals);

	OperationSessionInterface = Sessions;
	const uint64 Generation = OperationGeneration;
	const int32 ThisSearchGeneration = SearchGeneration;
	FindDelegateHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateWeakLambda(
			this, [this, Generation, ThisSearchGeneration](bool bWasSuccessful)
			{
				if (Generation != OperationGeneration
					|| ThisSearchGeneration != SearchGeneration)
				{
					return;
				}
				CancelOperationTimeout();
				if (OperationSessionInterface.IsValid()
					&& FindDelegateHandle.IsValid())
				{
					OperationSessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(
						FindDelegateHandle);
				}
				FindDelegateHandle.Reset();
				SearchEntries.Reset();
				if (!bWasSuccessful || !ActiveSearch.IsValid())
				{
					FailOperation(NSLOCTEXT(
						"TacticalSession", "FindFailed", "搜索 Steam 房间失败。"));
					OnResultsChanged.Broadcast(SearchEntries);
					return;
				}

				for (int32 RawIndex = 0;
					RawIndex < ActiveSearch->SearchResults.Num(); ++RawIndex)
				{
					const FOnlineSessionSearchResult& Result =
						ActiveSearch->SearchResults[RawIndex];
					FTacticalSessionSearchEntry& Entry = SearchEntries.AddDefaulted_GetRef();
					Entry.ResultHandle = RawIndex;
					Result.Session.SessionSettings.Get(
						TacticalSession::ServerNameSetting, Entry.ServerName);
					if (Entry.ServerName.IsEmpty())
					{
						Entry.ServerName = Result.Session.OwningUserName;
					}
					Entry.OwnerName = Result.Session.OwningUserName;
					Entry.MaxPlayers = Result.Session.SessionSettings.NumPublicConnections;
					Entry.CurrentPlayers = FMath::Max(
						0, Entry.MaxPlayers - Result.Session.NumOpenPublicConnections);
					Entry.PingMs = FMath::Max(0, Result.PingInMs);
					Entry.bCompatible = IsResultCompatible(Result);
					FString MatchState;
					Result.Session.SessionSettings.Get(
						TacticalSession::MatchStateSetting, MatchState);
					Entry.bMatchInProgress =
						MatchState == TacticalSession::InProgressState;
					Entry.bJoinable = Entry.bCompatible
						&& MatchState == TacticalSession::LobbyState
						&& Result.Session.NumOpenPublicConnections > 0;
				}
				OnResultsChanged.Broadcast(SearchEntries);
				SetStatus(ETacticalSessionOperation::Idle,
					SearchEntries.IsEmpty()
						? NSLOCTEXT("TacticalSession", "NoResults", "没有找到可加入的房间。")
						: FText::Format(
							NSLOCTEXT("TacticalSession", "ResultsFound", "找到 {0} 个房间。"),
							FText::AsNumber(SearchEntries.Num())));
			}));

	SetStatus(ETacticalSessionOperation::Finding,
		NSLOCTEXT("TacticalSession", "Finding", "正在搜索 Steam 房间……"));
	ArmOperationTimeout(ETimedOperation::Find, Generation);
	if (!Sessions->FindSessions(0, ActiveSearch.ToSharedRef()))
	{
		CancelOperationTimeout();
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindDelegateHandle);
		FindDelegateHandle.Reset();
		FailOperation(NSLOCTEXT(
			"TacticalSession", "FindRequestFailed", "无法提交搜索房间请求。"));
		return false;
	}
	return true;
}

bool UTacticalSessionSubsystem::JoinSessionByHandle(int32 ResultHandle)
{
	if (IsBusy() || !ActiveSearch.IsValid()
		|| !ActiveSearch->SearchResults.IsValidIndex(ResultHandle))
	{
		return false;
	}
	const FOnlineSessionSearchResult Result =
		ActiveSearch->SearchResults[ResultHandle];
	if (!IsResultCompatible(Result)
		|| Result.Session.NumOpenPublicConnections <= 0)
	{
		FailOperation(NSLOCTEXT(
			"TacticalSession", "JoinIncompatible", "该房间已满、已开局或版本不兼容。"));
		return false;
	}

	const FString MatchState = [&Result]()
	{
		FString Value;
		Result.Session.SessionSettings.Get(TacticalSession::MatchStateSetting, Value);
		return Value;
	}();
	if (MatchState != TacticalSession::LobbyState)
	{
		FailOperation(NSLOCTEXT(
			"TacticalSession", "JoinNotOpen", "该房间当前未开放加入。"));
		return false;
	}

	++OperationGeneration;
	ClearOperationDelegates();
	PendingJoinResult = Result;
	bHostingSession = false;
	if (const IOnlineSessionPtr Sessions = GetSessionInterface();
		Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession))
	{
		BeginDestroySession(EPostDestroyAction::Join);
	}
	else
	{
		BeginJoin(PendingJoinResult, false);
	}
	return true;
}

void UTacticalSessionSubsystem::BeginJoin(
	const FOnlineSessionSearchResult& SearchResult,
	bool bReconnect)
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid() || !SearchResult.IsValid())
	{
		if (bReconnect)
		{
			ReconnectCompletion.ExecuteIfBound(false, FString());
			ReconnectCompletion.Unbind();
		}
		FailOperation(NSLOCTEXT(
			"TacticalSession", "JoinUnavailable", "房间信息已失效，请重新搜索。"));
		return;
	}
	BindGlobalDelegates(Sessions);
	OperationSessionInterface = Sessions;
	const uint64 Generation = OperationGeneration;
	JoinDelegateHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateWeakLambda(
			this, [this, Generation, bReconnect](FName SessionName,
				EOnJoinSessionCompleteResult::Type Result)
			{
				if (Generation != OperationGeneration || SessionName != NAME_GameSession)
				{
					return;
				}
				CancelOperationTimeout();
				if (OperationSessionInterface.IsValid()
					&& JoinDelegateHandle.IsValid())
				{
					OperationSessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(
						JoinDelegateHandle);
				}
				JoinDelegateHandle.Reset();
				if (Result != EOnJoinSessionCompleteResult::Success
					&& Result != EOnJoinSessionCompleteResult::AlreadyInSession)
				{
					if (bReconnect)
					{
						ReconnectCompletion.ExecuteIfBound(false, FString());
						ReconnectCompletion.Unbind();
					}
					FailOperation(FText::Format(
						NSLOCTEXT("TacticalSession", "JoinFailed", "加入房间失败：{0}"),
						FText::FromString(LexToString(Result))));
					return;
				}

				FString ConnectString;
				if (!ResolveNamedSessionConnectString(ConnectString))
				{
					if (bReconnect)
					{
						ReconnectCompletion.ExecuteIfBound(false, FString());
						ReconnectCompletion.Unbind();
					}
					FailOperation(NSLOCTEXT(
						"TacticalSession", "ResolveFailed", "无法解析房主连接地址。"));
					return;
				}
				LastJoinedSearchResult = PendingJoinResult;
				bHostingSession = false;
				if (bReconnect)
				{
					SetStatus(ETacticalSessionOperation::Idle,
						NSLOCTEXT("TacticalSession", "ReconnectResolved", "已重新解析 Steam 房间。"));
					ReconnectCompletion.ExecuteIfBound(true, ConnectString);
					ReconnectCompletion.Unbind();
					return;
				}
				SetStatus(ETacticalSessionOperation::Traveling,
					NSLOCTEXT("TacticalSession", "JoinTravel", "已加入房间，正在连接房主……"));
				TravelClientToSession(ConnectString);
			}));

	SetStatus(ETacticalSessionOperation::Joining,
		bReconnect
			? NSLOCTEXT("TacticalSession", "Rejoining", "正在重新加入 Steam 房间……")
			: NSLOCTEXT("TacticalSession", "Joining", "正在加入 Steam 房间……"));
	ArmOperationTimeout(
		bReconnect ? ETimedOperation::ReconnectJoin : ETimedOperation::Join,
		Generation);
	if (!Sessions->JoinSession(0, NAME_GameSession, SearchResult))
	{
		CancelOperationTimeout();
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinDelegateHandle);
		JoinDelegateHandle.Reset();
		if (bReconnect)
		{
			ReconnectCompletion.ExecuteIfBound(false, FString());
			ReconnectCompletion.Unbind();
		}
		FailOperation(NSLOCTEXT(
			"TacticalSession", "JoinRequestFailed", "无法提交加入房间请求。"));
	}
}

void UTacticalSessionSubsystem::BeginDestroySession(EPostDestroyAction Action)
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid() || !Sessions->GetNamedSession(NAME_GameSession))
	{
		ContinueAfterDestroy(Action, true);
		return;
	}
	PostDestroyAction = Action;
	OperationSessionInterface = Sessions;
	const uint64 Generation = OperationGeneration;
	DestroyDelegateHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateWeakLambda(
			this, [this, Generation](FName SessionName, bool bWasSuccessful)
			{
				if (Generation != OperationGeneration || SessionName != NAME_GameSession)
				{
					return;
				}
				CancelOperationTimeout();
				if (OperationSessionInterface.IsValid()
					&& DestroyDelegateHandle.IsValid())
				{
					OperationSessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(
						DestroyDelegateHandle);
				}
				DestroyDelegateHandle.Reset();
				const EPostDestroyAction ActionToRun = PostDestroyAction;
				PostDestroyAction = EPostDestroyAction::None;
				ContinueAfterDestroy(ActionToRun, bWasSuccessful);
			}));

	SetStatus(Action == EPostDestroyAction::Cleanup
			? ETacticalSessionOperation::Leaving
			: ETacticalSessionOperation::Destroying,
		Action == EPostDestroyAction::Cleanup
			? NSLOCTEXT("TacticalSession", "Leaving", "正在离开 Steam 房间……")
			: NSLOCTEXT("TacticalSession", "Replacing", "正在清理上一个房间……"));
	ArmOperationTimeout(
		ETimedOperation::Destroy,
		Generation,
		TacticalSession::DestroyOperationTimeoutSeconds);
	if (!Sessions->DestroySession(NAME_GameSession))
	{
		CancelOperationTimeout();
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyDelegateHandle);
		DestroyDelegateHandle.Reset();
		PostDestroyAction = EPostDestroyAction::None;
		ContinueAfterDestroy(Action, false);
	}
}

void UTacticalSessionSubsystem::ContinueAfterDestroy(
	EPostDestroyAction Action,
	bool bDestroySucceeded)
{
	bHostingSession = false;
	if (!bDestroySucceeded && Action != EPostDestroyAction::Cleanup)
	{
		if (Action == EPostDestroyAction::ReconnectJoin)
		{
			ReconnectCompletion.ExecuteIfBound(false, FString());
			ReconnectCompletion.Unbind();
		}
		FailOperation(NSLOCTEXT(
			"TacticalSession", "DestroyBeforeActionFailed", "无法清理上一个在线房间。"));
		return;
	}

	switch (Action)
	{
	case EPostDestroyAction::Create:
		BeginCreate();
		break;
	case EPostDestroyAction::Join:
		BeginJoin(PendingJoinResult, false);
		break;
	case EPostDestroyAction::ReconnectJoin:
		BeginJoin(PendingJoinResult, true);
		break;
	case EPostDestroyAction::Cleanup:
		SetStatus(ETacticalSessionOperation::Idle,
			NSLOCTEXT("TacticalSession", "Left", "已离开房间。"));
		CleanupCompletion.ExecuteIfBound();
		CleanupCompletion.Unbind();
		break;
	case EPostDestroyAction::None:
	default:
		SetStatus(ETacticalSessionOperation::Idle,
			NSLOCTEXT("TacticalSession", "Idle", "在线会话已就绪。"));
		break;
	}
}

void UTacticalSessionSubsystem::CleanupSession(
	FOnTacticalSessionCleanupComplete Completion)
{
	++OperationGeneration;
	CancelSearchIfNeeded();
	ClearOperationDelegates();
	ReconnectCompletion.Unbind();
	bMatchStateTransitionInFlight = false;
	bHasQueuedMatchState = false;
	MatchStateRetryCount = 0;
	MatchStateTransitionDeadlineSeconds = 0.0;
	CleanupCompletion = MoveTemp(Completion);
	PendingJoinResult = FOnlineSessionSearchResult();
	LastJoinedSearchResult = FOnlineSessionSearchResult();
	ActiveSearch.Reset();
	SearchEntries.Reset();
	OnResultsChanged.Broadcast(SearchEntries);
	BeginDestroySession(EPostDestroyAction::Cleanup);
}

void UTacticalSessionSubsystem::ResolveReconnectTarget(
	FOnTacticalReconnectTargetResolved Completion)
{
	if (bHostingSession)
	{
		Completion.ExecuteIfBound(false, FString());
		return;
	}
	FString ConnectString;
	if (ResolveNamedSessionConnectString(ConnectString))
	{
		Completion.ExecuteIfBound(true, ConnectString);
		return;
	}
	if (!LastJoinedSearchResult.IsValid() || IsBusy())
	{
		Completion.ExecuteIfBound(false, FString());
		return;
	}

	++OperationGeneration;
	ClearOperationDelegates();
	ReconnectCompletion = MoveTemp(Completion);
	PendingJoinResult = LastJoinedSearchResult;
	if (const IOnlineSessionPtr Sessions = GetSessionInterface();
		Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession))
	{
		BeginDestroySession(EPostDestroyAction::ReconnectJoin);
	}
	else
	{
		BeginJoin(PendingJoinResult, true);
	}
}

bool UTacticalSessionSubsystem::ResolveNamedSessionConnectString(
	FString& OutConnectString) const
{
	OutConnectString.Reset();
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	return Sessions.IsValid()
		&& Sessions->GetNamedSession(NAME_GameSession)
		&& Sessions->GetResolvedConnectString(NAME_GameSession, OutConnectString)
		&& !OutConnectString.IsEmpty();
}

void UTacticalSessionSubsystem::TravelHostToGameplay()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		FailOperation(NSLOCTEXT(
			"TacticalSession", "HostWorldMissing", "房间已创建，但当前场景不可用。"));
		return;
	}
	if (UTacticalGameInstance* TacticalGI =
		Cast<UTacticalGameInstance>(GetGameInstance()))
	{
		TacticalGI->PrepareForOnlineTravel();
	}
	UGameplayStatics::OpenLevel(
		World, TacticalSession::GameplayMap, true,
		FString::Printf(TEXT("listen?MaxPlayers=%d"), MaxActivePlayers));
}

void UTacticalSessionSubsystem::TravelClientToSession(const FString& ConnectString)
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World
		? GetGameInstance()->GetFirstLocalPlayerController(World)
		: nullptr;
	if (!PlayerController || ConnectString.IsEmpty())
	{
		FailOperation(NSLOCTEXT(
			"TacticalSession", "ClientTravelUnavailable", "无法开始连接房主。"));
		return;
	}
	if (UTacticalGameInstance* TacticalGI =
		Cast<UTacticalGameInstance>(GetGameInstance()))
	{
		TacticalGI->PrepareForOnlineTravel();
	}
	PlayerController->ClientTravel(ConnectString, TRAVEL_Absolute);
}

void UTacticalSessionSubsystem::SetGameplayMatchInProgress(bool bInProgress)
{
	if (!bHostingSession)
	{
		return;
	}
	if (const UTacticalGameInstance* TacticalGI =
		Cast<UTacticalGameInstance>(GetGameInstance());
		TacticalGI && TacticalGI->IsLeaveMatchPending())
	{
		return;
	}
	if (bMatchStateTransitionInFlight)
	{
		bHasQueuedMatchState = true;
		bQueuedMatchState = bInProgress;
		return;
	}
	CancelMatchStateRetry();
	MatchStateRetryCount = 0;
	MatchStateTransitionDeadlineSeconds = FPlatformTime::Seconds()
		+ TacticalSession::MatchStateTransitionBudgetSeconds;
	if (IsMatchStatePublished(bInProgress))
	{
		MatchStateTransitionDeadlineSeconds = 0.0;
		return;
	}
	BeginMatchStateTransition(bInProgress);
}

void UTacticalSessionSubsystem::BeginMatchStateTransition(bool bInProgress)
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid() || !Sessions->GetNamedSession(NAME_GameSession))
	{
		UE_LOG(LogTacticalSession, Warning,
			TEXT("Event=SessionMatchStateFailed TargetInProgress=%d Reason=SessionUnavailable"),
			bInProgress);
		if (bHostingSession && FPlatformTime::Seconds()
			< MatchStateTransitionDeadlineSeconds)
		{
			ScheduleMatchStateConvergencePoll(bInProgress);
		}
		else if (bHostingSession)
		{
			bMatchStateTransitionInFlight = true;
			bMatchStateTransitionTarget = bInProgress;
			MatchStateRetryCount = TacticalSession::MaxMatchStateRetries;
			FinishMatchStateTransition(false);
		}
		return;
	}

	++OperationGeneration;
	ClearOperationDelegates();
	OperationSessionInterface = Sessions;
	bMatchStateTransitionInFlight = true;
	bMatchStateTransitionTarget = bInProgress;

	const EOnlineSessionState::Type State =
		Sessions->GetSessionState(NAME_GameSession);
	if (State == EOnlineSessionState::Starting
		|| State == EOnlineSessionState::Ending)
	{
		bMatchStateTransitionInFlight = false;
		if (FPlatformTime::Seconds() < MatchStateTransitionDeadlineSeconds)
		{
			ScheduleMatchStateConvergencePoll(bInProgress);
		}
		else
		{
			bMatchStateTransitionInFlight = true;
			MatchStateRetryCount = TacticalSession::MaxMatchStateRetries;
			FinishMatchStateTransition(false);
		}
		return;
	}
	if (!bInProgress && State == EOnlineSessionState::InProgress)
	{
		const uint64 Generation = OperationGeneration;
		EndDelegateHandle = Sessions->AddOnEndSessionCompleteDelegate_Handle(
			FOnEndSessionCompleteDelegate::CreateWeakLambda(
				this, [this, Generation](FName SessionName, bool bWasSuccessful)
				{
					if (Generation != OperationGeneration
						|| SessionName != NAME_GameSession)
					{
						return;
					}
					CancelOperationTimeout();
					if (OperationSessionInterface.IsValid()
						&& EndDelegateHandle.IsValid())
					{
						OperationSessionInterface->
							ClearOnEndSessionCompleteDelegate_Handle(EndDelegateHandle);
					}
					EndDelegateHandle.Reset();
					if (!bWasSuccessful)
					{
						UE_LOG(LogTacticalSession, Warning,
							TEXT("Event=SessionEndFailed Action=PublishClosedLobby"));
						FinishMatchStateTransition(false);
						return;
					}
					BeginMatchStateSettingsUpdate(false);
				}));
		ArmOperationTimeout(
			ETimedOperation::MatchState,
			Generation,
			TacticalSession::MatchStateOperationTimeoutSeconds);
		if (!Sessions->EndSession(NAME_GameSession))
		{
			CancelOperationTimeout();
			Sessions->ClearOnEndSessionCompleteDelegate_Handle(EndDelegateHandle);
			EndDelegateHandle.Reset();
			FinishMatchStateTransition(false);
		}
		return;
	}
	if ((!bInProgress && State != EOnlineSessionState::Pending
			&& State != EOnlineSessionState::Ended)
		|| (bInProgress && State != EOnlineSessionState::Pending
			&& State != EOnlineSessionState::Ended
			&& State != EOnlineSessionState::InProgress))
	{
		FinishMatchStateTransition(false);
		return;
	}

	BeginMatchStateSettingsUpdate(bInProgress);
}

void UTacticalSessionSubsystem::BeginMatchStateSettingsUpdate(bool bInProgress)
{
	const IOnlineSessionPtr Sessions = OperationSessionInterface.IsValid()
		? OperationSessionInterface
		: GetSessionInterface();
	const FOnlineSessionSettings* CurrentSettings = Sessions.IsValid()
		? Sessions->GetSessionSettings(NAME_GameSession)
		: nullptr;
	if (!Sessions.IsValid() || !CurrentSettings)
	{
		FinishMatchStateTransition(false);
		return;
	}

	FOnlineSessionSettings UpdatedSettings(*CurrentSettings);
	UpdatedSettings.bShouldAdvertise = true;
	UpdatedSettings.bAllowInvites = !bInProgress;
	// Keep the provider lobby joinable so an authenticated, rostered player can
	// recover after losing its local named Session. Search/UI filter IN_PROGRESS,
	// presence joining is disabled, and authoritative PreLogin rejects every
	// fresh or duplicate identity while the match is active.
	UpdatedSettings.bAllowJoinInProgress = true;
	UpdatedSettings.bAllowJoinViaPresence = !bInProgress;
	UpdatedSettings.Set(TacticalSession::MatchStateSetting,
		bInProgress ? TacticalSession::InProgressState : TacticalSession::LobbyState,
		EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	const uint64 Generation = OperationGeneration;
	UpdateDelegateHandle = Sessions->AddOnUpdateSessionCompleteDelegate_Handle(
		FOnUpdateSessionCompleteDelegate::CreateWeakLambda(
			this, [this, Generation, bInProgress](FName SessionName,
				bool bWasSuccessful)
			{
				if (Generation != OperationGeneration
					|| SessionName != NAME_GameSession)
				{
					return;
				}
				CancelOperationTimeout();
				if (OperationSessionInterface.IsValid()
					&& UpdateDelegateHandle.IsValid())
				{
					OperationSessionInterface->
						ClearOnUpdateSessionCompleteDelegate_Handle(UpdateDelegateHandle);
				}
				UpdateDelegateHandle.Reset();
				if (!bWasSuccessful)
				{
					FinishMatchStateTransition(false);
					return;
				}
				if (bInProgress)
				{
					BeginMatchSessionStart();
				}
				else
				{
					FinishMatchStateTransition(true);
				}
			}));

	ArmOperationTimeout(
		ETimedOperation::MatchState,
		Generation,
		TacticalSession::MatchStateOperationTimeoutSeconds);
	if (!Sessions->UpdateSession(NAME_GameSession, UpdatedSettings, true))
	{
		CancelOperationTimeout();
		Sessions->ClearOnUpdateSessionCompleteDelegate_Handle(UpdateDelegateHandle);
		UpdateDelegateHandle.Reset();
		FinishMatchStateTransition(false);
	}
}

void UTacticalSessionSubsystem::BeginMatchSessionStart()
{
	const IOnlineSessionPtr Sessions = OperationSessionInterface.IsValid()
		? OperationSessionInterface
		: GetSessionInterface();
	if (!Sessions.IsValid())
	{
		FinishMatchStateTransition(false);
		return;
	}

	const EOnlineSessionState::Type State = Sessions->GetSessionState(NAME_GameSession);
	if (State == EOnlineSessionState::InProgress)
	{
		FinishMatchStateTransition(true);
		return;
	}
	if (State != EOnlineSessionState::Pending
		&& State != EOnlineSessionState::Ended)
	{
		FinishMatchStateTransition(false);
		return;
	}

	const uint64 Generation = OperationGeneration;
	StartDelegateHandle = Sessions->AddOnStartSessionCompleteDelegate_Handle(
		FOnStartSessionCompleteDelegate::CreateWeakLambda(
			this, [this, Generation](FName SessionName, bool bWasSuccessful)
			{
				if (Generation != OperationGeneration
					|| SessionName != NAME_GameSession)
				{
					return;
				}
				CancelOperationTimeout();
				if (OperationSessionInterface.IsValid()
					&& StartDelegateHandle.IsValid())
				{
					OperationSessionInterface->
						ClearOnStartSessionCompleteDelegate_Handle(StartDelegateHandle);
				}
				StartDelegateHandle.Reset();
				FinishMatchStateTransition(bWasSuccessful);
			}));
	ArmOperationTimeout(
		ETimedOperation::MatchState,
		Generation,
		TacticalSession::MatchStateOperationTimeoutSeconds);
	if (!Sessions->StartSession(NAME_GameSession))
	{
		CancelOperationTimeout();
		Sessions->ClearOnStartSessionCompleteDelegate_Handle(StartDelegateHandle);
		StartDelegateHandle.Reset();
		FinishMatchStateTransition(false);
	}
}

void UTacticalSessionSubsystem::FinishMatchStateTransition(bool bSuccess)
{
	if (!bMatchStateTransitionInFlight)
	{
		return;
	}
	const bool bCompletedTarget = bMatchStateTransitionTarget;
	bMatchStateTransitionInFlight = false;
	UE_LOG(LogTacticalSession, Display,
		TEXT("Event=SessionMatchStatePublished InProgress=%d Success=%d"),
		bCompletedTarget, bSuccess);

	if (bHasQueuedMatchState)
	{
		const bool bNextTarget = bQueuedMatchState;
		bHasQueuedMatchState = false;
		SetGameplayMatchInProgress(bNextTarget);
		OnStatusChanged.Broadcast(Operation, StatusText, IsBusy());
		return;
	}
	if (!bSuccess && bHostingSession
		&& MatchStateRetryCount < TacticalSession::MaxMatchStateRetries
		&& FPlatformTime::Seconds() < MatchStateTransitionDeadlineSeconds)
	{
		ScheduleMatchStateRetry(bCompletedTarget);
		OnStatusChanged.Broadcast(Operation, StatusText, IsBusy());
	}
	else
	{
		MatchStateRetryCount = 0;
		MatchStateTransitionDeadlineSeconds = 0.0;
		if (!bSuccess)
		{
			SetStatus(ETacticalSessionOperation::Failed,
				bCompletedTarget
					? NSLOCTEXT("TacticalSession", "MatchClosePublishFailed",
						"比赛已开始，但 Steam 房间关闭状态同步失败；服务器仍会拒绝新玩家。")
					: NSLOCTEXT("TacticalSession", "LobbyOpenPublishFailed",
						"Steam 房间未能开放加入，请结束房间后重新创建。"));
		}
		else
		{
			OnStatusChanged.Broadcast(Operation, StatusText, IsBusy());
		}
	}
}

bool UTacticalSessionSubsystem::IsMatchStatePublished(bool bInProgress) const
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FOnlineSessionSettings* Settings = Sessions.IsValid()
		? Sessions->GetSessionSettings(NAME_GameSession)
		: nullptr;
	if (!Sessions.IsValid() || !Settings)
	{
		return false;
	}

	FString PublishedState;
	Settings->Get(TacticalSession::MatchStateSetting, PublishedState);
	const EOnlineSessionState::Type LifecycleState =
		Sessions->GetSessionState(NAME_GameSession);
	if (bInProgress)
	{
		return PublishedState == TacticalSession::InProgressState
			&& LifecycleState == EOnlineSessionState::InProgress
			&& Settings->bAllowJoinInProgress
			&& !Settings->bAllowInvites
			&& !Settings->bAllowJoinViaPresence;
	}
	return PublishedState == TacticalSession::LobbyState
		&& LifecycleState != EOnlineSessionState::InProgress
		&& Settings->bShouldAdvertise
		&& Settings->bAllowInvites
		&& Settings->bAllowJoinInProgress
		&& Settings->bAllowJoinViaPresence;
}

void UTacticalSessionSubsystem::ScheduleMatchStateRetry(bool bInProgress)
{
	CancelMatchStateRetry();
	bMatchStateRetryTarget = bInProgress;
	++MatchStateRetryCount;
	MatchStateRetryTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(
			this, &ThisClass::HandleMatchStateRetry),
		TacticalSession::MatchStateRetryDelaySeconds);
	UE_LOG(LogTacticalSession, Warning,
		TEXT("Event=SessionMatchStateRetryScheduled InProgress=%d Attempt=%d"),
		bInProgress, MatchStateRetryCount);
}

void UTacticalSessionSubsystem::ScheduleMatchStateConvergencePoll(bool bInProgress)
{
	CancelMatchStateRetry();
	bMatchStateRetryTarget = bInProgress;
	MatchStateRetryTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(
			this, &ThisClass::HandleMatchStateRetry),
		TacticalSession::MatchStateConvergencePollSeconds);
	UE_LOG(LogTacticalSession, Verbose,
		TEXT("Event=SessionMatchStateConvergencePoll InProgress=%d LifecyclePending=1"),
		bInProgress);
}

void UTacticalSessionSubsystem::CancelMatchStateRetry()
{
	if (MatchStateRetryTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(MatchStateRetryTickerHandle);
		MatchStateRetryTickerHandle.Reset();
	}
}

bool UTacticalSessionSubsystem::HandleMatchStateRetry(float DeltaTime)
{
	MatchStateRetryTickerHandle.Reset();
	if (bHostingSession && !bMatchStateTransitionInFlight)
	{
		if (FPlatformTime::Seconds() >= MatchStateTransitionDeadlineSeconds)
		{
			bMatchStateTransitionInFlight = true;
			bMatchStateTransitionTarget = bMatchStateRetryTarget;
			MatchStateRetryCount = TacticalSession::MaxMatchStateRetries;
			FinishMatchStateTransition(false);
			return false;
		}
		// Steam updates the local settings copy before the provider acknowledges
		// its remote lobby writes. A failed callback must retry the provider write;
		// the local copy alone cannot prove that publication succeeded.
		BeginMatchStateTransition(bMatchStateRetryTarget);
	}
	return false;
}

void UTacticalSessionSubsystem::PrepareHostSessionShutdown(
	FOnTacticalSessionCleanupComplete Completion)
{
	++OperationGeneration;
	ClearOperationDelegates();
	MatchStateRetryCount = 0;
	MatchStateTransitionDeadlineSeconds = 0.0;
	bMatchStateTransitionInFlight = false;
	bHasQueuedMatchState = false;
	HostShutdownPreparationCompletion = MoveTemp(Completion);

	const IOnlineSessionPtr Sessions = GetSessionInterface();
	const FOnlineSessionSettings* CurrentSettings = Sessions.IsValid()
		? Sessions->GetSessionSettings(NAME_GameSession)
		: nullptr;
	if (!bHostingSession || !Sessions.IsValid() || !CurrentSettings)
	{
		HostShutdownPreparationCompletion.ExecuteIfBound();
		HostShutdownPreparationCompletion.Unbind();
		return;
	}

	FOnlineSessionSettings ClosingSettings(*CurrentSettings);
	ClosingSettings.bShouldAdvertise = false;
	ClosingSettings.bAllowInvites = false;
	ClosingSettings.bAllowJoinInProgress = false;
	ClosingSettings.bAllowJoinViaPresence = false;
	ClosingSettings.bAllowJoinViaPresenceFriendsOnly = false;
	ClosingSettings.Set(TacticalSession::MatchStateSetting,
		TacticalSession::ClosingState,
		EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	OperationSessionInterface = Sessions;
	const uint64 Generation = OperationGeneration;
	UpdateDelegateHandle = Sessions->AddOnUpdateSessionCompleteDelegate_Handle(
		FOnUpdateSessionCompleteDelegate::CreateWeakLambda(
			this, [this, Generation](FName SessionName, bool bWasSuccessful)
			{
				if (Generation != OperationGeneration
					|| SessionName != NAME_GameSession)
				{
					return;
				}
				if (OperationSessionInterface.IsValid()
					&& UpdateDelegateHandle.IsValid())
				{
					OperationSessionInterface->
						ClearOnUpdateSessionCompleteDelegate_Handle(UpdateDelegateHandle);
				}
				UpdateDelegateHandle.Reset();
				UE_LOG(LogTacticalSession, Display,
					TEXT("Event=HostSessionClosingPublished Success=%d"),
					bWasSuccessful);
				HostShutdownPreparationCompletion.ExecuteIfBound();
				HostShutdownPreparationCompletion.Unbind();
			}));
	if (!Sessions->UpdateSession(NAME_GameSession, ClosingSettings, true))
	{
		Sessions->ClearOnUpdateSessionCompleteDelegate_Handle(UpdateDelegateHandle);
		UpdateDelegateHandle.Reset();
		UE_LOG(LogTacticalSession, Warning,
			TEXT("Event=HostSessionClosingPublished Success=0 Reason=RequestRejected"));
		HostShutdownPreparationCompletion.ExecuteIfBound();
		HostShutdownPreparationCompletion.Unbind();
	}
}

void UTacticalSessionSubsystem::ForceResetAfterCleanupTimeout(
	const FText& FrontendNotice)
{
	++OperationGeneration;
	CancelSearchIfNeeded();
	ClearOperationDelegates();
	ClearGlobalDelegates();
	ReconnectCompletion.Unbind();
	CleanupCompletion.Unbind();
	HostShutdownPreparationCompletion.Unbind();
	PostDestroyAction = EPostDestroyAction::None;
	bHostingSession = false;
	bMatchStateTransitionInFlight = false;
	bHasQueuedMatchState = false;
	MatchStateRetryCount = 0;
	MatchStateTransitionDeadlineSeconds = 0.0;
	PendingJoinResult = FOnlineSessionSearchResult();
	LastJoinedSearchResult = FOnlineSessionSearchResult();
	ActiveSearch.Reset();
	SearchEntries.Reset();
	OnResultsChanged.Broadcast(SearchEntries);
	SetStatus(ETacticalSessionOperation::Idle, FrontendNotice);
	UE_LOG(LogTacticalSession, Warning,
		TEXT("Event=SessionCleanupForceFinalized"));
}

bool UTacticalSessionSubsystem::ShowInviteUI()
{
	IOnlineSubsystem* Subsystem = GetOnlineSubsystem();
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Subsystem || !Sessions.IsValid()
		|| !Sessions->GetNamedSession(NAME_GameSession))
	{
		SetStatus(ETacticalSessionOperation::Failed,
			NSLOCTEXT("TacticalSession", "InviteNoSession", "当前没有可邀请好友加入的房间。"));
		return false;
	}
	const IOnlineExternalUIPtr ExternalUI = Subsystem->GetExternalUIInterface();
	if (!ExternalUI.IsValid()
		|| !ExternalUI->ShowInviteUI(0, NAME_GameSession))
	{
		SetStatus(ETacticalSessionOperation::Failed,
			NSLOCTEXT("TacticalSession", "InviteUnavailable", "无法打开 Steam 好友邀请界面。"));
		return false;
	}
	return true;
}

void UTacticalSessionSubsystem::HandleInviteAccepted(
	bool bWasSuccessful,
	int32 ControllerId,
	FUniqueNetIdPtr UserId,
	const FOnlineSessionSearchResult& InviteResult)
{
	// Invite callbacks are global and may arrive while another Session operation
	// or the retained-body reconnect flow owns travel. Never clear that owner's
	// delegates or overwrite its pending result.
	if (IsBusy())
	{
		UE_LOG(LogTacticalSession, Warning,
			TEXT("Event=SessionInviteIgnored Reason=OperationBusy"));
		return;
	}
	if (const UTacticalGameInstance* TacticalGI =
		Cast<UTacticalGameInstance>(GetGameInstance());
		TacticalGI && (TacticalGI->IsReconnectOverlayBlocking()
			|| TacticalGI->IsLeaveMatchPending()))
	{
		UE_LOG(LogTacticalSession, Warning,
			TEXT("Event=SessionInviteIgnored Reason=RecoveryOrLeaveInProgress"));
		return;
	}
	FString InviteMatchState;
	InviteResult.Session.SessionSettings.Get(
		TacticalSession::MatchStateSetting, InviteMatchState);
	if (!bWasSuccessful || ControllerId != 0 || !UserId.IsValid()
		|| !InviteResult.IsValid() || !IsResultCompatible(InviteResult)
		|| InviteMatchState != TacticalSession::LobbyState)
	{
		FailOperation(NSLOCTEXT(
			"TacticalSession", "InviteInvalid", "Steam 邀请已失效或版本不兼容。"));
		return;
	}
	if (GetWorld() && GetWorld()->GetNetMode() != NM_Standalone)
	{
		FailOperation(NSLOCTEXT(
			"TacticalSession", "InviteWhilePlaying",
			"请先从当前对局选择“离开比赛”，再接受新的邀请。"));
		return;
	}
	++OperationGeneration;
	ClearOperationDelegates();
	PendingJoinResult = InviteResult;
	bHostingSession = false;
	if (const IOnlineSessionPtr Sessions = GetSessionInterface();
		Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession))
	{
		BeginDestroySession(EPostDestroyAction::Join);
	}
	else
	{
		BeginJoin(PendingJoinResult, false);
	}
}

void UTacticalSessionSubsystem::HandleSessionFailure(
	const FUniqueNetId& UserId,
	ESessionFailure::Type FailureType)
{
	UE_LOG(LogTacticalSession, Warning,
		TEXT("Event=OnlineSessionFailure Type=%s"), LexToString(FailureType));
	if (UTacticalGameInstance* TacticalGI =
		Cast<UTacticalGameInstance>(GetGameInstance()))
	{
		TacticalGI->HandleOnlineSessionLost(
			NSLOCTEXT("TacticalSession", "SessionServiceLost",
				"Steam 会话已断开，当前比赛已结束。"));
	}
}

void UTacticalSessionSubsystem::HandleDestroySessionRequested(
	int32 LocalUserNum,
	FName SessionName)
{
	if (LocalUserNum == 0 && SessionName == NAME_GameSession)
	{
		if (UTacticalGameInstance* TacticalGI =
			Cast<UTacticalGameInstance>(GetGameInstance()))
		{
			TacticalGI->HandleOnlineSessionLost(
				NSLOCTEXT("TacticalSession", "SessionDestroyed",
					"房主已关闭 Steam 房间，比赛不计结果。"));
		}
	}
}

bool UTacticalSessionSubsystem::HasNamedSession() const
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	return Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession) != nullptr;
}

bool UTacticalSessionSubsystem::HasReconnectTarget() const
{
	return !bHostingSession
		&& (HasNamedSession() || LastJoinedSearchResult.IsValid());
}

bool UTacticalSessionSubsystem::IsSteamSubsystemActive() const
{
	const IOnlineSubsystem* Subsystem = GetOnlineSubsystem();
	return Subsystem
		&& Subsystem->GetSubsystemName() == TacticalSession::SteamSubsystemName;
}

bool UTacticalSessionSubsystem::IsResultCompatible(
	const FOnlineSessionSearchResult& Result) const
{
	if (!Result.IsValid()
		|| Result.Session.SessionSettings.BuildUniqueId != GetBuildUniqueId())
	{
		return false;
	}
	int32 ProtocolVersion = INDEX_NONE;
	FString ProductId;
	return Result.Session.SessionSettings.Get(
		TacticalSession::ProtocolSetting, ProtocolVersion)
		&& TacticalNetworkAdmission::IsCompatibleProtocolVersion(ProtocolVersion)
		&& Result.Session.SessionSettings.Get(
			TacticalSession::ProductSetting, ProductId)
		&& ProductId == TacticalSession::ProductId;
}

FString UTacticalSessionSubsystem::BuildDefaultServerName() const
{
	IOnlineSubsystem* Subsystem = GetOnlineSubsystem();
	const IOnlineIdentityPtr Identity = Subsystem
		? Subsystem->GetIdentityInterface()
		: nullptr;
	const FString Nickname = Identity.IsValid()
		? Identity->GetPlayerNickname(0)
		: FString();
	return Nickname.IsEmpty()
		? TEXT("Tactical FPS 房间")
		: FString::Printf(TEXT("%s 的房间"), *Nickname);
}

FString UTacticalSessionSubsystem::GetOnlineIdentityLabel() const
{
	IOnlineSubsystem* Subsystem = GetOnlineSubsystem();
	if (!Subsystem)
	{
		return TEXT("Steam：未连接");
	}
	const IOnlineIdentityPtr Identity = Subsystem->GetIdentityInterface();
	const FString Nickname = Identity.IsValid()
		? Identity->GetPlayerNickname(0)
		: FString();
	return FString::Printf(TEXT("%s：%s"),
		*Subsystem->GetSubsystemName().ToString(),
		Nickname.IsEmpty() ? TEXT("本地玩家") : *Nickname);
}
