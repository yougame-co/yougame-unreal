# YouGame plugin for Unreal

Sign-in, scores and leaderboards, saves, feedback and **online play** for Unreal games on
[YouGame](https://yougame.co). In a **browser build** (Unreal 5.9 web) online play comes entirely
from the plugin: **Find Match** puts the players in a YouGame room and **Room Send** passes your
game's messages between them. There is no server for you to build or run.

Full guide: **[GUIDE.md](GUIDE.md)**. Using a coding agent (Claude Code, Codex, Cursor)? Point it at
**[AGENTS.md](AGENTS.md)** and ask it to "make my game online with YouGame".

Beta. Checked on Unreal 5.9 web builds (Chrome) and Unreal 5.8 Windows desktop builds.

## Install

From your project's folder:

```
git clone https://github.com/yougame-co/yougame-unreal.git Plugins/YouGame
```

or download the zip from the [latest release](https://github.com/yougame-co/yougame-unreal/releases/latest)
and unzip it into `Plugins/` (it makes `Plugins/YouGame`). `YouGame.uplugin` must end up at
`Plugins/YouGame/YouGame.uplugin`; GitHub's green "Download ZIP" has an extra top folder to remove.
Your project must be a **C++** project (in a Blueprint project, add any C++ class first).

1. Reopen the project, let it build, and enable **YouGame** in Edit > Plugins.
2. In Project Settings > Plugins > YouGame, set **Game Slug** to the part after `/g/` in your
   listing's address. No listing yet? Leave it empty: on YouGame the build uses the page's own
   listing, and the same build works on yougame.co and on staging.yougame.co.

## Make a browser game

1. Install Unreal 5.9 and Visual Studio 2022 with "Desktop development with C++" on Windows.
2. Run `Extras\SetupWeb.bat` (double-click). It installs Emscripten 6.0.10, which Unreal 5.9 needs,
   sets `WASM_EMSDK_ROOT` and checks the rest. Restart the editor afterwards.
3. Wire the nodes, all under **YouGame** in the Blueprint menu: **Sign In**, then **Find Match**.
   Send your game's JSON with **Room Send**, and read the other players' with **On Room Message**.
   A ready C++ example is in `Extras/Example` (copy it into your game module; the guide says how).
4. Double-click `Plugins\YouGame\Extras\PackageWeb.bat` in your project (**not run on a developer
   PC yet**). It packages for **Wasm** (Shipping) and gives you
   `Saved\YouGameWeb\<Project>-YouGame.zip`; `PackageWeb.bat -Fast` links much faster, for test
   builds. By hand instead: package for Wasm (Shipping), put a `yougame.json` containing
   `{"yougame":{"crossOriginIsolation":true}}` beside `index.html` in the output's `Wasm` folder,
   and zip that folder's contents.
5. Upload the zip as a **Web game** at https://yougame.co/upload, then play it in Chrome. To try
   online, open it in two windows signed in as two accounts and press Find Match in both.

Windows desktop builds (played through the YouGame app) are covered in the
[guide](GUIDE.md) too.

## Licence

MIT, see [LICENSE](LICENSE). Security reports: see
[SECURITY.md](https://github.com/yougame-co/.github/blob/main/SECURITY.md).
