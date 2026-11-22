// Project Settings > Plugins > YouGame, saved in Config/DefaultGame.ini under
// [/Script/YouGame.YouGameSettings].
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "YouGameSettings.generated.h"

/** When a game started on its own asks the player to get or open YouGame. */
UENUM()
enum class EYouGameAskWhen : uint8
{
	/** At start: the window shows before play when YouGame is missing or can't start by itself. */
	AtStart UMETA(DisplayName = "At Start"),
	/** On first use: start YouGame quietly if it can, never a window at start; call Ask For YouGame
	 * when the player presses something that needs YouGame. */
	OnFirstUse UMETA(DisplayName = "On First Use"),
};

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "YouGame"))
class YOUGAME_API UYouGameSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	 * The game's slug on YouGame: the part after /g/ in its address (yougame.co/g/<slug>). Lower case
	 * letters, digits and dashes. Empty: the plugin does not look for YouGame when the player starts
	 * the game themselves.
	 */
	UPROPERTY(config, EditAnywhere, Category = "YouGame")
	FString GameSlug;

	/**
	 * The game's name in the Get YouGame window ("Play <name> online"). Up to 40 letters, digits,
	 * spaces and . , ' - : ! & + ( ). Empty or anything else: the window says "this game".
	 */
	UPROPERTY(config, EditAnywhere, Category = "YouGame")
	FString GameName;

	/** Use YouGame's staging app and site (staging.yougame.co): for test builds and jam testing. */
	UPROPERTY(config, EditAnywhere, Category = "YouGame")
	bool bUseStaging = false;

	/**
	 * The first YouGame app version that can start hidden in the tray. An installed YouGame older
	 * than this is not started (it would open its window); the player is told it needs an update.
	 */
	UPROPERTY(config, EditAnywhere, Category = "YouGame")
	FString TrayFromVersion = TEXT("0.13.1407");

	/** The build's version, told to YouGame. Empty: the Project Version in Project Settings > Description. */
	UPROPERTY(config, EditAnywhere, Category = "YouGame")
	FString GameVersion;

	/**
	 * Online matches pair only copies with the same value. Change it when your match packets change,
	 * so old and new builds never meet in a match. Empty: the build's version (above). Letters,
	 * digits and . _ - only, up to 64; other characters are dropped. "friends-lobby-1",
	 * "doubles-lobby-1" and values ending in "-squad" mean something else to YouGame: they send no
	 * lobby at all.
	 */
	UPROPERTY(config, EditAnywhere, Category = "YouGame")
	FString MatchCompatibility;

	/**
	 * Run the start check (start YouGame in the tray, or offer Get YouGame) when playing in the
	 * editor too. Off: only packaged builds run it; the editor still talks to a YouGame that is open.
	 */
	UPROPERTY(config, EditAnywhere, Category = "YouGame")
	bool bStartCheckInEditor = false;

	/**
	 * When to ask the player to get or open YouGame. At Start suits a game that needs YouGame to play
	 * at all (controller support, online-only). On First Use suits a game that plays offline: nothing
	 * shows at start, and the window comes up when the player presses Online, Friends or another
	 * YouGame button (call Ask For YouGame there).
	 */
	UPROPERTY(config, EditAnywhere, Category = "YouGame")
	EYouGameAskWhen AskWhen = EYouGameAskWhen::AtStart;

	virtual FName GetCategoryName() const override;
};
