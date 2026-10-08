#include <Arduino.h>
#include <WiFi.h>
#include <lwip/sockets.h>
#include <stdio.h>
#include <string.h>

/* Подключается к GURAWORK и отдаёт страницу по DHCP IP.
   GPIO6 мигает с частотой 4 Гц: полупериод 125 мс. */
static const char WIFI_SSID[] = "GURAWORK";
static const char WIFI_PASS[] = "GURA03071963";
static const int BLINK_PIN = 6;
static const unsigned long HALF_MS = 125;
static const unsigned long WIFI_WAIT_MS = 20000;

static char page[900];
static char reqLine[160];
static uint32_t clickCount;
static int blinkLevel;
static unsigned long nextBlinkMs;
static int wifiReady;
static int listenSock;

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

static int startServer(void)
{
	struct sockaddr_in addr;
	int yes;

	listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (listenSock < 0)
		return 0;

	yes = 1;
	setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(80);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(listenSock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(listenSock);
		listenSock = -1;
		return 0;
	}
	if (listen(listenSock, 2) < 0) {
		close(listenSock);
		listenSock = -1;
		return 0;
	}
	fcntl(listenSock, F_SETFL, O_NONBLOCK);
	return 1;
}

static void sendAll(int sock, const char *data, size_t len)
{
	int sent;

	while (len) {
		sent = send(sock, data, len, 0);
		if (sent <= 0)
			return;
		data += sent;
		len -= (size_t)sent;
	}
}

static void sendPage(int sock)
{
	IPAddress lip;
	char header[160];
	int hlen;
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

	hlen = snprintf(header, sizeof(header),
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html\r\n"
		"Connection: close\r\n"
		"Content-Length: %d\r\n\r\n", n);
	if (hlen > 0) {
		sendAll(sock, header, (size_t)hlen);
		sendAll(sock, page, (size_t)n);
	}
}

static void handleClient(void)
{
	struct sockaddr_in from;
	socklen_t fromLen;
	struct timeval timeout;
	int client;
	int n;
	int isClick;

	fromLen = sizeof(from);
	client = accept(listenSock, (struct sockaddr *)&from, &fromLen);
	if (client < 0)
		return;

	timeout.tv_sec = 2;
	timeout.tv_usec = 0;
	setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	n = recv(client, reqLine, sizeof(reqLine) - 1, 0);
	if (n < 0)
		n = 0;
	reqLine[n] = 0;
	Serial.print("REQ ");
	Serial.println(reqLine);

	isClick = (strncmp(reqLine, "GET /click", 10) == 0);
	if (isClick)
		clickCount++;

	sendPage(client);
	shutdown(client, SHUT_RDWR);
	close(client);
}

void setup(void)
{
	pinMode(BLINK_PIN, OUTPUT);
	digitalWrite(BLINK_PIN, LOW);
	blinkLevel = 0;
	nextBlinkMs = millis() + HALF_MS;
	wifiReady = 0;
	listenSock = -1;

	Serial.begin(115200);
	delay(200);
	Serial.println();
	Serial.println("ESP8684-MINI-1 STA web server, GPIO6 blink 4 Hz");

	wifiReady = connectWifi();
	if (!wifiReady)
		return;

	if (!startServer()) {
		Serial.println("Server start failed");
		return;
	}

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
		if (listenSock >= 0) {
			close(listenSock);
			listenSock = -1;
		}
		wifiReady = connectWifi();
		if (wifiReady)
			wifiReady = startServer();
		return;
	}
	handleClient();
}
