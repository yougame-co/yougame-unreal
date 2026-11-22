// YouGameWire.h: the YouGame plugin's request types, request bodies and answer readers, in plain
// C++17 with no Unreal types, so they compile and are tested outside Unreal
// (engines/unreal/tests/run-tests.sh). The Unreal module only converts strings around these.
//
// Every request type and every field name the plugin sends or reads is spelled here and nowhere
// else: when YouGame changes a call's shape, the edit is one line in this file.
#pragma once

#include <cstdio>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "yougame.h"

namespace YouGameWire
{
// Take one batch before calling game code: callbacks may enqueue more answers. Tick drains one
// batch; shutdown repeats until empty because there will be no next tick.
template <typename Queue, typename Complete>
inline void FlushLater(Queue& Later, Complete Deliver)
{
	Queue Now = std::move(Later);
	Later = Queue();
	for (const auto& Item : Now) Deliver(Item);
}

inline constexpr int ShutdownFlushPasses = 8;

// Shutdown: flush until nothing is left, but at most MaxPasses batches, so a callback that queues
// a new call every time it fails cannot keep the game from closing. Returns how many calls were
// still queued after the last pass; they are dropped without a callback.
template <typename Queue, typename Complete>
inline size_t FlushAll(Queue& Later, Complete Deliver, int MaxPasses = ShutdownFlushPasses)
{
	// Range-for only, so this works for both TArray (the plugin) and std::vector (the tests).
	for (int Pass = 0; Pass < MaxPasses && Later.begin() != Later.end(); ++Pass) FlushLater(Later, Deliver);
	size_t Dropped = 0;
	for (const auto& Item : Later) { (void)Item; ++Dropped; }
	Later = Queue();
	return Dropped;
}

// The request types (yg_call's `type`). Score, leaderboard and save are the types the YouGame app
// answers for a game the player started (desktop local-channel.ts STANDALONE_CALL_TYPES), the same
// ones a game the app starts sends (yougame.h yg_submit_score, yg_leaderboard, yg_save_*).
namespace Type
{
inline constexpr const char* SignIn = "signIn";
inline constexpr const char* SubmitScore = "submitScore";
inline constexpr const char* Leaderboard = "leaderboard";
inline constexpr const char* SaveWrite = "saveWrite";
inline constexpr const char* SaveLoad = "saveLoad";
inline constexpr const char* SaveClear = "saveClear";
inline constexpr const char* OpenFeedback = "openFeedback";
inline constexpr const char* FindMatch = "mpTicket";
inline constexpr const char* MatchResult = "mpResult";
inline constexpr const char* FriendRoom = "mpFriendRoom";
inline constexpr const char* InviteFriend = "mpInvite";
inline constexpr const char* MatchState = "matchState";
inline constexpr const char* InputKind = "inputKind";
inline constexpr const char* MenuButton = "menuButton";
inline constexpr const char* GameState = "gameState";
// Browser builds only (YouGameWeb.cpp): the player facts the desktop's hello carries, and room
// traffic through the page's SDK. On the desktop Room Send answers notAvailable for now.
inline constexpr const char* Hello = "hello";
inline constexpr const char* RoomSend = "roomSend";
}

// Browser builds: the requests the shim stops timing once the page's SDK has them (its own
// searching card with Cancel, or sign-in leaving the page). Must match NO_TIMEOUT in
// Private/Web/yougame-unreal-web.js (web_shim_test.mjs pins the same four).
inline bool WebUntimed(const char* RequestType)
{
	const std::string T = RequestType ? RequestType : "";
	return T == Type::FindMatch || T == Type::FriendRoom || T == Type::SignIn || T == "gameOver";
}

// The pushes YouGame sends (on_push's `type`). playerArrived is taken by yougame.h itself and
// reaches the plugin as config.on_player, never as a push.
namespace Push
{
inline constexpr const char* SignedIn = "signedIn";
inline constexpr const char* SignInFailed = "signInFailed";
inline constexpr const char* InviteNotShown = "inviteNotShown";
inline constexpr const char* UpdateChanged = "updateChanged";
inline constexpr const char* FriendRoom = "friendRoom";
// Room traffic: {from, data} and {players:[{id, name}], host, you, playing, closed?}.
inline constexpr const char* RoomMessage = "roomMessage";
inline constexpr const char* RoomPlayers = "roomPlayers";
}

// A JSON string literal, quotes included. UTF-8 passes through; quotes, backslashes and control
// characters are escaped.
inline std::string Quote(const std::string& Text)
{
	std::string Out;
	Out.reserve(Text.size() + 2);
	Out += '"';
	for (unsigned char C : Text)
	{
		switch (C)
		{
		case '"': Out += "\\\""; break;
		case '\\': Out += "\\\\"; break;
		case '\n': Out += "\\n"; break;
		case '\r': Out += "\\r"; break;
		case '\t': Out += "\\t"; break;
		default:
			if (C < 0x20)
			{
				char Buf[8];
				std::snprintf(Buf, sizeof Buf, "\\u%04x", static_cast<unsigned>(C));
				Out += Buf;
			}
			else
			{
				Out += static_cast<char>(C);
			}
		}
	}
	Out += '"';
	return Out;
}

inline std::string Number(long long Value)
{
	char Buf[32];
	std::snprintf(Buf, sizeof Buf, "%lld", Value);
	return Buf;
}

// ---- Request bodies (always a JSON object) ------------------------------------------------------

inline std::string EmptyBody() { return "{}"; }

inline std::string SignInBody(const std::string& Provider)
{
	return Provider.empty() ? EmptyBody() : "{\"provider\":" + Quote(Provider) + "}";
}

inline std::string InviteFriendBody() { return EmptyBody(); }

// EYouGameInputKind's order; invalid enum values never become wire data.
inline std::string InputKindBody(unsigned Kind)
{
  static constexpr const char* Names[] = {"keys", "ps", "pad", "gc"};
  return Kind < 4 ? std::string("{\"kind\":\"") + Names[Kind] + "\"}" : "";
}

// EYouGameGameState's order (Playing, Paused, Menu); invalid enum values never become wire data.
inline std::string GameStateBody(unsigned State)
{
	static constexpr const char* Names[] = {"playing", "paused", "menu"};
	return State < 3 ? std::string("{\"state\":\"") + Names[State] + "\"}" : "";
}

// The same order as yougame.h's yg_game_state; 0 for an invalid value (yg_set_game_state refuses it).
inline int GameStateNative(unsigned State)
{
	static constexpr int States[] = {YG_GAME_PLAYING, YG_GAME_PAUSED, YG_GAME_MENU};
	return State < 3 ? States[State] : 0;
}

inline std::string MatchStateBody(bool bOpen)
{
	return bOpen ? "{\"open\":true}" : "{\"open\":false}";
}

// submitScore {score}: a whole number, 0 to 1e15 (yougame.h YG__MAX_SCORE).
inline std::string SubmitScoreBody(long long Score)
{
	const long long Max = 1000000000000000LL;
	return "{\"score\":" + Number(Score < 0 ? 0 : (Score > Max ? Max : Score)) + "}";
}

// leaderboard {limit}
inline std::string LeaderboardBody(int Limit) { return "{\"limit\":" + Number(Limit) + "}"; }

inline bool HasEmbeddedNul(const std::string& Text) { return Text.find('\0') != std::string::npos; }

// Text a game passes as data: JSON other than null (an object such as {"level":3}, an array, a
// number, true/false or a "string") goes as that value; any other text goes as a JSON string.
inline std::string JsonValue(const std::string& Data)
{
	const long Length = Data.empty() || HasEmbeddedNul(Data) ? -1 : yg_json_raw(Data.c_str(), "", nullptr, 0);
	if (Length > 0)
	{
		std::vector<char> Value(static_cast<size_t>(Length) + 1);
		if (yg_json_raw(Data.c_str(), "", Value.data(), Value.size()) == Length)
		{
			const std::string Raw(Value.data(), static_cast<size_t>(Length));
			if (Raw != "null") return Raw;
		}
	}
	return Quote(Data);
}

// saveWrite {data}: one save per game and player (there is no slot). Data goes as JsonValue, so
// the save is the same one the game's in-app copy reads. YouGame refuses a save over 64 KB written
// out (code bad_request).
inline std::string SaveWriteBody(const std::string& Data)
{
	return "{\"data\":" + JsonValue(Data) + "}";
}

// roomSend {data, to?}: Data as JsonValue, to everyone else in the room, or to the player whose
// room id is To (On Room Players' ids). The room takes 16 KB and 120 messages a second at most.
inline std::string RoomSendBody(const std::string& Data, const std::string& To)
{
	std::string Out = "{\"data\":" + JsonValue(Data);
	if (!To.empty()) Out += ",\"to\":" + Quote(To);
	return Out + "}";
}

// The lobby compatibility YouGame's rooms pair players on: 1-64 of [A-Za-z0-9._-]. Other
// characters are dropped (':' too: the rooms read markers such as "orca1:" and ":b<hex>" from it)
// and the rest is cut to 64. Empty when nothing is left or the key is one the rooms treat
// specially (Melee Unlocked's "friends-lobby-1" / "doubles-lobby-1", a "-squad" ending).
inline std::string LobbyCompatibility(const std::string& Value)
{
	std::string Out;
	for (const char C : Value)
	{
		if (Out.size() == 64) break;
		if ((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '.' || C == '_' || C == '-')
		{
			Out += C;
		}
	}
	const std::string Squad = "-squad";
	if (Out == "friends-lobby-1" || Out == "doubles-lobby-1" ||
		(Out.size() >= Squad.size() && Out.compare(Out.size() - Squad.size(), Squad.size(), Squad) == 0))
	{
		return std::string();
	}
	return Out;
}

// mpTicket {players, mode, ranked, lobby}; an empty mode is left out. The lobby is Orb Duel's
// {version 1, minPlayers = players, maxLocalPlayers 1, compatibility}, so the rooms pair only
// builds with the same compatibility; it is left out when the compatibility comes out empty.
inline std::string FindMatchBody(int Players, const std::string& Mode, bool bRanked, const std::string& Compatibility = std::string())
{
	std::string Out = "{\"players\":" + Number(Players);
	if (!Mode.empty()) Out += ",\"mode\":" + Quote(Mode);
	Out += std::string(",\"ranked\":") + (bRanked ? "true" : "false");
	const std::string Key = LobbyCompatibility(Compatibility);
	if (!Key.empty())
	{
		Out += ",\"lobby\":{\"version\":1,\"minPlayers\":" + Number(Players) + ",\"maxLocalPlayers\":1,\"compatibility\":" + Quote(Key) + "}";
	}
	return Out + "}";
}

// mpResult {won, score}: Outcome 1 won, 0 lost, 2 a draw ("draw").
inline std::string MatchResultBody(int Outcome, long long Score)
{
	const char* Won = Outcome == 2 ? "\"draw\"" : (Outcome == 1 ? "true" : "false");
	return std::string("{\"won\":") + Won + ",\"score\":" + Number(Score) + "}";
}

// mpFriendRoom {players, mode?, fresh?}; join uses only {room}.
inline std::string FriendRoomBody(int Players, const std::string& Mode, bool bFresh = false)
{
	std::string Out = "{\"players\":" + Number(Players);
	if (!Mode.empty()) Out += ",\"mode\":" + Quote(Mode);
	if (bFresh) Out += ",\"fresh\":true";
	return Out + "}";
}

inline std::string JoinFriendRoomBody(const std::string& Room) { return "{\"room\":" + Quote(Room) + "}"; }

// ---- Reading answers -----------------------------------------------------------------------------

// A string field, whole (any length). False when absent or not a string.
inline bool ReadString(const char* Json, const std::string& Path, std::string& Out)
{
	if (!Json || !*Json) return false;
	char Small[256];
	const long Length = yg_json_string(Json, Path.c_str(), Small, sizeof Small);
	if (Length < 0) return false;
	if (static_cast<size_t>(Length) < sizeof Small)
	{
		Out.assign(Small, static_cast<size_t>(Length));
		return true;
	}
	std::vector<char> Big(static_cast<size_t>(Length) + 1);
	const long Again = yg_json_string(Json, Path.c_str(), Big.data(), Big.size());
	if (Again < 0) return false;
	Out.assign(Big.data(), static_cast<size_t>(Again));
	return true;
}

// A field as text: a string's own text, or any other value's JSON. False when absent.
inline bool ReadText(const char* Json, const std::string& Path, std::string& Out)
{
	if (ReadString(Json, Path, Out)) return true;
	if (!Json || !*Json) return false;
	// yg_json_raw says the length always and copies only when it fits.
	const long Length = yg_json_raw(Json, Path.c_str(), nullptr, 0);
	if (Length < 0) return false;
	std::vector<char> Buf(static_cast<size_t>(Length) + 1);
	if (yg_json_raw(Json, Path.c_str(), Buf.data(), Buf.size()) != Length) return false;
	Out.assign(Buf.data(), static_cast<size_t>(Length));
	return true;
}

inline long long OpenFeedback(const std::string& Body, yg_callback Callback, void* User)
{
	std::string Kind, Draft;
	ReadString(Body.c_str(), "kind", Kind);
	ReadString(Body.c_str(), "draftText", Draft);
	const int FeedbackKind = Kind == "bug" ? YG_FEEDBACK_BUG : (Kind == "idea" ? YG_FEEDBACK_IDEA : YG_FEEDBACK_ASK);
	return yg_open_feedback(FeedbackKind, Draft.empty() ? nullptr : Draft.c_str(), Callback, User);
}

inline bool ReadNumber(const char* Json, const std::string& Path, double& Out)
{
	return Json && *Json && yg_json_number(Json, Path.c_str(), &Out) == 1;
}

inline bool ReadBool(const char* Json, const std::string& Path, bool& Out)
{
	int Value = 0;
	if (!Json || !*Json || yg_json_bool(Json, Path.c_str(), &Value) != 1) return false;
	Out = Value != 0;
	return true;
}

// hello's `signedIn`: 1 or 0, or -1 when hello does not say (an app before sign-in facts, or a game
// the app started: then the player in hello says it).
inline int ReadSignedIn(const char* HelloJson)
{
	bool Value = false;
	if (!ReadBool(HelloJson, "signedIn", Value)) return -1;
	return Value ? 1 : 0;
}

// `update` in hello's answer and in the updateChanged push: {state, percent?, version?}.
enum class EUpdateState { None, Downloading, Ready, Installing, Waiting, Unknown };

inline EUpdateState UpdateState(const std::string& State)
{
	if (State.empty() || State == "none") return EUpdateState::None;
	if (State == "downloading") return EUpdateState::Downloading;
	if (State == "ready") return EUpdateState::Ready;
	if (State == "installing") return EUpdateState::Installing;
	if (State == "waiting") return EUpdateState::Waiting;
	return EUpdateState::Unknown;
}

struct FUpdate
{
	bool bPresent = false;
	std::string State;   // "downloading", "ready", "installing", "waiting", "none", ...
	int Percent = -1;    // 0..100, or -1 when not given
	std::string Version; // "" when not given
};

inline FUpdate ReadUpdate(const char* Json)
{
	FUpdate Out;
	if (!ReadString(Json, "update.state", Out.State)) return Out;
	Out.bPresent = true;
	double Percent = 0;
	if (Out.State == "downloading" && ReadNumber(Json, "update.percent", Percent) && std::isfinite(Percent))
	{
		Out.Percent = Percent < 0 ? 0 : (Percent > 100 ? 100 : static_cast<int>(Percent));
	}
	ReadString(Json, "update.version", Out.Version);
	return Out;
}

// signIn and mpInvite answer {opened:true, where:"overlay"|"browser"|"window"} ("window": YouGame Online's own
// sign-in window). Older apps omit where.
struct FOpened
{
	bool bOpened = false;
	std::string Where;
};

inline FOpened ReadOpened(const char* Json)
{
	FOpened Out;
	ReadBool(Json, "opened", Out.bOpened);
	ReadString(Json, "where", Out.Where);
	return Out;
}

// The page can still answer signed_out (standalone-friend-room.test.cjs); one game-facing code.
inline std::string FailureCode(const std::string& Code)
{
	return Code == "signed_out" ? "signedOut" : Code;
}

enum class ESignInAnswer { Opened, AlreadySignedIn, Busy, SignInFailed, BadRequest, Failure };

inline ESignInAnswer SignInAnswer(bool bOk, const std::string& Code)
{
	if (bOk) return ESignInAnswer::Opened;
	if (Code == "alreadySignedIn") return ESignInAnswer::AlreadySignedIn;
	if (Code == "busy") return ESignInAnswer::Busy;
	if (Code == "signInFailed") return ESignInAnswer::SignInFailed;
	if (Code == "badRequest") return ESignInAnswer::BadRequest;
	return ESignInAnswer::Failure;
}

enum class EPush { Unknown, SignedIn, SignInFailed, InviteNotShown, UpdateChanged, FriendRoom, RoomMessage, RoomPlayers };

inline EPush PushKind(const std::string& Type)
{
	if (Type == Push::SignedIn) return EPush::SignedIn;
	if (Type == Push::SignInFailed) return EPush::SignInFailed;
	if (Type == Push::InviteNotShown) return EPush::InviteNotShown;
	if (Type == Push::UpdateChanged) return EPush::UpdateChanged;
	if (Type == Push::FriendRoom) return EPush::FriendRoom;
	if (Type == Push::RoomMessage) return EPush::RoomMessage;
	if (Type == Push::RoomPlayers) return EPush::RoomPlayers;
	return EPush::Unknown;
}

// friendRoom pushes are facts to join, never tickets. Do not log Room.
struct FFriendRoom
{
	std::string Room;
	int Players = 2;
	std::string Mode;
};

inline FFriendRoom ReadFriendRoom(const char* Json)
{
	FFriendRoom Out;
	ReadString(Json, "room", Out.Room);
	double Players = 0;
	if (ReadNumber(Json, "players", Players) && Players >= 1 && Players <= 16) Out.Players = static_cast<int>(Players);
	ReadString(Json, "mode", Out.Mode);
	return Out;
}

// Generic push data is for game logic, never logs. Keep friendRoom's code only in its typed
// event; rebuild from the public fields so duplicate/escaped room keys cannot leak it.
inline std::string PushPayload(const std::string& Type, const char* Json)
{
	if (PushKind(Type) != EPush::FriendRoom) return Json ? Json : "";
	const FFriendRoom Fact = ReadFriendRoom(Json);
	std::string Out = "{\"players\":" + Number(Fact.Players);
	if (!Fact.Mode.empty()) Out += ",\"mode\":" + Quote(Fact.Mode);
	return Out + "}";
}

// submitScore's answer {score_id, best, previous_best, is_personal_best, rank, total, share_url, ...}:
// the parts a game shows.
struct FScore
{
	bool bHasRank = false;
	long long Rank = 0;
	bool bHasBest = false;
	long long Best = 0;
	bool bPersonalBest = false;
};

inline FScore ReadScore(const char* ResultJson)
{
	FScore Out;
	double Value = 0;
	if (ReadNumber(ResultJson, "rank", Value)) { Out.bHasRank = true; Out.Rank = static_cast<long long>(Value); }
	if (ReadNumber(ResultJson, "best", Value)) { Out.bHasBest = true; Out.Best = static_cast<long long>(Value); }
	ReadBool(ResultJson, "is_personal_best", Out.bPersonalBest);
	return Out;
}

// leaderboard's answer {entries:[{id, rank, name, score, player_id, at, ...}], total, me}: rows under
// `entries` (also `rows`, or the answer itself when it is an array). A row is the player's own when
// its player_id is me.player_id (or it says `you`). The game is off the board: refused, code board_off.
struct FRow
{
	long long Rank = 0;
	long long Score = 0;
	bool bYou = false;
	std::string Name; // "" when YouGame gives no public name
};

inline std::vector<FRow> ReadRows(const char* ResultJson)
{
	std::vector<FRow> Rows;
	std::string Base;
	long Count = yg_json_count(ResultJson, "entries");
	if (Count >= 0) Base = "entries.";
	else if ((Count = yg_json_count(ResultJson, "rows")) >= 0) Base = "rows.";
	else
	{
		const char* P = ResultJson ? ResultJson : "";
		while (*P == ' ' || *P == '\t' || *P == '\n' || *P == '\r') ++P;
		if (*P != '[' || (Count = yg_json_count(ResultJson, "")) < 0) return Rows;
	}
	if (Count > 100) Count = 100;
	std::string MyId; // compared only, never shown or logged
	ReadString(ResultJson, "me.player_id", MyId);
	for (long I = 0; I < Count; ++I)
	{
		const std::string At = Base + Number(I) + ".";
		FRow Row;
		double Value = 0;
		if (ReadNumber(ResultJson, At + "rank", Value)) Row.Rank = static_cast<long long>(Value);
		if (ReadNumber(ResultJson, At + "score", Value)) Row.Score = static_cast<long long>(Value);
		ReadBool(ResultJson, At + "you", Row.bYou);
		std::string Id;
		if (!Row.bYou && !MyId.empty() && ReadString(ResultJson, At + "player_id", Id) && Id == MyId) Row.bYou = true;
		ReadString(ResultJson, At + "name", Row.Name);
		Rows.push_back(Row);
	}
	return Rows;
}

// saveLoad's answer {save: {data, build, updated_at} or null, cloud}: data as the game saved it (a
// string's own text; any other JSON comes back as its text). False: no save (save null, or cloud
// false: signed out).
inline bool ReadSaveData(const char* ResultJson, std::string& Out)
{
	// A saved string "null" is data, distinct from a JSON null value.
	if (ReadString(ResultJson, "save.data", Out)) return true;
	std::string Raw;
	if (!ReadText(ResultJson, "save.data", Raw)) return false;
	if (Raw == "null") return false;
	Out = Raw;
	return true;
}

// saveWrite's, saveLoad's and saveClear's answer says cloud:false when the player is signed out (the
// call did nothing).
inline bool ReadSignedOutSave(const char* ResultJson)
{
	bool Cloud = true;
	return ReadBool(ResultJson, "cloud", Cloud) && !Cloud;
}

// mpTicket's and mpFriendRoom's answer, and the friendRoom push: {ticket, url, queue?, room?}.
struct FTicket
{
	std::string Ticket;
	std::string Url;
	std::string Queue; // a string, or the JSON of whatever YouGame sent
	std::string Room;
};

inline FTicket ReadTicket(const char* Json)
{
	FTicket Out;
	ReadString(Json, "ticket", Out.Ticket);
	ReadString(Json, "url", Out.Url);
	ReadText(Json, "queue", Out.Queue);
	ReadText(Json, "room", Out.Room);
	return Out;
}

// ---- Browser builds (YouGameWeb.cpp and the shim, Private/Web/yougame-unreal-web.js) -------------

// The shim's start: which SDK to load (staging.yougame.co or yougame.co), the game's slug for
// YouGame.init({game}) (Game Slug; empty: the shim skips init and says so), the lobby compatibility
// rooms pair on (LobbyCompatibility; empty: the shim uses "default") and the build's version.
inline std::string WebConfigBody(bool bStaging, const std::string& GameSlug, const std::string& Compatibility, const std::string& Version)
{
	return std::string("{\"staging\":") + (bStaging ? "true" : "false") + ",\"game\":" + Quote(GameSlug) +
		",\"compatibility\":" + Quote(LobbyCompatibility(Compatibility)) + ",\"version\":" + Quote(Version) + "}";
}

// Every answer the shim hands back: {ok:true, result} or {ok:false, code, error, retryAfter?}, the
// shape yougame.h's channel answers have. Unreadable: not ok, code noAnswer.
struct FEnvelope
{
	bool bOk = false;
	std::string Code;
	std::string Error;
	int RetryAfterSeconds = 0;
	std::string ResultJson;
};

inline FEnvelope ReadEnvelope(const char* Json)
{
	FEnvelope Out;
	if (!ReadBool(Json, "ok", Out.bOk))
	{
		Out.Code = "noAnswer";
		Out.Error = "The answer could not be read";
		return Out;
	}
	if (Out.bOk)
	{
		if (!ReadText(Json, "result", Out.ResultJson) || Out.ResultJson == "null") Out.ResultJson = "{}";
		return Out;
	}
	ReadString(Json, "code", Out.Code);
	Out.Code = FailureCode(Out.Code);
	if (Out.Code.empty()) Out.Code = "failed";
	ReadString(Json, "error", Out.Error);
	double Retry = 0;
	if (ReadNumber(Json, "retryAfter", Retry) && std::isfinite(Retry) && Retry > 0) Out.RetryAfterSeconds = Retry > 86400 ? 86400 : static_cast<int>(std::ceil(Retry));
	return Out;
}

// hello's answer in a browser: {signedIn, player:{name}, onYouGame, feedback}.
struct FWebHello
{
	int SignedIn = -1; // as ReadSignedIn
	std::string Name;
	bool bOnYouGame = false;
	bool bFeedback = false;
};

inline FWebHello ReadWebHello(const char* ResultJson)
{
	FWebHello Out;
	Out.SignedIn = ReadSignedIn(ResultJson);
	ReadString(ResultJson, "player.name", Out.Name);
	ReadBool(ResultJson, "onYouGame", Out.bOnYouGame);
	ReadBool(ResultJson, "feedback", Out.bFeedback);
	return Out;
}

// roomMessage {from, data}: From is the sender's room id; Data a string's own text, or any other
// value's JSON.
struct FRoomMessage
{
	std::string From;
	std::string Data;
};

inline FRoomMessage ReadRoomMessage(const char* Json)
{
	FRoomMessage Out;
	ReadText(Json, "from", Out.From);
	if (!ReadText(Json, "data", Out.Data)) Out.Data.clear();
	return Out;
}

// roomPlayers {players:[{id, name}], host, you, playing, closed?}: who is in the room, in join
// order (the same on every client), who hosts and which id is this player. Closed: the room is gone.
struct FRoomPlayer
{
	std::string Id;
	std::string Name;
};

struct FRoomPlayers
{
	std::vector<FRoomPlayer> Players;
	std::string Host;
	std::string You;
	bool bPlaying = false;
	bool bClosed = false;
};

inline FRoomPlayers ReadRoomPlayers(const char* Json)
{
	FRoomPlayers Out;
	long Count = yg_json_count(Json ? Json : "", "players");
	if (Count > 200) Count = 200;
	for (long I = 0; I < Count; ++I)
	{
		const std::string At = "players." + Number(I) + ".";
		FRoomPlayer Player;
		ReadText(Json, At + "id", Player.Id);
		ReadString(Json, At + "name", Player.Name);
		Out.Players.push_back(Player);
	}
	ReadText(Json, "host", Out.Host);
	ReadText(Json, "you", Out.You);
	ReadBool(Json, "playing", Out.bPlaying);
	ReadBool(Json, "closed", Out.bClosed);
	return Out;
}
} // namespace YouGameWire
