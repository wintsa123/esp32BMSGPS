@echo off
setlocal

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0serial_tcp_bridge.ps1" %*
set "BRIDGE_EXIT_CODE=%ERRORLEVEL%"

echo.
if "%BRIDGE_EXIT_CODE%"=="0" (
    echo ESP RFC2217 serial bridge stopped.
) else (
    echo ESP RFC2217 serial bridge failed with exit code %BRIDGE_EXIT_CODE%.
)
echo.
pause

exit /b %BRIDGE_EXIT_CODE%
