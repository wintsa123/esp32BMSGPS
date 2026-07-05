@echo off
setlocal

cd /d "%~dp0.."

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash.ps1"
set "FLASH_EXIT_CODE=%ERRORLEVEL%"

echo.
if "%FLASH_EXIT_CODE%"=="0" (
    echo Build and flash completed successfully.
) else (
    echo Build or flash failed with exit code %FLASH_EXIT_CODE%.
)
echo.
pause

exit /b %FLASH_EXIT_CODE%
