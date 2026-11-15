// The implementations of the YouGame plugin's two C headers (ThirdParty/YouGameNative, copies of
// engines/native: yougame.h talks to the YouGame app, yougame_start.h is the start check). They
// are compiled here, once, and nowhere else. The module builds without unity files, so Windows'
// headers stay in this file.
//
// UE59-CHECK: in a browser (Emscripten) build only yougame.h's implementation is compiled, for the
// JSON readers YouGameWire.h needs on every platform; the subsystem never starts it there
// (YouGameWeb.cpp carries the calls) and yougame_start.h is left out (YouGameNativeIncludes.h). Its
// socket and process code is never called there, so the linker drops it: if Emscripten still
// refuses a symbol from it, that is the place to look.
#include "CoreMinimal.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#endif

THIRD_PARTY_INCLUDES_START
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(push)
// C written to C99: unchecked CRT calls, narrowing, shadowing and unused helpers on some systems.
#pragma warning(disable : 4996 4244 4267 4100 4127 4189 4456 4457 4458 4459 4505 4701 4703 4706 4245 4389 4018 4310 4311 4312 4302 4668)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wmissing-braces"
#pragma clang diagnostic ignored "-Wundef"
#endif

#include "YouGameNativeIncludes.h"

#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif
THIRD_PARTY_INCLUDES_END

#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif
