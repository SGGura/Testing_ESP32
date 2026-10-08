# Flash ESP8684-MINI-1 to COM131 using D:\DEV_ESP32 tools
param(
  [string]$Port = "COM131",
  [string]$DevRoot = "D:\DEV_ESP32"
)

$ErrorActionPreference = "Stop"
$Proj = Join-Path $PSScriptRoot "..\esp8684-mini-webserver" | Resolve-Path

$pioCandidates = @(
  "$DevRoot\platformio\penv\Scripts\pio.exe",
  "$DevRoot\.platformio\penv\Scripts\pio.exe",
  "$DevRoot\penv\Scripts\pio.exe"
)
$Pio = $pioCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $Pio) {
  $cmd = Get-Command pio -ErrorAction SilentlyContinue
  if ($cmd) { $Pio = $cmd.Source }
}
if (-not $Pio) { throw "pio not found under $DevRoot" }

Write-Host "Using: $Pio"
Write-Host "Port:  $Port"
Write-Host "Project: $Proj"
Set-Location $Proj

& $Pio run -t upload --upload-port $Port
if ($LASTEXITCODE -ne 0) {
  Write-Host "Flash failed. Hold BOOT, press RESET, release BOOT, then retry."
  exit $LASTEXITCODE
}

Write-Host ""
Write-Host "OK. Wi-Fi SSID=ESP8684-Web  password=esp8684mini"
Write-Host "Open http://192.168.4.1"
& $Pio device monitor --port $Port --baud 115200
