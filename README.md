# Testing_ESP32 — ESP8684-MINI-1 SoftAP web server

Простой HTTP-сервер для модуля **ESP8684-MINI-1 (ESP32-C2)**.

| | |
|---|---|
| Wi‑Fi | SoftAP `ESP8684-Web` / пароль `esp8684mini` |
| URL | http://192.168.4.1 |
| JSON | http://192.168.4.1/api/status |
| Порт прошивки | `COM131` |
| Инструменты (Windows) | `D:\DEV_ESP32` |
| Проекты (Windows) | `D:\MCS_ESP32` |

## Структура

```
esp8684-mini-webserver/   # PlatformIO-проект
  platformio.ini
  src/main.cpp
scripts/
  build.bat               # сборка через pio из D:\DEV_ESP32
  flash.bat / flash.ps1   # прошивка на COM131
```

Скопируйте/склонируйте репозиторий в `D:\MCS_ESP32` (или держите checkout где удобно — скрипты относительно репо).

## Сборка и прошивка (Windows)

1. Убедитесь, что PlatformIO (`pio.exe`) лежит под `D:\DEV_ESP32` (типичные пути: `D:\DEV_ESP32\platformio\penv\Scripts\pio.exe` или `D:\DEV_ESP32\.platformio\penv\Scripts\pio.exe`).
2. Плата на **COM131**, USB-кабель data-capable.
3. Запуск:

```bat
scripts\flash.bat
```

или только сборка: `scripts\build.bat`.

Если upload падает — зажмите **BOOT**, нажмите **RESET**, отпустите **BOOT**, повторите.

## После прошивки

1. Подключитесь к Wi‑Fi `ESP8684-Web`.
2. Откройте http://192.168.4.1
