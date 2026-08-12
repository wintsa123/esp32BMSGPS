@echo off
setlocal
cd /d "%~dp0.."

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass ^
  -File "%~dp0run-lvgl-simulator.ps1" -HostBle %*

set "SIMULATOR_EXIT=%ERRORLEVEL%"
if not "%SIMULATOR_EXIT%"=="0" (
  echo.
  echo Simulator startup failed. See the error above.
  pause
)
exit /b %SIMULATOR_EXIT%
