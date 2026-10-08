@echo off
setlocal EnableExtensions
REM Flash ESP8684-MINI-1 (ESP32-C2) SoftAP web server to COM131
REM Tools: D:\DEV_ESP32   Projects: D:\MCS_ESP32

set "DEV_ROOT=D:\DEV_ESP32"
set "PORT=COM131"
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
  echo [ERR] pio not found under %DEV_ROOT% and not in PATH.
  exit /b 1
)

echo Using: %PIO%
echo Port:  %PORT%
echo Project: %PROJ%
cd /d "%PROJ%" || exit /b 1

"%PIO%" run -t upload --upload-port %PORT%
set "RC=%ERRORLEVEL%"
if %RC% neq 0 (
  echo.
  echo Flash failed. Hold BOOT, press RESET, release BOOT, then retry.
  exit /b %RC%
)

echo.
echo OK. Connect Wi-Fi SSID=ESP8684-Web  password=esp8684mini
echo Then open http://192.168.4.1
"%PIO%" device monitor --port %PORT% --baud 115200
exit /b 0
