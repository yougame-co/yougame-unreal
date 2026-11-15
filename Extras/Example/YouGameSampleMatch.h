// YouGameSampleMatch.h: an example of the YouGame plugin's online nodes, called from C++ (the same calls the Blueprint
// nodes make). Copy this file and YouGameSampleMatch.cpp into your game module's Source/<Module>/ folder and add
// "YouGame", "Slate" and "SlateCore" to PublicDependencyModuleNames in <Module>.Build.cs (Slate draws its status line). It is a world subsystem, so it runs in every game world by itself.
// This exact code is what our staging Unreal web sample runs.
//
// Keys: M Find Match. F Friend Room, then I Invite Friend (a friend who accepts joins the same room). K Submit Score
// (seconds played), L Get Leaderboard, B Open Feedback. O Save (where you stand), P Load (puts you back there), X Clear
// Save, U Sign In. In a room, every player sends its position about 10 times a
// second with Room Send, and every other player shows up as a copy of your own pawn (your character, or the default pawn in a blank
// project) that moves where they move. When a second player joins, each one still on the start spot steps to their own
// place beside it, so nobody stands inside anybody. A status line at the top left says whether you are online
// ("offline", "connecting...", "connected, 2 players") and what the last call answered. Leaving "SlateCore" out shows up as
// "unresolved external symbol" link errors that name STextBlock or SWidget.
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "YouGameTypes.h"
#include "YouGameSampleMatch.generated.h"

class APawn;
class STextBlock;

// One other player in the example: the pawn standing in for them, and where their last message put them.
struct FYouGameSampleOther
{
	TWeakObjectPtr<APawn> Body;
	FVector Target = FVector::ZeroVector;
	float Yaw = 0.f;
	float WalkSpeed = 600.f; // the character's own walking speed, read when its copy is made
	float Behind = 0.f;      // seconds it has been more than a few steps behind its player
	bool bRising = false;    // its player's height went up in the last message
};

UCLASS()
class UYouGameSampleMatch : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void Tick(float DeltaTime) override;
	virtual void Deinitialize() override;
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
	UFUNCTION() void OnSaved(const FYouGameReply& Reply);
	UFUNCTION() void OnCleared(const FYouGameReply& Reply);
	UFUNCTION() void OnSaveFailed(const FYouGameReply& Reply);
	UFUNCTION() void OnLoaded(const FYouGameLoadResult& Result);
	UFUNCTION() void OnLoadFailed(const FYouGameLoadResult& Result);
	UFUNCTION() void OnSignIn(const FYouGameOpenResult& Result);
	UFUNCTION() void OnSignedInAlready(const FYouGameOpenResult& Result);
	UFUNCTION() void OnSignInFailed(const FYouGameOpenResult& Result);
	UFUNCTION() void OnOpened(const FYouGameOpenResult& Result);
	UFUNCTION() void OnOpenFailed(const FYouGameOpenResult& Result);
	UFUNCTION() void OnScore(const FYouGameScoreResult& Result);
	UFUNCTION() void OnScoreFailed(const FYouGameScoreResult& Result);
	UFUNCTION() void OnBoard(const FYouGameLeaderboardResult& Result);
	UFUNCTION() void OnBoardFailed(const FYouGameLeaderboardResult& Result);

	void ShowStatus();
	void Flash(const FString& What);
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
	// The on-screen status: 0 offline, 1 connecting, 2 in a room. Note is the second line, shown for a few seconds.
	TSharedPtr<STextBlock> StatusText;
	int32 Stage = 0;
	FString Note;
	float NoteLeft = 0.f;
	// Where you started, and whether you already stepped to your own place beside it for this room.
	FVector Start = FVector::ZeroVector;
	FVector StartRight = FVector::RightVector;
	bool bHaveStart = false;
	bool bPlaced = false;
	FVector LastSent = FVector(FLT_MAX);
	float LastYaw = 0.f;
};
