# PackageWeb.ps1: packages this project as an Unreal 5.9 browser (Wasm) game and zips it for YouGame, in one step.
# Run PackageWeb.bat beside this file (double-click), or:
#   powershell -ExecutionPolicy Bypass -File PackageWeb.ps1 [-Fast] [-NoZip] [-Out <folder>] [-Project <.uproject>] [-Engine <folder>]
# Run SetupWeb.bat once before the first time. What this does, in order, saying what it found for each:
#   1. finds the project: the plugin sits in <Project>\Plugins\YouGame, so the project is three folders up from this
#      file (or -Project <path to the .uproject>);
#   2. finds Unreal 5.9 (Epic Games Launcher installs, or -Engine <folder>), the way SetupWeb.ps1 does;
#   3. checks Emscripten is where WASM_EMSDK_ROOT says (SetupWeb.bat installs it and sets that);
#   4. reads the .uproject and warns about the plugin entries a web build needs (it never edits your files);
#   5. packages: RunUAT BuildCookRun for Wasm, Shipping, into -Out (default <Project>\Saved\YouGameWeb). A build an
#      earlier run left in <Out>\Wasm is removed first, so none of its files end up in the new zip.
#      -Fast is for test builds: links much faster, runs a little slower;
#   6. makes sure <Out>\Wasm\yougame.json has {"yougame":{"crossOriginIsolation":true}} (a web build needs it on YouGame);
#   7. zips what is inside <Out>\Wasm, index.html at the top, to <Out>\<Project>-YouGame.zip (-NoZip skips this);
#   8. prints the zip, its size and the next step.
# It writes only inside -Out, plus what Unreal itself writes when it packages a project.
# Exit code: 0 done; 1 something is missing (named, with its fix) and nothing was packaged; when packaging itself
# fails, RunUAT's own exit code.
param([string]$Project = "", [Alias("EngineDir")][string]$Engine = "", [string]$Out = "", [switch]$Fast, [switch]$NoZip)
$ErrorActionPreference = "Stop"
$Version = "6.0.10"
$missing = 0
$notes = @()
# Ok, Bad and Note are SetupWeb.ps1's. Note also keeps its line: notes are shown again at the end, after Unreal's long output.
function Ok($m) { Write-Host "  ok   $m" -ForegroundColor Green }
function Bad($m, $fix) { Write-Host "  MISSING  $m" -ForegroundColor Red; if ($fix) { Write-Host "           fix: $fix" }; $script:missing++ }
function Note($m) { Write-Host "  note $m" -ForegroundColor Yellow; $script:notes += $m }
function Remind {
  if ($script:notes.Count -eq 0) { return }
  Write-Host "Notes from this run:"
  foreach ($n in $script:notes) { Write-Host "  note $n" -ForegroundColor Yellow }
}
# How one argument looks on a command line: in quotes when it has a space.
function Shown($a) { if ($a -match "\s") { return '"' + $a + '"' }; return $a }
Write-Host "YouGame: package an Unreal 5.9 web build$(if ($Fast) { ' (-Fast: a test build)' })"

# 1. The project. This file is <Project>\Plugins\YouGame\Extras\PackageWeb.ps1.
$uproject = $null
if (-not $Project) {
  $Project = $PSScriptRoot
  foreach ($i in 1..3) { if ($Project) { $Project = [IO.Path]::GetDirectoryName($Project) } }
}
if ($Project -and (Test-Path -LiteralPath $Project -PathType Container)) {
  $found = @(Get-ChildItem -LiteralPath $Project -Filter "*.uproject" -File)
  if ($found.Count -eq 1) { $uproject = $found[0].FullName }
  elseif ($found.Count -eq 0) { Bad "no .uproject in $Project" "put the plugin in <Project>\Plugins\YouGame, or run again with -Project <path to your .uproject>" }
  else { Bad "$($found.Count) .uproject files in $Project" "run again with -Project <path to the .uproject to package>" }
} elseif ($Project -and $Project -like "*.uproject" -and (Test-Path -LiteralPath $Project -PathType Leaf)) {
  $uproject = (Get-Item -LiteralPath $Project).FullName
} else {
  Bad "the project ($Project)" "run again with -Project <path to your .uproject>"
}
if ($uproject) {
  $projectDir = [IO.Path]::GetDirectoryName($uproject)
  $name = [IO.Path]::GetFileNameWithoutExtension($uproject)
  Ok "project $uproject"
  if (-not (Test-Path -LiteralPath (Join-Path $projectDir "Source"))) {
    Note "this project has no Source folder. The plugin needs a C++ project: in the editor, Tools > New C++ Class (any parent), then run this again"
  }
}

# 2. Unreal 5.9: the same lookup as SetupWeb.ps1 (its step 1). Change both together.
if (-not $Engine) {
  $dat = Join-Path $env:ProgramData "Epic\UnrealEngineLauncher\LauncherInstalled.dat"
  if (Test-Path $dat) {
    $list = (Get-Content $dat -Raw | ConvertFrom-Json).InstallationList
    $hit = $list | Where-Object { $_.AppName -match "^UE_5\.9" } | Select-Object -First 1
    if ($hit) { $Engine = $hit.InstallLocation }
  }
}
$uat = $null
if ($Engine -and (Test-Path (Join-Path $Engine "Engine\Build\BatchFiles\RunUAT.bat"))) {
  Ok "Unreal 5.9 at $Engine"
  $uat = (Get-Item -LiteralPath (Join-Path $Engine "Engine\Build\BatchFiles\RunUAT.bat")).FullName
  if (-not (Test-Path (Join-Path $Engine "Engine\Platforms\Wasm"))) {
    Bad "this engine has no web platform (Engine\Platforms\Wasm)" "Epic Games Launcher > Unreal Engine > Library > 5.9 > Options: tick the web (Wasm) platform, then Apply"
  }
} else {
  Bad "Unreal 5.9" "install it from the Epic Games Launcher, or run again with -Engine <the UE_5.9 folder>"
}

# 3. Emscripten, where WASM_EMSDK_ROOT says. SetupWeb.bat sets it for your Windows user; a window that was already
#    open then does not have it yet, so it is also read from the Windows settings and used for this run only.
$emsdk = $env:WASM_EMSDK_ROOT
if (-not $emsdk) { $emsdk = [Environment]::GetEnvironmentVariable("WASM_EMSDK_ROOT", "User") }
if (-not $emsdk) { $emsdk = [Environment]::GetEnvironmentVariable("WASM_EMSDK_ROOT", "Machine") }
$setupFix = "run SetupWeb.bat (beside this file) first, then restart the Unreal editor and run this again"
if (-not $emsdk) { Bad "WASM_EMSDK_ROOT is not set" $setupFix }
elseif (-not (Test-Path -LiteralPath (Join-Path $emsdk "upstream\emscripten"))) { Bad "Emscripten at WASM_EMSDK_ROOT = $emsdk (nothing is there)" $setupFix }
else {
  $env:WASM_EMSDK_ROOT = $emsdk
  $verFile = Join-Path $emsdk "upstream\emscripten\emscripten-version.txt"
  $have = if (Test-Path -LiteralPath $verFile) { (Get-Content -LiteralPath $verFile -Raw).Trim().Trim('"') } else { "" }
  if ($have -eq $Version) { Ok "Emscripten $Version at $emsdk (WASM_EMSDK_ROOT)" }
  else { Note "Emscripten at $emsdk is version '$have'; Unreal 5.9 needs $Version. If packaging stops on the Wasm SDK, run SetupWeb.bat" }
}

# 4. The .uproject's plugin entries. Only warnings: nothing is edited, and packaging still runs.
if ($uproject) {
  $plugins = $null
  $notesBefore = $notes.Count
  try { $plugins = @((Get-Content -LiteralPath $uproject -Raw | ConvertFrom-Json).Plugins) }
  catch { Note "could not read $uproject as JSON, so its plugin entries were not checked" }
  if ($null -ne $plugins) {
    foreach ($n in "OnlineSubsystemUtils", "OnlineSubsystemNull") {
      $p = $plugins | Where-Object { $_.Name -eq $n } | Select-Object -First 1
      if ($p -and $p.Enabled -and -not (@($p.PlatformDenyList) -contains "Wasm")) {
        Note ("$n is on for the web build, and it does not run in a browser. In the .uproject's Plugins list, make its entry:`n" +
          '         {"Name": "' + $n + '", "Enabled": true, "PlatformDenyList": ["Wasm"]}')
      }
    }
    $yg = $plugins | Where-Object { $_.Name -eq "YouGame" } | Select-Object -First 1
    if (-not $yg -or -not $yg.Enabled) {
      Note ("the .uproject's Plugins list " + $(if ($yg) { "turns the YouGame plugin off" } else { "has no YouGame entry" }) + ". It should have:`n" +
        '         {"Name": "YouGame", "Enabled": true}')
    }
    if ($notes.Count -eq $notesBefore) { Ok "the .uproject's plugin entries are right for the web" }
  }
}

# Where the build goes: <Out>\Wasm is the site, <Out>\<Project>-YouGame.zip the upload, <Out>\PackageWeb.log the log.
if ($uproject) {
  if (-not $Out) { $Out = Join-Path $projectDir "Saved\YouGameWeb" }
  $Out = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Out)
  if ($Out.Length -gt 3) { $Out = $Out.TrimEnd("\") }
  $stage = Join-Path $Out "Wasm"
  $zip = Join-Path $Out "$name-YouGame.zip"
  $log = Join-Path $Out "PackageWeb.log"
  $ygFile = Join-Path $stage "yougame.json"
  # Only a folder that is an earlier web build (it has index.html), or an empty one, is ever removed.
  if ((Test-Path -LiteralPath $stage) -and -not (Test-Path -LiteralPath (Join-Path $stage "index.html")) -and
      @(Get-ChildItem -LiteralPath $stage -Force).Count -gt 0) {
    Bad "$stage has files but no index.html, so it is not an earlier web build and is left alone" "run again with -Out <another folder>"
  }
}

Write-Host ""
if ($missing) { Write-Host "Not packaged: $missing thing(s) missing, listed above." -ForegroundColor Red; exit 1 }

# 5. Package. Unreal only adds and overwrites files in <Out>\Wasm, so an earlier build there would leave its old
#    files in the new zip: it is removed first. A yougame.json you kept there is put back afterwards.
$keptJson = $null
if (Test-Path -LiteralPath $stage) {
  if (Test-Path -LiteralPath $ygFile) { $keptJson = [IO.File]::ReadAllText($ygFile) }
  Remove-Item -LiteralPath $stage -Recurse -Force
  Write-Host "  ...  removed the earlier build in $stage"
}
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
[IO.Directory]::CreateDirectory($Out) | Out-Null
# One argument each, with no quotes of our own: PowerShell puts an argument that has a space in quotes itself.
$uatArgs = @("BuildCookRun", "-project=$uproject", "-platform=Wasm", "-clientconfig=Shipping",
  "-build", "-cook", "-stage", "-pak", "-iostore", "-package", "-archive", "-archivedirectory=$Out",
  "-unattended", "-utf8output")
# -Fast (test builds): the link runs at -O1 instead of Shipping's -O3, which skips most of its slowest step. The
# game is a little bigger and slower: package without -Fast for the build your players get.
if ($Fast) { $uatArgs += "-ubtargs=-WasmLinkOpt=-O1" }
Write-Host "  ...  packaging. A Third Person game takes about 35-40 minutes the first time. The log is $log"
Write-Host ("  ...  " + (Shown $uat) + " " + (($uatArgs | ForEach-Object { Shown $_ }) -join " "))
$started = Get-Date
$utf8 = New-Object System.Text.UTF8Encoding $false
# RunUAT prints UTF-8 (-utf8output): read it as that. Without a console window this cannot be set, and only
# letters outside English would look wrong.
try { [Console]::OutputEncoding = $utf8 } catch { }
$writer = New-Object System.IO.StreamWriter($log, $false, $utf8)
# RunUAT's error lines (2>&1) are shown and logged like the rest. "Continue" for this one call: under "Stop",
# Windows PowerShell would end the script at the first line a tool writes to its error output.
$ErrorActionPreference = "Continue"
try { & $uat @uatArgs 2>&1 | ForEach-Object { $line = "$_"; Write-Host $line; $writer.WriteLine($line) } }
finally { $writer.Close(); $ErrorActionPreference = "Stop" }
$code = $LASTEXITCODE
$minutes = [math]::Round(((Get-Date) - $started).TotalMinutes, 1)
if ($code) {
  Write-Host ""
  Write-Host "Packaging failed after $minutes minutes: RunUAT exit code $code. Its last 30 lines:" -ForegroundColor Red
  Get-Content -LiteralPath $log -Tail 30 -Encoding UTF8 | ForEach-Object { Write-Host "  $_" }
  Write-Host "The whole log: $log"
  Remind
  exit $code
}

# 6. The build, and its yougame.json beside index.html.
if (-not (Test-Path -LiteralPath (Join-Path $stage "index.html"))) {
  Write-Host ""
  Write-Host "RunUAT finished without an error, but there is no $stage\index.html. The log: $log" -ForegroundColor Red
  Remind
  exit 1
}
Write-Host ""
Ok "packaged in $minutes minutes: $stage"
$plain = '{"yougame":{"crossOriginIsolation":true}}'
if ($keptJson -and -not (Test-Path -LiteralPath $ygFile)) { [IO.File]::WriteAllText($ygFile, $keptJson, $utf8) }
if (-not (Test-Path -LiteralPath $ygFile)) {
  [IO.File]::WriteAllText($ygFile, $plain, $utf8)
  Ok "yougame.json written: $plain"
} else {
  $json = $null
  try { $json = [IO.File]::ReadAllText($ygFile) | ConvertFrom-Json } catch { $json = $null }
  if ($json -isnot [System.Management.Automation.PSCustomObject]) {
    $old = Join-Path $Out "yougame.json.old"
    Copy-Item -LiteralPath $ygFile -Destination $old -Force
    [IO.File]::WriteAllText($ygFile, $plain, $utf8)
    Note "the build's yougame.json was not a JSON object, so it was replaced with $plain (the old one is kept as $old)"
  } elseif ($json.yougame -is [System.Management.Automation.PSCustomObject] -and $json.yougame.crossOriginIsolation -is [bool] -and $json.yougame.crossOriginIsolation) {
    Ok "yougame.json already has crossOriginIsolation true: left as it is"
  } else {
    if ($json.yougame -isnot [System.Management.Automation.PSCustomObject]) {
      $json | Add-Member -NotePropertyName "yougame" -NotePropertyValue (New-Object PSObject) -Force
    }
    $json.yougame | Add-Member -NotePropertyName "crossOriginIsolation" -NotePropertyValue $true -Force
    [IO.File]::WriteAllText($ygFile, ($json | ConvertTo-Json -Depth 32), $utf8)
    Ok "yougame.json was already there: added crossOriginIsolation true under yougame and kept its other keys"
  }
}

# 7. The zip: what is inside <Out>\Wasm, never the folder itself, so index.html is at the top.
$stageFull = (Get-Item -LiteralPath $stage).FullName.TrimEnd("\")
$files = @(Get-ChildItem -LiteralPath $stageFull -Recurse -File -Force)
$count = 0
$left = 0
if (-not $NoZip) {
  Write-Host "  ...  zipping $($files.Count) files"
  Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
  $stream = [IO.File]::Open($zip, [IO.FileMode]::Create)
  try {
    # ZipArchive writes zip64 by itself when a file or the whole zip passes 4 GB, and copies each file as a stream.
    $archive = New-Object System.IO.Compression.ZipArchive($stream, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
      foreach ($f in $files) {
        # The name inside the zip: the path below <Out>\Wasm, with forward slashes.
        $entry = $f.FullName.Substring($stageFull.Length + 1).Replace("\", "/")
        # Debug files never go up (a Shipping build has none; Wasm-Debug is a folder beside Wasm, not inside it).
        if ($entry -match '(?i)(^|/)wasm-debug/|\.symbols$|\.pdb$') { $left++; continue }
        # Fastest: the big files (.wasm, .pak, .ucas) are compressed already.
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $f.FullName, $entry, [System.IO.Compression.CompressionLevel]::Fastest) | Out-Null
        $count++
      }
    } finally { $archive.Dispose() }
  } catch {
    # A zip that stopped half way still opens, with files missing: never leave one behind.
    $stream.Dispose()
    Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
    throw
  } finally { $stream.Dispose() }
  if ($left) { Note "$left debug file(s) (Wasm-Debug, .symbols, .pdb) were left out of the zip" }
}
# YouGame's limits for one build.
$big = @($files | Where-Object { $_.Length -gt 512MB })
if ($big.Count) { Note "$($big.Count) file(s) are over 512 MB, the most YouGame takes for one file: $(($big | ForEach-Object { $_.Name }) -join ', ')" }
$total = ($files | Measure-Object -Property Length -Sum).Sum
if ($total -gt 5GB) { Note "the build is $([math]::Round($total / 1GB, 1)) GB; YouGame takes up to 5 GB for one build" }

# 8. What was made, and what to do next.
Write-Host ""
if ($NoZip) {
  Write-Host "Done: $stageFull" -ForegroundColor Green
  Write-Host "  $($files.Count) files, $([math]::Round($total / 1MB, 1)) MB, packaged in $minutes minutes (no zip: -NoZip)"
  Write-Host "Next: have your coding agent run: node upload-build.mjs `"$stageFull`""
} else {
  Write-Host "Done: $zip" -ForegroundColor Green
  Write-Host "  $([math]::Round((Get-Item -LiteralPath $zip).Length / 1MB, 1)) MB, $count files, packaged in $minutes minutes"
  Write-Host "Next: upload it on the Web game tab at https://yougame.co/upload, or have your coding agent run: node upload-build.mjs `"$stageFull`""
}
Remind
exit 0
