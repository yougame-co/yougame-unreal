// Written by engines/unreal/sync-web-shim.sh from yougame-unreal-web.js beside it. Do not edit:
// edit the .js, run the script, commit both. Compiled only in browser (Emscripten) builds.
#pragma once

#if defined(__EMSCRIPTEN__)
static const char YouGameWebShimSource[] = R"YGWEB(// yougame-unreal-web.js: the YouGame plugin for Unreal in a browser (Unreal 5.9 web) build.
//
// The plugin's Blueprint nodes send the same channel requests on every platform
// (UYouGameSubsystem::Call). On the desktop yougame.h carries them to the YouGame app; in a
// browser this file answers them through the page's YouGame web SDK (window.YouGame,
// https://yougame.co/sdk.js, documented in web/public/sdk.md), in the same answer shape the app
// uses: {ok:true, result:{...}} or {ok:false, code, error, retryAfter?}. A request the SDK has
// no equivalent for answers {ok:false, code:"notAvailable"}; nothing here throws at the engine.
//
// It calls only window.YouGame, never the site's own code, and holds no socket of its own: the
// SDK owns the room connection, so the same build works on yougame.co and on a creator's own
// page that loads the SDK.
//
// Runs on the browser's main thread only. YouGameWeb.cpp evaluates it once (from
// YouGameWebShim.h, written by engines/unreal/sync-web-shim.sh: edit this file, run that script,
// commit both), then calls:
//   ygUnrealInit(config, sink)               config {staging, game, compatibility, version}
//   ygUnrealCall(key, type, bodyJson, timeoutMs)
//   ygUnrealStop()
// and answers go back through sink.answer(key, json) and sink.push(type, json).
// Every step logs one console line starting "[yougame-unreal]" (a browser harness reads them):
// codes, request types and the slug only, never tokens or player data.
// Tested in plain Node by engines/unreal/tests/web_shim_test.mjs.
(function (root) {
  "use strict";
  if (!root || root.ygUnrealCall) return; // evaluated once per page

  var SDK_URL = "https://yougame.co/sdk.js";
  var STAGING_SDK_URL = "https://staging.yougame.co/sdk.js";
  // yougame.h's default when a call names no timeout.
  var DEFAULT_TIMEOUT_MS = 15000;
  // The SDK draws its own searching card with Cancel for these, and sign-in leaves the page: no
  // timer once the SDK has the call (loading the SDK is still timed).
  var NO_TIMEOUT = { mpTicket: true, mpFriendRoom: true, signIn: true, gameOver: true };
  // "result" too: the SDK clears room.playing when a match result comes in.
  var ROOM_EVENTS = ["lobby", "join", "leave", "host", "ready", "away", "back", "result"];

  var PREFIX = "[yougame-unreal] ";

  var config = { staging: false, game: "", compatibility: "", version: "" };
  var pageSdk = ""; // set by init: the page's own YouGame site's sdk.js, "" off YouGame
  var sink = null;
  // Bumped by every init and stop: an answer from an earlier generation is dropped, so a game
  // instance never hears another one's calls.
  var generation = 0;
  var sdkPromise = null;
  // The SDK object YouGame.init({game}) already ran on (or was skipped for): once per page.
  var initedSdk = null;
  var room = null;
  var roomHandlers = null;
  // A new room's first roster goes out right after Find Match's or Friend Room's answer, so a game
  // that binds On Room Players in On Success still hears it.
  var rosterDue = null;

  // One line per step. Only fixed words, request types, codes, URLs and the slug go in.
  function info(message) {
    try {
      if (root.console && root.console.info) root.console.info(PREFIX + message);
    } catch (e) {}
  }

  function log(message) {
    try {
      if (root.console && root.console.warn) root.console.warn(PREFIX + message);
    } catch (e) {}
  }

  // ---- Answers ------------------------------------------------------------------------------

  function ok(result) {
    return { ok: true, result: result === undefined || result === null ? {} : result };
  }

  function fail(code, error, retryAfterSeconds) {
    var out = { ok: false, code: code, error: error || "" };
    if (retryAfterSeconds > 0) out.retryAfter = Math.ceil(retryAfterSeconds);
    return out;
  }

  // An SDK rejection: its own code when it has one (ranked_signed_out, ranked_off, storage_full ...).
  function failFrom(error) {
    var message = (error && error.message) || String(error || "");
    if (error && typeof error.code === "string" && error.code) return fail(error.code, message);
    if (message === "Cancelled") return fail("cancelled", "The player closed YouGame's card.");
    return fail("failed", message);
  }

  function sendAnswer(key, type, reply) {
    if (!sink) return;
    // Room Send can run many times a second: only its refusals are logged.
    if (!(type === "roomSend" && reply && reply.ok)) info("answer " + type + " " + (reply && reply.ok ? "ok" : String((reply && reply.code) || "failed")));
    var json;
    try { json = JSON.stringify(reply); } catch (e) { json = JSON.stringify(fail("failed", "The answer could not be written as JSON")); }
    try { sink.answer(key, json); } catch (e) { log("could not hand an answer to the game"); }
  }

  function push(type, payload) {
    if (!sink) return;
    try { sink.push(type, JSON.stringify(payload)); } catch (e) { log("could not hand a push to the game"); }
  }

  // ---- The SDK, loaded once -----------------------------------------------------------------

  // YouGame.init({game}) once per page, before ready(): the documented call that lets the same
  // build post scores and play online from any host (sdk.md init). On yougame.co it changes nothing.
  function initSdk(sdk) {
    if (initedSdk === sdk) return;
    initedSdk = sdk;
    // No Game Slug: "auto" asks YouGame for this page's own listing (made on first load, claimed
    // later with yougame.json), so a build hosted anywhere gets sign-in, scores and online play
    // before it has a listing. On yougame.co the listing is already known and this changes nothing.
    var game = config.game || "auto";
    if (typeof sdk.init !== "function") {
      log("this YouGame SDK has no init: skipped");
      return;
    }
    try {
      sdk.init({ game: game });
      info("init game=" + game);
    } catch (e) {
      log("YouGame.init failed");
    }
  }

  // The SDK of the YouGame site this page belongs to, or "" off YouGame. Game pages are served
  // from <id>.yougame.co (production), <id>-stg.yougame.co (staging) and <id>-sN.yougame.co (our
  // own staging copies, staging-N.yougame.co). A host we do not recognise leaves it to Use Staging.
  function pageSdkUrl() {
    var host = "";
    try {
      host = String((root.location && root.location.hostname) || "").toLowerCase();
    } catch (e) {}
    var m;
    if (host === "staging.yougame.co" || /^[a-z0-9]+-stg\.yougame\.co$/.test(host)) return STAGING_SDK_URL;
    if ((m = /^staging-([1-9])\.yougame\.co$/.exec(host)) || (m = /^[a-z0-9]+-s([1-9])\.yougame\.co$/.exec(host))) {
      return "https://staging-" + m[1] + ".yougame.co/sdk.js";
    }
    if (host === "yougame.co" || host === "www.yougame.co" || /^[a-z0-9]+\.yougame\.co$/.test(host)) return SDK_URL;
    return "";
  }

  function loadSdk() {
    if (sdkPromise) return sdkPromise;
    var attempt = new Promise(function (resolve, reject) {
      if (root.YouGame) {
        info("sdk.js already on the page");
        return resolve(root.YouGame);
      }
      var doc = root.document;
      if (!doc || !doc.createElement) return reject(new Error("No page to load the YouGame SDK into"));
      var script = doc.createElement("script");
      script.src = pageSdk || (config.staging ? STAGING_SDK_URL : SDK_URL);
      script.async = true;
      script.onload = function () {
        if (root.YouGame) {
          info("sdk.js loaded");
          resolve(root.YouGame);
        } else reject(new Error("The YouGame SDK loaded but did not start"));
      };
      script.onerror = function () { reject(new Error("The YouGame SDK did not load")); };
      info("loading sdk.js from " + script.src);
      (doc.head || doc.documentElement || doc.body).appendChild(script);
    }).then(function (sdk) {
      initSdk(sdk);
      return Promise.resolve(sdk.ready()).then(function () {
        info("ready (" + (sdk.mode === "bridge" ? "on YouGame" : "off YouGame") + ")");
        return sdk;
      });
    });
    sdkPromise = attempt;
    // A failed load is tried again by the next call rather than failing every call after it.
    attempt.catch(function () {
      log("sdk.js did not load or start; the next call tries again");
      if (sdkPromise === attempt) sdkPromise = null;
    });
    return attempt;
  }

  // ---- The room the SDK holds for us --------------------------------------------------------

  function participantId(p) {
    return p && p.id !== undefined && p.id !== null ? String(p.id) : "";
  }

  function playersFact(r) {
    var list = (r && r.players) || [];
    var players = [];
    for (var i = 0; i < list.length; i++) {
      var p = list[i] || {};
      players.push({ id: p.id === undefined || p.id === null ? "" : String(p.id), name: typeof p.name === "string" ? p.name : "" });
    }
    return {
      players: players,
      host: r && r.host !== undefined && r.host !== null ? String(r.host) : "",
      you: r && r.me !== undefined && r.me !== null ? String(r.me) : "",
      playing: !!(r && r.playing),
    };
  }

  function forgetRoom() {
    if (room && roomHandlers && room.off) {
      for (var name in roomHandlers) {
        if (Object.prototype.hasOwnProperty.call(roomHandlers, name)) {
          try { room.off(name, roomHandlers[name]); } catch (e) {}
        }
      }
    }
    room = null;
    roomHandlers = null;
    rosterDue = null;
  }

  function keepRoom(next) {
    if (next === room) {
      rosterDue = next;
      return next;
    }
    forgetRoom();
    room = next;
    var handlers = {};
    var players = function () { if (room === next) push("roomPlayers", playersFact(next)); };
    for (var i = 0; i < ROOM_EVENTS.length; i++) handlers[ROOM_EVENTS[i]] = players;
    handlers.message = function (m) {
      if (room !== next || !m) return;
      push("roomMessage", { from: m.from === undefined || m.from === null ? "" : String(m.from), data: m.data === undefined ? null : m.data });
    };
    handlers.close = function () {
      if (room !== next) return;
      forgetRoom();
      push("roomPlayers", { players: [], host: "", you: "", playing: false, closed: true });
    };
    roomHandlers = handlers;
    if (next.on) {
      for (var name in handlers) {
        if (Object.prototype.hasOwnProperty.call(handlers, name)) next.on(name, handlers[name]);
      }
    }
    rosterDue = next;
    return next;
  }

  function sendDueRoster() {
    var due = rosterDue;
    rosterDue = null;
    if (due && due === room) push("roomPlayers", playersFact(due));
  }

  // Find Match's and Friend Room's answer: the desktop's {ticket, url, queue, room}. In the browser
  // the SDK holds the connection, so there is no ticket or url to hand over: the room is live
  // already, and Room Send, On Room Message and On Room Players are how the game uses it.
  function roomAnswer(r) {
    var out = playersFact(r);
    out.web = true;
    out.queue = r.queue || "";
    if (r.queue === "private" && r.code) out.room = String(r.code);
    return out;
  }

  function lobbyCompatibility(body) {
    var lobby = body && body.lobby;
    if (lobby && typeof lobby.compatibility === "string" && lobby.compatibility) return lobby.compatibility;
    return config.compatibility || "default";
  }

  function modeName(body) {
    return typeof body.mode === "string" && body.mode ? body.mode : "default";
  }

  function wholeNumber(value, fallback) {
    return typeof value === "number" && isFinite(value) ? Math.floor(value) : fallback;
  }

  function joinFriends(sdk, players, mode) {
    return sdk.multiplayer.joinLobby({
      queue: "friends",
      players: players,
      minPlayers: Math.min(2, players),
      maxLocalPlayers: 1,
      mode: mode,
      compatibility: config.compatibility || "default",
    }).then(function (r) { return ok(roomAnswer(keepRoom(r))); });
  }

  function myParticipant(r) {
    var list = r.matchParticipants || r.participants || [];
    for (var i = 0; i < list.length; i++) {
      var p = list[i];
      if (p && String(p.connectionId) === String(r.me) && !(p.localIndex > 0)) return p;
    }
    return null;
  }

  // ---- The request types --------------------------------------------------------------------

  var FEEDBACK_WORDS = {
    notHere: "Feedback opens when the game is played on YouGame.",
    unsupported: "This page cannot open feedback yet.",
    noActivation: "Feedback opens from a button press. Try the button again.",
    alreadyOpen: "Feedback is already open.",
    tooSoon: "Feedback was just opened. Try again in a moment.",
    noAnswer: "YouGame did not answer. Try again in a moment.",
  };

  var handlers = {
    // The player facts the desktop's hello carries.
    hello: function (sdk) {
      var onYouGame = sdk.mode === "bridge";
      var player = sdk.player || {};
      var game = sdk.game || null;
      return ok({
        signedIn: !!(sdk.auth && sdk.auth.signedIn),
        player: { name: typeof player.name === "string" ? player.name : "" },
        onYouGame: onYouGame,
        feedback: onYouGame,
        game: game ? { slug: game.slug || "", title: game.title || "" } : null,
      });
    },

    // The SDK has no provider choice: the page's own sign-in opens, and the game reloads after it.
    signIn: function (sdk) {
      if (sdk.auth && sdk.auth.signedIn) return fail("alreadySignedIn", "The player is signed in.");
      if (sdk.mode !== "bridge" || !sdk.auth) return fail("notAvailable", "Open this game on YouGame to sign in.");
      return sdk.auth.signIn().then(function (r) {
        if (r && r.signedIn) return fail("alreadySignedIn", "The player is signed in.");
        return ok({ opened: true, where: "browser" });
      }, function (e) { return fail("signInFailed", (e && e.message) || "Sign-in did not open."); });
    },

    submitScore: function (sdk, body) {
      return sdk.submitScore(wholeNumber(body.score, 0)).then(ok);
    },

    // YouGame's end-of-run card (sdk.md gameOver); answers with the submit result.
    gameOver: function (sdk, body) {
      return Promise.resolve(sdk.gameOver(wholeNumber(body.score, 0), {})).then(ok);
    },

    leaderboard: function (sdk, body) {
      var options = { limit: wholeNumber(body.limit, 10) };
      if (body.period === "day" || body.period === "week") options.period = body.period;
      if (body.scope === "friends") options.scope = "friends";
      return sdk.getLeaderboard(options).then(function (r) {
        r = r || {};
        var me = sdk.player && sdk.player.id;
        var entries = r.entries || [];
        for (var i = 0; i < entries.length; i++) {
          if (entries[i] && me && entries[i].player_id === me) entries[i].you = true;
        }
        return ok(r);
      });
    },

    // In the browser a save always lands in the browser (a guest's too) and also on the account
    // when the player is signed in; a guest's browser save joins their account when they sign in
    // (sdk.md Saves). So only "nothing was stored" is a failure here, never signed out.
    saveWrite: function (sdk, body) {
      if (body.data === undefined || body.data === null) return fail("bad_request", "Save needs data.");
      return sdk.save(body.data).then(function (r) {
        r = r || {};
        if (r.cloud) return ok({ cloud: true, updatedAt: r.updatedAt || null });
        if (r.local) return ok({ local: true, updatedAt: r.updatedAt || null });
        return fail("notStored", "The browser did not keep the save.");
      }, function (e) { return fail((e && e.code) || "bad_request", (e && e.message) || "The save was refused."); });
    },

    saveLoad: function (sdk) {
      return sdk.load().then(function (r) {
        r = r || {};
        var has = r.data !== undefined && r.data !== null;
        return ok({ save: has ? { data: r.data, build: r.build || null, updated_at: r.updatedAt || null } : null, source: r.source || "local" });
      });
    },

    saveClear: function (sdk) {
      return sdk.clearSave().then(function (r) {
        r = r || {};
        if (r.error) return fail("failed", r.error);
        return ok({ cleared: true, account: !!r.cloud });
      });
    },

    openFeedback: function (sdk, body) {
      var options = {};
      if (body.kind === "bug" || body.kind === "idea") options.kind = body.kind;
      if (typeof body.draftText === "string" && body.draftText) options.text = body.draftText;
      return sdk.feedback(options).then(function (r) {
        r = r || {};
        if (r.opened) return ok({ opened: true, where: "overlay" });
        var reason = typeof r.reason === "string" && r.reason ? r.reason : "notHere";
        return fail(reason, FEEDBACK_WORDS[reason] || FEEDBACK_WORDS.noAnswer, (r.retryInMs || 0) / 1000);
      });
    },

    // Find Match: YouGame's casual or ranked queue; the SDK draws the searching card.
    mpTicket: function (sdk, body) {
      var players = wholeNumber(body.players, 2);
      var lobby = body.lobby || {};
      return sdk.multiplayer.joinLobby({
        queue: body.ranked ? "ranked" : "casual",
        players: players,
        minPlayers: wholeNumber(lobby.minPlayers, players),
        maxLocalPlayers: 1,
        mode: modeName(body),
        compatibility: lobbyCompatibility(body),
      }).then(function (r) { return ok(roomAnswer(keepRoom(r))); });
    },

    // Friend Room {players, mode?, fresh?} makes or reuses a friends room; {room} joins the one an
    // invite opened the game with (On Friend Room).
    mpFriendRoom: function (sdk, body) {
      if (typeof body.room === "string" && body.room) {
        if (sdk.multiplayer.invite !== body.room) return fail("noFriendRoom", "That invite is no longer open.");
        return joinFriends(sdk, wholeNumber(body.players, 2), modeName(body));
      }
      if (!body.fresh && room && room.queue === "private" && room.connected !== false) return ok(roomAnswer(room));
      return joinFriends(sdk, wholeNumber(body.players, 2), modeName(body));
    },

    mpInvite: function () {
      if (!room || room.queue !== "private") return fail("noFriendRoom", "Make a friend room first.");
      return Promise.resolve(room.showLobby()).then(function () { return ok({ opened: true, where: "overlay" }); });
    },

    // Set Match State (true) is the game's Start: the host begins the match (sdk.md beginMatch),
    // which every player hears as On Room Players with Playing set, and which Report Match Result needs.
    matchState: function (sdk, body) {
      var open = !!body.open;
      if (!open || !room) return ok({ open: open });
      if (String(room.host) !== String(room.me) || room.playing) return ok({ open: true, began: false });
      return Promise.resolve(room.beginMatch({ revision: room.revision })).then(function () { return ok({ open: true, began: true }); });
    },

    // Report Match Result {won: true | false | "draw", score}: every player reports the same
    // outcome by participant id (sdk.md completeMatch). With more than two players, a YouGame
    // Request of this type with {winner: <a room player id from On Room Players>} names the winner.
    mpResult: function (sdk, body) {
      if (!room) return fail("noRoom", "There is no match to report.");
      var outcome;
      if (body.won === "draw") outcome = { draw: true };
      else if (typeof body.winner === "string" && body.winner) {
        var all = room.matchParticipants || room.participants || [];
        for (var j = 0; j < all.length && !outcome; j++) {
          if (all[j] && String(all[j].connectionId) === body.winner && !(all[j].localIndex > 0)) outcome = { winner: participantId(all[j]) };
        }
        if (!outcome) return fail("badRequest", "That player is not in this match.");
      } else {
        var me = myParticipant(room);
        var list = room.matchParticipants || room.participants || [];
        var others = [];
        for (var i = 0; i < list.length; i++) {
          if (list[i] && (!me || participantId(list[i]) !== participantId(me))) others.push(list[i]);
        }
        if (body.won === true && me) outcome = { winner: participantId(me) };
        else if (body.won === false && others.length === 1) outcome = { winner: participantId(others[0]) };
        else return fail("badRequest", "With more than two players, send mpResult with a YouGame Request and {winner: <room player id>}.");
      }
      return Promise.resolve(room.completeMatch(outcome)).then(ok);
    },

    // Room Send {data, to?}: any JSON value to everyone else in the room, or to one player.
    roomSend: function (sdk, body) {
      if (!room) return fail("noRoom", "There is no room to send to.");
      if (body.data === undefined) return fail("badRequest", "Room Send needs data.");
      if (typeof body.to === "string" && body.to) room.send(body.data, body.to);
      else room.send(body.data);
      return ok({ sent: true });
    },
  };

  // ---- Unreal's UDP relay, through YouGame's engine world -----------------------------------
  //
  // Unreal 5.9's web socket layer (SocketSubsystemWasm) sends each UDP datagram for ip:port as one
  // binary message on new WebSocket(Relay + "/udp?to=ip:port"), and reads each binary message back
  // as one datagram from there. Relay is "wss://" + the -WebRelay=<host> switch. Launched with
  // -WebRelay=yougame-relay.invalid, those sockets come here instead: each one becomes a socket to
  // this game's engine world (YouGame.servers.connect(world), sdk.md "Engine worlds"), where
  // engines/unreal/server/relay.mjs hands its frames to the dedicated server as UDP. Every other
  // WebSocket on the page is the browser's own, untouched. The LAN beacon (/beacon) has no world:
  // its socket stays connecting and drops what it is sent, so the layer neither errors nor retries.

  var RELAY_HOST = "yougame-relay.invalid";
  var WORLD_RE = /^[a-z0-9-]{1,32}$/;
  var relayWorld = "main";
  var RELAY_QUEUE = 256; // datagrams kept while the world starts; UDP drops the rest anyway

  function relayEvent(type, socket, extra) {
    var e = { type: type, target: socket, currentTarget: socket };
    for (var k in extra) if (Object.prototype.hasOwnProperty.call(extra, k)) e[k] = extra[k];
    return e;
  }

  function RelaySocket(url, beacon) {
    var self = this;
    var real = null;
    var queue = [];
    var listeners = {};
    var state = 0; // CONNECTING
    var binaryType = "arraybuffer";
    this.url = url;
    this.protocol = "";
    this.extensions = "";
    this.onopen = this.onmessage = this.onclose = this.onerror = null;
    Object.defineProperty(this, "readyState", { get: function () { return state; } });
    Object.defineProperty(this, "bufferedAmount", { get: function () { return real ? real.bufferedAmount || 0 : 0; } });
    Object.defineProperty(this, "binaryType", {
      get: function () { return binaryType; },
      set: function (v) { binaryType = v; if (real) real.binaryType = v; },
    });
    function fire(type, extra) {
      var e = relayEvent(type, self, extra || {});
      var on = self["on" + type];
      try { if (typeof on === "function") on.call(self, e); } catch (err) { log("the game's " + type + " handler threw"); }
      var list = (listeners[type] || []).slice();
      for (var i = 0; i < list.length; i++) {
        try { list[i].call(self, e); } catch (err) { log("the game's " + type + " listener threw"); }
      }
    }
    function closed(code, reason) {
      if (state === 3) return;
      state = 3;
      queue = [];
      fire("close", { code: code, reason: reason || "", wasClean: code === 1000 });
    }
    this.addEventListener = function (type, fn) { if (typeof fn === "function") (listeners[type] = listeners[type] || []).push(fn); };
    this.removeEventListener = function (type, fn) { listeners[type] = (listeners[type] || []).filter(function (f) { return f !== fn; }); };
    this.dispatchEvent = function (e) { if (e && e.type) fire(e.type, e); return true; };
    this.send = function (data) {
      if (state === 1 && real) { real.send(data); return; }
      if (state === 0 && !beacon && queue.length < RELAY_QUEUE) queue.push(data);
    };
    this.close = function (code, reason) {
      if (state >= 2) return;
      if (real) { state = 2; try { real.close(code || 1000, reason || ""); } catch (e) {} return; }
      // Not connected yet: the world socket, if it opens later, is closed at once.
      Promise.resolve().then(function () { closed(code || 1000, reason); });
      state = 2;
    };
    if (beacon) return; // stays connecting: no LAN broadcast on YouGame
    var world = relayWorld;
    info("relay connecting world=" + world);
    loadSdk().then(function (sdk) {
      if (!sdk.servers || typeof sdk.servers.connect !== "function") throw new Error("This YouGame SDK has no engine worlds");
      return sdk.servers.connect(world);
    }).then(function (ws) {
      if (state !== 0) { try { ws.close(1000, "left"); } catch (e) {} return; }
      real = ws;
      ws.binaryType = binaryType;
      ws.onmessage = function (e) { if (state === 1) fire("message", { data: e.data }); };
      ws.onerror = function () { fire("error", {}); };
      ws.onclose = function (e) { real = null; info("relay closed " + (e && e.code)); closed(e ? e.code : 1006, e ? e.reason : ""); };
      state = 1;
      info("relay open world=" + world);
      fire("open", {});
      var waiting = queue;
      queue = [];
      for (var i = 0; i < waiting.length && state === 1; i++) ws.send(waiting[i]);
    }).catch(function (e) {
      if (state === 3) return;
      log("relay could not open world " + world + ": " + ((e && e.message) || "failed"));
      fire("error", {});
      closed(1006, "");
    });
  }

  function relayTarget(url) {
    var m = /^wss?:\/\/([^\/:?#]+)(?::\d+)?(\/[^?#]*)?/i.exec(String(url || ""));
    if (!m || m[1].toLowerCase() !== RELAY_HOST) return null;
    return { beacon: (m[2] || "/").indexOf("/beacon") === 0 };
  }

  function installRelay() {
    var Native = root.WebSocket;
    if (typeof Native !== "function" || Native.__yougameRelay) return;
    var Wrapped = function WebSocket(url, protocols) {
      var target = relayTarget(url);
      if (target) return new RelaySocket(String(url), target.beacon);
      return protocols === undefined ? new Native(url) : new Native(url, protocols);
    };
    Wrapped.prototype = Native.prototype;
    Wrapped.CONNECTING = 0;
    Wrapped.OPEN = 1;
    Wrapped.CLOSING = 2;
    Wrapped.CLOSED = 3;
    Wrapped.__yougameRelay = true;
    RelaySocket.prototype.CONNECTING = 0;
    RelaySocket.prototype.OPEN = 1;
    RelaySocket.prototype.CLOSING = 2;
    RelaySocket.prototype.CLOSED = 3;
    root.WebSocket = Wrapped;
  }

  // Which engine world the relay's sockets open (default "main"). The C++ side calls
  // window.__yougameUnreal.setWorld("<id>") before it travels to the server; sockets already open
  // stay in their world. Answers true when the id was taken.
  function setWorld(id) {
    if (typeof id !== "string" || !WORLD_RE.test(id)) {
      log("setWorld: a world id is 1 to 32 lowercase letters, digits or -");
      return false;
    }
    relayWorld = id;
    info("relay world=" + id);
    return true;
  }

  installRelay();
  root.__yougameUnreal = { setWorld: setWorld, world: function () { return relayWorld; }, relayHost: RELAY_HOST };

  // The same through a YouGame Request: {"world":"<id>"}.
  handlers.serverWorld = function (sdk, body) {
    return setWorld(body.world) ? ok({ world: relayWorld }) : fail("badRequest", "A world id is 1 to 32 lowercase letters, digits or -.");
  };

  // ---- The three functions the engine calls -------------------------------------------------

  function call(key, type, bodyJson, timeoutMs) {
    var mine = generation;
    var done = false;
    var timer = null;
    function finish(reply) {
      if (done) return;
      done = true;
      if (timer !== null) root.clearTimeout(timer);
      if (mine === generation) {
        sendAnswer(key, type, reply);
        sendDueRoster();
      } else info("answer " + type + " dropped (an earlier start)");
    }
    var body = {};
    if (typeof bodyJson === "string" && bodyJson.replace(/\s+/g, "") !== "") {
      try { body = JSON.parse(bodyJson); } catch (e) { body = undefined; }
    }
    // Every answer comes later, never inside the call that asked.
    if (!body || typeof body !== "object" || Array.isArray(body)) {
      Promise.resolve().then(function () { finish(fail("badRequest", "The request body must be a JSON object.")); });
      return;
    }
    var handler = Object.prototype.hasOwnProperty.call(handlers, type) ? handlers[type] : null;
    if (!handler) {
      Promise.resolve().then(function () { finish(fail("notAvailable", "This request is not available in the browser.")); });
      return;
    }
    var ms = typeof timeoutMs === "number" && timeoutMs > 0 ? timeoutMs : DEFAULT_TIMEOUT_MS;
    timer = root.setTimeout(function () { timer = null; finish(fail("noAnswer", "YouGame did not answer in time.")); }, ms);
    loadSdk().then(function (sdk) {
      if (NO_TIMEOUT[type] && timer !== null) {
        root.clearTimeout(timer);
        timer = null;
      }
      return handler(sdk, body);
    }).then(finish, function (e) { finish(failFrom(e)); });
  }

  function init(nextConfig, nextSink) {
    generation++;
    forgetRoom();
    nextConfig = nextConfig || {};
    // On a YouGame game page the site is the page's own, whatever Use Staging says: one build
    // plays on yougame.co and on staging.yougame.co. Anywhere else the setting decides.
    pageSdk = pageSdkUrl();
    var wantStaging = !!nextConfig.staging;
    config = {
      staging: pageSdk ? pageSdk !== SDK_URL : wantStaging,
      game: typeof nextConfig.game === "string" ? nextConfig.game.trim() : "",
      compatibility: typeof nextConfig.compatibility === "string" ? nextConfig.compatibility : "",
      version: typeof nextConfig.version === "string" ? nextConfig.version : "",
    };
    sink = nextSink || defaultSink();
    info("start (" + (config.staging ? "staging" : "production") + ")");
    if (pageSdk && config.staging !== wantStaging) info("this page is on YouGame's " + (config.staging ? "staging" : "production") + " site: Use Staging is ignored here");
    var mine = generation;
    // A friend's invite opened the game: tell it, the way the desktop's friendRoom push does.
    loadSdk().then(function (sdk) {
      if (mine !== generation) return;
      var code = sdk.multiplayer && sdk.multiplayer.invite;
      if (typeof code === "string" && code) push("friendRoom", { room: code });
    }, function () { log("the YouGame SDK is not available"); });
  }

  function stop() {
    generation++;
    info("stop");
    var leaving = room;
    forgetRoom();
    sink = null;
    if (leaving && leaving.leave) {
      try { leaving.leave(); } catch (e) {}
    }
  }

  // The sink YouGameWeb.cpp builds: its two exported functions take a UTF-8 copy the C++ side
  // frees, made by Emscripten's stringToNewUTF8. Key and pointers cross as plain Numbers (the
  // exports take doubles, so wasm64 needs no BigInt); exact up to 2^53.
  function makeSink(answerFn, pushFn, toC) {
    return {
      answer: function (key, json) { answerFn(key, toC(json)); },
      push: function (type, json) { pushFn(toC(type), toC(json)); },
    };
  }

  // UE59-CHECK: only when the engine passes no sink. Unreal 5.9's page names its Emscripten
  // Module either as a global or on window; look for both.
  function defaultSink() {
    var M = root.Module || (typeof Module !== "undefined" ? Module : null); // eslint-disable-line no-undef
    if (M && typeof M._yg_web_answer === "function" && typeof M._yg_web_push === "function" && typeof M.stringToNewUTF8 === "function") {
      return makeSink(M._yg_web_answer, M._yg_web_push, M.stringToNewUTF8);
    }
    log("no way back to the game was found; answers are dropped");
    return null;
  }

  root.ygUnrealInit = init;
  root.ygUnrealCall = call;
  root.ygUnrealStop = stop;
  root.ygUnrealSink = makeSink;
})(typeof window !== "undefined" ? window : typeof globalThis !== "undefined" ? globalThis : this);
)YGWEB";
#endif
