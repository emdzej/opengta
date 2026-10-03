@echo off
rem OpenGTA @VERSION@ on gasm-run @GASM_VERSION@. Double-click to play; OpenGTA.cmd --help for the options.
rem The work is done by opengta.ps1 (game data picker, saved location, gasm-run command line).
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0opengta.ps1" %*
set rc=%errorlevel%
rem Keep the window open on errors when started by double-click (OPENGTA_NO_PAUSE=1 skips this).
if not "%rc%"=="0" if not "%OPENGTA_NO_PAUSE%"=="1" pause
exit /b %rc%
