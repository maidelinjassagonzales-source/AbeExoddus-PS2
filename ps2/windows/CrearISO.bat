@echo off
rem Abe's Exoddus PS2: builds the PS2 ISO from your Steam copy of the game. No extra software needed.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0CrearISO.ps1" %*
echo.
pause
