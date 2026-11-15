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
3. **`Source/<Module>/<Module>.Build.cs`.** Add `"YouGame"` to `PublicDependencyModuleNames`.
4. **Game code, in C++.** Blueprint assets are binary, so write C++. The calls are on
   `UYouGameSubsystem` and the async actions in `Source/YouGame/Public`: **Sign In**, then **Find
   Match** with Mode empty (on Success the room is live), **Room Send** for your own JSON, **On Room
   Message** and **On Room Players** to read the others. The fastest working start: copy
   `Extras/Example/YouGameSampleMatch.h` and `.cpp` into `Source/<Module>/` unchanged. It is a world
   subsystem, so it runs by itself: **M** finds a match, and every player sees the others as copies
   of their own pawn. Then adapt it to the game.
5. **Listing.** `Config/DefaultGame.ini`, section `[/Script/YouGame.YouGameSettings]`:
   `GameSlug=<the part after /g/ in the listing's address>`, and `bUseStaging=True` for a listing
   on staging.yougame.co. Leaving `GameSlug` empty is fine: on YouGame the build uses the listing
   of the page it runs on.

Do **not** edit the packaged `index.html`, add YouGame's JavaScript SDK tag, or call `YouGame.*`
from the page. The plugin talks to the SDK itself.

## What the developer does on their Windows PC (tell them exactly)

1. Unreal 5.9 and Visual Studio 2022 with "Desktop development with C++".
2. Double-click `Plugins\YouGame\Extras\SetupWeb.bat` once (installs Emscripten 6.0.10, which
   Unreal 5.9 needs), then restart the editor.
3. Open the project, let it rebuild, check **YouGame** is ticked in Edit > Plugins.
4. Package: **Platforms > Wasm > Package Project** (Shipping), or
   `RunUAT.bat BuildCookRun -project=<P>.uproject -platform=Wasm -clientconfig=Shipping -build -cook -stage -pak -iostore -package -archive -archivedirectory=<Out> -unattended`.
   The first package of a Third Person game takes about 35-40 minutes; later ones about 15.
5. Put a `yougame.json` containing `{"yougame":{"crossOriginIsolation":true}}` beside `index.html`
   in `<Out>\Wasm`, and zip that folder's **contents** (never `Wasm-Debug`).

## Upload and test

- With the YouGame MCP connected and signed in: `upload_token`, then
  `node upload-build.mjs <Out>\Wasm` (it sends the ~140 MB `.wasm` in parts). Without it: the
  **Web game** tab of the upload page takes the zip.
- Test online: open the listing in two Chrome windows signed in as two accounts and press the
  game's Find Match in both. The browser console prints `[yougame-unreal] ...` for each step.
