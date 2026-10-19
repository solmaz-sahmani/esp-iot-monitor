idf_component_register(SRCS "esp-iot-monitor.c"
                    INCLUDE_DIRS "."
                    REQUIRES dht11 esp_wifi esp_event esp_netif
                             nvs_flash esp_http_client)
