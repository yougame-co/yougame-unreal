using System.IO;
using UnrealBuildTool;

public class YouGame : ModuleRules
{
	public YouGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// YouGameNative.cpp compiles the two C headers' implementations with Windows' own headers;
		// keeping it out of unity files keeps those headers and macros out of the other files.
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"EngineSettings", // UGeneralProjectSettings: the Project Version told to YouGame
		});

		// yougame.h and yougame_start.h, copied from engines/native by engines/unreal/sync-headers.sh.
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "ThirdParty", "YouGameNative"));

		// UE59-CHECK: Unreal 5.9's browser (WebAssembly) platform is assumed to be named "Wasm".
		// The C++ side keys off Emscripten's own __EMSCRIPTEN__, so a different name only costs the
		// definition below (the subsystem then logs a warning on start); compared as a string so this
		// file still compiles on engines without that platform.
		if (Target.Platform.ToString() == "Wasm")
		{
			PrivateDefinitions.Add("YOUGAME_WEB_TARGET=1");
		}

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// yougame.h: the local channel's user check and the browser opener (advapi32, shell32,
			// ole32). yougame_start.h: its small window (user32, gdi32); newer copies read the app's
			// file version (version).
			PublicSystemLibraries.AddRange(new string[]
			{
				"advapi32.lib",
				"shell32.lib",
				"ole32.lib",
				"user32.lib",
				"gdi32.lib",
				"version.lib",
			});
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			// The start hook's install lookup and its own small offer window.
			PublicFrameworks.AddRange(new string[] { "CoreServices", "CoreFoundation" });
		}
	}
}
