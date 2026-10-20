idf_component_register(
    SRCS "esp-iot-monitor.c"
    INCLUDE_DIRS "."
    REQUIRES
        wifi_manager
        dht11
        esp_driver_gpio
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp_http_client
)