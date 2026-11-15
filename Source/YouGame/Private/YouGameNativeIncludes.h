// Keep Unreal's assertion macros away from identifiers in the native implementations.
#pragma once
#pragma push_macro("verify")
#pragma push_macro("check")
#undef verify
#undef check

#if defined(__EMSCRIPTEN__)
// Emscripten's libc has no getpeereid, which yougame.h's unix-socket code (Mac and BSD branch) calls.
// A browser has no YouGame app to connect to and the plugin never starts yougame.h there, so a
// refusal is the truth; without it em++ stops at "undeclared identifier 'getpeereid'".
#include <sys/types.h>
static inline int getpeereid(int /*Fd*/, uid_t* /*Uid*/, gid_t* /*Gid*/) { return -1; }
#endif
#define YOUGAME_IMPLEMENTATION
#include "yougame.h"
// A browser build has no YouGame app to start or offer: yougame_start.h stays out. It still
// compiles yougame.h, whose JSON readers YouGameWire.h uses; nothing calls yg_init there.
#if !defined(__EMSCRIPTEN__)
#define YOUGAME_START_IMPLEMENTATION
#include "yougame_start.h"
#endif

#pragma pop_macro("check")
#pragma pop_macro("verify")
