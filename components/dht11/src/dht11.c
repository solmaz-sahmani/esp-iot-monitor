
#include "dht11.h"

#include <stdint.h>
#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

#define DHT11_GPIO              GPIO_NUM_4
#define DHT11_TIMEOUT_US        120
#define DHT11_START_SIGNAL_US   20000
#define DHT11_BIT_THRESHOLD_US  40

/* Initialize the DHT11 data pin. */
esp_err_t dht11_init(void)
{
    gpio_config_t config = {
        .pin_bit_mask = (1ULL << DHT11_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t err = gpio_config(&config);

    if (err != ESP_OK)
    {
        return err;
    }

    /* Release the open-drain bus. */
    return gpio_set_level(DHT11_GPIO, 1);
}

/* Wait until the data line reaches the requested level. */
static bool wait_for_level(int level, int timeout_us)
{
    int64_t start = esp_timer_get_time();

    while (gpio_get_level(DHT11_GPIO) != level)
    {
        if ((esp_timer_get_time() - start) >= timeout_us)
        {
            return false;
        }
    }

    return true;
}

/* Read one byte, most significant bit first. */
static bool dht11_read_byte(uint8_t *value)
{
    uint8_t result = 0U;

    for (unsigned i = 0U; i < 8U; i++)
    {
        int64_t start;
        int64_t pulse_width;

        /* Wait for the beginning of the high pulse. */
        if (!wait_for_level(1, DHT11_TIMEOUT_US))
        {
            return false;
        }

        start = esp_timer_get_time();

        /* The end of the high pulse determines the bit value. */
        if (!wait_for_level(0, DHT11_TIMEOUT_US))
        {
            return false;
        }

        pulse_width = esp_timer_get_time() - start;

        result <<= 1U;

        if (pulse_width > DHT11_BIT_THRESHOLD_US)
        {
            result |= 1U;
        }
    }

    *value = result;

    return true;
}

/* Read and validate one DHT11 measurement. */
esp_err_t dht11_read(uint8_t *humidity, uint8_t *temperature)
{
    uint8_t data[5] = {0U};

    if (humidity == NULL || temperature == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    /* Pull the data line low to start a measurement. */
    esp_err_t err = gpio_set_direction(
        DHT11_GPIO, GPIO_MODE_INPUT_OUTPUT_OD);

    if (err != ESP_OK)
    {
        return err;
    }

    err = gpio_set_level(DHT11_GPIO, 0);

    if (err != ESP_OK)
    {
        return err;
    }

    esp_rom_delay_us(DHT11_START_SIGNAL_US);

    /* Release the bus for the sensor response. */
    err = gpio_set_level(DHT11_GPIO, 1);

    if (err != ESP_OK)
    {
        return err;
    }

    err = gpio_set_direction(DHT11_GPIO, GPIO_MODE_INPUT);

    if (err != ESP_OK)
    {
        return err;
    }

    /* The sensor responds with low, high, then low. */
    if (!wait_for_level(0, DHT11_TIMEOUT_US) ||
        !wait_for_level(1, DHT11_TIMEOUT_US) ||
        !wait_for_level(0, DHT11_TIMEOUT_US))
    {
        return ESP_ERR_TIMEOUT;
    }

    /* Read humidity, temperature, and checksum bytes. */
    for (unsigned i = 0U; i < 5U; i++)
    {
        if (!dht11_read_byte(&data[i]))
        {
            return ESP_ERR_TIMEOUT;
        }
    }

    /* Validate the checksum before accepting the reading. */
    uint8_t checksum = (uint8_t)(
        data[0] + data[1] + data[2] + data[3]);

    if (checksum != data[4])
    {
        return ESP_ERR_INVALID_CRC;
    }

    /* DHT11 uses integer and decimal bytes for each measurement. */
    if (data[1] > 9U || data[3] > 9U ||
        data[0] > 100U || data[2] > 80U)
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *humidity = data[0];
    *temperature = data[2];

    return ESP_OK;
}
