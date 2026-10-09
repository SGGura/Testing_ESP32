#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#define WIFI_SSID "GURAWORK"
#define WIFI_PASS "GURA03071963"
#define WIFI_MAX_RETRY 20
#define BLINK_GPIO GPIO_NUM_6
#define BLINK_HALF_MS 125
#define WEATHER_PERIOD_MS 60000
#define WEATHER_HTTP_BUF 4096
#define WEATHER_FORECAST_DAYS 3
#define PAGE_BUF_SIZE 8192
#define FORECAST_BUF_SIZE 2048
#define ICON_CDN \
	"https://cdn.jsdelivr.net/gh/basmilius/weather-icons@3.0.0/production/fill/svg/"

#define WEATHER_URL \
	"https://api.open-meteo.com/v1/forecast" \
	"?latitude=48.45&longitude=35.04" \
	"&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m" \
	"&daily=weather_code,temperature_2m_max,temperature_2m_min" \
	"&timezone=Europe%2FKyiv&forecast_days=3"

static const char *TAG = "esp8684";
static EventGroupHandle_t wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT = BIT1;
static int wifi_retry;
static volatile int blink_level;
static volatile uint32_t click_count;
static httpd_handle_t http_server;
static SemaphoreHandle_t weather_mutex;
static SemaphoreHandle_t weather_kick;

typedef struct {
	bool valid;
	int temp_x10;
	int humidity;
	int wind_x10;
	int weather_code;
	char weather_text[40];
	char day_date[WEATHER_FORECAST_DAYS][12];
	int day_tmax_x10[WEATHER_FORECAST_DAYS];
	int day_tmin_x10[WEATHER_FORECAST_DAYS];
	int day_code[WEATHER_FORECAST_DAYS];
	char day_text[WEATHER_FORECAST_DAYS][40];
	char updated[24];
	char status[48];
} weather_info_t;

static weather_info_t weather_info = {
	.valid = false,
	.status = "ожидание...",
};

static int float_to_x10(double v)
{
	if (v >= 0.0)
		return (int)(v * 10.0 + 0.5);
	return (int)(v * 10.0 - 0.5);
}

static void weather_code_text(int code, char *out, size_t out_len)
{
	const char *text;

	if (code == 0)
		text = "ясно";
	else if (code <= 3)
		text = "переменная облачность";
	else if (code == 45 || code == 48)
		text = "туман";
	else if (code <= 57)
		text = "морось";
	else if (code <= 67)
		text = "дождь";
	else if (code <= 77)
		text = "снег";
	else if (code <= 82)
		text = "ливень";
	else if (code <= 86)
		text = "снегопад";
	else if (code >= 95)
		text = "гроза";
	else
		text = "нет данных";

	strncpy(out, text, out_len - 1);
	out[out_len - 1] = '\0';
}

static const char *weather_icon_slug(int code)
{
	if (code == 0)
		return "clear-day";
	if (code == 1)
		return "partly-cloudy-day";
	if (code == 2)
		return "partly-cloudy-day";
	if (code == 3)
		return "overcast";
	if (code == 45 || code == 48)
		return "fog";
	if (code >= 51 && code <= 55)
		return "drizzle";
	if (code == 56 || code == 57)
		return "sleet";
	if (code >= 61 && code <= 65)
		return "rain";
	if (code == 66 || code == 67)
		return "sleet";
	if (code >= 71 && code <= 77)
		return "snow";
	if (code >= 80 && code <= 82)
		return "rain";
	if (code >= 85 && code <= 86)
		return "snow";
	if (code == 95)
		return "thunderstorms";
	if (code >= 96)
		return "thunderstorms-rain";
	return "not-available";
}

static void format_temp_x10(int x10, char *out, size_t out_len)
{
	int whole;
	int frac;

	whole = x10 / 10;
	frac = abs(x10 % 10);
	snprintf(out, out_len, "%d.%d", whole, frac);
}

static void blink_task(void *arg)
{
	(void)arg;
	gpio_reset_pin(BLINK_GPIO);
	gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
	gpio_set_level(BLINK_GPIO, 0);
	blink_level = 0;

	while (1) {
		blink_level = blink_level ? 0 : 1;
		gpio_set_level(BLINK_GPIO, blink_level);
		vTaskDelay(pdMS_TO_TICKS(BLINK_HALF_MS));
	}
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
			       int32_t event_id, void *event_data)
{
	(void)arg;
	(void)event_data;

	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		if (wifi_retry < WIFI_MAX_RETRY) {
			esp_wifi_connect();
			wifi_retry++;
			ESP_LOGW(TAG, "retry WiFi connect (%d)", wifi_retry);
		} else {
			xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
		}
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
		ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
		wifi_retry = 0;
		xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
	}
}

static void wifi_init_sta(void)
{
	EventBits_t bits;
	wifi_config_t wifi_config = { 0 };
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

	wifi_event_group = xEventGroupCreate();
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	ESP_ERROR_CHECK(esp_event_handler_instance_register(
		WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(
		IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

	strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
	strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));
	wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
	ESP_ERROR_CHECK(esp_wifi_start());

	bits = xEventGroupWaitBits(wifi_event_group,
				   WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
				   pdFALSE, pdFALSE, portMAX_DELAY);
	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG, "connected to %s", WIFI_SSID);
	} else {
		ESP_LOGE(TAG, "failed to connect to %s", WIFI_SSID);
		abort();
	}
}

typedef struct {
	char *buf;
	int len;
	int cap;
} http_accum_t;

static esp_err_t weather_http_event(esp_http_client_event_t *evt)
{
	http_accum_t *acc;
	int n;

	acc = (http_accum_t *)evt->user_data;
	if (evt->event_id != HTTP_EVENT_ON_DATA)
		return ESP_OK;
	if (!acc || !acc->buf || evt->data_len <= 0)
		return ESP_OK;

	n = evt->data_len;
	if (acc->len + n >= acc->cap)
		n = acc->cap - acc->len - 1;
	if (n <= 0)
		return ESP_OK;

	memcpy(acc->buf + acc->len, evt->data, n);
	acc->len += n;
	acc->buf[acc->len] = '\0';
	return ESP_OK;
}

static void weather_set_status(const char *status)
{
	xSemaphoreTake(weather_mutex, portMAX_DELAY);
	strncpy(weather_info.status, status, sizeof(weather_info.status) - 1);
	weather_info.status[sizeof(weather_info.status) - 1] = '\0';
	xSemaphoreGive(weather_mutex);
}

static bool weather_parse_json(const char *json)
{
	cJSON *root;
	cJSON *current;
	cJSON *daily;
	cJSON *item;
	cJSON *dates;
	cJSON *tmax;
	cJSON *tmin;
	cJSON *codes;
	weather_info_t next;
	int i;
	int days;
	bool ok;

	memset(&next, 0, sizeof(next));
	strncpy(next.status, "ошибка разбора", sizeof(next.status) - 1);

	root = cJSON_Parse(json);
	if (!root)
		return false;

	ok = false;
	current = cJSON_GetObjectItem(root, "current");
	if (cJSON_IsObject(current)) {
		item = cJSON_GetObjectItem(current, "temperature_2m");
		if (cJSON_IsNumber(item))
			next.temp_x10 = float_to_x10(item->valuedouble);

		item = cJSON_GetObjectItem(current, "relative_humidity_2m");
		if (cJSON_IsNumber(item))
			next.humidity = (int)item->valuedouble;

		item = cJSON_GetObjectItem(current, "wind_speed_10m");
		if (cJSON_IsNumber(item))
			next.wind_x10 = float_to_x10(item->valuedouble);

		item = cJSON_GetObjectItem(current, "weather_code");
		if (cJSON_IsNumber(item))
			next.weather_code = (int)item->valuedouble;

		item = cJSON_GetObjectItem(current, "time");
		if (cJSON_IsString(item) && item->valuestring) {
			strncpy(next.updated, item->valuestring, sizeof(next.updated) - 1);
			next.updated[sizeof(next.updated) - 1] = '\0';
		}

		weather_code_text(next.weather_code, next.weather_text, sizeof(next.weather_text));
		ok = true;
	}

	daily = cJSON_GetObjectItem(root, "daily");
	if (ok && cJSON_IsObject(daily)) {
		dates = cJSON_GetObjectItem(daily, "time");
		tmax = cJSON_GetObjectItem(daily, "temperature_2m_max");
		tmin = cJSON_GetObjectItem(daily, "temperature_2m_min");
		codes = cJSON_GetObjectItem(daily, "weather_code");
		days = 0;
		if (cJSON_IsArray(dates))
			days = cJSON_GetArraySize(dates);
		if (days > WEATHER_FORECAST_DAYS)
			days = WEATHER_FORECAST_DAYS;

		for (i = 0; i < days; i++) {
			item = cJSON_GetArrayItem(dates, i);
			if (cJSON_IsString(item) && item->valuestring) {
				strncpy(next.day_date[i], item->valuestring,
					sizeof(next.day_date[i]) - 1);
				next.day_date[i][sizeof(next.day_date[i]) - 1] = '\0';
			}

			item = cJSON_GetArrayItem(tmax, i);
			if (cJSON_IsNumber(item))
				next.day_tmax_x10[i] = float_to_x10(item->valuedouble);

			item = cJSON_GetArrayItem(tmin, i);
			if (cJSON_IsNumber(item))
				next.day_tmin_x10[i] = float_to_x10(item->valuedouble);

			item = cJSON_GetArrayItem(codes, i);
			if (cJSON_IsNumber(item)) {
				next.day_code[i] = (int)item->valuedouble;
				weather_code_text(next.day_code[i], next.day_text[i],
						  sizeof(next.day_text[i]));
			}
		}
	}

	if (ok) {
		next.valid = true;
		strncpy(next.status, "ok", sizeof(next.status) - 1);
	}

	cJSON_Delete(root);

	xSemaphoreTake(weather_mutex, portMAX_DELAY);
	weather_info = next;
	xSemaphoreGive(weather_mutex);
	return ok;
}

static void weather_fetch_once(void)
{
	esp_http_client_config_t cfg;
	esp_http_client_handle_t client;
	http_accum_t acc;
	esp_err_t err;
	int status;
	char *body;

	body = malloc(WEATHER_HTTP_BUF);
	if (!body) {
		weather_set_status("нет памяти");
		ESP_LOGE(TAG, "weather malloc failed");
		return;
	}

	memset(body, 0, WEATHER_HTTP_BUF);
	acc.buf = body;
	acc.len = 0;
	acc.cap = WEATHER_HTTP_BUF;

	memset(&cfg, 0, sizeof(cfg));
	cfg.url = WEATHER_URL;
	cfg.timeout_ms = 15000;
	cfg.event_handler = weather_http_event;
	cfg.user_data = &acc;
	cfg.crt_bundle_attach = esp_crt_bundle_attach;
	cfg.buffer_size = 1024;
	cfg.buffer_size_tx = 1024;

	weather_set_status("обновление...");
	ESP_LOGI(TAG, "weather fetch start");

	client = esp_http_client_init(&cfg);
	if (!client) {
		weather_set_status("http init fail");
		free(body);
		return;
	}

	err = esp_http_client_perform(client);
	status = esp_http_client_get_status_code(client);
	esp_http_client_cleanup(client);

	if (err != ESP_OK) {
		weather_set_status("сеть/TLS ошибка");
		ESP_LOGE(TAG, "weather http err=%s", esp_err_to_name(err));
		free(body);
		return;
	}
	if (status != 200) {
		weather_set_status("HTTP не 200");
		ESP_LOGE(TAG, "weather http status=%d", status);
		free(body);
		return;
	}
	if (acc.len <= 0) {
		weather_set_status("пустой ответ");
		free(body);
		return;
	}

	if (weather_parse_json(body))
		ESP_LOGI(TAG, "weather ok temp=%d.%d",
			 weather_info.temp_x10 / 10, abs(weather_info.temp_x10 % 10));
	else
		ESP_LOGE(TAG, "weather json parse failed");

	free(body);
}

static void weather_task(void *arg)
{
	(void)arg;

	weather_fetch_once();
	while (1) {
		xSemaphoreTake(weather_kick, pdMS_TO_TICKS(WEATHER_PERIOD_MS));
		weather_fetch_once();
	}
}

static void weather_copy(weather_info_t *out)
{
	xSemaphoreTake(weather_mutex, portMAX_DELAY);
	*out = weather_info;
	xSemaphoreGive(weather_mutex);
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
	char *page;
	char *forecast;
	esp_netif_ip_info_t ip_info;
	esp_netif_t *netif;
	wifi_ap_record_t ap;
	weather_info_t w;
	char temp_s[16];
	char wind_s[16];
	char tmax_s[16];
	char tmin_s[16];
	const char *icon;
	int rssi;
	int n;
	int i;
	int used;

	page = malloc(PAGE_BUF_SIZE);
	forecast = malloc(FORECAST_BUF_SIZE);
	if (!page || !forecast) {
		free(page);
		free(forecast);
		return httpd_resp_send_500(req);
	}

	netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
	memset(&ip_info, 0, sizeof(ip_info));
	if (netif)
		esp_netif_get_ip_info(netif, &ip_info);

	rssi = 0;
	if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
		rssi = ap.rssi;

	weather_copy(&w);
	format_temp_x10(w.temp_x10, temp_s, sizeof(temp_s));
	format_temp_x10(w.wind_x10, wind_s, sizeof(wind_s));
	icon = weather_icon_slug(w.valid ? w.weather_code : -1);

	used = 0;
	forecast[0] = '\0';
	if (w.valid) {
		for (i = 0; i < WEATHER_FORECAST_DAYS; i++) {
			if (w.day_date[i][0] == '\0')
				continue;
			format_temp_x10(w.day_tmax_x10[i], tmax_s, sizeof(tmax_s));
			format_temp_x10(w.day_tmin_x10[i], tmin_s, sizeof(tmin_s));
			n = snprintf(forecast + used, FORECAST_BUF_SIZE - used,
				"<article class=\"day\">"
				"<img class=\"ico\" src=\"%s%s.svg\" alt=\"\" width=\"72\" height=\"72\">"
				"<div class=\"day-date\">%s</div>"
				"<div class=\"day-temp\">%s…%s°</div>"
				"<div class=\"day-desc\">%s</div>"
				"</article>",
				ICON_CDN, weather_icon_slug(w.day_code[i]),
				w.day_date[i], tmin_s, tmax_s, w.day_text[i]);
			if (n < 0 || used + n >= FORECAST_BUF_SIZE)
				break;
			used += n;
		}
	} else {
		snprintf(forecast, FORECAST_BUF_SIZE,
			 "<article class=\"day\"><div class=\"day-desc\">нет данных</div></article>");
	}

	n = snprintf(page, PAGE_BUF_SIZE,
		"<!DOCTYPE html><html lang=\"ru\"><head><meta charset=\"utf-8\">"
		"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
		"<meta http-equiv=\"refresh\" content=\"60\">"
		"<title>Погода — Днепр</title>"
		"<link rel=\"preconnect\" href=\"https://fonts.googleapis.com\">"
		"<link rel=\"preconnect\" href=\"https://fonts.gstatic.com\" crossorigin>"
		"<link href=\"https://fonts.googleapis.com/css2?family=Outfit:wght@400;600;700&display=swap\" rel=\"stylesheet\">"
		"<style>"
		":root{--ink:#16324f;--muted:#3d5a73;--glass:rgba(255,255,255,.34);--line:rgba(255,255,255,.55)}"
		"*{box-sizing:border-box}"
		"body{margin:0;min-height:100vh;font-family:Outfit,Segoe UI,sans-serif;color:var(--ink);"
		"background:"
		"radial-gradient(900px 520px at 12%% -8%%,rgba(255,214,140,.62),transparent 58%%),"
		"radial-gradient(780px 460px at 92%% 8%%,rgba(110,190,255,.5),transparent 55%%),"
		"radial-gradient(640px 380px at 50%% 100%%,rgba(180,220,255,.35),transparent 60%%),"
		"linear-gradient(165deg,#6eb0e4 0%%,#9ec8ea 36%%,#d7e7f4 68%%,#eef4f8 100%%);"
		"background-attachment:fixed}"
		"body::before,body::after{content:\"\";position:fixed;border-radius:50%%;pointer-events:none;"
		"background:rgba(255,255,255,.18);filter:blur(2px);animation:drift 26s linear infinite}"
		"body::before{width:220px;height:90px;top:8%%;left:-40px;box-shadow:70px 20px 0 10px rgba(255,255,255,.12)}"
		"body::after{width:180px;height:70px;top:18%%;right:-30px;animation-duration:34s;animation-direction:reverse;"
		"box-shadow:-50px 16px 0 8px rgba(255,255,255,.1)}"
		"@keyframes drift{from{transform:translateX(0)}to{transform:translateX(-90px)}}"
		"@keyframes float{0%%,100%%{transform:translateY(0)}50%%{transform:translateY(-8px)}}"
		"main{max-width:720px;margin:0 auto;padding:28px 18px 40px}"
		".brand{font-size:.85rem;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);margin:0 0 8px}"
		"h1{font-size:clamp(1.8rem,5vw,2.6rem);margin:0 0 4px;font-weight:700}"
		".sub{margin:0 0 22px;color:var(--muted)}"
		".now{display:grid;grid-template-columns:auto 1fr;gap:8px 18px;align-items:center;"
		"padding:18px 20px;border:1px solid var(--line);background:var(--glass);"
		"backdrop-filter:blur(10px);border-radius:28px}"
		".now img{width:120px;height:120px;animation:float 5s ease-in-out infinite}"
		".temp{font-size:clamp(2.8rem,9vw,4rem);font-weight:700;line-height:1;margin:0}"
		".desc{font-size:1.15rem;margin:6px 0 0;text-transform:capitalize}"
		".meta{grid-column:1/-1;display:flex;flex-wrap:wrap;gap:10px 18px;margin:8px 0 0;color:var(--muted);font-size:.95rem}"
		".actions{margin:16px 0 28px;display:flex;gap:12px;flex-wrap:wrap}"
		"a.btn,button.btn{display:inline-block;padding:10px 16px;border-radius:999px;text-decoration:none;"
		"border:0;cursor:pointer;background:#16324f;color:#f4f8fc;font:inherit;font-weight:600}"
		"a.link{color:var(--ink);align-self:center}"
		"h2{font-size:1.15rem;margin:0 0 12px}"
		".days{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px}"
		".day{padding:14px 10px 16px;text-align:center;border:1px solid var(--line);"
		"background:rgba(255,255,255,.28);backdrop-filter:blur(8px);border-radius:22px}"
		".day .ico{width:72px;height:72px;margin:0 auto 4px;display:block}"
		".day-date{font-size:.82rem;color:var(--muted)}"
		".day-temp{font-weight:700;margin-top:4px}"
		".day-desc{font-size:.85rem;margin-top:4px;color:var(--muted)}"
		".device{margin-top:28px;padding-top:16px;border-top:1px solid rgba(22,50,79,.15);"
		"color:var(--muted);font-size:.9rem;display:grid;gap:4px}"
		"@media(max-width:560px){.days{grid-template-columns:1fr}.now{grid-template-columns:1fr;justify-items:center;text-align:center}.meta{justify-content:center}}"
		"</style></head><body><main>"
		"<p class=\"brand\">ESP8684-MINI-1</p>"
		"<h1>Днепр</h1>"
		"<p class=\"sub\">погода сейчас и прогноз на 3 дня</p>"
		"<section class=\"now\">"
		"<img src=\"%s%s.svg\" alt=\"%s\" width=\"120\" height=\"120\">"
		"<div><p class=\"temp\">%s°</p><p class=\"desc\">%s</p></div>"
		"<div class=\"meta\">"
		"<span>влажность %d%%</span>"
		"<span>ветер %s км/ч</span>"
		"<span>обновлено %s</span>"
		"<span>%s</span>"
		"</div></section>"
		"<div class=\"actions\">"
		"<form action=\"/refresh\" method=\"post\" style=\"margin:0\">"
		"<button class=\"btn\" type=\"submit\">Обновить погоду</button>"
		"</form>"
		"<a class=\"link\" href=\"/click\">Click (%u)</a>"
		"</div>"
		"<h2>Прогноз</h2>"
		"<div class=\"days\">%s</div>"
		"<div class=\"device\">"
		"<div>SSID: %s · IP: " IPSTR " · RSSI: %d dBm</div>"
		"<div>GPIO6: 4 Hz %s · uptime %u с · heap %u</div>"
		"<div>Автообновление страницы каждые 60 с</div>"
		"</div></main></body></html>",
		ICON_CDN, icon, w.valid ? w.weather_text : "погода",
		w.valid ? temp_s : "--",
		w.valid ? w.weather_text : w.status,
		w.valid ? w.humidity : 0,
		w.valid ? wind_s : "--",
		w.updated[0] ? w.updated : "-",
		w.status,
		(unsigned)click_count,
		forecast,
		WIFI_SSID,
		IP2STR(&ip_info.ip),
		rssi,
		blink_level ? "ON" : "OFF",
		(unsigned)(esp_timer_get_time() / 1000000ULL),
		(unsigned)esp_get_free_heap_size());

	if (n < 0 || n >= PAGE_BUF_SIZE) {
		free(page);
		free(forecast);
		return httpd_resp_send_500(req);
	}

	httpd_resp_set_type(req, "text/html");
	httpd_resp_send(req, page, n);
	free(page);
	free(forecast);
	return ESP_OK;
}

static esp_err_t click_get_handler(httpd_req_t *req)
{
	click_count++;
	httpd_resp_set_status(req, "303 See Other");
	httpd_resp_set_hdr(req, "Location", "/");
	return httpd_resp_send(req, NULL, 0);
}

static esp_err_t refresh_get_handler(httpd_req_t *req)
{
	xSemaphoreGive(weather_kick);
	httpd_resp_set_status(req, "303 See Other");
	httpd_resp_set_hdr(req, "Location", "/");
	return httpd_resp_send(req, NULL, 0);
}

static httpd_handle_t start_webserver(void)
{
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	httpd_uri_t root = {
		.uri = "/",
		.method = HTTP_GET,
		.handler = root_get_handler,
		.user_ctx = NULL
	};
	httpd_uri_t click = {
		.uri = "/click",
		.method = HTTP_GET,
		.handler = click_get_handler,
		.user_ctx = NULL
	};
	httpd_uri_t refresh = {
		.uri = "/refresh",
		.method = HTTP_POST,
		.handler = refresh_get_handler,
		.user_ctx = NULL
	};

	config.lru_purge_enable = true;
	config.max_open_sockets = 4;
	config.stack_size = 8192;
	if (httpd_start(&http_server, &config) != ESP_OK) {
		ESP_LOGE(TAG, "httpd_start failed");
		return NULL;
	}

	httpd_register_uri_handler(http_server, &root);
	httpd_register_uri_handler(http_server, &click);
	httpd_register_uri_handler(http_server, &refresh);
	ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
	return http_server;
}

void app_main(void)
{
	esp_err_t ret;

	ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	weather_mutex = xSemaphoreCreateMutex();
	weather_kick = xSemaphoreCreateBinary();
	if (!weather_mutex || !weather_kick)
		abort();

	xTaskCreate(blink_task, "blink", 2048, NULL, 5, NULL);
	wifi_init_sta();
	start_webserver();
	xTaskCreate(weather_task, "weather", 8192, NULL, 4, NULL);
}
