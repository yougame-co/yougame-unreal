#include "YouGameSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GeneralProjectSettings.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "CoreGlobals.h"
#include "YouGameLog.h"
#include "YouGameSettings.h"
#include "YouGameStartGlue.h"
#include "YouGameWeb.h"
#include "YouGameWire.h"

namespace
{
	/** The game instance whose subsystem owns yougame.h; yougame.h is one per process. */
	UYouGameSubsystem* GYouGameOwner = nullptr;
	/** Reconnects say their "looking for YouGame" lines at Verbose. */
	bool GYouGameQuietLogs = false;
	/**
	 * Call keys, unique in the process (game thread only), so an answer meant for an earlier game
	 * instance never matches a call of a later one.
	 */
	uint64 GYouGameNextKey = 1;

	/** After YouGame was started in the tray, or the connection dropped: look again this long. */
	constexpr double ReconnectWindowSeconds = 120.0;
	/** ...this often (yougame.h itself looks at most once every 5 s). */
	constexpr double ReconnectEverySeconds = 5.0;

	FString FromUtf8(const char* Text)
	{
		return (Text && *Text) ? FString(UTF8_TO_TCHAR(Text)) : FString();
	}

	FString FromUtf8(const std::string& Text)
	{
		return FromUtf8(Text.c_str());
	}

	std::string ToUtf8(const FString& Text)
	{
		return std::string(TCHAR_TO_UTF8(*Text));
	}

	EYouGameUpdateState UpdateKind(const std::string& State)
	{
		switch (YouGameWire::UpdateState(State))
		{
		case YouGameWire::EUpdateState::None: return EYouGameUpdateState::None;
		case YouGameWire::EUpdateState::Downloading: return EYouGameUpdateState::Downloading;
		case YouGameWire::EUpdateState::Ready: return EYouGameUpdateState::Ready;
		case YouGameWire::EUpdateState::Installing: return EYouGameUpdateState::Installing;
		case YouGameWire::EUpdateState::Waiting: return EYouGameUpdateState::Waiting;
		default: return EYouGameUpdateState::Unknown;
		}
	}

	void YouGameLogLine(void* /*User*/, int Level, const char* Message)
	{
		// yougame.h's lines carry no secrets, names, drafts or answers (its config.log contract).
		const FString Line = FromUtf8(Message);
		if (GYouGameQuietLogs)
		{
			UE_LOG(LogYouGame, Verbose, TEXT("%s"), *Line);
		}
		else if (Level == YG_LOG_WARN)
		{
			UE_LOG(LogYouGame, Warning, TEXT("%s"), *Line);
		}
		else
		{
			UE_LOG(LogYouGame, Log, TEXT("%s"), *Line);
		}
	}

	void YouGameOnReady(void* /*User*/)
	{
		if (GYouGameOwner) GYouGameOwner->HandleReady();
	}

	void YouGameOnPlayer(void* /*User*/)
	{
		if (GYouGameOwner) GYouGameOwner->HandlePlayer();
	}

	void YouGameOnPush(void* /*User*/, const char* Type, const char* Json)
	{
		if (GYouGameOwner) GYouGameOwner->HandlePush(Type, Json);
	}

	void YouGameOnQuit(void* /*User*/)
	{
		if (GYouGameOwner) GYouGameOwner->HandleQuit();
	}

	void YouGameOnAnswer(void* User, const yg_result* Result)
	{
		FYouGameReply Reply;
		Reply.bOk = Result->ok != 0;
		Reply.Code = FromUtf8(YouGameWire::FailureCode(Result->code ? Result->code : ""));
		Reply.Error = FromUtf8(Result->error);
		Reply.RetryAfterSeconds = Result->retry_after_seconds;
		Reply.ResultJson = FromUtf8(Result->result_json);
		if (Result->type && FCStringAnsi::Strcmp(Result->type, YouGameWire::Type::OpenFeedback) == 0)
		{
			// yougame.h maps feedback itself: with no YouGame answering a game started alone, the
			// game's page opened in the browser instead, and that counts as opened.
			Reply.bOk = Result->feedback.opened != 0;
			if (Result->feedback.in_browser)
			{
				Reply.Code.Reset();
				Reply.Error.Reset();
				Reply.ResultJson = TEXT("{\"opened\":true,\"inBrowser\":true}");
			}
			else if (!Reply.bOk && Result->feedback.player_message && *Result->feedback.player_message)
			{
				Reply.Error = FromUtf8(Result->feedback.player_message);
			}
		}
		if (GYouGameOwner) GYouGameOwner->HandleAnswer(static_cast<uint64>(reinterpret_cast<UPTRINT>(User)), Reply);
	}
}

bool UYouGameSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && !IsRunningCommandlet() && Super::ShouldCreateSubsystem(Outer);
}

void UYouGameSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UYouGameSubsystem::HandleTick), 0.0f);
	if (GYouGameOwner != nullptr)
	{
		// Another game instance in this process (a second play-in-editor client) already owns it:
		// this one's calls answer notAvailable.
		UE_LOG(LogYouGame, Log, TEXT("YouGame: another game instance in this process talks to YouGame; this one does not"));
		return;
	}
	GYouGameOwner = this;
	bOwner = true;

#if YOUGAME_WEB
	// A browser build: no YouGame app to look for or start; the page's SDK answers.
	StartWeb();
#else

	// When the player started the game themselves: start YouGame in the tray, or offer Get YouGame.
	// A game the YouGame app started has YOUGAME_STDIO or YOUGAME_BRIDGE set and needs neither.
	const bool bStartedByApp = !FPlatformMisc::GetEnvironmentVariable(TEXT("YOUGAME_STDIO")).IsEmpty() ||
		!FPlatformMisc::GetEnvironmentVariable(TEXT("YOUGAME_BRIDGE")).IsEmpty();
	if (!bStartedByApp)
	{
		RunStartCheck();
	}
	StartClient(false);
#endif
}

void UYouGameSubsystem::Deinitialize()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	if (bOwner)
	{
		StopClient(TEXT("notAvailable"), TEXT("The game is closing"));
	}
	// Complete deferred calls even for non-owner game instances. There is no next tick, and game
	// callbacks may queue more calls. Pending removes each call before invoking it, so duplicates
	// from StopClient cannot deliver twice. All calls still waiting at shutdown fail notAvailable.
	const FYouGameReply Closing = FYouGameReply::Failure(TEXT("notAvailable"), TEXT("The game is closing"));
	// Capped: a game callback that queues another call each time it fails would otherwise loop forever.
	const size_t Dropped = YouGameWire::FlushAll(Later, [this, &Closing](const auto& Item)
	{
		HandleAnswer(Item.Key, Closing);
	});
	if (Dropped > 0)
	{
		UE_LOG(LogYouGame, Warning, TEXT("YouGame: %d call(s) queued by callbacks while closing were dropped; don't start new YouGame calls from a failure callback during shutdown"), (int32)Dropped);
	}
	Pending.Reset();
	if (bOwner)
	{
		GYouGameOwner = nullptr;
		bOwner = false;
	}
	Super::Deinitialize();
}

UYouGameSubsystem* UYouGameSubsystem::Get(const UObject* WorldContextObject)
{
	if (WorldContextObject == nullptr || GEngine == nullptr)
	{
		return nullptr;
	}
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UYouGameSubsystem>() : nullptr;
}

FYouGameStartState UYouGameSubsystem::AskForYouGame()
{
#if !YOUGAME_WEB
	const bool bStartedByApp = !FPlatformMisc::GetEnvironmentVariable(TEXT("YOUGAME_STDIO")).IsEmpty() ||
		!FPlatformMisc::GetEnvironmentVariable(TEXT("YOUGAME_BRIDGE")).IsEmpty();
	if (bOwner && !bStartedByApp)
	{
		RunStartCheck(false, YG_START_ASK_ON_USE);
	}
#endif
	return StartState;
}

#if !YOUGAME_WEB
void UYouGameSubsystem::PollGetYouGame()
{
	int Percent = -1;
	const int Stage = yg_start_get_progress(&Percent);
	if (!YouGameStart::GetProgressChanged(GetStage, GetPercent, Stage, Stage == YG_START_GET_DOWNLOADING ? Percent : -1))
	{
		return;
	}
	StartState.Version.Reset();
	StartState.Percent = -1;
	switch (Stage)
	{
	case YG_START_GET_DOWNLOADING:
		StartState.Result = EYouGameStartResult::Installing;
		StartState.Percent = FMath::Clamp(Percent, 0, 100);
		break;
	case YG_START_GET_INSTALLING:
		StartState.Result = EYouGameStartResult::Installing;
		break;
	case YG_START_GET_READY:
		// Installed and started in the tray: look for its channel, as after Opened.
		bStartGetting = false;
		StartState.Result = EYouGameStartResult::Opened;
		ReconnectUntil = FPlatformTime::Seconds() + ReconnectWindowSeconds;
		NextReconnectAt = FPlatformTime::Seconds() + 2.0;
		break;
	case YG_START_GET_FAILED:
		// The hook opened YouGame's download page instead.
		bStartGetting = false;
		StartState.Result = EYouGameStartResult::Offered;
		break;
	default:
		bStartGetting = false;
		StartState.Result = EYouGameStartResult::Declined;
		break;
	}
	UE_LOG(LogYouGame, Log, TEXT("YouGame background install: stage %d, %d%%"), Stage, StartState.Percent);
	OnStartState.Broadcast(StartState);
}
#endif

void UYouGameSubsystem::RunStartCheck(bool bQuiet, int32 Ask)
{
#if YOUGAME_WEB
	(void)bQuiet;
	(void)Ask;
#else
	const UYouGameSettings* Settings = GetDefault<UYouGameSettings>();
	if (GIsEditor && !Settings->bStartCheckInEditor)
	{
		return;
	}
	const std::string Slug = ToUtf8(Settings->GameSlug.TrimStartAndEnd());
	if (Slug.empty() || yg_valid_slug(Slug.c_str()) == 0)
	{
		UE_LOG(LogYouGame, Warning, TEXT("YouGame: set Project Settings > Plugins > YouGame > Game Slug (the part after /g/ in the game's address); the start check is off until then"));
		return;
	}
	const std::string TrayFrom = ToUtf8(Settings->TrayFromVersion.TrimStartAndEnd());
	if (Ask < 0)
	{
		Ask = Settings->AskWhen == EYouGameAskWhen::OnFirstUse ? YG_START_ASK_LATER : YG_START_ASK_AT_START;
	}
	const std::string Name = ToUtf8(Settings->GameName.TrimStartAndEnd());
	const YouGameStart::FOutcome Outcome = YouGameStart::Run(Slug.c_str(), Settings->bUseStaging, TrayFrom.c_str(), bQuiet, Ask,
		Name.empty() ? nullptr : Name.c_str());
	StartState.Version = FromUtf8(Outcome.UpdateVersion);
	StartState.Percent = -1;
	switch (Outcome.Result)
	{
	case YG_START_RUNNING: StartState.Result = EYouGameStartResult::Running; break;
	case YG_START_OPENED: StartState.Result = EYouGameStartResult::Opened; break;
	case YG_START_OFFERED: StartState.Result = EYouGameStartResult::Offered; break;
	case YG_START_DECLINED: StartState.Result = EYouGameStartResult::Declined; break;
	case YG_START_INSTALLED: StartState.Result = EYouGameStartResult::Installed; break;
	case YG_START_MISSING: StartState.Result = EYouGameStartResult::Missing; break;
	case YG_START_UPDATING: StartState.Result = EYouGameStartResult::Updating; break;
	case YG_START_GETTING:
		StartState.Result = EYouGameStartResult::Installing;
		// Asked again while it installs: keep the last percent rather than jumping back to 0.
		StartState.Percent = bStartGetting && GetPercent >= 0 ? GetPercent : 0;
		break;
	default: StartState.Result = EYouGameStartResult::Unavailable; break;
	}
	if (Outcome.Result == YG_START_GETTING && !bStartGetting)
	{
		bStartGetting = true;
		GetStage = -1;
		GetPercent = -2;
	}
	YouGameStart::FUpdateRetry Retry{StartUpdateUntil, NextStartCheckAt, bStartUpdating};
	Retry.Observe(Outcome.Result, FPlatformTime::Seconds());
	StartUpdateUntil = Retry.Until;
	NextStartCheckAt = Retry.Next;
	bStartUpdating = Retry.bActive;
	OnStartState.Broadcast(StartState);
	UE_LOG(LogYouGame, Log, TEXT("YouGame start check: %s"), *FromUtf8(YouGameStart::ResultName(Outcome.Result)));
	if (Outcome.Result == YG_START_OPENED || Outcome.Result == YG_START_RUNNING)
	{
		// Also covers a quiet check finding the app after an update took over two minutes.
		ReconnectUntil = FPlatformTime::Seconds() + ReconnectWindowSeconds;
		NextReconnectAt = FPlatformTime::Seconds() + (Outcome.Result == YG_START_OPENED ? 2.0 : 0.0);
	}
#endif
}

FString UYouGameSubsystem::BuildVersion()
{
	const FString Version = GetDefault<UYouGameSettings>()->GameVersion.TrimStartAndEnd();
	return Version.IsEmpty() ? GetDefault<UGeneralProjectSettings>()->ProjectVersion : Version;
}

FString UYouGameSubsystem::MatchCompatibility()
{
	const FString Compatibility = GetDefault<UYouGameSettings>()->MatchCompatibility.TrimStartAndEnd();
	return Compatibility.IsEmpty() ? BuildVersion() : Compatibility;
}

void UYouGameSubsystem::StartClient(bool bQuiet)
{
#if YOUGAME_WEB
	(void)bQuiet;
#else
	const UYouGameSettings* Settings = GetDefault<UYouGameSettings>();
	const std::string Slug = ToUtf8(Settings->GameSlug.TrimStartAndEnd());
	const std::string VersionUtf8 = ToUtf8(BuildVersion());

	yg_config Config;
	Config = yg_config();
	Config.game_slug = Slug.c_str();
	Config.game_version = VersionUtf8.c_str();
	Config.engine = "unreal";
	Config.feedback_site = Settings->bUseStaging ? "https://staging.yougame.co" : nullptr;
	Config.staging = Settings->bUseStaging ? 1 : 0;
	Config.log = &YouGameLogLine;
	Config.on_ready = &YouGameOnReady;
	Config.on_push = &YouGameOnPush;
	Config.on_quit = &YouGameOnQuit;
	Config.on_player = &YouGameOnPlayer;

	GYouGameQuietLogs = bQuiet;
	// yg_init copies every string it keeps.
	bClientUp = yg_init(&Config) == 0;
	// A reconnect starts yougame.h afresh: it hears the game's state again, sent after its hello.
	if (bClientUp && GameState >= 0)
	{
		yg_set_game_state(static_cast<yg_game_state>(YouGameWire::GameStateNative(static_cast<unsigned>(GameState))));
	}
#endif
}

void UYouGameSubsystem::StartWeb()
{
#if YOUGAME_WEB
#if !defined(YOUGAME_WEB_TARGET)
	// UE59-CHECK: YouGame.Build.cs defines YOUGAME_WEB_TARGET when it sees the "Wasm" platform.
	UE_LOG(LogYouGame, Warning, TEXT("YouGame: YouGame.Build.cs did not see the platform name Wasm for this browser build; the plugin works, but tell the plugin's makers"));
#endif
	const UYouGameSettings* Settings = GetDefault<UYouGameSettings>();
	YouGameWeb::Start(YouGameWire::WebConfigBody(Settings->bUseStaging, ToUtf8(Settings->GameSlug.TrimStartAndEnd()), ToUtf8(MatchCompatibility()), ToUtf8(BuildVersion())));
	bClientUp = true;
	UE_LOG(LogYouGame, Log, TEXT("YouGame: browser build, talking to the page's YouGame SDK"));
	AskWebHello();
#endif
}

void UYouGameSubsystem::AskWebHello()
{
#if YOUGAME_WEB
	if (bWebHelloAsking || !bClientUp)
	{
		return;
	}
	bWebHelloAsking = true;
	WebHelloRetryAt = 0.0;
	const TWeakObjectPtr<UYouGameSubsystem> WeakThis(this);
	Call(FString(YouGameWire::Type::Hello), TEXT("{}"), [WeakThis](const FYouGameReply& Reply)
	{
		UYouGameSubsystem* Self = WeakThis.Get();
		if (Self == nullptr)
		{
			return;
		}
		Self->bWebHelloAsking = false;
		if (!Reply.bOk)
		{
			// The SDK did not load (offline, blocked): ask again after 10 s, 20 s ... up to 5 minutes.
			if (Self->bClientUp)
			{
				Self->WebHelloRetryAt = FPlatformTime::Seconds() + FMath::Min(10.0 * FMath::Pow(2.0, static_cast<double>(FMath::Min(Self->WebHelloFailures, 5))), 300.0);
				++Self->WebHelloFailures;
			}
			return;
		}
		const YouGameWire::FWebHello Hello = YouGameWire::ReadWebHello(ToUtf8(Reply.ResultJson).c_str());
		Self->WebHelloFailures = 0;
		Self->bWebConnected = true;
		Self->SignedInFact = Hello.SignedIn;
		Self->WebPlayerName = FromUtf8(Hello.Name);
		Self->bWebOnYouGame = Hello.bOnYouGame && Hello.bFeedback;
		// A state said while the SDK was not answering (its send failed) goes now, as on the desktop.
		if (Self->GameState >= 0 && Self->WebSentGameState != Self->GameState)
		{
			Self->SetGameState(static_cast<EYouGameGameState>(Self->GameState));
		}
		Self->OnPlayerChanged.Broadcast(Self->IsSignedIn());
	});
#endif
}

void UYouGameSubsystem::DrainWeb()
{
#if YOUGAME_WEB
	const double Now = FPlatformTime::Seconds();
	for (const YouGameWebQueue::FInbound& In : YouGameWeb::TakeInbound())
	{
		if (In.bPush)
		{
			if (In.Type == YouGameWire::Push::FriendRoom && !OnFriendRoom.IsBound())
			{
				// An invite opened the game before its Blueprint bound On Friend Room: keep it a while.
				WebHeldFriendRoom = FromUtf8(In.Json);
				WebHeldFriendRoomUntil = Now + 60.0;
				continue;
			}
			HandlePush(In.Type.c_str(), In.Json.c_str());
			continue;
		}
		const YouGameWire::FEnvelope Envelope = YouGameWire::ReadEnvelope(In.Json.c_str());
		FYouGameReply Reply;
		Reply.bOk = Envelope.bOk;
		Reply.Code = FromUtf8(Envelope.Code);
		Reply.Error = FromUtf8(Envelope.Error);
		Reply.RetryAfterSeconds = Envelope.RetryAfterSeconds;
		Reply.ResultJson = FromUtf8(Envelope.ResultJson);
		// Keys are whole numbers below 2^53 (GYouGameNextKey); anything else matches no call.
		if (In.Key >= 1.0 && In.Key < 9007199254740992.0)
		{
			const uint64 Key = static_cast<uint64>(In.Key);
			WebDeadlines.Remove(Key);
			HandleAnswer(Key, Reply);
		}
	}
	if (!WebHeldFriendRoom.IsEmpty() && (OnFriendRoom.IsBound() || Now >= WebHeldFriendRoomUntil))
	{
		const std::string Held = ToUtf8(WebHeldFriendRoom);
		WebHeldFriendRoom.Reset();
		HandlePush(YouGameWire::Push::FriendRoom, Held.c_str());
	}
	// A backstop for answers that never came back from the page (a lost hand-off to the main thread).
	TArray<uint64> Late;
	for (const TPair<uint64, double>& Deadline : WebDeadlines)
	{
		if (Now >= Deadline.Value) Late.Add(Deadline.Key);
	}
	for (const uint64 Key : Late)
	{
		WebDeadlines.Remove(Key);
		HandleAnswer(Key, FYouGameReply::Failure(TEXT("noAnswer"), TEXT("YouGame did not answer in time")));
	}
	if (!bWebConnected && WebHelloRetryAt > 0.0 && Now >= WebHelloRetryAt)
	{
		AskWebHello();
	}
#endif
}

void UYouGameSubsystem::StopClient(const FString& Code, const FString& Error)
{
	if (bClientUp)
	{
#if YOUGAME_WEB
		YouGameWeb::Stop();
		bWebConnected = false;
		bWebHelloAsking = false;
		WebHelloRetryAt = 0.0;
		WebDeadlines.Reset();
		WebHeldFriendRoom.Reset();
#else
		yg_shutdown();
#endif
		bClientUp = false;
	}
	// yg_shutdown drops the calls still waiting without their callbacks: answer them here.
	TArray<uint64> Keys;
	Pending.GetKeys(Keys);
	for (const uint64 Key : Keys)
	{
		AnswerLater(Key, FYouGameReply::Failure(Code, Error));
	}
}

bool UYouGameSubsystem::HandleTick(float /*DeltaTime*/)
{
#if YOUGAME_WEB
	if (bOwner && bClientUp)
	{
		DrainWeb();
	}
#else
	if (bOwner && bStartGetting)
	{
		PollGetYouGame();
	}
	if (bOwner && bStartUpdating)
	{
		YouGameStart::FUpdateRetry Retry{StartUpdateUntil, NextStartCheckAt, bStartUpdating};
		const bool bDue = Retry.Due(FPlatformTime::Seconds());
		bStartUpdating = Retry.bActive;
		if (bDue) RunStartCheck(true);
		else if (!bStartUpdating)
		{
			// Stop the game's update line as well as the retries at the five-minute limit.
			StartState.Result = EYouGameStartResult::Unavailable;
			StartState.Version.Reset();
			OnStartState.Broadcast(StartState);
		}
	}
	if (bOwner && bClientUp)
	{
		yg_update();
	}

	if (bOwner && bClientUp && yg_transport_kind() == YG_TRANSPORT_LOCAL)
	{
		const bool bConnected = yg_connected() != 0;
		const double Now = FPlatformTime::Seconds();
		if (bWasConnected && !bConnected)
		{
			// YouGame closed, restarted after an update, or ended the connection: who is playing is
			// unknown until it answers again; keep looking for a while.
			bWasConnected = false;
			SignedInFact = -1;
			OnPlayerChanged.Broadcast(IsSignedIn());
			ReconnectUntil = Now + ReconnectWindowSeconds;
			NextReconnectAt = Now + ReconnectEverySeconds;
			// The app may have quit to install an update. The hook reports the marker quietly.
			RunStartCheck(true);
		}
		if (GYouGameQuietLogs && Now >= ReconnectUntil)
		{
			GYouGameQuietLogs = false; // done looking: yougame.h's lines are news again
		}
		// Not while calls wait: a call looks for YouGame by itself, and a restart would cut it off.
		const bool bTimedRetry = !bConnected && Now < ReconnectUntil && Now >= NextReconnectAt && Pending.Num() == 0;
		if ((bReconnectNow && Pending.Num() == 0) || bTimedRetry)
		{
			// A fresh connection says hello again (yougame.h sends hello only when it connects).
			bReconnectNow = false;
			bWasConnected = false; // this drop is ours: not news for the game
			NextReconnectAt = Now + ReconnectEverySeconds;
			StopClient(TEXT("noAnswer"), TEXT("YouGame reconnected: ask again"));
			StartClient(true);
		}
	}
#endif

	// Answers decided here (no YouGame, a reconnect) reach the game on a later tick, never inside the call.
	if (Later.Num() > 0)
	{
		YouGameWire::FlushLater(Later, [this](const auto& Item)
		{
			HandleAnswer(Item.Key, Item.Value);
		});
	}
	return true;
}

void UYouGameSubsystem::AnswerLater(uint64 Key, const FYouGameReply& Reply)
{
	Later.Emplace(Key, Reply);
}

void UYouGameSubsystem::Call(const FString& Type, const FString& BodyJson, FYouGameReplyCallback Callback, float TimeoutSeconds)
{
	const uint64 Key = GYouGameNextKey++;
	Pending.Add(Key, TPair<FString, FYouGameReplyCallback>(Type, MoveTemp(Callback)));
	if (!bOwner || !bClientUp)
	{
		AnswerLater(Key, FYouGameReply::Failure(TEXT("notAvailable"), TEXT("YouGame is not available here")));
		return;
	}
#if YOUGAME_WEB
	// The shim answers through the page's SDK and times the call itself (0: its default, 15 s; the
	// untimed requests only while the SDK loads). The deadline here is a backstop a little later.
	const std::string TypeUtf8 = ToUtf8(Type);
	const int TimeoutMs = TimeoutSeconds > 0.f ? FMath::CeilToInt(FMath::Min(TimeoutSeconds, 86400.f) * 1000.f) : 0;
	if (!YouGameWire::WebUntimed(TypeUtf8.c_str()))
	{
		WebDeadlines.Add(Key, FPlatformTime::Seconds() + (TimeoutMs > 0 ? TimeoutMs / 1000.0 : 15.0) + 5.0);
	}
	YouGameWeb::Call(Key, TypeUtf8, ToUtf8(BodyJson), TimeoutMs);
#else
	if (Type.Equals(FString(YouGameWire::Type::RoomSend), ESearchCase::CaseSensitive))
	{
		// Room traffic on the desktop comes later (through yougame_net.h); nothing carries it yet.
		AnswerLater(Key, FYouGameReply::Failure(TEXT("notAvailable"), TEXT("Room Send works in browser builds for now")));
		return;
	}
	const std::string TypeUtf8 = ToUtf8(Type);
	const std::string BodyUtf8 = ToUtf8(BodyJson);
	void* User = reinterpret_cast<void*>(static_cast<UPTRINT>(Key));
	long long Id = 0;
	if (TypeUtf8 == YouGameWire::Type::OpenFeedback)
	{
		// yougame.h's own feedback call: it opens the game's page in the browser when no YouGame answers.
		Id = YouGameWire::OpenFeedback(BodyUtf8, &YouGameOnAnswer, User);
	}
	else
	{
		Id = yg_call(TypeUtf8.c_str(), BodyUtf8.empty() ? nullptr : BodyUtf8.c_str(), TimeoutSeconds, &YouGameOnAnswer, User);
	}
	if (Id == 0)
	{
		AnswerLater(Key, FYouGameReply::Failure(TEXT("notAvailable"), TEXT("YouGame is not available here")));
	}
#endif
}

void UYouGameSubsystem::HandleAnswer(uint64 Key, const FYouGameReply& Reply)
{
	TPair<FString, FYouGameReplyCallback> Entry;
	if (!Pending.RemoveAndCopyValue(Key, Entry))
	{
		return;
	}
	if (Reply.bOk)
	{
		UE_LOG(LogYouGame, Log, TEXT("YouGame: %s ok"), *Entry.Key);
	}
	else if (Entry.Key == TEXT("matchState"))
	{
		UE_LOG(LogYouGame, Verbose, TEXT("YouGame: matchState ignored"));
	}
	else
	{
		UE_LOG(LogYouGame, Log, TEXT("YouGame: %s refused (%s)"), *Entry.Key, Reply.Code.IsEmpty() ? TEXT("no code") : *Reply.Code);
	}
	if (Entry.Value)
	{
		Entry.Value(Reply);
	}
}

void UYouGameSubsystem::ReadHello()
{
#if YOUGAME_WEB
	return;
#else
	const char* Hello = yg_hello_json();
	SignedInFact = YouGameWire::ReadSignedIn(Hello);
	const YouGameWire::FUpdate Update = YouGameWire::ReadUpdate(Hello);
	FYouGameUpdateState State;
	if (Update.bPresent)
	{
		State.State = UpdateKind(Update.State);
		State.Percent = Update.Percent;
		State.Version = FromUtf8(Update.Version);
	}
	SetUpdate(State);
#endif
}

void UYouGameSubsystem::HandleReady()
{
	GYouGameQuietLogs = false;
#if !YOUGAME_WEB
	bWasConnected = yg_connected() != 0;
#endif
	ReconnectUntil = 0.0;
	if (bStartUpdating)
	{
		bStartUpdating = false;
		StartState.Result = EYouGameStartResult::Running;
		StartState.Version.Reset();
		OnStartState.Broadcast(StartState);
	}
	ReadHello();
	OnPlayerChanged.Broadcast(IsSignedIn());
}

void UYouGameSubsystem::HandlePlayer()
{
	ReadHello();
	OnPlayerChanged.Broadcast(IsSignedIn());
}

void UYouGameSubsystem::HandlePush(const char* Type, const char* Json)
{
	const FString TypeText = FromUtf8(Type);
	const YouGameWire::EPush Kind = YouGameWire::PushKind(Type ? Type : "");
	if (Kind == YouGameWire::EPush::SignedIn)
	{
		// Re-read hello on a fresh connection once calls already in flight have answered. The push
		// tells sign-in now; that hello brings the player facts.
		bReconnectNow = true;
		SignedInFact = 1;
		OnSignedIn.Broadcast();
		OnPlayerChanged.Broadcast(true);
	}
	else if (Kind == YouGameWire::EPush::SignInFailed)
	{
		OnSignInFailed.Broadcast();
	}
	else if (Kind == YouGameWire::EPush::InviteNotShown)
	{
		OnInviteNotShown.Broadcast();
	}
	else if (Kind == YouGameWire::EPush::UpdateChanged)
	{
		const YouGameWire::FUpdate Update = YouGameWire::ReadUpdate(Json);
		FYouGameUpdateState State;
		if (Update.bPresent)
		{
			State.State = UpdateKind(Update.State);
			State.Percent = Update.Percent;
			State.Version = FromUtf8(Update.Version);
		}
		SetUpdate(State);
	}
	else if (Kind == YouGameWire::EPush::FriendRoom)
	{
		const YouGameWire::FFriendRoom Fact = YouGameWire::ReadFriendRoom(Json);
		FYouGameFriendRoom Room;
		Room.Room = FromUtf8(Fact.Room);
		Room.Players = Fact.Players;
		Room.Mode = FromUtf8(Fact.Mode);
		UE_LOG(LogYouGame, Log, TEXT("YouGame: a friend's room is ready"));
		OnFriendRoom.Broadcast(Room);
	}
	else if (Kind == YouGameWire::EPush::RoomMessage)
	{
		const YouGameWire::FRoomMessage Message = YouGameWire::ReadRoomMessage(Json);
		OnRoomMessage.Broadcast(FromUtf8(Message.From), FromUtf8(Message.Data));
	}
	else if (Kind == YouGameWire::EPush::RoomPlayers)
	{
		const YouGameWire::FRoomPlayers Fact = YouGameWire::ReadRoomPlayers(Json);
		TArray<FYouGameRoomPlayer> Players;
		Players.Reserve(static_cast<int32>(Fact.Players.size()));
		for (const YouGameWire::FRoomPlayer& Player : Fact.Players)
		{
			FYouGameRoomPlayer& Out = Players.AddDefaulted_GetRef();
			Out.Id = FromUtf8(Player.Id);
			Out.Name = FromUtf8(Player.Name);
		}
		OnRoomPlayers.Broadcast(Players, FromUtf8(Fact.Host), FromUtf8(Fact.You), Fact.bPlaying);
	}
	OnPush.Broadcast(TypeText, FromUtf8(YouGameWire::PushPayload(Type ? Type : "", Json)));
}

void UYouGameSubsystem::HandleQuit()
{
	OnQuitRequested.Broadcast();
}

void UYouGameSubsystem::SetUpdate(const FYouGameUpdateState& Update)
{
	if (Update.State == UpdateState.State && Update.Percent == UpdateState.Percent && Update.Version == UpdateState.Version)
	{
		return;
	}
	UpdateState = Update;
	OnUpdateState.Broadcast(UpdateState);
}

bool UYouGameSubsystem::IsSignedIn() const
{
	if (!bOwner || !bClientUp)
	{
		return false;
	}
	if (SignedInFact >= 0)
	{
		return SignedInFact == 1;
	}
#if YOUGAME_WEB
	return false;
#else
	const yg_player_state State = yg_player_status();
	return State == YG_PLAYER_KNOWN || State == YG_PLAYER_PENDING;
#endif
}

bool UYouGameSubsystem::IsConnected() const
{
#if YOUGAME_WEB
	return bOwner && bClientUp && bWebConnected;
#else
	return bOwner && bClientUp && yg_connected() != 0;
#endif
}

FString UYouGameSubsystem::GetPlayerName() const
{
#if YOUGAME_WEB
	return (bOwner && bClientUp) ? WebPlayerName : FString();
#else
	return (bOwner && bClientUp) ? FromUtf8(yg_player_name()) : FString();
#endif
}

bool UYouGameSubsystem::ShouldShowFeedbackButton() const
{
#if YOUGAME_WEB
	// Feedback opens over the game only on YouGame's own page.
	return bOwner && bClientUp && bWebConnected && bWebOnYouGame;
#else
	return bOwner && bClientUp && yg_should_show_feedback_button() != 0;
#endif
}

void UYouGameSubsystem::SetInputKind(EYouGameInputKind Kind)
{
	const std::string Json = YouGameWire::InputKindBody(static_cast<unsigned>(Kind));
	if (!Json.empty()) Call(FString(YouGameWire::Type::InputKind), UTF8_TO_TCHAR(Json.c_str()), [](const FYouGameReply&) {});
}

void UYouGameSubsystem::MenuButton(EYouGameInputKind Kind)
{
	const std::string Json = YouGameWire::InputKindBody(static_cast<unsigned>(Kind));
	if (!Json.empty()) Call(FString(YouGameWire::Type::MenuButton), UTF8_TO_TCHAR(Json.c_str()), [](const FYouGameReply&) {});
}

void UYouGameSubsystem::SetGameState(EYouGameGameState State)
{
	const unsigned Value = static_cast<unsigned>(State);
	const std::string Json = YouGameWire::GameStateBody(Value);
	if (Json.empty())
	{
		return;
	}
	GameState = static_cast<int32>(Value);
	if (!bOwner || !bClientUp)
	{
		return; // kept: StartClient (desktop) sends it once yougame.h starts
	}
#if YOUGAME_WEB
	// The page's SDK keeps the state for the page; only changes go to it.
	if (WebSentGameState == GameState)
	{
		return;
	}
	WebSentGameState = GameState;
	const TWeakObjectPtr<UYouGameSubsystem> WeakThis(this);
	const int32 Sent = GameState;
	Call(FString(YouGameWire::Type::GameState), UTF8_TO_TCHAR(Json.c_str()), [WeakThis, Sent](const FYouGameReply& Reply)
	{
		// Not taken (the SDK not loaded, or one without setGameState): the next call tries again.
		UYouGameSubsystem* Self = WeakThis.Get();
		if (Self && !Reply.bOk && Self->WebSentGameState == Sent) Self->WebSentGameState = -1;
	});
#else
	// yougame.h sends only changes, after hello, and again on each new connection.
	yg_set_game_state(static_cast<yg_game_state>(YouGameWire::GameStateNative(Value)));
#endif
}
