@echo off
setlocal
title EpicenterDSP Windows - Compilar y verificar
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Build-EpicenterWindows.ps1"
set "EPICENTER_BUILD_RESULT=%errorlevel%"
echo.
if not "%EPICENTER_BUILD_RESULT%"=="0" echo [ERROR] La compilacion no se completo. Conserva el mensaje anterior.
pause
exit /b %EPICENTER_BUILD_RESULT%
