// The YouGame plugin, one per game instance: thin glue over yougame.h (talking to the YouGame app)
// and yougame_start.h (the start check). It draws no UI and logs no player name, id, code or ticket.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "YouGameTypes.h"
#include "YouGameSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FYouGamePlayerChangedSignature, bool, bSignedIn);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FYouGameUpdateStateSignature, const FYouGameUpdateState&, Update);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FYouGameFriendRoomSignature, const FYouGameFriendRoom&, Room);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FYouGameStartStateSignature, const FYouGameStartState&, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FYouGamePushSignature, const FString&, Type, const FString&, Json);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FYouGameQuitSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FYouGameRoomMessageSignature, const FString&, From, const FString&, Data);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FYouGameRoomPlayersSignature, const TArray<FYouGameRoomPlayer>&, Players, const FString&, Host, const FString&, You, bool, bPlaying);

/** A call's answer in C++: runs once, on the game thread, never inside the call that asked. */
using FYouGameReplyCallback = TFunction<void(const FYouGameReply&)>;

UCLASS()
class YOUGAME_API UYouGameSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Not on a dedicated server or in a commandlet: there is no player there. */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The subsystem of WorldContextObject's game instance, or null. */
	static UYouGameSubsystem* Get(const UObject* WorldContextObject);

	/** The build's version told to YouGame: Game Version in the YouGame settings, else the Project Version. */
	static FString BuildVersion();

	/** What online matches pair on: Match Compatibility in the YouGame settings, else BuildVersion(). */
	static FString MatchCompatibility();

	/** The player is signed in to YouGame (as far as YouGame has said). */
	UFUNCTION(BlueprintPure, Category = "YouGame", meta = (DisplayName = "Is Signed In"))
	bool IsSignedIn() const;

	/** YouGame answers now. */
	UFUNCTION(BlueprintPure, Category = "YouGame", meta = (DisplayName = "Is YouGame Connected"))
	bool IsConnected() const;

	/**
	 * The player's public name in games, or empty: show "Player" then. For display only; never log it.
	 */
	UFUNCTION(BlueprintPure, Category = "YouGame", meta = (DisplayName = "Get Player Name"))
	FString GetPlayerName() const;

	/** YouGame's last word on updating itself (State None: nothing to say). */
	UFUNCTION(BlueprintPure, Category = "YouGame", meta = (DisplayName = "Get Update State"))
	FYouGameUpdateState GetUpdateState() const { return UpdateState; }

	/** Last start-hook outcome. Updating includes its target Version; draw your own short update line. */
	UFUNCTION(BlueprintPure, Category = "YouGame", meta = (DisplayName = "Get Start State"))
	FYouGameStartState GetStartState() const { return StartState; }

	/** Show a feedback button only while this is true. */
	UFUNCTION(BlueprintPure, Category = "YouGame", meta = (DisplayName = "Should Show Feedback Button"))
	bool ShouldShowFeedbackButton() const;

	/** The player signed in or out, or who is playing became known. */
	UPROPERTY(BlueprintAssignable, Category = "YouGame", meta = (DisplayName = "On Player Changed"))
	FYouGamePlayerChangedSignature OnPlayerChanged;

	/** The signedIn push arrived; the plugin asks hello again for player facts automatically. */
	UPROPERTY(BlueprintAssignable, Category = "YouGame", meta = (DisplayName = "On Signed In"))
	FYouGameQuitSignature OnSignedIn;

	/** The browser sign-in could not finish. Offer Sign In again when the player chooses. */
	UPROPERTY(BlueprintAssignable, Category = "YouGame", meta = (DisplayName = "On Sign In Failed"))
	FYouGameQuitSignature OnSignInFailed;

	/** The invite picker did not appear after mpInvite was accepted. */
	UPROPERTY(BlueprintAssignable, Category = "YouGame", meta = (DisplayName = "On Invite Not Shown"))
	FYouGameQuitSignature OnInviteNotShown;

	/** Start-hook outcome (also read Get Start State after binding: the first check runs at startup). */
	UPROPERTY(BlueprintAssignable, Category = "YouGame", meta = (DisplayName = "On Start State"))
	FYouGameStartStateSignature OnStartState;

	/** YouGame is updating itself: show State (and Percent) and carry on; online comes back by itself. */
	UPROPERTY(BlueprintAssignable, Category = "YouGame", meta = (DisplayName = "On Update State"))
	FYouGameUpdateStateSignature OnUpdateState;

	/** A friend's invite was accepted: Room, Players, Mode. Call Join Friend Room for Url + Ticket. */
	UPROPERTY(BlueprintAssignable, Category = "YouGame", meta = (DisplayName = "On Friend Room"))
	FYouGameFriendRoomSignature OnFriendRoom;

	/** Every push by type and JSON for game logic, never logs. friendRoom omits its room code. */
	UPROPERTY(BlueprintAssignable, Category = "YouGame", meta = (DisplayName = "On YouGame Push"))
	FYouGamePushSignature OnPush;

	/**
	 * Browser builds: another player in the room sent Data (a string's own text, or any other
	 * value's JSON) with Room Send. From is their room id. Desktop builds: not yet.
	 */
	UPROPERTY(BlueprintAssignable, Category = "YouGame", meta = (DisplayName = "On Room Message"))
	FYouGameRoomMessageSignature OnRoomMessage;

	/**
	 * Browser builds: who is in the room (join order, the same on every client), the host's id and
	 * this player's id (You), whenever that changes. bPlaying: the host started the match (Set Match
	 * State true). Players empty and You empty: the room closed. Desktop builds: not yet.
	 */
	UPROPERTY(BlueprintAssignable, Category = "YouGame", meta = (DisplayName = "On Room Players"))
	FYouGameRoomPlayersSignature OnRoomPlayers;

	/** The YouGame app is closing a game it started: save now; it ends the game a few seconds later. */
	UPROPERTY(BlueprintAssignable, Category = "YouGame", meta = (DisplayName = "On Quit Requested"))
	FYouGameQuitSignature OnQuitRequested;

	/**
	 * Any request by its type, with a JSON object body (empty: none). Callback runs once on the game
	 * thread, after this returns. Every Blueprint node goes through here.
	 */
	void Call(const FString& Type, const FString& BodyJson, FYouGameReplyCallback Callback, float TimeoutSeconds = 0.f);

	/** Call from the game's existing input events when the input kind changes. */
	UFUNCTION(BlueprintCallable, Category = "YouGame")
	void SetInputKind(EYouGameInputKind Kind);

	/** Call once on the game's PS/Home press or GameCube menu chord. Older apps ignore it. */
	UFUNCTION(BlueprintCallable, Category = "YouGame")
	void MenuButton(EYouGameInputKind Kind);

	// Called from yougame.h's callbacks (inside yg_update, on the game thread). Not for games.
	void HandleReady();
	void HandlePlayer();
	void HandlePush(const char* Type, const char* Json);
	void HandleQuit();
	void HandleAnswer(uint64 Key, const FYouGameReply& Reply);

private:
	bool HandleTick(float DeltaTime);
	/** Starts yougame.h (yg_init) with the settings. Quiet: its first lines go to the verbose log. */
	void StartClient(bool bQuiet);
	/** yg_shutdown, then every call still waiting fails with Code. */
	void StopClient(const FString& Code, const FString& Error);
	void RunStartCheck(bool bQuiet = false);
	void ReadHello();
	/** Browser builds: starts the page's shim and asks it for the player facts. */
	void StartWeb();
	/** Browser builds: hands the shim's answers and pushes to their callbacks and events. */
	void DrainWeb();
	/** Browser builds: asks the shim for hello; a failure asks again later, backing off. */
	void AskWebHello();
	void SetUpdate(const FYouGameUpdateState& Update);
	void AnswerLater(uint64 Key, const FYouGameReply& Reply);

	FTSTicker::FDelegateHandle TickHandle;
	/** Only one game instance in the process owns yougame.h (it is one per process). */
	bool bOwner = false;
	bool bClientUp = false;

	TMap<uint64, TPair<FString, FYouGameReplyCallback>> Pending;
	TArray<TPair<uint64, FYouGameReply>> Later;

	/** Browser builds: the shim's hello (who is playing, whether the page is YouGame). */
	FString WebPlayerName;
	bool bWebConnected = false;
	bool bWebOnYouGame = false;
	bool bWebHelloAsking = false;
	int32 WebHelloFailures = 0;
	double WebHelloRetryAt = 0.0;
	/** Browser builds: when a timed call is answered noAnswer if the shim never answers it. */
	TMap<uint64, double> WebDeadlines;
	/** Browser builds: an invite's friendRoom push, held until On Friend Room is bound (or a minute). */
	FString WebHeldFriendRoom;
	double WebHeldFriendRoomUntil = 0.0;

	/** hello's signedIn (1, 0), or -1 when YouGame did not say. */
	int32 SignedInFact = -1;
	bool bWasConnected = false;
	FYouGameUpdateState UpdateState;
	FYouGameStartState StartState;
	bool bStartUpdating = false;
	double StartUpdateUntil = 0.0;
	double NextStartCheckAt = 0.0;

	/** A started-alone game reconnects to say hello again: after the signedIn push, after YouGame was started in the tray, after the connection dropped. */
	bool bReconnectNow = false;
	double ReconnectUntil = 0.0;
	double NextReconnectAt = 0.0;
};
