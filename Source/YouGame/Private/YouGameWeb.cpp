// The YouGame plugin in a browser (Unreal 5.9 web) build: see YouGameWeb.h. Compiled only by
// Emscripten. The game thread never touches window: it queues requests in the outbox and asks the
// main thread to flush it; the main thread calls the shim, and the shim's answers land in the inbox
// through the two exported functions below.
#include "YouGameWeb.h"

#if YOUGAME_WEB

#include <cstdint>
#include <cstdlib>

#include <emscripten/emscripten.h>
#if defined(__EMSCRIPTEN_PTHREADS__)
#include <emscripten/proxying.h>
#include <emscripten/threading.h>
#endif

#include "Web/YouGameWebShim.h"

// The Emscripten library functions the EM_ASM blocks below use.
EM_JS_DEPS(yougame_unreal_web, "$UTF8ToString,$stringToNewUTF8");

namespace
{
using YouGameWebQueue::FInbound;
using YouGameWebQueue::FOutbound;

// Room traffic waiting while the game does not tick (a hidden tab) is capped; answers never are.
constexpr size_t MaxQueuedForPushes = 4096;

YouGameWebQueue::TLockedQueue<FOutbound>& Outbox()
{
	static YouGameWebQueue::TLockedQueue<FOutbound> Queue;
	return Queue;
}

YouGameWebQueue::TLockedQueue<FInbound>& Inbox()
{
	static YouGameWebQueue::TLockedQueue<FInbound> Queue;
	return Queue;
}

YouGameWebQueue::FFlushGate& Gate()
{
	static YouGameWebQueue::FFlushGate Flush;
	return Flush;
}

void AnswerNow(double Key, const char* Code, const char* Error)
{
	FInbound In;
	In.Key = Key;
	In.Json = std::string("{\"ok\":false,\"code\":\"") + Code + "\",\"error\":\"" + Error + "\"}";
	Inbox().Push(std::move(In));
}

// Main thread. Evaluates the shim once (a Function, or a script tag where the page refuses that)
// and starts it with a sink back into this module. Property names are quoted so a minifier never
// renames what the shim, evaluated at run time, defines.
bool StartShim(const std::string& ConfigJson)
{
	return EM_ASM_INT({
		var source = UTF8ToString($0);
		if (!window["ygUnrealCall"]) {
			try { (new Function(source))(); } catch (error) {
				console.warn("[yougame-unreal] starting the bridge with a script tag");
				try { var tag = document.createElement("script"); tag.textContent = source; (document.head || document.documentElement).appendChild(tag); } catch (ignored) {}
			}
		}
		if (!window["ygUnrealInit"] || !window["ygUnrealSink"]) return 0;
		window["ygUnrealInit"](JSON.parse(UTF8ToString($1)), window["ygUnrealSink"](_yg_web_answer, _yg_web_push, stringToNewUTF8));
		return 1;
	}, YouGameWebShimSource, ConfigJson.c_str()) != 0;
}

// Main thread.
bool CallShim(const FOutbound& Out)
{
	return EM_ASM_INT({
		var call = window["ygUnrealCall"];
		if (!call) return 0;
		call($0, UTF8ToString($1), UTF8ToString($2), $3);
		return 1;
	}, Out.Key, Out.Type.c_str(), Out.Json.c_str(), Out.TimeoutMs) != 0;
}

// Main thread.
void FlushOutbox(void* /*Unused*/)
{
	Gate().BeginFlush();
	for (const FOutbound& Out : Outbox().Take())
	{
		switch (Out.Kind)
		{
		case FOutbound::EKind::Start:
			// Pushes still queued from an earlier start belong to nobody now. The shim pushes only on
			// this thread, so this is the exact point between the two starts.
			Inbox().Clear();
			if (!StartShim(Out.Json))
			{
				EM_ASM({ console.warn("[yougame-unreal] the browser bridge did not start; YouGame calls answer notAvailable"); });
			}
			break;
		case FOutbound::EKind::Call:
			if (!CallShim(Out))
			{
				AnswerNow(Out.Key, "notAvailable", "YouGame is not available here");
			}
			break;
		case FOutbound::EKind::Stop:
			EM_ASM({ var stop = window["ygUnrealStop"]; if (stop) stop(); });
			Inbox().Clear();
			break;
		}
	}
}

// Any thread: asks the main thread for one flush, or flushes now when this is the main thread.
void ScheduleFlush()
{
	if (!Gate().Schedule())
	{
		return;
	}
#if defined(__EMSCRIPTEN_PTHREADS__)
	if (!emscripten_is_main_runtime_thread())
	{
		// UE59-CHECK: Emscripten's proxying queue (emscripten/proxying.h); the main thread runs
		// FlushOutbox from its event loop. Works whether or not the game thread is the main thread.
		if (!emscripten_proxy_async(emscripten_proxy_get_system_queue(), emscripten_main_runtime_thread_id(), &FlushOutbox, nullptr))
		{
			Gate().BeginFlush(); // not queued: the next request asks again (the subsystem's deadline answers this one)
		}
		return;
	}
#endif
	FlushOutbox(nullptr);
}

// The shim passes each pointer from stringToNewUTF8 as a JS Number; heap addresses are below 2^53.
std::string TakeJsString(double Pointer)
{
	char* Text = Pointer > 0 ? reinterpret_cast<char*>(static_cast<uintptr_t>(Pointer)) : nullptr;
	std::string Out = Text ? Text : "";
	std::free(Text);
	return Out;
}
} // namespace

// Called by the shim on the main thread with UTF-8 copies made by stringToNewUTF8, which these take
// over and free. The pointers are doubles in the signature, so the same JS works on wasm32 and wasm64
// (an i64 parameter would need a BigInt from JS). UE59-CHECK: stringToNewUTF8 hands back a Number on
// Unreal 5.9's wasm64 build; the browser main thread locks the inbox's mutex here (held only for a
// push or a vector swap), which a build with -sALLOW_BLOCKING_ON_MAIN_THREAD=0 would refuse.
extern "C" EMSCRIPTEN_KEEPALIVE void yg_web_answer(double Key, double JsonPointer)
{
	FInbound In;
	In.Key = Key;
	In.Json = TakeJsString(JsonPointer);
	Inbox().Push(std::move(In));
}

extern "C" EMSCRIPTEN_KEEPALIVE void yg_web_push(double TypePointer, double JsonPointer)
{
	FInbound In;
	In.bPush = true;
	In.Type = TakeJsString(TypePointer);
	In.Json = TakeJsString(JsonPointer);
	Inbox().PushIfSizeBelow(std::move(In), MaxQueuedForPushes);
}

namespace YouGameWeb
{
void Start(const std::string& ConfigJson)
{
	FOutbound Out;
	Out.Kind = FOutbound::EKind::Start;
	Out.Json = ConfigJson;
	Outbox().Push(std::move(Out));
	ScheduleFlush();
}

void Call(uint64_t Key, const std::string& Type, const std::string& BodyJson, int TimeoutMs)
{
	FOutbound Out;
	Out.Kind = FOutbound::EKind::Call;
	Out.Key = static_cast<double>(Key);
	Out.Type = Type;
	Out.Json = BodyJson.empty() ? std::string("{}") : BodyJson;
	Out.TimeoutMs = TimeoutMs;
	Outbox().Push(std::move(Out));
	ScheduleFlush();
}

void Stop()
{
	FOutbound Out;
	Out.Kind = FOutbound::EKind::Stop;
	Outbox().Push(std::move(Out));
	ScheduleFlush();
	// Answers still on their way carry keys no game instance waits for: the subsystem drops them.
	// The main thread clears again once the shim has stopped (pushes have no key).
	Inbox().Clear();
}

std::vector<YouGameWebQueue::FInbound> TakeInbound()
{
	return Inbox().Take();
}
} // namespace YouGameWeb

#endif // YOUGAME_WEB
