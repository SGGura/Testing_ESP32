#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#define WIFI_SSID "GURAWORK"
#define WIFI_PASS "GURA03071963"
#define WIFI_MAX_RETRY 20
#define BLINK_GPIO GPIO_NUM_6
#define BLINK_HALF_MS 125

static const char *TAG = "esp8684";
static EventGroupHandle_t wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT = BIT1;
static int wifi_retry;
static volatile int blink_level;
static volatile uint32_t click_count;
static httpd_handle_t http_server;

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

static esp_err_t root_get_handler(httpd_req_t *req)
{
	char page[700];
	esp_netif_ip_info_t ip_info;
	esp_netif_t *netif;
	wifi_ap_record_t ap;
	int rssi;
	int n;

	netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
	memset(&ip_info, 0, sizeof(ip_info));
	if (netif)
		esp_netif_get_ip_info(netif, &ip_info);

	rssi = 0;
	if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
		rssi = ap.rssi;

	n = snprintf(page, sizeof(page),
		"<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
		"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
		"<title>ESP8684</title></head><body>"
		"<h1>ESP8684-MINI-1</h1>"
		"<p>ESP-IDF + esp_http_server</p>"
		"<p>SSID: %s</p>"
		"<p>IP: " IPSTR "</p>"
		"<p>RSSI: %d dBm</p>"
		"<p>GPIO6: 4 Hz, now %s</p>"
		"<p>Uptime: %u s</p>"
		"<p>Free heap: %u</p>"
		"<p>Clicks: %u</p>"
		"<p><a href=\"/click\">Click</a></p>"
		"</body></html>",
		WIFI_SSID,
		IP2STR(&ip_info.ip),
		rssi,
		blink_level ? "ON" : "OFF",
		(unsigned)(esp_timer_get_time() / 1000000ULL),
		(unsigned)esp_get_free_heap_size(),
		(unsigned)click_count);
	if (n < 0)
		return httpd_resp_send_500(req);

	httpd_resp_set_type(req, "text/html");
	return httpd_resp_send(req, page, n);
}

static esp_err_t click_get_handler(httpd_req_t *req)
{
	click_count++;
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

	config.lru_purge_enable = true;
	config.max_open_sockets = 4;
	config.stack_size = 6144;
	if (httpd_start(&http_server, &config) != ESP_OK) {
		ESP_LOGE(TAG, "httpd_start failed");
		return NULL;
	}

	httpd_register_uri_handler(http_server, &root);
	httpd_register_uri_handler(http_server, &click);
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

	xTaskCreate(blink_task, "blink", 2048, NULL, 5, NULL);
	wifi_init_sta();
	start_webserver();
}
