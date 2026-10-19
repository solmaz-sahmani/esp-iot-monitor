idf_component_register(
    SRCS "src/dht11.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_timer
)