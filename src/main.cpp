#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <stdio.h>

/* Подключается к домашней сети и отдаёт страницу по полученному IP.
   GPIO6 мигает с частотой 4 Гц: полупериод 125 мс. */
static const char WIFI_SSID[] = "GORAWORK";
static const char WIFI_PASS[] = "GURA03071963";
static const int BLINK_PIN = 6;
static const unsigned long HALF_MS = 125;
static const unsigned long WIFI_WAIT_MS = 20000;

static WebServer server(80);
static char page[1400];
static uint32_t clickCount;
static int blinkLevel;
static unsigned long nextBlinkMs;
static int wifiReady;

static void blinkTick(void)
{
	unsigned long now;

	now = millis();
	if ((long)(now - nextBlinkMs) < 0)
		return;
	nextBlinkMs = now + HALF_MS;
	blinkLevel = blinkLevel ? 0 : 1;
	digitalWrite(BLINK_PIN, blinkLevel);
}

static int connectWifi(void)
{
	unsigned long start;
	wl_status_t st;

	WiFi.mode(WIFI_STA);
	WiFi.begin(WIFI_SSID, WIFI_PASS);
	Serial.print("WiFi connect ");
	Serial.println(WIFI_SSID);

	start = millis();
	while ((long)(millis() - start) < (long)WIFI_WAIT_MS) {
		blinkTick();
		st = WiFi.status();
		if (st == WL_CONNECTED)
			return 1;
		Serial.print(".");
		delay(250);
	}
	Serial.println();
	Serial.print("WiFi failed, status=");
	Serial.println((int)WiFi.status());
	return 0;
}

static void handleRoot(void)
{
	String mac;
	String ip;
	unsigned long sec;
	int n;
	int rssi;

	mac = WiFi.macAddress();
	ip = WiFi.localIP().toString();
	sec = millis() / 1000UL;
	rssi = WiFi.RSSI();
	n = snprintf(page, sizeof(page),
		"<!DOCTYPE html><html><head><meta charset='utf-8'>"
		"<meta name='viewport' content='width=device-width,initial-scale=1'>"
		"<title>ESP8684</title></head><body>"
		"<h1>ESP8684-MINI-1</h1>"
		"<p>ESP32-C2, web-сервер в сети %s</p>"
		"<p>GPIO6 мигает с частотой 4 Гц, сейчас %s</p>"
		"<p>MAC: %s</p>"
		"<p>IP: %s</p>"
		"<p>RSSI: %d дБм</p>"
		"<p>Время работы: %lu с</p>"
		"<p>Свободная память: %u байт</p>"
		"<p>Нажатий: %lu</p>"
		"<form action='/click' method='get'>"
		"<button type='submit'>Нажать</button>"
		"</form></body></html>",
		WIFI_SSID,
		blinkLevel ? "ВКЛ" : "ВЫКЛ",
		mac.c_str(),
		ip.c_str(),
		rssi,
		sec,
		(unsigned)ESP.getFreeHeap(),
		(unsigned long)clickCount);
	if (n < 0 || n >= (int)sizeof(page)) {
		server.send(500, "text/plain", "page too long");
		return;
	}
	server.send(200, "text/html; charset=utf-8", page);
}

static void handleClick(void)
{
	clickCount++;
	server.sendHeader("Location", "/");
	server.send(303);
}

void setup(void)
{
	pinMode(BLINK_PIN, OUTPUT);
	digitalWrite(BLINK_PIN, LOW);
	blinkLevel = 0;
	nextBlinkMs = millis() + HALF_MS;
	wifiReady = 0;

	Serial.begin(115200);
	delay(200);
	Serial.println();
	Serial.println("ESP8684-MINI-1 STA web server, GPIO6 blink 4 Hz");

	wifiReady = connectWifi();
	if (!wifiReady)
		return;

	server.on("/", handleRoot);
	server.on("/click", handleClick);
	server.begin();

	Serial.print("SSID ");
	Serial.println(WIFI_SSID);
	Serial.print("IP ");
	Serial.println(WiFi.localIP());
}

void loop(void)
{
	blinkTick();
	if (wifiReady)
		server.handleClient();
}
