@echo off
setlocal
title ESP32 BMS GPS Firmware Configurator
chcp 65001 >nul
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0start.ps1" %*
exit /b %errorlevel%
