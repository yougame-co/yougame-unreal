// YouGameSampleMatch.cpp: the example's code (see YouGameSampleMatch.h for how to add it and its keys).
// Lines start "[yougame-unreal] sample: " for the web test. Player names, room codes and tickets are never logged.
#include "YouGameSampleMatch.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Engine/GameViewportClient.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Text/STextBlock.h"
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
	if (!bHaveStart)
	{
		if (const APawn* Me = PC->GetPawn()) { Start = Me->GetActorLocation(); StartRight = Me->GetActorRightVector(); bHaveStart = true; }
	}
	if (NoteLeft > 0.f)
	{
		NoteLeft -= DeltaTime;
		if (NoteLeft <= 0.f) { Note.Reset(); ShowStatus(); }
	}
	if (!bReady)
	{
		bReady = true;
		ShowStatus();
		UYouGameSubsystem* YG = UYouGameSubsystem::Get(W);
		if (YG)
		{
			YG->OnRoomPlayers.AddUniqueDynamic(this, &UYouGameSampleMatch::OnRoomPlayers);
			YG->OnRoomMessage.AddUniqueDynamic(this, &UYouGameSampleMatch::OnRoomMessage);
			YG->OnFriendRoom.AddUniqueDynamic(this, &UYouGameSampleMatch::OnFriendRoom);
		}
		SampleLog(FString::Printf(TEXT("ready on %s, M = Find Match, F = Friend Room, I = Invite, K = score, L = leaderboard, B = feedback, O = save, P = load, X = clear save, U = sign in%s"),
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
		Stage = 1; ShowStatus();
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
		Stage = 1; ShowStatus();
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
	// Save keeps one piece of text per player and game (here: where you stand and how long you have played);
	// Load brings it back on any device the player signs in on, and here puts you back where you saved.
	if (PC->WasInputKeyJustPressed(EKeys::O))
	{
		// The same {"p":[x,y,z,yaw]} shape the room messages use, plus the seconds played.
		const APawn* Me = PC->GetPawn();
		const FVector At = Me ? Me->GetActorLocation() : FVector::ZeroVector;
		const float Yaw = Me ? Me->GetActorRotation().Yaw : 0.f;
		const FString Data = FString::Printf(TEXT("{\"p\":[%d,%d,%d,%d],\"played\":%d}"), int32(FMath::RoundToInt(At.X)), int32(FMath::RoundToInt(At.Y)),
			int32(FMath::RoundToInt(At.Z)), int32(FMath::RoundToInt(Yaw)), int32(FMath::RoundToInt(Played)));
		SampleLog(FString::Printf(TEXT("save pressed: %s"), *Data));
		if (UYouGameSaveAction* A = UYouGameSaveAction::Save(W, Data))
		{
			A->OnSuccess.AddDynamic(this, &UYouGameSampleMatch::OnSaved);
			A->OnFailure.AddDynamic(this, &UYouGameSampleMatch::OnSaveFailed);
			A->Activate();
		}
	}
	if (PC->WasInputKeyJustPressed(EKeys::P))
	{
		SampleLog(TEXT("load pressed"));
		if (UYouGameLoadAction* A = UYouGameLoadAction::Load(W))
		{
			A->OnSuccess.AddDynamic(this, &UYouGameSampleMatch::OnLoaded);
			A->OnFailure.AddDynamic(this, &UYouGameSampleMatch::OnLoadFailed);
			A->Activate();
		}
	}
	if (PC->WasInputKeyJustPressed(EKeys::X))
	{
		SampleLog(TEXT("clear save pressed"));
		if (UYouGameClearSaveAction* A = UYouGameClearSaveAction::ClearSave(W))
		{
			A->OnSuccess.AddDynamic(this, &UYouGameSampleMatch::OnCleared);
			A->OnFailure.AddDynamic(this, &UYouGameSampleMatch::OnSaveFailed);
			A->Activate();
		}
	}
	// Sign In opens YouGame's own sign-in (in a browser the page may reload once the player is in).
	if (PC->WasInputKeyJustPressed(EKeys::U))
	{
		SampleLog(TEXT("sign in pressed"));
		if (UYouGameSignInAction* A = UYouGameSignInAction::SignIn(W))
		{
			A->OnSuccess.AddDynamic(this, &UYouGameSampleMatch::OnSignIn);
			A->OnAlreadySignedIn.AddDynamic(this, &UYouGameSampleMatch::OnSignedInAlready);
			A->OnBusy.AddDynamic(this, &UYouGameSampleMatch::OnSignInFailed);
			A->OnSignInFailed.AddDynamic(this, &UYouGameSampleMatch::OnSignInFailed);
			A->OnBadRequest.AddDynamic(this, &UYouGameSampleMatch::OnSignInFailed);
			A->OnFailure.AddDynamic(this, &UYouGameSampleMatch::OnSignInFailed);
			A->Activate();
		}
	}
}

// One line near the top left of the game, so a player sees whether they are online without opening the browser
// console, and a second line for a few seconds after each call answers. It never takes clicks.
void UYouGameSampleMatch::ShowStatus()
{
	UWorld* W = GetWorld();
	UGameViewportClient* View = W ? W->GetGameViewport() : nullptr;
	if (!View) return;
	if (!StatusText.IsValid())
	{
		View->AddViewportWidgetContent(SAssignNew(StatusText, STextBlock)
			.Visibility(EVisibility::HitTestInvisible)
			// Below YouGame's own button and its notices, which sit in the top left corner of a game on YouGame.
			.Margin(FMargin(24.f, 76.f, 24.f, 0.f))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
			.ColorAndOpacity(FLinearColor::White)
			.ShadowOffset(FVector2D(1.f, 1.f))
			.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f)), 100);
	}
	FString Line;
	if (RoomSize >= 2) Line = FString::Printf(TEXT("YouGame: connected, %d players"), RoomSize);
	else if (Stage == 2) Line = TEXT("YouGame: in a room, waiting for another player...");
	else if (Stage == 1) Line = TEXT("YouGame: connecting...");
	else Line = TEXT("YouGame: offline. M = find a match, F = room for a friend, then I = invite");
	if (!Note.IsEmpty()) Line += TEXT("\n") + Note;
	StatusText->SetText(FText::FromString(Line));
}

void UYouGameSampleMatch::Flash(const FString& What)
{
	Note = What;
	NoteLeft = 6.f;
	ShowStatus();
}

void UYouGameSampleMatch::Deinitialize()
{
	if (StatusText.IsValid())
	{
		UWorld* W = GetWorld();
		if (UGameViewportClient* View = W ? W->GetGameViewport() : nullptr) View->RemoveViewportWidgetContent(StatusText.ToSharedRef());
		StatusText.Reset();
	}
	Super::Deinitialize();
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
		FYouGameSampleOther& O = It.Value;
		APawn* Body = O.Body.Get();
		if (!Body) continue;
		const FVector From = Body->GetActorLocation();
		const FVector Gap = O.Target - From;
		const FRotator Facing(0.f, O.Yaw, 0.f);
		ACharacter* AsCharacter = Cast<ACharacter>(Body);
		UCharacterMovementComponent* Move = AsCharacter ? AsCharacter->GetCharacterMovement() : nullptr;
		if (!Move)
		{
			// Not a character: it glides to where its player is.
			Body->SetActorLocationAndRotation(FMath::VInterpTo(From, O.Target, DeltaTime, 12.f), Facing, false, nullptr, ETeleportType::TeleportPhysics);
			continue;
		}
		// A character runs to where its player is with its own movement, so it runs, turns and jumps like yours.
		const float Flat = Gap.Size2D();
		O.Behind = Flat > 300.f ? O.Behind + DeltaTime : 0.f;
		// Far away, far above or below, or behind for too long (something is in the way): put it there in one step.
		if (Flat > 1200.f || FMath::Abs(Gap.Z) > 400.f || O.Behind > 1.5f)
		{
			Body->SetActorLocationAndRotation(O.Target, Facing, false, nullptr, ETeleportType::TeleportPhysics);
			Move->Velocity = FVector::ZeroVector;
			Move->SetDefaultMovementMode();
			O.Behind = 0.f;
			continue;
		}
		if (Flat > 15.f)
		{
			// Messages arrive a moment late: behind by more than a couple of steps, it runs a little faster to catch up.
			Move->MaxWalkSpeed = O.WalkSpeed * (Flat > 200.f ? 1.5f : 1.f);
			AsCharacter->AddMovementInput(FVector(Gap.X, Gap.Y, 0.f).GetSafeNormal(), FMath::Clamp(Flat / 100.f, 0.3f, 1.f));
		}
		else
		{
			AsCharacter->SetActorRotation(FMath::RInterpTo(Body->GetActorRotation(), Facing, DeltaTime, 10.f));
		}
		if (Gap.Z > 45.f && Move->IsMovingOnGround()) AsCharacter->Jump();
		else if (Move->IsFalling()) AsCharacter->StopJumping();
	}
}

void UYouGameSampleMatch::OnTicket(const FYouGameMatchTicket& Result)
{
	SampleLog(FString::Printf(TEXT("room ok (queue %s, ranked %d), waiting for players"), *Result.Queue, Result.bRanked ? 1 : 0));
	Stage = 2; ShowStatus();
}

void UYouGameSampleMatch::OnTicketFailed(const FYouGameMatchTicket& Result)
{
	SampleLog(FString::Printf(TEXT("room failed: %s %s"), *Result.Reply.Code, *Result.Reply.Error));
	Stage = 0;
	Flash(Result.Reply.Code == TEXT("signedOut") ? FString(TEXT("Sign in to play online (press U)")) : FString::Printf(TEXT("Could not go online: %s"), *Result.Reply.Code));
}

void UYouGameSampleMatch::OnFriendRoom(const FYouGameFriendRoom& Room)
{
	// A friend's invite opened the game: join their room.
	SampleLog(TEXT("invite accepted, joining the friend's room"));
	Stage = 1; ShowStatus();
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
	Stage = Players.Num() >= 1 ? 2 : 0;
	ShowStatus();
	if (Players.Num() < 2) { bSentHello = false; bPlaced = false; return; }
	// Everyone starts a level on the same spot, where the other player's copy would stand inside you and you would
	// see one character. So each player still near the start steps to their own place beside it: the same order of
	// players on every screen gives each a different place.
	if (!bPlaced)
	{
		bPlaced = true;
		TArray<FString> Ids;
		for (const FYouGameRoomPlayer& P : Players) Ids.Add(P.Id);
		Ids.Sort();
		const int32 Place = FMath::Max(0, Ids.IndexOfByKey(You));
		UWorld* W = GetWorld();
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		APawn* Me = PC ? PC->GetPawn() : nullptr;
		if (Me && bHaveStart && FVector::DistSquared2D(Me->GetActorLocation(), Start) < FMath::Square(250.f))
		{
			// Side by side, 2 m apart, across the way everyone faces at the start: in front of or behind the start a
			// player would stand in the others' cameras.
			const float Across = (float(Place) - 0.5f * float(Ids.Num() - 1)) * 200.f;
			const FVector To = Start + FVector(StartRight.X, StartRight.Y, 0.f).GetSafeNormal() * Across;
			Me->SetActorLocation(FVector(To.X, To.Y, Me->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
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
	if (ACharacter* AsCharacter = Cast<ACharacter>(Body))
	{
		// A character walks to where its player is with its own movement (MoveOthers), so your Animation Blueprint plays
		// its run and jump as it does for you. It stands on the level but passes through characters and the camera.
		if (UCapsuleComponent* Capsule = AsCharacter->GetCapsuleComponent())
		{
			Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
			Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		}
		if (USkeletalMeshComponent* Mesh = AsCharacter->GetMesh()) Mesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		if (UCharacterMovementComponent* Move = AsCharacter->GetCharacterMovement())
		{
			// A character nobody controls does not move by itself: switch its movement on and put it in its walking
			// mode (the engine only does that at spawn for a character that already has this set).
			Move->bRunPhysicsWithNoController = true;
			Move->SetDefaultMovementMode();
			O.WalkSpeed = Move->MaxWalkSpeed;
		}
	}
	else
	{
		Body->SetActorEnableCollision(false);
	}
	O.Body = Body;
	SampleLog(FString::Printf(TEXT("other player %s is here, %d shown"), *From.Left(8), Others.Num()));
}

void UYouGameSampleMatch::OnSent(const FYouGameReply& Reply) { SampleLog(TEXT("room send ok")); }

void UYouGameSampleMatch::OnSendFailed(const FYouGameReply& Reply)
{
	SampleLog(FString::Printf(TEXT("room send refused: %s %s"), *Reply.Code, *Reply.Error));
}

void UYouGameSampleMatch::OnReply(const FYouGameReply& Reply) { SampleLog(TEXT("feedback ok")); Flash(TEXT("Feedback is open")); }

void UYouGameSampleMatch::OnReplyFailed(const FYouGameReply& Reply)
{
	SampleLog(FString::Printf(TEXT("feedback refused: %s %s"), *Reply.Code, *Reply.Error));
}

void UYouGameSampleMatch::OnSaved(const FYouGameReply& Reply) { SampleLog(TEXT("save ok")); Flash(TEXT("Saved where you stand (P loads it)")); }

void UYouGameSampleMatch::OnCleared(const FYouGameReply& Reply) { SampleLog(TEXT("clear save ok")); Flash(TEXT("Save cleared")); }

void UYouGameSampleMatch::OnSaveFailed(const FYouGameReply& Reply)
{
	SampleLog(FString::Printf(TEXT("save refused: %s %s"), *Reply.Code, *Reply.Error));
	Flash(Reply.Code == TEXT("signedOut") ? FString(TEXT("Sign in to save (press U)")) : FString::Printf(TEXT("Save refused: %s"), *Reply.Code));
}

void UYouGameSampleMatch::OnLoaded(const FYouGameLoadResult& Result)
{
	if (!Result.bHasData)
	{
		SampleLog(TEXT("load ok: nothing saved yet"));
		Flash(TEXT("Nothing saved yet (O saves)"));
		return;
	}
	SampleLog(FString::Printf(TEXT("load ok: %s"), *Result.Data.Left(120)));
	Flash(TEXT("Loaded: back where you saved"));
	// Back to the saved spot.
	FVector At = FVector::ZeroVector;
	float Yaw = 0.f;
	if (!ParsePosition(Result.Data, At, Yaw)) return;
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (APawn* Me = PC ? PC->GetPawn() : nullptr) Me->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
}

void UYouGameSampleMatch::OnLoadFailed(const FYouGameLoadResult& Result)
{
	SampleLog(FString::Printf(TEXT("load refused: %s %s"), *Result.Reply.Code, *Result.Reply.Error));
	Flash(Result.Reply.Code == TEXT("signedOut") ? FString(TEXT("Sign in to load (press U)")) : FString::Printf(TEXT("Load refused: %s"), *Result.Reply.Code));
}

void UYouGameSampleMatch::OnSignIn(const FYouGameOpenResult& Result) { SampleLog(TEXT("sign in ok: YouGame's sign-in is open")); }

void UYouGameSampleMatch::OnSignedInAlready(const FYouGameOpenResult& Result) { SampleLog(TEXT("sign in: already signed in")); Flash(TEXT("You are signed in")); }

void UYouGameSampleMatch::OnSignInFailed(const FYouGameOpenResult& Result)
{
	SampleLog(FString::Printf(TEXT("sign in refused: %s %s"), *Result.Reply.Code, *Result.Reply.Error));
}

void UYouGameSampleMatch::OnOpened(const FYouGameOpenResult& Result) { SampleLog(TEXT("invite ok: pick a friend in YouGame's sheet")); Flash(TEXT("Pick a friend to invite")); }

void UYouGameSampleMatch::OnOpenFailed(const FYouGameOpenResult& Result)
{
	SampleLog(FString::Printf(TEXT("invite refused: %s %s"), *Result.Reply.Code, *Result.Reply.Error));
}

void UYouGameSampleMatch::OnScore(const FYouGameScoreResult& Result)
{
	SampleLog(FString::Printf(TEXT("score ok (rank %lld, best %lld, new best %d)"), Result.bHasRank ? Result.Rank : -1LL, Result.bHasBest ? Result.Best : -1LL, Result.bPersonalBest ? 1 : 0));
	Flash(Result.bHasRank ? FString::Printf(TEXT("Score sent: you are rank %lld"), Result.Rank) : FString(TEXT("Score sent")));
}

void UYouGameSampleMatch::OnScoreFailed(const FYouGameScoreResult& Result)
{
	SampleLog(FString::Printf(TEXT("score refused: %s %s"), *Result.Reply.Code, *Result.Reply.Error));
	Flash(FString::Printf(TEXT("Score refused: %s"), *Result.Reply.Code));
}

void UYouGameSampleMatch::OnBoard(const FYouGameLeaderboardResult& Result)
{
	SampleLog(FString::Printf(TEXT("leaderboard ok: %d rows%s"), Result.Rows.Num(), Result.Rows.Num() ? *FString::Printf(TEXT(", top score %lld"), Result.Rows[0].Score) : TEXT("")));
	Flash(Result.Rows.Num() ? FString::Printf(TEXT("Leaderboard: %d on it, top score %lld"), Result.Rows.Num(), Result.Rows[0].Score) : FString(TEXT("Leaderboard: no scores yet (K sends one)")));
}

void UYouGameSampleMatch::OnBoardFailed(const FYouGameLeaderboardResult& Result)
{
	SampleLog(FString::Printf(TEXT("leaderboard refused: %s %s"), *Result.Reply.Code, *Result.Reply.Error));
}
