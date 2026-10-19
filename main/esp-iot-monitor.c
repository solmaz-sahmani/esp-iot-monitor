#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include <dht11.h>

#define PC_SERVER_IP  "192.168.100.133" /* CHANGE THIS */
#define SERVER_URL    "http://" PC_SERVER_IP \
                      ":8080/api/measurements"

#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "IOT_MONITOR";
static EventGroupHandle_t wifi_events;

//static const char *TAG = "DHT11";

static void wifi_event_handler(
    void *arg,
    esp_event_base_t base,
    int32_t id,
    void *data)
{
    (void)arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_ERROR_CHECK(esp_wifi_connect());
    }
    else if (base == WIFI_EVENT &&
             id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(
            wifi_events, WIFI_CONNECTED_BIT);

        ESP_LOGW(TAG, "Wi-Fi disconnected; reconnecting");

        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Reconnect request failed: %s",
                     esp_err_to_name(err));
        }
    }
    else if (base == IP_EVENT &&
             id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)data;

        ESP_LOGI(TAG, "Got IP: " IPSTR,
                 IP2STR(&event->ip_info.ip));

        xEventGroupSetBits(
            wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    wifi_events = xEventGroupCreate();
    configASSERT(wifi_events != NULL);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        wifi_event_handler, NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        wifi_event_handler, NULL));

    wifi_config_t config = {0};

    strlcpy((char *)config.sta.ssid,
            CONFIG_MONITOR_WIFI_SSID,
            sizeof(config.sta.ssid));

    strlcpy((char *)config.sta.password,
            CONFIG_MONITOR_WIFI_PASSWORD,
            sizeof(config.sta.password));

    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(
        WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    ESP_ERROR_CHECK(dht11_init());

    wifi_init();

    ESP_LOGI(TAG, "Waiting for Wi-Fi connection");

    xEventGroupWaitBits(
        wifi_events,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY);

    esp_http_client_config_t http_cfg = {
        .url = SERVER_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&http_cfg);

    configASSERT(client != NULL);

    ESP_ERROR_CHECK(esp_http_client_set_header(
        client, "Content-Type", "application/json"));

    while (true) {
        /* Wait for connectivity before every transmission. */
        xEventGroupWaitBits(
            wifi_events,
            WIFI_CONNECTED_BIT,
            pdFALSE,
            pdTRUE,
            portMAX_DELAY);

        uint8_t humidity = 0;
        uint8_t temperature = 0;

        err = dht11_read(&humidity, &temperature);

        if (err == ESP_OK) {
            char payload[128];

            snprintf(payload, sizeof(payload),
                     "{\"device_id\":\"esp32-01\","
                     "\"temperature_c\":%u,"
                     "\"humidity_percent\":%u}",
                     (unsigned)temperature,
                     (unsigned)humidity);

            ESP_ERROR_CHECK_WITHOUT_ABORT(
                esp_http_client_set_post_field(
                    client, payload, strlen(payload)));

            err = esp_http_client_perform(client);

            if (err == ESP_OK) {
                int status =
                    esp_http_client_get_status_code(client);

                if (status == 200) {
                    ESP_LOGI(TAG,
                             "Sent: %u C, %u %% RH",
                             (unsigned)temperature,
                             (unsigned)humidity);
                } else {
                    ESP_LOGW(TAG, "Server HTTP status: %d",
                             status);
                }
            } else {
                ESP_LOGE(TAG, "HTTP request failed: %s",
                         esp_err_to_name(err));
            }
        } else {
            ESP_LOGW(TAG, "Sensor read failed: %s",
                     esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}