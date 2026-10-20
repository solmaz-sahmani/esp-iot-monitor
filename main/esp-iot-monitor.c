
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "dht11.h"
#include "wifi_manager.h"

#define PC_SERVER_IP       "192.168.100.133"
#define SERVER_URL         "http://" PC_SERVER_IP ":8080/api/measurements"

#define WIFI_CONNECTED_BIT BIT0

#define SENSOR_PERIOD_MS   2000
#define MEASUREMENT_QUEUE_LENGTH 5

#define SENSOR_TASK_STACK_SIZE 4096
#define HTTP_TASK_STACK_SIZE   6144

#define SENSOR_TASK_PRIORITY 5
#define HTTP_TASK_PRIORITY   4

static const char *TAG = "IOT_MONITOR";

static QueueHandle_t measurement_queue;

static uint32_t tx_success_count = 0;
static uint32_t tx_failure_count = 0;

/* One complete sensor measurement. */
typedef struct {
    uint8_t temperature;
    uint8_t humidity;
} measurement_t;


/* ---------- Wi-Fi event handling ---------- */


/* ---------- Task 1: Sensor acquisition ---------- */

static void sensor_task(void *arg)
{
    (void)arg;

    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        measurement_t measurement = {0};

        esp_err_t err = dht11_read(
            &measurement.humidity,
            &measurement.temperature);

        if (err == ESP_OK) {

            /* Do not block if the queue is full. */
            if (xQueueSend(
                    measurement_queue,
                    &measurement,
                    0) == pdPASS) {

                ESP_LOGI(TAG,
                         "Sensor: %u C, %u %% RH; queued",
                         (unsigned)measurement.temperature,
                         (unsigned)measurement.humidity);
            }
            else {
                ESP_LOGW(TAG,
                         "Measurement queue full; sample dropped");
            }
        }
        else {
            ESP_LOGW(TAG,
                     "DHT11 read failed: %s",
                     esp_err_to_name(err));
        }

        /* Keep a regular sampling period. */
        vTaskDelayUntil(
            &last_wake,
            pdMS_TO_TICKS(SENSOR_PERIOD_MS));
    }
}


/* ---------- Task 2: HTTP transmission ---------- */

static void http_sender_task(void *arg)
{
    (void)arg;

    esp_http_client_config_t http_cfg = {
        .url = SERVER_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&http_cfg);

    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP client initialization failed");
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_http_client_set_header(
        client, "Content-Type", "application/json");

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP header setup failed: %s",
                 esp_err_to_name(err));

        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        measurement_t measurement;

        /* Wait until a measurement is available. */
        if (xQueueReceive(
                measurement_queue,
                &measurement,
                portMAX_DELAY) != pdPASS) {
            continue;
        }

        /* Wait until Wi-Fi is connected. */
        wifi_manager_wait_connected();

        char payload[128];

        int length = snprintf(
            payload,
            sizeof(payload),
            "{\"device_id\":\"esp32-01\","
            "\"temperature_c\":%u,"
            "\"humidity_percent\":%u}",
            (unsigned)measurement.temperature,
            (unsigned)measurement.humidity);

        if (length < 0 || (size_t)length >= sizeof(payload)) {
            tx_failure_count++;

            ESP_LOGE(TAG, "JSON payload creation failed");
            continue;
        }

        err = esp_http_client_set_post_field(
            client, payload, length);

        if (err == ESP_OK) {
            err = esp_http_client_perform(client);
        }

        if (err == ESP_OK) {
            int status =
                esp_http_client_get_status_code(client);

            if (status >= 200 && status < 300) {
                tx_success_count++;

                ESP_LOGI(TAG,
                         "HTTP OK: %u C, %u %% RH | "
                         "success=%lu failure=%lu",
                         (unsigned)measurement.temperature,
                         (unsigned)measurement.humidity,
                         (unsigned long)tx_success_count,
                         (unsigned long)tx_failure_count);
            }
            else {
                tx_failure_count++;

                ESP_LOGW(TAG,
                         "HTTP status=%d | "
                         "success=%lu failure=%lu",
                         status,
                         (unsigned long)tx_success_count,
                         (unsigned long)tx_failure_count);
            }
        }
        else {
            tx_failure_count++;

            ESP_LOGE(TAG,
                     "HTTP failed: %s | "
                     "success=%lu failure=%lu",
                     esp_err_to_name(err),
                     (unsigned long)tx_success_count,
                     (unsigned long)tx_failure_count);
        }
    }
}


/* ---------- Application entry point ---------- */

void app_main(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    else {
        ESP_ERROR_CHECK(err);
    }

    ESP_ERROR_CHECK(dht11_init());

    /* Create the queue before starting either task. */
    measurement_queue = xQueueCreate(
        MEASUREMENT_QUEUE_LENGTH,
        sizeof(measurement_t));

    configASSERT(measurement_queue != NULL);

    ESP_ERROR_CHECK(wifi_manager_init());

    /* Start independent sensor and HTTP tasks. */
    BaseType_t result = xTaskCreate(
        sensor_task,
        "sensor_task",
        SENSOR_TASK_STACK_SIZE,
        NULL,
        SENSOR_TASK_PRIORITY,
        NULL);

    configASSERT(result == pdPASS);

    result = xTaskCreate(
        http_sender_task,
        "http_sender_task",
        HTTP_TASK_STACK_SIZE,
        NULL,
        HTTP_TASK_PRIORITY,
        NULL);

    configASSERT(result == pdPASS);

    ESP_LOGI(TAG, "Monitoring tasks started");
}
