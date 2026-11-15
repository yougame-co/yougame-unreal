// YouGameWebQueue.h: the two hand-offs between the game thread and the browser's main thread in a
// browser build, in plain C++17 with no Unreal or Emscripten types (tested by
// engines/unreal/tests/run-tests.sh). The game thread may or may not be the browser's main thread;
// these work either way.
//   Outbox: the game thread queues requests; the main thread takes them and calls the shim.
//   Inbox: the shim's answers and pushes, queued on the main thread; the subsystem's tick takes them.
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace YouGameWebQueue
{
// What the game thread asks of the page.
struct FOutbound
{
	enum class EKind { Start, Call, Stop };
	EKind Kind = EKind::Call;
	double Key = 0;        // Call: the subsystem's key (exact up to 2^53)
	std::string Type;      // Call: the request type
	std::string Json;      // Start: the shim's config; Call: the body
	int TimeoutMs = 0;     // Call: 0 means the shim's default
};

// What the page hands back: an answer to Key, or a push of Type.
struct FInbound
{
	bool bPush = false;
	double Key = 0;
	std::string Type;
	std::string Json;
};

template <typename T>
class TLockedQueue
{
public:
	void Push(T Item)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Items.push_back(std::move(Item));
	}

	// Everything queued so far, oldest first; the queue is left empty.
	std::vector<T> Take()
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		std::vector<T> Out;
		Out.swap(Items);
		return Out;
	}

	// Push unless Max items are already queued; says whether it pushed.
	bool PushIfSizeBelow(T Item, size_t Max)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		if (Items.size() >= Max) return false;
		Items.push_back(std::move(Item));
		return true;
	}

	void Clear()
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Items.clear();
	}

private:
	std::mutex Mutex;
	std::vector<T> Items;
};

// One main-thread flush at a time is enough however many requests are queued: Schedule() says
// whether the caller should ask the main thread for one, BeginFlush() runs at its start. A request
// queued after BeginFlush() asks for another.
class FFlushGate
{
public:
	bool Schedule() { return !bScheduled.exchange(true); }
	void BeginFlush() { bScheduled.store(false); }

private:
	std::atomic<bool> bScheduled{false};
};
} // namespace YouGameWebQueue
