@echo off
rem Packages this project as an Unreal 5.9 browser game and zips it for YouGame (see PackageWeb.ps1). Add -Fast for a test build.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0PackageWeb.ps1" %*
set YG_EXIT=%ERRORLEVEL%
pause
exit /b %YG_EXIT%
