// YouGameSampleMatch.h: an example of the YouGame plugin's online nodes, called from C++ (the same calls the Blueprint
// nodes make). Copy this file and YouGameSampleMatch.cpp into your game module's Source/<Module>/ folder and add "YouGame"
// to PublicDependencyModuleNames in <Module>.Build.cs. It is a world subsystem, so it runs in every game world by itself.
// This exact code is what our staging Unreal web sample runs.
//
// Keys: M Find Match. F Friend Room, then I Invite Friend (a friend who accepts joins the same room). K Submit Score
// (seconds played), L Get Leaderboard, B Open Feedback. In a room, every player sends its position about 10 times a
// second with Room Send, and every other player shows up as a copy of your own pawn (your character, or the default pawn in a blank
// project) that moves where they move.
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "YouGameTypes.h"
#include "YouGameSampleMatch.generated.h"

class APawn;

// One other player in the example: the pawn standing in for them, and where their last message put them.
struct FYouGameSampleOther
{
	TWeakObjectPtr<APawn> Body;
	FVector Target = FVector::ZeroVector;
	float Yaw = 0.f;
};

UCLASS()
class UYouGameSampleMatch : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UYouGameSampleMatch, STATGROUP_Tickables); }
protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override { return WorldType == EWorldType::Game || WorldType == EWorldType::PIE; }
private:
	UFUNCTION() void OnTicket(const FYouGameMatchTicket& Result);
	UFUNCTION() void OnTicketFailed(const FYouGameMatchTicket& Result);
	UFUNCTION() void OnRoomPlayers(const TArray<FYouGameRoomPlayer>& Players, const FString& Host, const FString& You, bool bPlaying);
	UFUNCTION() void OnRoomMessage(const FString& From, const FString& Data);
	UFUNCTION() void OnFriendRoom(const FYouGameFriendRoom& Room);
	UFUNCTION() void OnSent(const FYouGameReply& Reply);
	UFUNCTION() void OnSendFailed(const FYouGameReply& Reply);
	UFUNCTION() void OnReply(const FYouGameReply& Reply);
	UFUNCTION() void OnReplyFailed(const FYouGameReply& Reply);
	UFUNCTION() void OnOpened(const FYouGameOpenResult& Result);
	UFUNCTION() void OnOpenFailed(const FYouGameOpenResult& Result);
	UFUNCTION() void OnScore(const FYouGameScoreResult& Result);
	UFUNCTION() void OnScoreFailed(const FYouGameScoreResult& Result);
	UFUNCTION() void OnBoard(const FYouGameLeaderboardResult& Result);
	UFUNCTION() void OnBoardFailed(const FYouGameLeaderboardResult& Result);

	void SendPosition(bool bForce);
	void MoveOthers(float DeltaTime);

	TMap<FString, FYouGameSampleOther> Others;
	FString MyId;
	int32 RoomSize = 0;
	bool bReady = false;
	bool bSentHello = false;
	float SinceSend = 0.f;
	float SinceForced = 0.f;
	float Played = 0.f;
	FVector LastSent = FVector(FLT_MAX);
	float LastYaw = 0.f;
};
