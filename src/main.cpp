#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <stdio.h>

/* Точка доступа. Страница: http://192.168.4.1
   GPIO6 мигает с частотой 4 Гц: полупериод 125 мс. */
static const char AP_SSID[] = "ESP8684-WEB";
static const char AP_PASS[] = "12345678";
static const int BLINK_PIN = 6;
static const unsigned long HALF_MS = 125;

static WebServer server(80);
static char page[1400];
static uint32_t clickCount;
static int blinkLevel;
static unsigned long nextBlinkMs;

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

static void handleRoot(void)
{
	String mac;
	String ip;
	unsigned long sec;
	int n;

	mac = WiFi.softAPmacAddress();
	ip = WiFi.softAPIP().toString();
	sec = millis() / 1000UL;
	n = snprintf(page, sizeof(page),
		"<!DOCTYPE html><html><head><meta charset='utf-8'>"
		"<meta name='viewport' content='width=device-width,initial-scale=1'>"
		"<title>ESP8684</title></head><body>"
		"<h1>ESP8684-MINI-1</h1>"
		"<p>ESP32-C2, web-сервер</p>"
		"<p>GPIO6 мигает с частотой 4 Гц, сейчас %s</p>"
		"<p>MAC: %s</p>"
		"<p>IP: %s</p>"
		"<p>Время работы: %lu с</p>"
		"<p>Свободная память: %u байт</p>"
		"<p>Нажатий: %lu</p>"
		"<form action='/click' method='get'>"
		"<button type='submit'>Нажать</button>"
		"</form></body></html>",
		blinkLevel ? "ВКЛ" : "ВЫКЛ",
		mac.c_str(),
		ip.c_str(),
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
	bool apOk;

	pinMode(BLINK_PIN, OUTPUT);
	digitalWrite(BLINK_PIN, LOW);
	blinkLevel = 0;
	nextBlinkMs = millis() + HALF_MS;

	Serial.begin(115200);
	delay(200);
	Serial.println();
	Serial.println("ESP8684-MINI-1 web server, GPIO6 blink 4 Hz");

	WiFi.mode(WIFI_AP);
	apOk = WiFi.softAP(AP_SSID, AP_PASS);
	if (!apOk) {
		Serial.println("softAP failed");
		return;
	}

	server.on("/", handleRoot);
	server.on("/click", handleClick);
	server.begin();

	Serial.print("SSID ");
	Serial.println(AP_SSID);
	Serial.print("PASS ");
	Serial.println(AP_PASS);
	Serial.print("IP ");
	Serial.println(WiFi.softAPIP());
}

void loop(void)
{
	blinkTick();
	server.handleClient();
}
