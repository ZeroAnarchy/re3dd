@echo off
rem Removes Resident Evil 3DD Mod from this game folder (run from <game>\3DD-Mod).
cd /d "%~dp0.." & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Uninstall-3DD.ps1" %* & exit /b
