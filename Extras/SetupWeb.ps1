# SetupWeb.ps1: gets a Windows PC ready to package an Unreal 5.9 browser (Wasm) game, in one step.
# Run SetupWeb.bat beside this file (double-click), or: powershell -ExecutionPolicy Bypass -File SetupWeb.ps1 [-Check]
# What it does, in order, saying what it found for each:
#   1. finds Unreal 5.9 (Epic Games Launcher installs, or -Engine <folder>);
#   2. checks Visual Studio 2022 has the C++ tools (it never installs Visual Studio);
#   3. installs Emscripten 6.0.10 (emsdk) into -Emsdk (default %LOCALAPPDATA%\emsdk) unless it is already there,
#      using the engine's own Python, so no Git or Python is needed;
#   4. sets the user environment variable WASM_EMSDK_ROOT to it (only that variable, only for this Windows user);
#   5. asks Unreal itself (RunUAT Turnkey -command=VerifySdk -platform=Wasm).
# -Check only reports, and changes nothing.
param([string]$Engine = "", [string]$Emsdk = "", [switch]$Check)
$ErrorActionPreference = "Stop"
$Version = "6.0.10"
$missing = 0
function Ok($m) { Write-Host "  ok   $m" -ForegroundColor Green }
function Bad($m, $fix) { Write-Host "  MISSING  $m" -ForegroundColor Red; if ($fix) { Write-Host "           fix: $fix" }; $script:missing++ }
function Note($m) { Write-Host "  note $m" -ForegroundColor Yellow }
if (-not $Emsdk) { $Emsdk = Join-Path $env:LOCALAPPDATA "emsdk" }
Write-Host "YouGame: Unreal 5.9 web setup$(if ($Check) { ' (check only)' })"

# 1. Unreal 5.9
if (-not $Engine) {
  $dat = Join-Path $env:ProgramData "Epic\UnrealEngineLauncher\LauncherInstalled.dat"
  if (Test-Path $dat) {
    $list = (Get-Content $dat -Raw | ConvertFrom-Json).InstallationList
    $hit = $list | Where-Object { $_.AppName -match "^UE_5\.9" } | Select-Object -First 1
    if ($hit) { $Engine = $hit.InstallLocation }
  }
}
$python = $null
if ($Engine -and (Test-Path (Join-Path $Engine "Engine\Build\BatchFiles\RunUAT.bat"))) {
  Ok "Unreal 5.9 at $Engine"
  $python = Join-Path $Engine "Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
  if (-not (Test-Path (Join-Path $Engine "Engine\Platforms\Wasm"))) {
    Bad "this engine has no web platform (Engine\Platforms\Wasm)" "Epic Games Launcher > Unreal Engine > Library > 5.9 > Options: tick the web (Wasm) platform, then Apply"
  }
} else {
  Bad "Unreal 5.9" "install it from the Epic Games Launcher, or run again with -Engine <the UE_5.9 folder>"
}

# 2. Visual Studio 2022 C++ tools
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vs = $null
if (Test-Path $vswhere) { $vs = & $vswhere -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1 }
if ($vs) { Ok "Visual Studio 2022 C++ tools at $vs" }
else { Bad "Visual Studio 2022 with C++" "Visual Studio Installer: add the workload 'Desktop development with C++' (Unreal needs it to build the plugin)" }

# 3. Emscripten 6.0.10
$verFile = Join-Path $Emsdk "upstream\emscripten\emscripten-version.txt"
$have = if (Test-Path $verFile) { (Get-Content $verFile -Raw).Trim().Trim('"') } else { "" }
if ($have -eq $Version) { Ok "Emscripten $Version at $Emsdk" }
elseif ($Check) { Bad "Emscripten $Version (found: $(if ($have) { $have } else { 'none' }) at $Emsdk)" "run SetupWeb.bat without -Check" }
elseif (-not $python -or -not (Test-Path $python)) { Bad "Emscripten $Version" "needs Unreal 5.9 first (its Python runs the installer)" }
else {
  Write-Host "  ...  installing Emscripten $Version into $Emsdk (about 2 GB, 5-15 minutes)"
  if (-not (Test-Path (Join-Path $Emsdk "emsdk.py"))) {
    $zip = Join-Path $env:TEMP "yougame-emsdk.zip"
    Invoke-WebRequest "https://github.com/emscripten-core/emsdk/archive/refs/heads/main.zip" -OutFile $zip -UseBasicParsing
    $tmp = Join-Path $env:TEMP "yougame-emsdk"
    if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
    Expand-Archive $zip $tmp
    New-Item -ItemType Directory -Force (Split-Path $Emsdk) | Out-Null
    Move-Item (Join-Path $tmp "emsdk-main") $Emsdk
    Remove-Item $zip, $tmp -Recurse -Force -ErrorAction SilentlyContinue
  }
  Push-Location $Emsdk
  try {
    & $python emsdk.py install $Version; if ($LASTEXITCODE) { throw "emsdk install failed ($LASTEXITCODE)" }
    & $python emsdk.py activate $Version; if ($LASTEXITCODE) { throw "emsdk activate failed ($LASTEXITCODE)" }
  } finally { Pop-Location }
  $have = if (Test-Path $verFile) { (Get-Content $verFile -Raw).Trim().Trim('"') } else { "" }
  if ($have -eq $Version) { Ok "Emscripten $Version installed at $Emsdk" } else { Bad "Emscripten $Version did not install (found: $have)" "see the emsdk output above" }
}

# 4. WASM_EMSDK_ROOT (the engine checks it first, before EMSDK and AutoSDK)
$cur = [Environment]::GetEnvironmentVariable("WASM_EMSDK_ROOT", "User")
if ($cur -eq $Emsdk) { Ok "WASM_EMSDK_ROOT = $Emsdk" }
elseif ($Check) { Bad "WASM_EMSDK_ROOT = $Emsdk (now: $(if ($cur) { $cur } else { 'not set' }))" "run SetupWeb.bat without -Check" }
elseif ($have -eq $Version) {
  [Environment]::SetEnvironmentVariable("WASM_EMSDK_ROOT", $Emsdk, "User")
  $env:WASM_EMSDK_ROOT = $Emsdk
  Ok "WASM_EMSDK_ROOT set to $Emsdk (for your Windows user)"
}
$stale = [Environment]::GetEnvironmentVariable("EMSDK", "User"), [Environment]::GetEnvironmentVariable("EMSDK", "Machine") | Where-Object { $_ }
if ($stale) { Note "EMSDK is also set ($($stale -join ', ')); WASM_EMSDK_ROOT wins over it, so it is harmless for Unreal" }

# 5. Unreal's own check
if ($missing -eq 0 -and $Engine) {
  $env:WASM_EMSDK_ROOT = $Emsdk
  & (Join-Path $Engine "Engine\Build\BatchFiles\RunUAT.bat") Turnkey -command=VerifySdk -platform=Wasm | Select-Object -Last 15
  if ($LASTEXITCODE) { Bad "Unreal's SDK check (VerifySdk) failed" "read its lines above" } else { Ok "Unreal's SDK check passed" }
}

Write-Host ""
if ($missing) { Write-Host "Not ready: $missing thing(s) missing, listed above." -ForegroundColor Red; exit 1 }
Write-Host "Ready. Restart the Unreal editor (it reads WASM_EMSDK_ROOT only at start-up), then Platforms > Wasm > Package Project." -ForegroundColor Green
