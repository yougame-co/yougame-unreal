# For coding agents: put an Unreal game online with YouGame

You are in (or beside) an Unreal project and the developer asked for YouGame: online play, sign-in,
scores, saves or feedback. This file is the short version; [GUIDE.md](GUIDE.md) has every detail.

**Default to a browser build** (Unreal 5.9 web, the `Wasm` platform). It is an ordinary YouGame web
game: no server, no desktop app. Use the Windows desktop path in the guide only when the developer
asks for a desktop build.

## What you can do yourself (text files only)

1. **Plugin.** This repo goes in the project's `Plugins/YouGame` folder
   (`git clone https://github.com/yougame-co/yougame-unreal.git Plugins/YouGame`). The project must
   be a C++ project (it has a `Source/` folder).
2. **`<Project>.uproject`.** Add `{"Name": "YouGame", "Enabled": true}` to `Plugins`. For the web,
   keep the two online subsystem plugins off, since they do not run in a browser:
   `{"Name": "OnlineSubsystemUtils", "Enabled": true, "PlatformDenyList": ["Wasm"]}` and the same
   for `OnlineSubsystemNull`.
3. **`Source/<Module>/<Module>.Build.cs`.** Add `"YouGame"` to `PublicDependencyModuleNames`
   (and `"Slate"`, `"SlateCore"` when you copy the example in step 4).
4. **Game code, in C++.** Blueprint assets are binary, so write C++. The calls are on
   `UYouGameSubsystem` and the async actions in `Source/YouGame/Public`: **Sign In**, then **Find
   Match** with Mode empty (on Success the room is live), **Room Send** for your own JSON, **On Room
   Message** and **On Room Players** to read the others. Find Match is the public queue: for
   friend invites also bind `UYouGameSubsystem::OnFriendRoom` and call **Join Friend Room** with
   its Room, or an invited friend never reaches the room (the example does this). Unreal's own
   replication does not run in a browser build: everything another player should see (positions,
   hits, pickups, score) goes through **Room Send** as JSON and is applied in **On Room Message**. The fastest working start: copy
   `Extras/Example/YouGameSampleMatch.h` and `.cpp` into `Source/<Module>/` unchanged. It draws
   its status line with Slate, so the module needs `"Slate"` and `"SlateCore"` too (the Third Person
   template lists Slate but not SlateCore; without it the build stops on link errors). It is a world
   subsystem, so it runs by itself: **M** finds a match, and every player sees the others as copies
   of their own pawn. It also shows every other call on a key: **F** friend room, **I** invite,
   **K** submit score, **L** leaderboard, **B** feedback, **O** save, **P** load, **X** clear save,
   **U** sign in. Then adapt it to the game.
5. **Listing.** `Config/DefaultGame.ini`, section `[/Script/YouGame.YouGameSettings]`:
   `GameSlug=<the part after /g/ in the listing's address>`. Leaving it empty is fine: on YouGame
   the build uses the listing of the page it runs on, and the site it is played on (yougame.co or
   staging.yougame.co), so one zip works on both. `bUseStaging=True` only matters when the game
   is hosted somewhere else and its listing is on staging.yougame.co.

Do **not** edit the packaged `index.html`, add YouGame's JavaScript SDK tag, or call `YouGame.*`
from the page. The plugin talks to the SDK itself.

## What the developer does on their Windows PC (tell them exactly)

1. Unreal 5.9 and Visual Studio 2022 with "Desktop development with C++".
2. Double-click `Plugins\YouGame\Extras\SetupWeb.bat` once (installs Emscripten 6.0.10, which
   Unreal 5.9 needs), then restart the editor.
3. Open the project, let it rebuild, check **YouGame** is ticked in Edit > Plugins.
4. Package and zip: double-click `Plugins\YouGame\Extras\PackageWeb.bat` (run on our own Windows
   PC so far; an agent with a terminal runs `PackageWeb.ps1` beside it, since the .bat waits for a
   key). Tell them their first Unreal build may show a Windows firewall prompt for `UbaServer`,
   Unreal's own build accelerator. It packages for Wasm (Shipping), adds the `yougame.json` and writes
   `Saved\YouGameWeb\<Project>-YouGame.zip`; the build itself is `Saved\YouGameWeb\Wasm` (that is
   `<Out>\Wasm` below). The first package of a Third Person game takes about 35-40 minutes; later
   ones about 15. `-Fast` is for test builds: it links much faster and the game runs a little
   slower. If you can run commands on that PC, run the script itself, which does not wait for a
   key and exits with 0 only when the zip is made:
   `powershell -NoProfile -ExecutionPolicy Bypass -File Plugins\YouGame\Extras\PackageWeb.ps1`.
5. By hand instead of step 4: **Platforms > Wasm > Package Project** (Shipping), or
   `RunUAT.bat BuildCookRun -project=<P>.uproject -platform=Wasm -clientconfig=Shipping -build -cook -stage -pak -iostore -package -archive -archivedirectory=<Out> -unattended`.
   Then put a `yougame.json` containing `{"yougame":{"crossOriginIsolation":true}}` beside
   `index.html` in `<Out>\Wasm`, and zip that folder's **contents** (never `Wasm-Debug`).

## Upload (you can do this for the developer)

1. YouGame's MCP: `claude mcp add --transport http yougame https://yougame.co/mcp` (Codex:
   `codex mcp add yougame --url https://yougame.co/mcp`). Its guides and search work signed out.
   Uploading needs the developer signed in: ask them to type `/mcp` in Claude Code, choose
   yougame and sign in (Codex: `codex mcp login yougame`).
2. Call `upload_token` and run the line it returns. It is
   `node upload-build.mjs <folder or zip>` with the token in the environment; get the script with
   `curl -fsS https://yougame.co/upload-build.mjs -o upload-build.mjs`. Point it at `<Out>\Wasm`
   (or the zip). Files over 100 MB go up in parts, so the ~140 MB `.wasm` is fine.
3. A new game: `publish_game` with the upload id (it makes the listing; ask the developer for a
   title and a real screenshot as the thumbnail). A new version: `update_game` with the slug.
4. No MCP? The developer drops the zip on the **Web game** tab at https://yougame.co/upload.

The upload report lists an Unreal build as an "Unreal web package": the plugin is its SDK, so
there is no script tag to add.

## Hosting it on the developer's own site instead

It works there too (sign-in, Find Match, friend rooms, feedback). The host must send COOP
`same-origin` and COEP `require-corp` and serve a ~150 MB file. Keep `yougame.json` beside
`index.html`. With `GameSlug` empty the game gets its own YouGame page on first load: the console
prints a claim link; add `"game": "<address they want>"` and `"owner": "<their YouGame username>"`
to `yougame.json`, and have the developer open the link, claim, add a screenshot and publish.
See "On your own site" in [GUIDE.md](GUIDE.md).

## Test

Tell the developer how to play with one friend: press the game's friend-room key (**F** in the
example), open YouGame's sheet over the game (Shift+Tab) > Lobby > Invite friends, and use **Copy
invite link** (works for anyone) or type the friend's @username. Find Match (**M**) is the public
queue, where a stranger can match in.

Open the listing in two Chrome windows signed in as two accounts (one can be Incognito), click
the game, and press the game's Find Match in both. Each should see the other move. The browser
console prints `[yougame-unreal] ...` for every step, which is the first place to look when
something does not answer.
