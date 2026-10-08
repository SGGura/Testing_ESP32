@echo off
setlocal EnableExtensions
REM Build ESP8684 SoftAP web server using tools from D:\DEV_ESP32
REM Project tree expected under D:\MCS_ESP32 (or this repo clone)

set "DEV_ROOT=D:\DEV_ESP32"
set "PROJ_ROOT=%~dp0.."
set "PROJ=%PROJ_ROOT%\esp8684-mini-webserver"

if exist "%DEV_ROOT%\platformio\penv\Scripts\pio.exe" (
  set "PIO=%DEV_ROOT%\platformio\penv\Scripts\pio.exe"
) else if exist "%DEV_ROOT%\.platformio\penv\Scripts\pio.exe" (
  set "PIO=%DEV_ROOT%\.platformio\penv\Scripts\pio.exe"
) else if exist "%DEV_ROOT%\penv\Scripts\pio.exe" (
  set "PIO=%DEV_ROOT%\penv\Scripts\pio.exe"
) else (
  where pio >nul 2>&1 && set "PIO=pio"
)

if not defined PIO (
  echo [ERR] pio not found. Put PlatformIO under %DEV_ROOT% or add pio to PATH.
  exit /b 1
)

echo Using: %PIO%
echo Project: %PROJ%
cd /d "%PROJ%" || exit /b 1
"%PIO%" run
exit /b %ERRORLEVEL%
