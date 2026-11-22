# The YouGame plugin in an Unreal game

The YouGame plugin gives an Unreal game sign-in, scores, saves, feedback and **online play**. In a
**browser build** (Unreal 5.9 web) online play comes entirely from the plugin: **Find Match** puts
the players in a YouGame room and **Room Send** passes your game's messages between them, with no
server for you to build or run. That is the path most games take, and it comes first below.
Windows desktop builds, played through the YouGame app, follow after it.

## Browser builds (Unreal 5.9 web)

### Make a browser game, step by step

1. Install Unreal 5.9 from the Epic Games Launcher on Windows, and Visual Studio 2022 with
   "Desktop development with C++".
2. Get the plugin from GitHub, github.com/yougame-co/yougame-unreal: clone it or download the
   latest release's zip (the site's `/unreal/plugin.zip` is the same plugin). Run
   `Extras\SetupWeb.bat` from it (double-click). It finds Unreal 5.9,
   checks Visual Studio's C++ tools and the engine's web platform, installs Emscripten **6.0.10**
   (Unreal 5.9 needs exactly that version and does not bundle it) into `%LOCALAPPDATA%\emsdk`, sets
   `WASM_EMSDK_ROOT`, and runs Unreal's own SDK check. It names each missing piece and its fix;
   `SetupWeb.bat -Check` only reports. Then **restart the editor**: it reads environment variables
   only at start-up. **Platforms > Wasm** in the editor shows the SDK status too. By hand instead:
   clone `emsdk`, run `emsdk install 6.0.10` and `emsdk activate 6.0.10`, set `WASM_EMSDK_ROOT` to
   that folder (it wins over a stale `EMSDK`).
3. Make or open a **C++** project (a Blueprint project: Tools > New C++ Class, any parent, first).
   For the web, keep the two online subsystem plugins off in the `.uproject`, since they do not run
   in a browser: `{"Name": "OnlineSubsystemUtils", "Enabled": true, "PlatformDenyList": ["Wasm"]}`
   and the same for `OnlineSubsystemNull` (our sample project has both).
4. Put the plugin in your project's `Plugins/YouGame` folder: from the project's folder run
   `git clone https://github.com/yougame-co/yougame-unreal.git Plugins/YouGame`, or unzip the zip
   into `Plugins/`. Reopen the project, let it build, and enable **YouGame** in Edit > Plugins.
5. In Project Settings > Plugins > YouGame set **Game Slug** to the part after `/g/` in your
   listing's address. On YouGame the build always uses the site it is played on (yougame.co or
   staging.yougame.co), so the same zip works on both; **Use Staging** only matters on your own
   host, for a listing on staging.yougame.co. No listing yet? Leave Game Slug
   empty: hosted on your own https page, the build asks YouGame for that page's own listing
   (made on its first load, claimed later with a `yougame.json`), so sign-in and online work
   before you have one.
6. Wire the nodes (all under **YouGame** in the Blueprint menu): **Sign In**, then **Find Match**
   with Mode empty; on its Success the room is live. Send your own JSON with **Room Send**; read the
   others' with **On Room Message** and who is there with **On Room Players**.
   Where to put it: a world subsystem (as the example is) runs in every level by itself. A new game
   mode set only as the project's default does not run in the Third Person template's level, which
   sets its own game mode in World Settings.
   **Find Match is the public queue.** For friends to play together, also bind **On Friend Room**
   and pass its Room to **Join Friend Room**: that is how a player who was invited, or whose invite
   was accepted, lands in the same room (the example does it).
   **Playing with one friend, fast:** in the example press **F** (a private room), then open
   YouGame's sheet over the game (Shift+Tab) > Lobby > Invite friends. **Copy invite link** works for
   anyone, friends on YouGame or not; or type their @username there.
   **Your own game events:** Room Send carries any JSON, so send what happens in your game the same
   way the example sends positions, and act on it in On Room Message. In C++:
   `UYouGameRoomSendAction::RoomSend(World, TEXT("{\"hit\":{\"target\":\"<player id>\",\"damage\":10}}"), FString())->Activate();`
   and in your On Room Message handler look for `"hit"` in `Data`. Unreal's own replication does
   not run in a browser build: what you do not send, the other player does not see.
   A ready example is in the zip: `YouGame/Extras/Example/YouGameSampleMatch.h` and `.cpp` (C++, the
   same calls). Copy both into your game module's `Source/<Module>/` folder and add `"YouGame"` to
   `PublicDependencyModuleNames` in `<Module>.Build.cs`, with `"Slate"` and `"SlateCore"` (the example
   draws its status line with them; without `"SlateCore"` the build stops on link errors). In the game: **M** Find Match; **F** Friend
   Room, then **I** Invite Friend (the friend who accepts lands in your room); **K** Submit Score, **L**
   Get Leaderboard, **B** Open Feedback; **O** Save (where you stand), **P** Load (puts you back
   there), **X** Clear Save; **U** Sign In. In a room each player sends its position about 10 times a
   second with Room Send, and every other player appears as a copy of your character that moves where
   they move. When a second player joins, the players line up side by side at the start, and a line
   at the top left says "connected, 2 players". The browser console logs each step as `[yougame-unreal] sample: ...`.
7. Package and zip: double-click `Plugins\YouGame\Extras\PackageWeb.bat` in your project.
   It finds the project and Unreal 5.9, packages it for Wasm
   (Shipping), adds the `yougame.json` a web build needs on YouGame, and zips the result. You get
   `Saved\YouGameWeb\<Project>-YouGame.zip`, ready to upload; the same build is the folder
   `Saved\YouGameWeb\Wasm`. A Third Person game takes about 35-40 minutes and is about 130 MB.
   `PackageWeb.bat -Fast` is for test builds: it links much faster and the game runs a little
   slower. It warns when the `.uproject` entries of step 3 are missing, and never edits your files.
   `-Out <folder>` puts the build somewhere else; `-NoZip` leaves out the zip.
   Your first Unreal build may show a Windows firewall prompt for `UbaServer`: that is Unreal's own
   build accelerator, not the plugin.
8. By hand instead of step 7: in the editor, **Platforms > Wasm > Package Project** (Shipping),
   then pick an output folder `<Out>`. Or from a command prompt in `Engine\Build\BatchFiles`:
   `RunUAT.bat BuildCookRun -project=<P>.uproject -platform=Wasm -clientconfig=Shipping -build -cook
-stage -pak -iostore -package -archive -archivedirectory=<Out> -unattended`. `<Out>\Wasm` is the
   whole site; never upload `Wasm-Debug`. Put a `yougame.json` with
   `{"yougame":{"crossOriginIsolation":true}}` at the root of `<Out>\Wasm`, beside `index.html`,
   and zip the folder's contents.
9. Upload it as a **web game**: on YouGame's upload page (Create) keep the first tab, **Web game**,
   and drop the zip (a new game), or upload it as a new version from your game's Manage page. The
   **Unreal game** tab only explains these steps; its Windows choice is for desktop builds. Or have
   your coding agent run `node upload-build.mjs build.zip` (the zip, or the `Wasm` folder
   itself; the YouGame MCP's `upload_token` gives it the token). Up to 5 GB per build and 512 MB per file; files over 100 MB go up in parts,
   so the ~140 MB `.wasm` is fine.
10. Play it from the listing in Chrome (WebGPU, Chrome 133 or newer). To try online, open it in two
    windows signed in as two accounts and press your Find Match button in both.

### On your own site, and still on YouGame

Hosting it elsewhere works too, with sign-in, Find Match, friend rooms and feedback. The page needs cross-origin isolation (COOP same-origin, COEP
require-corp) and serves a file of about 150 MB, so itch.io, GitHub Pages and plain Cloudflare Pages
do not fit as they come. The `yougame.json` beside `index.html` (PackageWeb.bat writes it) is
where you name the game and yourself to claim its listing, below.

Off yougame.co, scores and online play use the Game Slug's listing. With **no slug**, the game gets
its own YouGame page just by being played:

1. The first time the page loads, YouGame makes an unlisted listing for it. The browser console
   prints its address and a claim link.
2. Add two keys at the top level of `yougame.json`: `"game": "<the address you want>"` and
   `"owner": "<your YouGame username>"`.
3. Open the claim link, sign in, press **This is my game: claim it**, add a screenshot and a title,
   and **Publish**. Your game's page on YouGame now has **Play on <your site>**, and it shows in
   search.

In a browser the same nodes talk to YouGame's web SDK (`sdk.js`) on the page instead of the YouGame
app: Sign In, Submit Score, Get Leaderboard, Save, Load, Clear Save, Open Feedback, Find Match,
Friend Room, Join Friend Room, Invite Friend, Set Match State and Report Match Result. Differences:

- Sign In opens the page's own sign-in, which may reload the game. Provider is ignored.
- Save keeps a copy in the browser for guests too and also on the account when signed in.
- Open Feedback opens only on yougame.co (elsewhere: `notHere`).
- Find Match and Friend Room answer with no ticket: the room is already live. The page's SDK holds
  the connection.
- Set Match State (true) on the host starts the match; Report Match Result names the winner for two
  players. With more players, send **YouGame Request** with Type `mpResult` and Body
  `{"winner":"<the winner's id from On Room Players>"}`.
- Update state and the start check do nothing in a browser.

**Online play:** YouGame rooms only (see "Online play in a browser build" below). On the
desktop Room Send answers `notAvailable` for now.

**YouGame's button over your game:** YouGame draws its menu button over the canvas: bottom-right on
desktop and embedded phones (keep 192 x 60 CSS px clear), top-left in phone full screen (72 x 72 CSS px
plus the device's safe area). Keep your own HUD out of those corners. The plugin has no layout node yet
(the page's `YouGame.ui.getLayout()` gives the exact rectangles to JavaScript).

**Full screen:** the plugin never asks for full screen. If your game does, ask for it on the page
container (the canvas's parent, or the whole page), never on the canvas, so YouGame's button and
cards stay visible.

The browser console shows one line per step starting `[yougame-unreal]` (SDK load, init, ready,
each answer's code), with no player data.

### Online play in a browser build

Unreal web builds get online play through the plugin, the same way every browser game on YouGame
does, with no server to build or run: **Find Match** (or **Friend Room** and **Invite Friend**) puts
the players in a room, then each player sends small JSON messages with **Room Send** (its position,
a hit, a score) and reads the others' with **On Room Message**; **On Room Players** says who is there
and who is the host. Your game decides what the messages mean and draws the other players itself.
Limits: text JSON only, 16 KB a message, up to 120 messages and 256 KB a second per player. The
example in step 6 does this. Unreal's own replication (listen or dedicated servers) is not supported
in browser builds.

## Nodes and events

Get **YouGame Subsystem** from your game instance. Bind events, then read its getters: the first
hello or start check may have happened before your Blueprint bound them. Calls run asynchronously;
results include **Reply**, with **Code**, **Error** and **Retry After Seconds** on refusal.

| You want                                        | Node / event                                      | Request or push                                              |
| ----------------------------------------------- | ------------------------------------------------- | ------------------------------------------------------------ |
| Know whether someone signed in                  | **Is Signed In**, **On Player Changed**           | hello `signedIn` (falls back to player facts for older apps) |
| Sign in                                         | **Sign In** (optional Provider)                   | `signIn {provider?}`                                         |
| Know sign-in completed or failed later          | **On Signed In**, **On Sign In Failed**           | `signedIn`, `signInFailed` pushes                            |
| Submit a run                                    | **Submit Score** (Score)                          | `submitScore {score}`                                        |
| Show scores                                     | **Get Leaderboard** (Limit, 1–100)                | `leaderboard {limit}`                                        |
| Keep progress                                   | **Save** (Data), **Load**, **Clear Save**         | `saveWrite {data}`, `saveLoad {}`, `saveClear {}`            |
| Send feedback                                   | **Open Feedback**                                 | `openFeedback {}` (browser fallback when no app answers)     |
| Find a two-player match                         | **Find Match** (Mode, Ranked)                     | `mpTicket {players:2,mode?,ranked,lobby}`                    |
| Make or reuse a private room                    | **Friend Room** (Mode, Players, Fresh)            | `mpFriendRoom {players,mode?,fresh?}`                        |
| Join an accepted friend's room                  | **Join Friend Room** (Room)                       | `mpFriendRoom {room}`                                        |
| Pick a friend to invite                         | **Invite Friend**                                 | `mpInvite {}`                                                |
| Hear that the picker never appeared             | **On Invite Not Shown**                           | `inviteNotShown` push                                        |
| Hear a friend's room to join                    | **On Friend Room** (Room, Players, Mode)          | `friendRoom {room,players,mode?}` push                       |
| Keep YouGame's button out of a match            | **Set Match State** (Open)                        | `matchState {open:true/false}`                               |
| Keep YouGame's button out of play               | **Set Game State** (Playing, Paused, Menu)        | `gameState {state}`; browser builds: `YouGame.setGameState`  |
| Report the result                               | **Report Match Result** (Outcome, Score)          | `mpResult {won:true/false/"draw",score}`                     |
| Show update progress                            | **Get Update State**, **On Update State**         | hello `update`, `updateChanged` push                         |
| Read the startup outcome                        | **Get Start State**, **On Start State**           | local start hook; no channel request                         |
| Send to the others in the room (browser builds) | **Room Send** (Data, To optional)                 | `roomSend {data,to?}`                                        |
| Hear a message from the room (browser builds)   | **On Room Message** (From, Data)                  | `roomMessage {from,data}` push                               |
| Know who is in the room (browser builds)        | **On Room Players** (Players, Host, You, Playing) | `roomPlayers {players,host,you,playing}` push                |

**YouGame Request** sends another request Type with a JSON object Body. **On YouGame Push** exposes
pushes by type and JSON for game logic, not logs. Its `friendRoom` payload contains only players
and optional mode; the room code stays in **On Friend Room**. Never log push JSON: other pushes
can contain private data.

### Sign in

Leave Provider empty, or use `apple`, `google`, `x`, `discord` or `github`. Without a provider the
browser fallback defaults to Google; an overlay may offer its own choice. **On Success** says only
that a surface opened: Result has **Opened**, **Where** (`Overlay`, `Browser`, `Window` for YouGame
Online's own sign-in window, or `Unknown` for an older app), and Reply. Wait for **On Signed In** to know it finished; the plugin reconnects to get
player facts automatically. **On Player Changed** reports those facts. Show "Player" when **Get
Player Name** is empty.

Sign In has separate **On Already Signed In**, **On Busy**, **On Sign In Failed** and **On Bad
Request** pins; each includes Reply. Wait Retry After Seconds on Busy. **On Failure** covers other
failures such as no app answering. The later `signInFailed` push uses the subsystem event, after the
node has finished. Do not automatically loop Sign In.

### Find a match

**Find Match** (Mode, Ranked) asks for a two-player match ticket. It sends the same lobby as the
Orb Duel sample, `{version:1, minPlayers:2, maxLocalPlayers:1, compatibility}`, so YouGame pairs only
copies with the same compatibility: **Match Compatibility** in Project Settings > Plugins > YouGame,
else **Game Version**, else the Project Version (1.0.0.0 unless you set it). Change it when your match packets
change. Only letters, digits and `. _ -` are kept and the value is cut to 64 characters, so `1.0 beta`
and `1.0beta` pair together; `friends-lobby-1`, `doubles-lobby-1` and values ending in
`-squad` mean something else to YouGame and send no lobby. On Success, connect your own multiplayer
code to **Url** using **Ticket**. The result does not name the opponent yet; show your own
placeholder until your match code learns it.

### Play with a friend

1. Call **Friend Room**, for example Mode `duel`, Players `2`, Fresh `false`:
   `{"type":"mpFriendRoom","players":2,"mode":"duel"}` (the channel adds an id).
2. On Success, connect your own multiplayer code to **Url** using **Ticket**. Then **Invite Friend**
   sends `{"type":"mpInvite"}`; the player picks the friend in YouGame's overlay. The success result
   has Opened and Where. It may be followed by **On Invite Not Shown** if the picker failed to appear.
3. On the friend's copy, **On Friend Room** supplies a room code, players and optional mode, **no
   ticket**. Pass its Room to **Join Friend Room** (`mpFriendRoom {room}`), then connect using the
   returned Ticket and Url. Mode is empty when absent. Never log the room or ticket.

Players can be 1–16. Fresh `true` asks for a new room instead of reusing the existing one; it is
never sent on a join. Room or ticket refusals go to On Failure with YouGame's code, including
`noFriendRoom`, `inviteNotShown`, `friends_held`, `room_full`, `not_invited`, build refusals and `busy`.
The game never names a friend or gets the friends list.

The app implements matchState. Call **Set Match State(true)** when the match starts and again every
few minutes during a long match: `open:true` lasts 10 minutes unless renewed. Call
**Set Match State(false)** when the match ends. A rematch on the same ticket counts as out of a match until you call
**Set Match State(true)** again. Renewals hold for at most 2 hours in a row; a false or a new
Find Match starts the count again.
On Complete means completed or ignored. Every refusal, timeout or unavailable app is ignored; there
is no error for the game to handle.

### Scores and saves

Submit Score sends a whole number clamped to 0–1,000,000,000,000,000. Results include rank, best and
whether this is a personal best. A busy score has Retry After Seconds: wait, then resend that score.
Leaderboard rows come from `entries`; the player's own row matches `me.player_id`.

Each player has one save per game, with no slot. **Save** takes text, for example `{"level":3}`;
valid JSON other than null goes as its JSON value, other text as a JSON string. The limit is 64 KiB
**after JSON encoding**, not 64 KiB of unescaped text. **Load** reads `save.data`; `save:null` means
no save. A `cloud:false` answer on any save call becomes On Failure with Code `signedOut`.

An old recognised copy may still read the leaderboard and load its save. It cannot post scores
(`old_build`, "Update this game to post scores."). A save write or clear refused because a newer
build owns the save keeps `old_build` and exactly **"Update this game to save."** An old copy's save
is not refused merely because a newer build exists; the site's save guard decides.

Hello's `lineMax` caps the entire encoded request, including the prefix and newline, in the C client.
Today's app advertises 69,632 bytes (68 KiB), which fits a full 64 KiB JSON save; an app that omits
it gets the 65,536-byte fallback. The vendored client never raises its cap above 68 KiB. A request
over the negotiated cap fails locally with `tooLong` and is not sent.

### Update state and start check

**Get Update State** returns an enum: None, Downloading, Ready, Installing, Waiting, or Unknown
(for a future state). Percent is 0–100 while downloading, or -1 when absent; Version may be empty.
Hello omits `update` when there is none; `updateChanged {update:{state:"none"}}` clears it. Keep
playing while it updates; show a short status only when helpful.

The plugin **vendors and calls `yougame_start.h` automatically** for a game started on its own
(start check disabled in the editor by default). Read **Get Start State** after binding **On Start
State**. Results mean:

| Result              | Meaning                                                                                       |
| ------------------- | --------------------------------------------------------------------------------------------- |
| NotChecked          | Started by the app, editor check disabled, or no valid slug configured                        |
| Running             | YouGame's local channel is up                                                                 |
| Opened              | Windows started YouGame hidden in the tray; the plugin looks for its channel                  |
| Offered             | The player chose Get YouGame; the download page opened                                        |
| Declined            | The player chose Play offline, closed the offer, or no offer could be shown                   |
| Installed / Missing | A quiet check found YouGame installed / absent; nothing shown or started                      |
| Unavailable         | Bad options, unsupported system, hook error or update retry window ended; carry on without it |
| Updating            | Windows update marker says an install is underway; Version is the target version              |
| Installing          | Windows: the player chose Get YouGame; it downloads and installs in the background, Percent 0-100 while downloading, -1 while installing |

For Installing, **keep the game running and show a short line such as "Installing YouGame… 42%"** (no percent
once Percent is -1). The plugin follows the install by itself: when YouGame is installed and started in the tray, On
Start State sends Opened and the plugin connects; if the install fails it opens YouGame's download page and sends
Offered. Clear your line on either.

For Updating, **show your own "YouGame is updating" line and call again after about 10 seconds,
for at most 5 minutes**. The plugin does these quiet retries for you and emits On Start State;
your game only needs to draw and clear its line. A dropped local connection checks the hook again
quietly so an install beginning after hello can also be reported. The initial Windows start does
not wait on the app; channel reconnects continue in the background.

The updated hook also has Mac/Linux install detection and its own offer. It cannot start YouGame
in the tray there and has no update marker there.

**When to ask** (Project Settings > Plugins > YouGame > **Ask When**): your game decides when the
player sees the Get YouGame window.

- **At Start** (default): the check above, before play. Suits a game that needs YouGame to play
  at all, such as one that uses it for controller support or plays only online.
- **On First Use**: at start the plugin starts an installed YouGame in the tray if it can, and
  never shows the window (Start State reads Installed or Missing otherwise). When the player
  presses Online, Friends or another YouGame button, call **Ask For YouGame**: it starts YouGame
  if it can, or shows the window, whose **Play offline** goes back to the game. It returns the outcome
  and also sends On Start State. Suits a game that plays offline with online as one choice.

Set **Game Name** (same page) to the name the window prints: "Play <Game Name> online". Empty, it
says "this game". With At Start, a player who presses Play offline once is not asked at start again;
the game asks at its YouGame button from then on, so call Ask For YouGame there either way.

Ask For YouGame waits while the window is up (as the start check does), and does nothing in a
browser build, in a game the app started, or in the editor unless Start Check In Editor is on.
After Running or Opened, go online as usual; after Declined or Offered, stay in your menu.

## Failure codes

| Code                       | What your game does                                                                                                                                                                                                                                                    |
| -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `signedOut`                | Offer Sign In when the player wants an account feature. Also normalises the page's `signed_out` and save `cloud:false`.                                                                                                                                                |
| `alreadySignedIn`          | Nothing to open; continue. Sign In uses its own pin.                                                                                                                                                                                                                   |
| `busy`                     | Wait Retry After Seconds before retrying. Sign In uses its own pin.                                                                                                                                                                                                    |
| `signInFailed`             | Sign-in did not start; allow another player-chosen attempt. Also a later push/event.                                                                                                                                                                                   |
| `badRequest`               | Fix the sign-in provider. Sign In uses its own pin.                                                                                                                                                                                                                    |
| `bad_request`              | Fix malformed score/save/match fields, null or oversized saves. Distinct from sign-in's `badRequest`.                                                                                                                                                                  |
| `noFriendRoom`             | Make or join a room before inviting (also after reconnect).                                                                                                                                                                                                            |
| `inviteNotShown`           | The overlay could not show the friend picker. Also a later push/event.                                                                                                                                                                                                 |
| `friends_held`             | YouGame refused the friends action; show Error.                                                                                                                                                                                                                        |
| `unknown_build`            | YouGame doesn't recognise this copy (a local or changed build never is): get the game from YouGame.                                                                                                                                                                    |
| `old_build`                | Use the latest version; preserve Error's score/save/online explanation.                                                                                                                                                                                                |
| `room_full`, `not_invited` | The ticket/join was refused; show Error and let the player choose another room.                                                                                                                                                                                        |
| `notHere`                  | Copy not checked yet, or its answer page unavailable; retry after the supplied delay.                                                                                                                                                                                  |
| `accountChanged`           | Another account signed in; reconnect and request the room again.                                                                                                                                                                                                       |
| `board_off`                | This game's leaderboard is off.                                                                                                                                                                                                                                        |
| `notAvailable`             | No YouGame answered here; continue playing. Also every call still waiting when the game closes (Error "The game is closing"): don't start a new YouGame call from that failure. Calls queued while closing get a few more passes, then are dropped without a callback. |
| `notYet`                   | This app does not answer this call; continue playing.                                                                                                                                                                                                                  |
| `noAnswer`                 | No answer before timeout, or connection closed; try later.                                                                                                                                                                                                             |
| `tooLong`                  | Reduce the encoded request to the channel's limit.                                                                                                                                                                                                                     |
| `noRoom`                   | Browser builds: Room Send or Report Match Result with no room; Find Match or Friend Room first.                                                                                                                                                                        |
| `cancelled`                | Browser builds: the player closed YouGame's searching card.                                                                                                                                                                                                            |
| `notStored`                | Browser builds: the browser kept no copy of the save.                                                                                                                                                                                                                  |
| `failed`                   | Browser builds: the page's SDK refused with no code of its own; show Error or try later.                                                                                                                                                                               |

Other app codes (including `closed`) pass through with Error and Retry After Seconds. Every call
refused signed out exposes the single `signedOut` code. Match-state refusals are always ignored.
Never log a room code, ticket, player name/id, save, or raw result/push JSON.

## Windows desktop builds (through the YouGame app)

A packaged Windows game gets sign-in, scores, saves, feedback and match tickets through the YouGame
app on the player's computer. On the desktop, **your game still connects to its own match server
and implements online play**: a ticket alone does not play a match, and Room Send answers
`notAvailable` there for now.

### Add it

1. Put the plugin in your project's `Plugins/YouGame` folder (clone github.com/yougame-co/yougame-unreal
   there, or unzip the zip into `Plugins/`). Unreal 5.8.4 on Windows.
   **The project needs C++ code.** A Blueprint-only project packages Unreal's stock `UnrealGame.exe`,
   and the plugin never loads (the log says `Plugin 'YouGame' failed to load because module 'YouGame'
could not be found`). In a Blueprint-only project, add one C++ class first (Tools > New C++
   Class, any parent), so the game gets its own executable with the plugin built in.
2. Let Unreal build it; enable **YouGame** in Edit > Plugins.
3. In Project Settings > Plugins > YouGame, set **Game Slug** to the part after `/g/` in your game's
   listing address. Until it is set, the start check is off and the log warns about the slug.
   Enable **Use Staging** for a test listing. Leave **Tray From Version** at
   `0.13.1407`. **Game Version** defaults to your project's version; it does not prove which build
   this is: YouGame recognises the published executable itself.
4. Package a Win64 **Shipping** build to upload (a Development build is for testing: Shipping writes
   no log file). Upload the packaged folder as your listing's Windows build, then make it live from
   your game's Manage page under Desktop builds, so YouGame recognises it for scores, saves and
   match tickets.

### What the player sees

The YouGame button appears at game start with the **Shift+Tab** hint and fades. It comes back for a
message, an invite or a lobby event. The game never draws YouGame's menus itself and never tells
the player to open the YouGame app or the tray. Use **Sign In**, **Invite Friend** and **Open Feedback**
when the player chooses those actions; YouGame owns their surfaces.

On Windows the start check leaves a running YouGame alone, starts an installed, sufficiently new
signed YouGame hidden in the tray, or shows YouGame's own small **Get YouGame / Play offline** offer.
The game continues whichever the player picks: Get YouGame closes the offer at once and installs YouGame in the
background (Start State Installing). While YouGame installs an update, the hook shows
nothing; your game can show its own short update line (see Start check below).

### Check your packaged game on staging

1. Install the staging app from staging.yougame.co and sign in. Use a staging listing and the
   published build's executable; close the regular app while testing because the C client prefers
   it when both run.
2. Run your packaged Win64 Development `.exe` (Shipping writes no log). The start check is normally skipped in the editor
   (enable **Start Check In Editor** to include it).
3. Bind the events above and wire your game's score, save, sign-in, feedback and match transitions.
   Test signed out too. Your game supplies the match-server connection, gameplay, local/offline
   saves if wanted, and its own update/status lines.
4. Inspect `Saved/Logs/<project>.log`: `LogYouGame` reports request types and refusal codes, never
   room codes or tickets. Shipping builds may omit these logs.

A self-started game gets account calls, scores, saves and friend rooms only on Windows, and only
where the app's match-ticket switch is on (staging for now). On Mac and Linux it answers hello,
sign-in and feedback; account calls answer `notYet`. A game the app starts uses stdin/stdout; Mac/Linux
packages also need `yougame-native.json` beside the entry for that transport. `mpResult` answers
`notYet` for a self-started game for now. `friends_held` is passed through if returned.

### The YouGame button and the game's state

Call the subsystem's **Set Game State** at each change: Playing when play starts, Paused on the
game's own pause, Menu on a main menu. YouGame uses it to keep its button out of the way in play and
show it on pause and in menus; until the app and site follow it, and for a game that never calls it,
the button keeps its usual fade. Desktop builds send it through yougame.h (`yg_set_game_state`); the
plugin sends it again after each reconnect. Browser builds call the page's `YouGame.setGameState`,
again once the SDK answers if the first send failed. Fire-and-forget: older apps and SDKs ignore it.

### Controller hints over a self-started game

Call the subsystem's `Set Input Kind` from your game's existing input events when the input changes
(Keys, PlayStation, Gamepad, GameCube). Call `Menu Button` once on its PS/Home press or GameCube
Up + Start chord. These additive SDK messages use the existing local channel; the plugin reads
no input itself. Older apps ignore them. With no report, the outside overlay keeps the keyboard
hint. The outside overlay currently requires a recognised, hardened Windows session.
