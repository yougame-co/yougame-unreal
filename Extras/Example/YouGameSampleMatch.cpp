// YouGameSampleMatch.cpp: the example's code (see YouGameSampleMatch.h for how to add it and its keys).
// Lines start "[yougame-unreal] sample: " for the web test. Player names, room codes and tickets are never logged.
#include "YouGameSampleMatch.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "HAL/PlatformProperties.h"
#include "YouGameSubsystem.h"
#include "YouGameAsyncActions.h"
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

static void SampleLog(const FString& S)
{
	const FString Line = TEXT("[yougame-unreal] sample: ") + S;
#ifdef __EMSCRIPTEN__
	emscripten_log(EM_LOG_CONSOLE, "%s", TCHAR_TO_UTF8(*Line));
#else
	UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
#endif
}

// Positions go out as {"p":[x,y,z,yaw]} (whole centimetres and degrees): small JSON, well under the room's limits.
static bool ParsePosition(const FString& Data, FVector& Out, float& Yaw)
{
	int32 At = Data.Find(TEXT("\"p\":["));
	if (At == INDEX_NONE) return false;
	At += 5;
	const int32 End = Data.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, At);
	if (End == INDEX_NONE) return false;
	TArray<FString> Parts;
	Data.Mid(At, End - At).ParseIntoArray(Parts, TEXT(","));
	if (Parts.Num() != 4) return false;
	Out = FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]));
	Yaw = FCString::Atof(*Parts[3]);
	return !Out.ContainsNaN() && FMath::IsFinite(Yaw);
}

void UYouGameSampleMatch::Tick(float DeltaTime)
{
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!PC) return;
	Played += DeltaTime;
	if (!bReady)
	{
		bReady = true;
		UYouGameSubsystem* YG = UYouGameSubsystem::Get(W);
		if (YG)
		{
			YG->OnRoomPlayers.AddUniqueDynamic(this, &UYouGameSampleMatch::OnRoomPlayers);
			YG->OnRoomMessage.AddUniqueDynamic(this, &UYouGameSampleMatch::OnRoomMessage);
			YG->OnFriendRoom.AddUniqueDynamic(this, &UYouGameSampleMatch::OnFriendRoom);
		}
		SampleLog(FString::Printf(TEXT("ready on %s, M = Find Match, F = Friend Room, I = Invite, K = score, L = leaderboard, B = feedback%s"),
			ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName()), YG ? TEXT("") : TEXT(" (no YouGame subsystem)")));
	}
	if (RoomSize >= 2)
	{
		SendPosition(false);
		MoveOthers(DeltaTime);
	}

	if (PC->WasInputKeyJustPressed(EKeys::M))
	{
		SampleLog(TEXT("find match pressed"));
		if (UYouGameFindMatchAction* A = UYouGameFindMatchAction::FindMatch(W, FString(), false))
		{
			A->OnSuccess.AddDynamic(this, &UYouGameSampleMatch::OnTicket);
			A->OnFailure.AddDynamic(this, &UYouGameSampleMatch::OnTicketFailed);
			A->Activate();
		}
	}
	if (PC->WasInputKeyJustPressed(EKeys::F))
	{
		SampleLog(TEXT("friend room pressed"));
		if (UYouGameFriendRoomAction* A = UYouGameFriendRoomAction::FriendRoom(W, FString(), 2, false))
		{
			A->OnSuccess.AddDynamic(this, &UYouGameSampleMatch::OnTicket);
			A->OnFailure.AddDynamic(this, &UYouGameSampleMatch::OnTicketFailed);
			A->Activate();
		}
	}
	if (PC->WasInputKeyJustPressed(EKeys::I))
	{
		SampleLog(TEXT("invite pressed"));
		if (UYouGameInviteFriendAction* A = UYouGameInviteFriendAction::InviteFriend(W))
		{
			A->OnSuccess.AddDynamic(this, &UYouGameSampleMatch::OnOpened);
			A->OnFailure.AddDynamic(this, &UYouGameSampleMatch::OnOpenFailed);
			A->Activate();
		}
	}
	if (PC->WasInputKeyJustPressed(EKeys::K))
	{
		const int64 Score = (int64)Played;
		SampleLog(FString::Printf(TEXT("submit score %lld pressed"), Score));
		if (UYouGameSubmitScoreAction* A = UYouGameSubmitScoreAction::SubmitScore(W, Score))
		{
			A->OnSuccess.AddDynamic(this, &UYouGameSampleMatch::OnScore);
			A->OnFailure.AddDynamic(this, &UYouGameSampleMatch::OnScoreFailed);
			A->Activate();
		}
	}
	if (PC->WasInputKeyJustPressed(EKeys::L))
	{
		SampleLog(TEXT("leaderboard pressed"));
		if (UYouGameGetLeaderboardAction* A = UYouGameGetLeaderboardAction::GetLeaderboard(W, 5))
		{
			A->OnSuccess.AddDynamic(this, &UYouGameSampleMatch::OnBoard);
			A->OnFailure.AddDynamic(this, &UYouGameSampleMatch::OnBoardFailed);
			A->Activate();
		}
	}
	if (PC->WasInputKeyJustPressed(EKeys::B))
	{
		SampleLog(TEXT("feedback pressed"));
		if (UYouGameOpenFeedbackAction* A = UYouGameOpenFeedbackAction::OpenFeedback(W))
		{
			A->OnSuccess.AddDynamic(this, &UYouGameSampleMatch::OnReply);
			A->OnFailure.AddDynamic(this, &UYouGameSampleMatch::OnReplyFailed);
			A->Activate();
		}
	}
}

// About 10 a second while moving, and once a second standing still, so a player who joins late still sees you.
void UYouGameSampleMatch::SendPosition(bool bForce)
{
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	APawn* Me = PC ? PC->GetPawn() : nullptr;
	if (!Me) return;
	const float Dt = W->GetDeltaSeconds();
	SinceSend += Dt;
	SinceForced += Dt;
	if (SinceSend < 0.1f) return;
	const FVector Pos = Me->GetActorLocation();
	const float Yaw = Me->GetActorRotation().Yaw;
	const bool bMoved = FVector::DistSquared(Pos, LastSent) > 1.f || FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, LastYaw)) > 1.f;
	if (!bForce && !bMoved && SinceForced < 1.f) return;
	SinceSend = 0.f;
	SinceForced = 0.f;
	LastSent = Pos;
	LastYaw = Yaw;
	const FString Data = FString::Printf(TEXT("{\"p\":[%d,%d,%d,%d]}"), int32(FMath::RoundToInt(Pos.X)), int32(FMath::RoundToInt(Pos.Y)), int32(FMath::RoundToInt(Pos.Z)), int32(FMath::RoundToInt(Yaw)));
	if (UYouGameRoomSendAction* Send = UYouGameRoomSendAction::RoomSend(W, Data, FString()))
	{
		Send->OnFailure.AddDynamic(this, &UYouGameSampleMatch::OnSendFailed);
		Send->Activate();
	}
}

// Each other player is a copy of your own pawn (no controller, no collision, no movement) that glides to where they last
// were.
void UYouGameSampleMatch::MoveOthers(float DeltaTime)
{
	for (TPair<FString, FYouGameSampleOther>& It : Others)
	{
		APawn* Body = It.Value.Body.Get();
		if (!Body) continue;
		const FVector From = Body->GetActorLocation();
		const FVector To = FMath::VInterpTo(From, It.Value.Target, DeltaTime, 12.f);
		Body->SetActorLocationAndRotation(To, FRotator(0.f, It.Value.Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		// Velocity for your character's Animation Blueprint to read (what it plays is up to it); movement itself is off.
		ACharacter* AsCharacter = Cast<ACharacter>(Body);
		if (UCharacterMovementComponent* Move = AsCharacter ? AsCharacter->GetCharacterMovement() : nullptr)
		{
			Move->Velocity = DeltaTime > 0.f ? (To - From) / DeltaTime : FVector::ZeroVector;
		}
	}
}

void UYouGameSampleMatch::OnTicket(const FYouGameMatchTicket& Result)
{
	SampleLog(FString::Printf(TEXT("room ok (queue %s, ranked %d), waiting for players"), *Result.Queue, Result.bRanked ? 1 : 0));
}

void UYouGameSampleMatch::OnTicketFailed(const FYouGameMatchTicket& Result)
{
	SampleLog(FString::Printf(TEXT("room failed: %s %s"), *Result.Reply.Code, *Result.Reply.Error));
}

void UYouGameSampleMatch::OnFriendRoom(const FYouGameFriendRoom& Room)
{
	// A friend's invite opened the game: join their room.
	SampleLog(TEXT("invite accepted, joining the friend's room"));
	if (UYouGameJoinFriendRoomAction* A = UYouGameJoinFriendRoomAction::JoinFriendRoom(GetWorld(), Room.Room))
	{
		A->OnSuccess.AddDynamic(this, &UYouGameSampleMatch::OnTicket);
		A->OnFailure.AddDynamic(this, &UYouGameSampleMatch::OnTicketFailed);
		A->Activate();
	}
}

void UYouGameSampleMatch::OnRoomPlayers(const TArray<FYouGameRoomPlayer>& Players, const FString& Host, const FString& You, bool bPlaying)
{
	MyId = You;
	RoomSize = Players.Num();
	SampleLog(FString::Printf(TEXT("room players %d (host is me: %d, playing %d)"), Players.Num(), (!You.IsEmpty() && Host == You) ? 1 : 0, bPlaying ? 1 : 0));
	// Remove the copies of players who left.
	TSet<FString> Here;
	for (const FYouGameRoomPlayer& P : Players) Here.Add(P.Id);
	for (auto It = Others.CreateIterator(); It; ++It)
	{
		if (Here.Contains(It.Key())) continue;
		if (APawn* Body = It.Value().Body.Get()) Body->Destroy();
		It.RemoveCurrent();
	}
	if (Players.Num() < 2) { bSentHello = false; return; }
	if (!bSentHello)
	{
		bSentHello = true;
		const FString Data = FString::Printf(TEXT("{\"hello\":\"%s\"}"), ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName()));
		SampleLog(TEXT("room send ") + Data);
		if (UYouGameRoomSendAction* Send = UYouGameRoomSendAction::RoomSend(GetWorld(), Data, FString()))
		{
			Send->OnSuccess.AddDynamic(this, &UYouGameSampleMatch::OnSent);
			Send->OnFailure.AddDynamic(this, &UYouGameSampleMatch::OnSendFailed);
			Send->Activate();
		}
	}
	SendPosition(true);
}

void UYouGameSampleMatch::OnRoomMessage(const FString& From, const FString& Data)
{
	FVector Pos;
	float Yaw = 0.f;
	if (!ParsePosition(Data, Pos, Yaw))
	{
		SampleLog(FString::Printf(TEXT("room message from %s: %s"), *From.Left(40), *Data.Left(400)));
		return;
	}
	if (From == MyId) return;
	FYouGameSampleOther& O = Others.FindOrAdd(From);
	O.Target = Pos;
	O.Yaw = Yaw;
	if (O.Body.IsValid()) return;
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	APawn* Me = PC ? PC->GetPawn() : nullptr;
	if (!Me) return;
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SP.ObjectFlags |= RF_Transient;
	APawn* Body = W->SpawnActor<APawn>(Me->GetClass(), Pos, FRotator(0.f, Yaw, 0.f), SP);
	if (!Body) { SampleLog(TEXT("could not spawn the other player's pawn")); return; }
	Body->SetActorEnableCollision(false);
	if (ACharacter* AsCharacter = Cast<ACharacter>(Body))
	{
		if (UCharacterMovementComponent* Move = AsCharacter->GetCharacterMovement()) Move->DisableMovement();
	}
	O.Body = Body;
	SampleLog(FString::Printf(TEXT("other player %s is here, %d shown"), *From.Left(8), Others.Num()));
}

void UYouGameSampleMatch::OnSent(const FYouGameReply& Reply) { SampleLog(TEXT("room send ok")); }

void UYouGameSampleMatch::OnSendFailed(const FYouGameReply& Reply)
{
	SampleLog(FString::Printf(TEXT("room send refused: %s %s"), *Reply.Code, *Reply.Error));
}

void UYouGameSampleMatch::OnReply(const FYouGameReply& Reply) { SampleLog(TEXT("feedback ok")); }

void UYouGameSampleMatch::OnReplyFailed(const FYouGameReply& Reply)
{
	SampleLog(FString::Printf(TEXT("feedback refused: %s %s"), *Reply.Code, *Reply.Error));
}

void UYouGameSampleMatch::OnOpened(const FYouGameOpenResult& Result) { SampleLog(TEXT("invite ok: pick a friend in YouGame's sheet")); }

void UYouGameSampleMatch::OnOpenFailed(const FYouGameOpenResult& Result)
{
	SampleLog(FString::Printf(TEXT("invite refused: %s %s"), *Result.Reply.Code, *Result.Reply.Error));
}

void UYouGameSampleMatch::OnScore(const FYouGameScoreResult& Result)
{
	SampleLog(FString::Printf(TEXT("score ok (rank %lld, best %lld, new best %d)"), Result.bHasRank ? Result.Rank : -1LL, Result.bHasBest ? Result.Best : -1LL, Result.bPersonalBest ? 1 : 0));
}

void UYouGameSampleMatch::OnScoreFailed(const FYouGameScoreResult& Result)
{
	SampleLog(FString::Printf(TEXT("score refused: %s %s"), *Result.Reply.Code, *Result.Reply.Error));
}

void UYouGameSampleMatch::OnBoard(const FYouGameLeaderboardResult& Result)
{
	SampleLog(FString::Printf(TEXT("leaderboard ok: %d rows%s"), Result.Rows.Num(), Result.Rows.Num() ? *FString::Printf(TEXT(", top score %lld"), Result.Rows[0].Score) : TEXT("")));
}

void UYouGameSampleMatch::OnBoardFailed(const FYouGameLeaderboardResult& Result)
{
	SampleLog(FString::Printf(TEXT("leaderboard refused: %s %s"), *Result.Reply.Code, *Result.Reply.Error));
}
