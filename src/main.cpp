#include <Arduino.h>
#include <WiFi.h>
#include <NetworkClient.h>
#include <NetworkServer.h>
#include <stdio.h>
#include <string.h>

/* Подключается к GURAWORK и отдаёт страницу по DHCP IP.
   GPIO6 мигает с частотой 4 Гц: полупериод 125 мс. */
static const char WIFI_SSID[] = "GURAWORK";
static const char WIFI_PASS[] = "GURA03071963";
static const int BLINK_PIN = 6;
static const unsigned long HALF_MS = 125;
static const unsigned long WIFI_WAIT_MS = 20000;

static NetworkServer server(80);
static char page[900];
static char reqLine[160];
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
	WiFi.setSleep(false);
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

static int readRequestLine(NetworkClient &client)
{
	size_t n;
	int c;
	unsigned long start;

	n = 0;
	start = millis();
	while (client.connected() && (long)(millis() - start) < 2000) {
		if (!client.available()) {
			delay(1);
			continue;
		}
		c = client.read();
		if (c < 0)
			continue;
		if (c == '\n')
			break;
		if (c == '\r')
			continue;
		if (n + 1 < sizeof(reqLine)) {
			reqLine[n] = (char)c;
			n++;
		}
	}
	reqLine[n] = 0;

	/* дочитать и отбросить остальные заголовки */
	while (client.connected() && client.available()) {
		c = client.read();
		if (c < 0)
			break;
	}
	return (int)n;
}

static void sendPage(NetworkClient &client)
{
	IPAddress lip;
	int n;
	int rssi;

	lip = WiFi.localIP();
	rssi = WiFi.RSSI();
	n = snprintf(page, sizeof(page),
		"<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
		"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
		"<title>ESP8684</title></head><body>"
		"<h1>ESP8684-MINI-1</h1>"
		"<p>SSID: %s</p>"
		"<p>IP: %u.%u.%u.%u</p>"
		"<p>RSSI: %d dBm</p>"
		"<p>GPIO6: 4 Hz, now %s</p>"
		"<p>Uptime: %lu s</p>"
		"<p>Free heap: %u</p>"
		"<p>Clicks: %lu</p>"
		"<p><a href=\"/click\">Click</a></p>"
		"</body></html>",
		WIFI_SSID,
		(unsigned)lip[0], (unsigned)lip[1], (unsigned)lip[2], (unsigned)lip[3],
		rssi,
		blinkLevel ? "ON" : "OFF",
		millis() / 1000UL,
		(unsigned)ESP.getFreeHeap(),
		(unsigned long)clickCount);
	if (n < 0)
		n = 0;
	if (n >= (int)sizeof(page))
		n = (int)sizeof(page) - 1;

	client.print("HTTP/1.1 200 OK\r\n");
	client.print("Content-Type: text/html\r\n");
	client.print("Connection: close\r\n");
	client.print("Content-Length: ");
	client.println(n);
	client.print("\r\n");
	client.write((const uint8_t *)page, (size_t)n);
}

static void handleClient(void)
{
	NetworkClient client;
	int isClick;

	client = server.accept();
	if (!client)
		return;

	readRequestLine(client);
	Serial.print("REQ ");
	Serial.println(reqLine);

	isClick = (strncmp(reqLine, "GET /click", 10) == 0);
	if (isClick)
		clickCount++;

	sendPage(client);
	client.stop();
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

	server.begin();

	Serial.print("SSID ");
	Serial.println(WIFI_SSID);
	Serial.print("PASS ok len=");
	Serial.println((unsigned)strlen(WIFI_PASS));
	Serial.print("IP ");
	Serial.println(WiFi.localIP());
}

void loop(void)
{
	blinkTick();
	if (!wifiReady)
		return;
	if (WiFi.status() != WL_CONNECTED) {
		Serial.println("WiFi lost, reconnect");
		wifiReady = connectWifi();
		if (wifiReady)
			server.begin();
		return;
	}
	handleClient();
}
