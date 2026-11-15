#include "YouGameAsyncActions.h"

#include "Containers/Ticker.h"
#include "YouGameSubsystem.h"
#include "YouGameWire.h"

namespace
{
	template <typename ActionType>
	ActionType* NewAction(UObject* WorldContextObject)
	{
		ActionType* Action = NewObject<ActionType>();
		Action->RegisterWithGameInstance(WorldContextObject);
		return Action;
	}

	FString FromUtf8(const std::string& Text)
	{
		return Text.empty() ? FString() : FString(UTF8_TO_TCHAR(Text.c_str()));
	}

	std::string ToUtf8(const FString& Text)
	{
		const FTCHARToUTF8 Utf8(*Text, Text.Len());
		return std::string(Utf8.Get(), Utf8.Length());
	}

	FString Body(const std::string& Json)
	{
		return FromUtf8(Json);
	}

	/** The answer's JSON as UTF-8, for YouGameWire's readers. */
	std::string Answer(const FYouGameReply& Reply)
	{
		return ToUtf8(Reply.ResultJson);
	}

	FYouGameOpenResult ReadOpened(const FYouGameReply& Reply)
	{
		FYouGameOpenResult Result;
		Result.Reply = Reply;
		if (Reply.bOk)
		{
			const YouGameWire::FOpened Opened = YouGameWire::ReadOpened(Answer(Reply).c_str());
			Result.bOpened = Opened.bOpened;
			if (Opened.Where == "overlay") Result.Where = EYouGameOpenedWhere::Overlay;
			else if (Opened.Where == "browser") Result.Where = EYouGameOpenedWhere::Browser;
		}
		return Result;
	}

	/** A save call answered cloud:false (signed out, nothing done): On Failure with Code signedOut. */
	FYouGameReply SaveReply(const FYouGameReply& Reply)
	{
		if (Reply.bOk && YouGameWire::ReadSignedOutSave(Answer(Reply).c_str()))
		{
			return FYouGameReply::Failure(TEXT("signedOut"), TEXT("Sign in to YouGame to keep your save."));
		}
		return Reply;
	}

	/** mpTicket waits on YouGame's queue route, so it gets longer than the usual 15 s. */
	constexpr float TicketTimeoutSeconds = 30.f;
}

// ---------------------------------------------------------------------------------------------
// Shared

void UYouGameAsyncAction::ValidateInput(const FString& Text)
{
	bInputHasNul |= YouGameWire::HasEmbeddedNul(ToUtf8(Text));
}

void UYouGameAsyncAction::Prepare(UObject* WorldContextObject, const FString& InType, const FString& InBody, float InTimeoutSeconds)
{
	ValidateInput(InType);
	ValidateInput(InBody);
	WorldContext = WorldContextObject;
	RequestType = InType;
	RequestBody = InBody;
	TimeoutSeconds = InTimeoutSeconds;
}

void UYouGameAsyncAction::Activate()
{
	const TWeakObjectPtr<UYouGameAsyncAction> WeakThis(this);
	FYouGameReplyCallback Done = [WeakThis](const FYouGameReply& Reply)
	{
		if (UYouGameAsyncAction* Self = WeakThis.Get())
		{
			Self->Deliver(Reply);
		}
	};

	UYouGameSubsystem* YouGame = UYouGameSubsystem::Get(WorldContext.Get());
	if (bInputHasNul || YouGame == nullptr)
	{
		// Input refusals and an unavailable game instance both answer on a later tick.
		const FYouGameReply Reply = bInputHasNul
			? FYouGameReply::Failure(TEXT("badRequest"), TEXT("YouGame text cannot contain an embedded NUL character"))
			: FYouGameReply::Failure(TEXT("notAvailable"), TEXT("YouGame is not available here"));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Done, Reply](float) -> bool
		{
			Done(Reply);
			return false;
		}));
		return;
	}
	YouGame->Call(RequestType, RequestBody, MoveTemp(Done), TimeoutSeconds);
}

void UYouGameAsyncAction::Deliver(const FYouGameReply& Reply)
{
	if (bDelivered)
	{
		return;
	}
	bDelivered = true;
	HandleReply(Reply);
	SetReadyToDestroy();
}

void UYouGameReplyAction::HandleReply(const FYouGameReply& Reply)
{
	(Reply.bOk ? OnSuccess : OnFailure).Broadcast(Reply);
}

// ---------------------------------------------------------------------------------------------
// Sign In

UYouGameSignInAction* UYouGameSignInAction::SignIn(UObject* WorldContextObject, const FString& Provider)
{
	UYouGameSignInAction* Action = NewAction<UYouGameSignInAction>(WorldContextObject);
	Action->ValidateInput(Provider);
	Action->Prepare(WorldContextObject, FString(YouGameWire::Type::SignIn), Body(YouGameWire::SignInBody(ToUtf8(Provider))));
	return Action;
}

void UYouGameSignInAction::HandleReply(const FYouGameReply& Reply)
{
	const FYouGameOpenResult Result = ReadOpened(Reply);
	switch (YouGameWire::SignInAnswer(Reply.bOk, ToUtf8(Reply.Code)))
	{
	case YouGameWire::ESignInAnswer::Opened: OnSuccess.Broadcast(Result); break;
	case YouGameWire::ESignInAnswer::AlreadySignedIn: OnAlreadySignedIn.Broadcast(Result); break;
	case YouGameWire::ESignInAnswer::Busy: OnBusy.Broadcast(Result); break;
	case YouGameWire::ESignInAnswer::SignInFailed: OnSignInFailed.Broadcast(Result); break;
	case YouGameWire::ESignInAnswer::BadRequest: OnBadRequest.Broadcast(Result); break;
	default: OnFailure.Broadcast(Result); break;
	}
}

// ---------------------------------------------------------------------------------------------
// Submit Score

UYouGameSubmitScoreAction* UYouGameSubmitScoreAction::SubmitScore(UObject* WorldContextObject, int64 Score)
{
	UYouGameSubmitScoreAction* Action = NewAction<UYouGameSubmitScoreAction>(WorldContextObject);
	Action->Prepare(WorldContextObject, FString(YouGameWire::Type::SubmitScore), Body(YouGameWire::SubmitScoreBody(Score)));
	return Action;
}

void UYouGameSubmitScoreAction::HandleReply(const FYouGameReply& Reply)
{
	FYouGameScoreResult Result;
	Result.Reply = Reply;
	if (Reply.bOk)
	{
		const YouGameWire::FScore Score = YouGameWire::ReadScore(Answer(Reply).c_str());
		Result.bHasRank = Score.bHasRank;
		Result.Rank = Score.Rank;
		Result.bHasBest = Score.bHasBest;
		Result.Best = Score.Best;
		Result.bPersonalBest = Score.bPersonalBest;
	}
	(Reply.bOk ? OnSuccess : OnFailure).Broadcast(Result);
}

// ---------------------------------------------------------------------------------------------
// Get Leaderboard

UYouGameGetLeaderboardAction* UYouGameGetLeaderboardAction::GetLeaderboard(UObject* WorldContextObject, int32 Limit)
{
	UYouGameGetLeaderboardAction* Action = NewAction<UYouGameGetLeaderboardAction>(WorldContextObject);
	Action->Prepare(WorldContextObject, FString(YouGameWire::Type::Leaderboard), Body(YouGameWire::LeaderboardBody(FMath::Clamp(Limit, 1, 100))));
	return Action;
}

void UYouGameGetLeaderboardAction::HandleReply(const FYouGameReply& Reply)
{
	FYouGameLeaderboardResult Result;
	Result.Reply = Reply;
	if (Reply.bOk)
	{
		for (const YouGameWire::FRow& Row : YouGameWire::ReadRows(Answer(Reply).c_str()))
		{
			FYouGameLeaderboardRow& Out = Result.Rows.AddDefaulted_GetRef();
			Out.Rank = Row.Rank;
			Out.Score = Row.Score;
			Out.bYou = Row.bYou;
			Out.Name = FromUtf8(Row.Name);
		}
	}
	(Reply.bOk ? OnSuccess : OnFailure).Broadcast(Result);
}

// ---------------------------------------------------------------------------------------------
// Save / Load / Clear Save

UYouGameSaveAction* UYouGameSaveAction::Save(UObject* WorldContextObject, const FString& Data)
{
	UYouGameSaveAction* Action = NewAction<UYouGameSaveAction>(WorldContextObject);
	Action->ValidateInput(Data);
	Action->Prepare(WorldContextObject, FString(YouGameWire::Type::SaveWrite), Body(YouGameWire::SaveWriteBody(ToUtf8(Data))));
	return Action;
}

void UYouGameSaveAction::HandleReply(const FYouGameReply& Reply)
{
	UYouGameReplyAction::HandleReply(SaveReply(Reply));
}

UYouGameClearSaveAction* UYouGameClearSaveAction::ClearSave(UObject* WorldContextObject)
{
	UYouGameClearSaveAction* Action = NewAction<UYouGameClearSaveAction>(WorldContextObject);
	Action->Prepare(WorldContextObject, FString(YouGameWire::Type::SaveClear), Body(YouGameWire::EmptyBody()));
	return Action;
}

void UYouGameClearSaveAction::HandleReply(const FYouGameReply& Reply)
{
	UYouGameReplyAction::HandleReply(SaveReply(Reply));
}

UYouGameLoadAction* UYouGameLoadAction::Load(UObject* WorldContextObject)
{
	UYouGameLoadAction* Action = NewAction<UYouGameLoadAction>(WorldContextObject);
	Action->Prepare(WorldContextObject, FString(YouGameWire::Type::SaveLoad), Body(YouGameWire::EmptyBody()));
	return Action;
}

void UYouGameLoadAction::HandleReply(const FYouGameReply& Reply)
{
	FYouGameLoadResult Result;
	Result.Reply = SaveReply(Reply);
	if (Result.Reply.bOk)
	{
		std::string Data;
		Result.bHasData = YouGameWire::ReadSaveData(Answer(Reply).c_str(), Data);
		Result.Data = FromUtf8(Data);
	}
	(Result.Reply.bOk ? OnSuccess : OnFailure).Broadcast(Result);
}

// ---------------------------------------------------------------------------------------------
// Open Feedback

UYouGameOpenFeedbackAction* UYouGameOpenFeedbackAction::OpenFeedback(UObject* WorldContextObject)
{
	UYouGameOpenFeedbackAction* Action = NewAction<UYouGameOpenFeedbackAction>(WorldContextObject);
	// The subsystem sends this through yg_open_feedback (same request type, plus the browser fallback).
	Action->Prepare(WorldContextObject, FString(YouGameWire::Type::OpenFeedback), Body(YouGameWire::EmptyBody()));
	return Action;
}

// ---------------------------------------------------------------------------------------------
// Matches

namespace
{
	FYouGameMatchTicket ReadTicket(const FYouGameReply& Reply)
	{
		FYouGameMatchTicket Result;
		Result.Reply = Reply;
		if (Reply.bOk)
		{
			const YouGameWire::FTicket Ticket = YouGameWire::ReadTicket(Answer(Reply).c_str());
			Result.Ticket = FromUtf8(Ticket.Ticket);
			Result.Url = FromUtf8(Ticket.Url);
			Result.Queue = FromUtf8(Ticket.Queue);
			Result.Room = FromUtf8(Ticket.Room);
		}
		return Result;
	}
}

UYouGameFindMatchAction* UYouGameFindMatchAction::FindMatch(UObject* WorldContextObject, const FString& Mode, bool bRanked)
{
	UYouGameFindMatchAction* Action = NewAction<UYouGameFindMatchAction>(WorldContextObject);
	Action->ValidateInput(Mode);
	Action->bRankedAsked = bRanked;
	Action->Prepare(WorldContextObject, FString(YouGameWire::Type::FindMatch), Body(YouGameWire::FindMatchBody(2, ToUtf8(Mode), bRanked, ToUtf8(UYouGameSubsystem::MatchCompatibility()))), TicketTimeoutSeconds);
	return Action;
}

void UYouGameFindMatchAction::HandleReply(const FYouGameReply& Reply)
{
	FYouGameMatchTicket Result = ReadTicket(Reply);
	Result.bRanked = bRankedAsked;
	(Reply.bOk ? OnSuccess : OnFailure).Broadcast(Result);
}

UYouGameFriendRoomAction* UYouGameFriendRoomAction::FriendRoom(UObject* WorldContextObject, const FString& Mode, int32 Players, bool bFresh)
{
	UYouGameFriendRoomAction* Action = NewAction<UYouGameFriendRoomAction>(WorldContextObject);
	Action->ValidateInput(Mode);
	Action->Prepare(WorldContextObject, FString(YouGameWire::Type::FriendRoom), Body(YouGameWire::FriendRoomBody(Players, ToUtf8(Mode), bFresh)), TicketTimeoutSeconds);
	return Action;
}

UYouGameJoinFriendRoomAction* UYouGameJoinFriendRoomAction::JoinFriendRoom(UObject* WorldContextObject, const FString& Room)
{
	UYouGameJoinFriendRoomAction* Action = NewAction<UYouGameJoinFriendRoomAction>(WorldContextObject);
	Action->ValidateInput(Room);
	Action->Prepare(WorldContextObject, FString(YouGameWire::Type::FriendRoom), Body(YouGameWire::JoinFriendRoomBody(ToUtf8(Room))), TicketTimeoutSeconds);
	return Action;
}

void UYouGameFriendRoomAction::HandleReply(const FYouGameReply& Reply)
{
	const FYouGameMatchTicket Result = ReadTicket(Reply);
	(Reply.bOk ? OnSuccess : OnFailure).Broadcast(Result);
}

UYouGameInviteFriendAction* UYouGameInviteFriendAction::InviteFriend(UObject* WorldContextObject)
{
	UYouGameInviteFriendAction* Action = NewAction<UYouGameInviteFriendAction>(WorldContextObject);
	Action->Prepare(WorldContextObject, FString(YouGameWire::Type::InviteFriend), Body(YouGameWire::InviteFriendBody()));
	return Action;
}

void UYouGameInviteFriendAction::HandleReply(const FYouGameReply& Reply)
{
	const FYouGameOpenResult Result = ReadOpened(Reply);
	(Reply.bOk ? OnSuccess : OnFailure).Broadcast(Result);
}

UYouGameSetMatchStateAction* UYouGameSetMatchStateAction::SetMatchState(UObject* WorldContextObject, bool bOpen)
{
	UYouGameSetMatchStateAction* Action = NewAction<UYouGameSetMatchStateAction>(WorldContextObject);
	Action->Prepare(WorldContextObject, FString(YouGameWire::Type::MatchState), Body(YouGameWire::MatchStateBody(bOpen)));
	return Action;
}

void UYouGameSetMatchStateAction::HandleReply(const FYouGameReply& /*Reply*/)
{
	OnComplete.Broadcast();
}

UYouGameReportMatchResultAction* UYouGameReportMatchResultAction::ReportMatchResult(UObject* WorldContextObject, EYouGameMatchOutcome Outcome, int64 Score)
{
	UYouGameReportMatchResultAction* Action = NewAction<UYouGameReportMatchResultAction>(WorldContextObject);
	const int Won = Outcome == EYouGameMatchOutcome::Draw ? 2 : (Outcome == EYouGameMatchOutcome::Won ? 1 : 0);
	Action->Prepare(WorldContextObject, FString(YouGameWire::Type::MatchResult), Body(YouGameWire::MatchResultBody(Won, FMath::Max<int64>(Score, 0))));
	return Action;
}

// ---------------------------------------------------------------------------------------------
// Room Send (browser builds)

UYouGameRoomSendAction* UYouGameRoomSendAction::RoomSend(UObject* WorldContextObject, const FString& Data, const FString& To)
{
	UYouGameRoomSendAction* Action = NewAction<UYouGameRoomSendAction>(WorldContextObject);
	Action->ValidateInput(Data);
	Action->ValidateInput(To);
	Action->Prepare(WorldContextObject, FString(YouGameWire::Type::RoomSend), Body(YouGameWire::RoomSendBody(ToUtf8(Data), ToUtf8(To.TrimStartAndEnd()))));
	return Action;
}

// ---------------------------------------------------------------------------------------------
// Any request

UYouGameCallAction* UYouGameCallAction::Request(UObject* WorldContextObject, const FString& Type, const FString& BodyJson)
{
	UYouGameCallAction* Action = NewAction<UYouGameCallAction>(WorldContextObject);
	Action->Prepare(WorldContextObject, Type, BodyJson.TrimStartAndEnd());
	return Action;
}
