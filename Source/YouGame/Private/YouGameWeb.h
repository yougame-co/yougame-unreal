// YouGameWeb.h: the YouGame plugin in a browser (Unreal 5.9 web) build. The subsystem's Call()
// hands each request to the page's shim (Private/Web/yougame-unreal-web.js), which answers it
// through the YouGame web SDK; answers and pushes come back through a queue the subsystem takes on
// its tick. Every touch of the page runs on the browser's main thread, whichever thread the game
// runs on. Desktop builds compile none of this.
#pragma once

// UE59-CHECK: a browser build is told apart by Emscripten's own macro (emcc always defines it),
// not by an Unreal platform macro, since Unreal 5.9's name for its web platform is not confirmed.
#if defined(__EMSCRIPTEN__)
#define YOUGAME_WEB 1
#else
#define YOUGAME_WEB 0
#endif

#if YOUGAME_WEB
#include <cstdint>
#include <string>
#include <vector>

#include "Web/YouGameWebQueue.h"

namespace YouGameWeb
{
/** Puts the shim on the page (once) and starts it with ConfigJson (YouGameWire::WebConfigBody). */
void Start(const std::string& ConfigJson);

/** Hands one request to the shim. Its answer comes back through TakeInbound(), never inside this. */
void Call(uint64_t Key, const std::string& Type, const std::string& BodyJson, int TimeoutMs);

/** Drops the shim's answers from here on and leaves its room. */
void Stop();

/** Answers and pushes that arrived since the last take, oldest first. Game thread. */
std::vector<YouGameWebQueue::FInbound> TakeInbound();
} // namespace YouGameWeb
#endif
