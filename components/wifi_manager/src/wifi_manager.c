
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "wifi_manager.h"

#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "WIFI_MANAGER";

static EventGroupHandle_t wifi_events;


/**
 * @brief Handle Wi-Fi and IP events.
 */
static void wifi_event_handler(
    void *arg,
    esp_event_base_t base,
    int32_t id,
    void *data)
{
    (void)arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_err_t err = esp_wifi_connect();

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Initial connect failed: %s",
                     esp_err_to_name(err));
        }
    }
    else if (base == WIFI_EVENT &&
             id == WIFI_EVENT_STA_DISCONNECTED) {

        xEventGroupClearBits(
            wifi_events, WIFI_CONNECTED_BIT);

        ESP_LOGW(TAG, "Disconnected; reconnecting");

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

        ESP_LOGI(TAG, "Connected; IP: " IPSTR,
                 IP2STR(&event->ip_info.ip));

        xEventGroupSetBits(
            wifi_events, WIFI_CONNECTED_BIT);
    }
}


/**
 * @brief Initialize Wi-Fi in station mode.
 */
esp_err_t wifi_manager_init(void)
{
    wifi_events = xEventGroupCreate();

    if (wifi_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_netif_init();

    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_loop_create_default();

    if (err != ESP_OK) {
        return err;
    }

    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_FAIL;
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();

    err = esp_wifi_init(&init_cfg);

    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_handler_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        wifi_event_handler,
        NULL);

    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_handler_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        wifi_event_handler,
        NULL);

    if (err != ESP_OK) {
        return err;
    }

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

    err = esp_wifi_set_mode(WIFI_MODE_STA);

    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &config);

    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_start();

    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Wi-Fi initialization complete");

    return ESP_OK;
}


/**
 * @brief Block until the station obtains an IP address.
 */
void wifi_manager_wait_connected(void)
{
    xEventGroupWaitBits(
        wifi_events,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY);
}
