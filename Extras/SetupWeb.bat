@echo off
rem Gets this PC ready to package an Unreal 5.9 browser game (see SetupWeb.ps1). Add -Check to only report.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0SetupWeb.ps1" %*
pause
