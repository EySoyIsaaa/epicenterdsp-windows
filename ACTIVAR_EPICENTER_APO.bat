@echo off
setlocal
title Epicenter DSP Professional 1.0 - Activador APO
cd /d "%~dp0"

set "SCRIPT_PATH=%~dp0scripts\Register-EpicenterApo.ps1"

:: Comprobar si se ejecuta con permisos de Administrador
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [INFO] Solicitando permisos de Administrador (UAC)...
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$p = '%SCRIPT_PATH%'; Start-Process powershell.exe -Verb RunAs -ArgumentList ('-NoProfile', '-ExecutionPolicy', 'Bypass', '-NoExit', '-File', ('\"' + $p + '\"'))"
    exit /b
)

:: Si ya fue iniciado como Administrador (ej. clic derecho "Ejecutar como administrador"):
powershell.exe -NoProfile -ExecutionPolicy Bypass -NoExit -File "%SCRIPT_PATH%"
pause
