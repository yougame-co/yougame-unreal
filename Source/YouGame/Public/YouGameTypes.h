// What the YouGame plugin's calls answer, for Blueprint and C++.
#pragma once

#include "CoreMinimal.h"
#include "YouGameTypes.generated.h"

/** Last input the game itself received; the plugin installs no input reader. */
UENUM(BlueprintType)
enum class EYouGameInputKind : uint8
{
	Keys,
	PlayStation,
	Gamepad,
	GameCube
};

/** What the game is doing now, for YouGame's button over it (Set Game State). */
UENUM(BlueprintType)
enum class EYouGameGameState : uint8
{
	/** In play: the button keeps out of the way. */
	Playing,
	/** The game's own pause: the button shows (controllers, settings, friends). */
	Paused,
	/** A main menu or another menu outside play: the button shows. */
	Menu
};

/** Every call's answer. On failure, Code is the reason (YouGame's own code or one of the plugin's) and Error is YouGame's sentence. */
USTRUCT(BlueprintType)
struct YOUGAME_API FYouGameReply
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	bool bOk = false;

	/**
	 * Set when bOk is false: notAvailable (no YouGame answered), notYet (this YouGame does not do that
	 * call yet), noAnswer (nothing answered in time: try later), busy (try again after
	 * RetryAfterSeconds), signedOut, unknown_build (YouGame doesn't recognise this copy: the player gets
	 * the game from YouGame), old_build (the player needs the latest version of the game from YouGame),
	 * bad_request (YouGame could not read the call, e.g. a save over 64 KB),
	 * board_off (Get Leaderboard: the game's leaderboard is off), alreadySignedIn, signInFailed,
	 * badRequest (sign-in provider), noFriendRoom, inviteNotShown, friends_held, notHere,
	 * accountChanged, room_full, not_invited, tooLong, or YouGame's own code. Save writes/clears
	 * refused old_build keep YouGame's text: "Update this game to save." In a browser build also:
	 * cancelled (the player closed YouGame's searching card), noRoom (Room Send or Report Match
	 * Result with no room), notStored (the browser kept no save), failed (the SDK refused; Error says why).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	FString Code;

	/** YouGame's own sentence when bOk is false; empty when it worked. */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	FString Error;

	/** When not ok and YouGame said how long to wait before asking again; else 0. */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	int32 RetryAfterSeconds = 0;

	/** The whole answer as JSON, for fields the typed results leave out. */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	FString ResultJson;

	static FYouGameReply Failure(const FString& InCode, const FString& InError)
	{
		FYouGameReply Reply;
		Reply.Code = InCode;
		Reply.Error = InError;
		return Reply;
	}
};

/** Submit Score's answer. */
USTRUCT(BlueprintType)
struct YOUGAME_API FYouGameScoreResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	FYouGameReply Reply;

	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	bool bHasRank = false;

	/** The run's place on the leaderboard, when bHasRank. */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	int64 Rank = 0;

	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	bool bHasBest = false;

	/** The player's best score, when bHasBest. */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	int64 Best = 0;

	/** True when this run is the player's new best. */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	bool bPersonalBest = false;
};

USTRUCT(BlueprintType)
struct YOUGAME_API FYouGameLeaderboardRow
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	int64 Rank = 0;

	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	int64 Score = 0;

	/** This row is the player's own. */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	bool bYou = false;

	/** The public name YouGame shows on the game's leaderboard; empty when it gives none. */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	FString Name;
};

/** Get Leaderboard's answer. */
USTRUCT(BlueprintType)
struct YOUGAME_API FYouGameLeaderboardResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	FYouGameReply Reply;

	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	TArray<FYouGameLeaderboardRow> Rows;
};

/** Load's answer. */
USTRUCT(BlueprintType)
struct YOUGAME_API FYouGameLoadResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	FYouGameReply Reply;

	/** False when this player has no save for this game. */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	bool bHasData = false;

	/** The text Save stored. */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	FString Data;
};

/**
 * Find Match's and Friend Room's answer. The plugin does not connect
 * to the match itself yet: Url is the match server's address and Ticket the pass for it. Never show
 * or log the ticket.
 */
USTRUCT(BlueprintType)
struct YOUGAME_API FYouGameMatchTicket
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	FYouGameReply Reply;

	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	FString Ticket;

	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	FString Url;

	/** The queue YouGame put the player in (its text or JSON); empty for a friend room. */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	FString Queue;

	/** The room, when YouGame names one. Never log it. */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	FString Room;

	/** Find Match asked for a ranked match. */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	bool bRanked = false;
};

/* ScriptName: Python strips E and F, so this and FYouGameUpdateState would share one name there. */
UENUM(BlueprintType, meta = (ScriptName = "YouGameUpdateStatus"))
enum class EYouGameUpdateState : uint8
{
	None,
	Downloading,
	Ready,
	Installing,
	Waiting,
	Unknown,
};

/** YouGame updating itself: hello's `update` and the updateChanged push. */
USTRUCT(BlueprintType)
struct YOUGAME_API FYouGameUpdateState
{
	GENERATED_BODY()

	/** None when hello omits update or updateChanged clears it with state:none. */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	EYouGameUpdateState State = EYouGameUpdateState::None;

	/** 0 to 100 while downloading; -1 when YouGame gave no percent. */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	int32 Percent = -1;

	/** The version being installed, when YouGame says. */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	FString Version;
};

UENUM(BlueprintType)
enum class EYouGameOpenedWhere : uint8
{
	Unknown,
	Overlay,
	Browser,
	/** YouGame Online's own sign-in window: email and password there, other providers hand off to the browser. */
	Window,
};

USTRUCT(BlueprintType)
struct YOUGAME_API FYouGameOpenResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	FYouGameReply Reply;

	/** Opened a sign-in surface or invite picker; sign-in itself may finish later. */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	bool bOpened = false;

	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	EYouGameOpenedWhere Where = EYouGameOpenedWhere::Unknown;
};

/** A friend's proven room to join: call Join Friend Room to obtain its ticket. Never log Room. */
USTRUCT(BlueprintType)
struct YOUGAME_API FYouGameFriendRoom
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	FString Room;

	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	int32 Players = 2;

	/** Empty when absent. */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	FString Mode;
};

UENUM(BlueprintType)
enum class EYouGameStartResult : uint8
{
	NotChecked,
	Running,
	Opened,
	Offered,
	Declined,
	Installed,
	Missing,
	Unavailable,
	Updating,
	/** Get YouGame was pressed: YouGame downloads and installs in the background while the game runs. Percent is the download's progress. */
	Installing,
};

USTRUCT(BlueprintType)
struct YOUGAME_API FYouGameStartState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	EYouGameStartResult Result = EYouGameStartResult::NotChecked;

	/** Target version on Updating; empty for all other results. */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	FString Version;

	/** On Installing: the download's progress, 0-100, or -1 once it is installing (or for any other result). */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	int32 Percent = -1;
};

/** One player in the room, from On Room Players. Id is the room's id for them (Room Send's To). */
USTRUCT(BlueprintType)
struct YOUGAME_API FYouGameRoomPlayer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	FString Id;

	/** Their public YouGame name; for display only, never log it. */
	UPROPERTY(BlueprintReadOnly, Category = "YouGame")
	FString Name;
};

/** How a match ended, for Report Match Result. */
UENUM(BlueprintType)
enum class EYouGameMatchOutcome : uint8
{
	Lost,
	Won,
	Draw,
};
