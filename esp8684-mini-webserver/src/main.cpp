#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// SoftAP — no external Wi‑Fi credentials needed
static const char *AP_SSID = "ESP8684-Web";
static const char *AP_PASS = "esp8684mini";  // min 8 chars

static WebServer server(80);
static uint32_t bootMs = 0;

static String htmlPage() {
  const uint32_t uptimeSec = (millis() - bootMs) / 1000UL;
  String html;
  html.reserve(1200);
  html += F("<!DOCTYPE html><html lang=\"ru\"><head><meta charset=\"utf-8\">");
  html += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  html += F("<title>ESP8684-MINI-1</title><style>");
  html += F("body{font-family:system-ui,sans-serif;margin:2rem;background:#0f172a;color:#e2e8f0}");
  html += F("h1{color:#38bdf8} .card{background:#1e293b;padding:1.25rem;border-radius:12px;max-width:28rem}");
  html += F("code{background:#334155;padding:.15rem .4rem;border-radius:4px}");
  html += F("a{color:#7dd3fc}</style></head><body><div class=\"card\">");
  html += F("<h1>ESP8684-MINI-1</h1>");
  html += F("<p>Простой web server на <b>ESP32-C2</b>.</p>");
  html += F("<p>Uptime: <code>");
  html += String(uptimeSec);
  html += F(" s</code></p>");
  html += F("<p>IP SoftAP: <code>");
  html += WiFi.softAPIP().toString();
  html += F("</code></p>");
  html += F("<p>Free heap: <code>");
  html += String(ESP.getFreeHeap());
  html += F("</code> bytes</p>");
  html += F("<p><a href=\"/api/status\">/api/status</a> (JSON)</p>");
  html += F("</div></body></html>");
  return html;
}

static void handleRoot() {
  server.send(200, "text/html; charset=utf-8", htmlPage());
}

static void handleStatus() {
  String json = "{";
  json += "\"board\":\"ESP8684-MINI-1\",";
  json += "\"chip\":\"ESP32-C2\",";
  json += "\"ssid\":\"" + String(AP_SSID) + "\",";
  json += "\"ip\":\"" + WiFi.softAPIP().toString() + "\",";
  json += "\"uptime_s\":" + String((millis() - bootMs) / 1000UL) + ",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap());
  json += "}";
  server.send(200, "application/json", json);
}

static void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  bootMs = millis();

  Serial.println();
  Serial.println(F("ESP8684-MINI-1 SoftAP web server"));

  WiFi.mode(WIFI_AP);
  const bool ok = WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("SoftAP %s  SSID=%s  pass=%s  IP=%s\n",
                ok ? "OK" : "FAIL", AP_SSID, AP_PASS,
                WiFi.softAPIP().toString().c_str());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println(F("HTTP server on port 80 — open http://192.168.4.1"));
}

void loop() {
  server.handleClient();
}
