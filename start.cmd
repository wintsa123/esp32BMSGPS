@echo off
setlocal EnableExtensions
title ESP32 BMS GPS Firmware Configurator
chcp 65001 >nul
set "PS_EXE=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"

if not exist "%PS_EXE%" (
    echo PowerShell was not found at:
    echo %PS_EXE%
    echo Install or enable Windows PowerShell, then run start.cmd again.
    if "%~1"=="" pause
    exit /b 1
)

"%PS_EXE%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0start.ps1" %*
set "START_EXIT_CODE=%ERRORLEVEL%"
if not "%START_EXIT_CODE%"=="0" (
    echo.
    echo Failed to start the firmware configurator. PowerShell exit code: %START_EXIT_CODE%
    echo Read the PowerShell error above before retrying.
    if "%~1"=="" pause
)
exit /b %START_EXIT_CODE%
