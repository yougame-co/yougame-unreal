// The YouGame Blueprint nodes: one request each, with On Success and On Failure. Each sends one
// request type (listed on each node) and nothing else. Outside YouGame every node takes On Failure on
// a later tick with Code "notAvailable".
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "YouGameTypes.h"
#include "YouGameAsyncActions.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FYouGameReplyPin, const FYouGameReply&, Reply);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FYouGameScorePin, const FYouGameScoreResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FYouGameLeaderboardPin, const FYouGameLeaderboardResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FYouGameLoadPin, const FYouGameLoadResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FYouGameTicketPin, const FYouGameMatchTicket&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FYouGameOpenPin, const FYouGameOpenResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FYouGameCompletePin);

/** What every node shares: one request by type, one answer, then the node is done. */
UCLASS(Abstract)
class YOUGAME_API UYouGameAsyncAction : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	virtual void Activate() override;

protected:
	/** The answer, once, on the game thread. */
	virtual void HandleReply(const FYouGameReply& /*Reply*/) {}

	void Prepare(UObject* WorldContextObject, const FString& InType, const FString& InBody, float InTimeoutSeconds = 0.f);
	void ValidateInput(const FString& Text);

private:
	void Deliver(const FYouGameReply& Reply);

	TWeakObjectPtr<UObject> WorldContext;
	FString RequestType;
	FString RequestBody;
	float TimeoutSeconds = 0.f;
	bool bDelivered = false;
	bool bInputHasNul = false;
};

/** Nodes whose answer is just ok or a refusal. */
UCLASS(Abstract)
class YOUGAME_API UYouGameReplyAction : public UYouGameAsyncAction
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "YouGame")
	FYouGameReplyPin OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "YouGame")
	FYouGameReplyPin OnFailure;

protected:
	virtual void HandleReply(const FYouGameReply& Reply) override;
};

UCLASS()
class YOUGAME_API UYouGameSignInAction : public UYouGameAsyncAction
{
	GENERATED_BODY()

public:
	/** Sends `signIn`. Provider may be empty, apple, google, x, discord or github. Where says overlay/browser; On Signed In signals completion. */
	UFUNCTION(BlueprintCallable, Category = "YouGame", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Sign In", Keywords = "YouGame login account"))
	static UYouGameSignInAction* SignIn(UObject* WorldContextObject, const FString& Provider = TEXT(""));

	UPROPERTY(BlueprintAssignable, Category = "YouGame")
	FYouGameOpenPin OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "YouGame")
	FYouGameOpenPin OnAlreadySignedIn;

	/** Read Result.Reply.RetryAfterSeconds before trying again. */
	UPROPERTY(BlueprintAssignable, Category = "YouGame")
	FYouGameOpenPin OnBusy;

	UPROPERTY(BlueprintAssignable, Category = "YouGame")
	FYouGameOpenPin OnSignInFailed;

	UPROPERTY(BlueprintAssignable, Category = "YouGame")
	FYouGameOpenPin OnBadRequest;

	/** Other failures (e.g. notAvailable); the four refusals above use their own pins. */
	UPROPERTY(BlueprintAssignable, Category = "YouGame")
	FYouGameOpenPin OnFailure;

protected:
	virtual void HandleReply(const FYouGameReply& Reply) override;
};

UCLASS()
class YOUGAME_API UYouGameSubmitScoreAction : public UYouGameAsyncAction
{
	GENERATED_BODY()

public:
	/** Sends `submitScore` with the run's score (whole number, 0 or more). The answer may carry the run's rank, the player's best and whether it is a new best. */
	UFUNCTION(BlueprintCallable, Category = "YouGame", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Submit Score", Keywords = "YouGame game over leaderboard high score"))
	static UYouGameSubmitScoreAction* SubmitScore(UObject* WorldContextObject, int64 Score);

	UPROPERTY(BlueprintAssignable, Category = "YouGame")
	FYouGameScorePin OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "YouGame")
	FYouGameScorePin OnFailure;

protected:
	virtual void HandleReply(const FYouGameReply& Reply) override;
};

UCLASS()
class YOUGAME_API UYouGameGetLeaderboardAction : public UYouGameAsyncAction
{
	GENERATED_BODY()

public:
	/** Sends `leaderboard`: the top Limit rows (1 to 100) to show in the game. Code board_off: the game's leaderboard is off. */
	UFUNCTION(BlueprintCallable, Category = "YouGame", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Get Leaderboard", Keywords = "YouGame scores top"))
	static UYouGameGetLeaderboardAction* GetLeaderboard(UObject* WorldContextObject, int32 Limit = 10);

	UPROPERTY(BlueprintAssignable, Category = "YouGame")
	FYouGameLeaderboardPin OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "YouGame")
	FYouGameLeaderboardPin OnFailure;

protected:
	virtual void HandleReply(const FYouGameReply& Reply) override;
};

UCLASS()
class YOUGAME_API UYouGameSaveAction : public UYouGameReplyAction
{
	GENERATED_BODY()

public:
	/** Sends `saveWrite`: keeps Data (any text, often JSON; 64 KB at most) as the player's one save of this game on their YouGame account. JSON goes as JSON, other text as a string. Signed out: On Failure, Code signedOut. */
	UFUNCTION(BlueprintCallable, Category = "YouGame", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Save", Keywords = "YouGame cloud save progress"))
	static UYouGameSaveAction* Save(UObject* WorldContextObject, const FString& Data);

protected:
	virtual void HandleReply(const FYouGameReply& Reply) override;
};

UCLASS()
class YOUGAME_API UYouGameClearSaveAction : public UYouGameReplyAction
{
	GENERATED_BODY()

public:
	/** Sends `saveClear`: removes the player's save of this game from their YouGame account. */
	UFUNCTION(BlueprintCallable, Category = "YouGame", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Clear Save", Keywords = "YouGame cloud save delete reset"))
	static UYouGameClearSaveAction* ClearSave(UObject* WorldContextObject);

protected:
	virtual void HandleReply(const FYouGameReply& Reply) override;
};

UCLASS()
class YOUGAME_API UYouGameLoadAction : public UYouGameAsyncAction
{
	GENERATED_BODY()

public:
	/** Sends `saveLoad`: what Save kept. bHasData is false when there is nothing. Signed out: On Failure, Code signedOut. */
	UFUNCTION(BlueprintCallable, Category = "YouGame", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Load", Keywords = "YouGame cloud save progress"))
	static UYouGameLoadAction* Load(UObject* WorldContextObject);

	UPROPERTY(BlueprintAssignable, Category = "YouGame")
	FYouGameLoadPin OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "YouGame")
	FYouGameLoadPin OnFailure;

protected:
	virtual void HandleReply(const FYouGameReply& Reply) override;
};

UCLASS()
class YOUGAME_API UYouGameOpenFeedbackAction : public UYouGameReplyAction
{
	GENERATED_BODY()

public:
	/** Sends `openFeedback`: YouGame's feedback sheet opens over the game (or, with no YouGame answering, the game's YouGame page in the browser). The game draws nothing. On Failure, Error holds words to show the player. */
	UFUNCTION(BlueprintCallable, Category = "YouGame", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Open Feedback", Keywords = "YouGame bug idea report"))
	static UYouGameOpenFeedbackAction* OpenFeedback(UObject* WorldContextObject);
};

UCLASS()
class YOUGAME_API UYouGameFindMatchAction : public UYouGameAsyncAction
{
	GENERATED_BODY()

public:
	/**
	 * Sends `mpTicket` for a two-player match: puts the player in YouGame's queue for this game and
	 * answers a Ticket and Url for the match server. The plugin does not connect to the match: your
	 * game does, with Url and Ticket. Mode: the game's own name for the mode (may be empty).
	 */
	UFUNCTION(BlueprintCallable, Category = "YouGame", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Find Match", Keywords = "YouGame online multiplayer ranked queue matchmaking"))
	static UYouGameFindMatchAction* FindMatch(UObject* WorldContextObject, const FString& Mode, bool bRanked);

	UPROPERTY(BlueprintAssignable, Category = "YouGame")
	FYouGameTicketPin OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "YouGame")
	FYouGameTicketPin OnFailure;

protected:
	virtual void HandleReply(const FYouGameReply& Reply) override;

private:
	bool bRankedAsked = false;
};

UCLASS()
class YOUGAME_API UYouGameFriendRoomAction : public UYouGameAsyncAction
{
	GENERATED_BODY()

public:
	/** Sends `mpFriendRoom` {players, mode?, fresh?}. Players 1..16. Fresh replaces an existing lobby. Answers Ticket and Url; then Invite Friend. */
	UFUNCTION(BlueprintCallable, Category = "YouGame", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Friend Room", Keywords = "YouGame online multiplayer private lobby"))
	static UYouGameFriendRoomAction* FriendRoom(UObject* WorldContextObject, const FString& Mode, int32 Players = 2, bool bFresh = false);

	UPROPERTY(BlueprintAssignable, Category = "YouGame")
	FYouGameTicketPin OnSuccess;

	UPROPERTY(BlueprintAssignable, Category = "YouGame")
	FYouGameTicketPin OnFailure;

protected:
	virtual void HandleReply(const FYouGameReply& Reply) override;
};

UCLASS()
class YOUGAME_API UYouGameJoinFriendRoomAction : public UYouGameFriendRoomAction
{
	GENERATED_BODY()

public:
	/** Sends `mpFriendRoom` {room} using the room from On Friend Room. Its success provides Ticket and Url. */
	UFUNCTION(BlueprintCallable, Category = "YouGame", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Join Friend Room", Keywords = "YouGame online multiplayer invite"))
	static UYouGameJoinFriendRoomAction* JoinFriendRoom(UObject* WorldContextObject, const FString& Room);
};

UCLASS()
class YOUGAME_API UYouGameInviteFriendAction : public UYouGameAsyncAction
{
	GENERATED_BODY()

public:
	/** Sends `mpInvite`: YouGame lets the player pick a friend in its overlay; the game never sees the friends list. */
	UFUNCTION(BlueprintCallable, Category = "YouGame", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Invite Friend", Keywords = "YouGame online multiplayer friends"))
	static UYouGameInviteFriendAction* InviteFriend(UObject* WorldContextObject);

	UPROPERTY(BlueprintAssignable, Category = "YouGame")
	FYouGameOpenPin OnSuccess;

	/** Code includes noFriendRoom, inviteNotShown, friends_held, busy and the app's own refusal. */
	UPROPERTY(BlueprintAssignable, Category = "YouGame")
	FYouGameOpenPin OnFailure;

protected:
	virtual void HandleReply(const FYouGameReply& Reply) override;
};

/** Fire and forget: an older app's refusal is ignored. */
UCLASS()
class YOUGAME_API UYouGameSetMatchStateAction : public UYouGameAsyncAction
{
	GENERATED_BODY()

public:
	/** Sends `matchState` {open}. True while a match is open; false when it ends. No error pin. */
	UFUNCTION(BlueprintCallable, Category = "YouGame", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Set Match State"))
	static UYouGameSetMatchStateAction* SetMatchState(UObject* WorldContextObject, bool bOpen);

	/** Completed or ignored; no game error to handle. */
	UPROPERTY(BlueprintAssignable, Category = "YouGame")
	FYouGameCompletePin OnComplete;

protected:
	virtual void HandleReply(const FYouGameReply& Reply) override;
};

UCLASS()
class YOUGAME_API UYouGameReportMatchResultAction : public UYouGameReplyAction
{
	GENERATED_BODY()

public:
	/** Sends `mpResult` at the end of a match: each side reports its own view. */
	UFUNCTION(BlueprintCallable, Category = "YouGame", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Report Match Result", Keywords = "YouGame online multiplayer ranked win lose"))
	static UYouGameReportMatchResultAction* ReportMatchResult(UObject* WorldContextObject, EYouGameMatchOutcome Outcome, int64 Score);
};

UCLASS()
class YOUGAME_API UYouGameRoomSendAction : public UYouGameReplyAction
{
	GENERATED_BODY()

public:
	/** Sends `roomSend`: Data (JSON goes as JSON, other text as a string) to everyone else in the room, or only to the player whose id is To. Others get it through On Room Message. Browser builds only for now: on the desktop, On Failure with Code notAvailable. */
	UFUNCTION(BlueprintCallable, Category = "YouGame", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Room Send", Keywords = "YouGame online multiplayer message room browser web"))
	static UYouGameRoomSendAction* RoomSend(UObject* WorldContextObject, const FString& Data, const FString& To = TEXT(""));
};

UCLASS()
class YOUGAME_API UYouGameCallAction : public UYouGameReplyAction
{
	GENERATED_BODY()

public:
	/** Sends any request Type with a JSON object Body (empty: none). For requests the other nodes do not cover yet. */
	UFUNCTION(BlueprintCallable, Category = "YouGame", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "YouGame Request", Keywords = "YouGame call raw json"))
	static UYouGameCallAction* Request(UObject* WorldContextObject, const FString& Type, const FString& BodyJson);
};
