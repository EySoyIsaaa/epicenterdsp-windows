@echo off
setlocal
title Epicenter DSP Professional 1.0 - Compilar Todo
cd /d "%~dp0"

echo =================================================================
echo    EPICENTER DSP PROFESSIONAL 1.0 - COMPILACION COMPLETA
echo =================================================================
echo.

:: 1. Compilar Epicenter Core C++
echo [1/3] Compilando EpicenterDSP Core (C++17)...
cmake -B "epicenter-core\build" -S "epicenter-core" -DCMAKE_BUILD_TYPE=Release
if %errorlevel% neq 0 (
    echo [ERROR] Fallo la configuracion de CMake para epicenter-core.
    pause
    exit /b %errorlevel%
)
cmake --build "epicenter-core\build" --config Release
if %errorlevel% neq 0 (
    echo [ERROR] Fallo la compilacion de epicenter-core.
    pause
    exit /b %errorlevel%
)

:: 2. Compilar Epicenter Windows APO
echo.
echo [2/3] Compilando Epicenter Windows APO (C++ COM)...
cmake -B "epicenter-apo\build" -S "epicenter-apo" -DCMAKE_BUILD_TYPE=Release
if %errorlevel% neq 0 (
    echo [ERROR] Fallo la configuracion de CMake para epicenter-apo.
    pause
    exit /b %errorlevel%
)
cmake --build "epicenter-apo\build" --config Release
if %errorlevel% neq 0 (
    echo [ERROR] Fallo la compilacion de epicenter-apo.
    pause
    exit /b %errorlevel%
)

:: 3. Compilar Epicenter UI (WinUI 3 .NET 9 LTS)
echo.
echo [3/3] Compilando Interfaz WinUI 3 (.NET 9 LTS)...
dotnet build "epicenter-ui\EpicenterUI.sln" -c Release
if %errorlevel% neq 0 (
    echo [ERROR] Fallo la compilacion de la interfaz WinUI 3.
    pause
    exit /b %errorlevel%
)

echo.
echo =================================================================
echo   COMPILACION FINALIZADA CON EXITO (Core + APO + WinUI 3 UI)
echo =================================================================
echo Puedes ejecutar ACTIVAR_EPICENTER_APO.bat para activar el sistema.
echo.
pause
