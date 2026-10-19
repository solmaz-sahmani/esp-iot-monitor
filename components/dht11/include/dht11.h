#ifndef DHT11_H
#define DHT11_H

#include <stdint.h>
#include "esp_err.h"

esp_err_t dht11_init(void);

esp_err_t dht11_read(
    uint8_t *humidity,
    uint8_t *temperature);

#endif