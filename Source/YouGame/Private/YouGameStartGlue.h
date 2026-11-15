// YouGameStartGlue.h: runs yougame_start.h's start hook so that it never brings up YouGame's
// window, in plain C++17 with no Unreal types (tested by engines/unreal/tests/run-tests.sh).
// The hook is vendored alongside this glue, including tray_from and update_to. Updating returns
// the target version without showing anything; the subsystem schedules bounded quiet retries.
#pragma once

#include <string>

#include "yougame_start.h"

namespace YouGameStart
{
struct FOutcome
{
	yg_start_result Result = YG_START_UNAVAILABLE;
	std::string UpdateVersion;
};

// Updating is a bounded retry, never a wait on the game thread. All times are monotonic seconds.
struct FUpdateRetry
{
	double Until = 0;
	double Next = 0;
	bool bActive = false;

	void Observe(yg_start_result Result, double Now)
	{
		if (Result != YG_START_UPDATING) { bActive = false; return; }
		if (!bActive) Until = Now + 300;
		bActive = true;
		Next = Now + 10;
	}

	bool Due(double Now)
	{
		if (!bActive) return false;
		if (Now >= Until) { bActive = false; return false; }
		return Now >= Next;
	}
};

// Slug: the game's listing slug. TrayFrom: "0.13.1407" or similar; NULL/empty disables tray start.
// Quiet: only look, never show or open anything.
inline FOutcome Run(const char* Slug, bool bStaging, const char* TrayFrom, bool bQuiet)
{
	FOutcome Out;
	yg_start_options Options;
	Options = yg_start_options();
	Options.game = Slug;
	Options.staging = bStaging ? 1 : 0;
	Options.quiet = bQuiet ? 1 : 0;
	char UpdateTo[33] = {};
	Options.update_to = UpdateTo;
	Options.update_to_cap = sizeof UpdateTo;
	Options.update_to_magic = YG_START_UPDATE_TO_MAGIC;
	Options.tray_from = (TrayFrom && *TrayFrom) ? TrayFrom : nullptr;
	Out.Result = yg_start(&Options);
	if (Out.Result == YG_START_UPDATING) Out.UpdateVersion = UpdateTo;
	return Out;
}

inline const char* ResultName(yg_start_result Result)
{
	switch (Result)
	{
	case YG_START_RUNNING: return "running";
	case YG_START_OPENED: return "started";
	case YG_START_OFFERED: return "offered (Get YouGame pressed)";
	case YG_START_DECLINED: return "offered (Not now)";
	case YG_START_INSTALLED: return "installed, not running";
	case YG_START_MISSING: return "not installed";
	case YG_START_UPDATING: return "updating";
	default: return "not available here";
	}
}
} // namespace YouGameStart
