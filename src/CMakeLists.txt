idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash esp_wifi esp_event esp_netif
                             esp_http_server esp_http_client json
                             esp-tls mbedtls driver esp_timer mdns)
