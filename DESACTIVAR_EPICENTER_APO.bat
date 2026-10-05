@echo off
setlocal
title Epicenter DSP - Restaurar prueba
set "EPICENTER_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "EPICENTER_PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
"%EPICENTER_PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\Invoke-ApoInstaller.ps1" -Mode Uninstall
if errorlevel 1 echo [ERROR] No se confirmo el exito. Revisa el mensaje anterior y conserva el respaldo.
pause
