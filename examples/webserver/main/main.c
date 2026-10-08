/*
 * Copyright (c) 2022 <qb4.dev@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <driver/gpio.h>

#include <libsupla/device.h>
#include <esp-supla.h>

#include "webserver.h"
#include "wifi.h"

#if CONFIG_IDF_TARGET_ESP8266
#define PUSH_BUTTON_PIN GPIO_NUM_0
#define LED_PIN GPIO_NUM_2
#else //ESP32xx
#define PUSH_BUTTON_PIN GPIO_NUM_0
#define LED_PIN GPIO_NUM_27
#endif

static const char *TAG = "APP";

static struct supla_config supla_config = {
#ifdef CONFIG_ESP_LIBSUPLA_USE_ESP_TLS
    .ssl = 1
#endif
};

static char hostname[32];
static supla_dev_t *supla_dev;
static supla_channel_t *relay_channel;
static supla_channel_t *at_channel;

//RELAY
int led_set_value(supla_channel_t *ch, TSD_SuplaChannelNewValue *new_value)
{
    TRelayChannel_Value *relay_val = (TRelayChannel_Value *)new_value->value;

    supla_log(LOG_INFO, "Relay set value %d", relay_val->hi);
    gpio_set_level(LED_PIN, !relay_val->hi);
    return supla_channel_set_relay_value(ch, relay_val);
}

supla_channel_config_t relay_channel_config = {
    .type = SUPLA_CHANNELTYPE_RELAY,
    .supported_functions = 0xFF,
    .default_function = SUPLA_CHANNELFNC_LIGHTSWITCH,
    .flags = SUPLA_CHANNEL_FLAG_CHANNELSTATE,
    .on_set_value = led_set_value //
};

//ACTION TRIGGER
supla_channel_config_t at_channel_config = {
    .type = SUPLA_CHANNELTYPE_ACTIONTRIGGER,
    .supported_functions = 0xFF,
    .default_function = SUPLA_CHANNELFNC_ACTIONTRIGGER,
    .action_trigger_caps = SUPLA_ACTION_CAP_SHORT_PRESS_x1,
    //.action_trigger_related_channel = &relay_channel
};

static esp_err_t io_init(void)
{
    gpio_set_direction(PUSH_BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_PIN, 1);
    return ESP_OK;
}

static void io_task(void *arg)
{
    int level, prev_level = 0;

    while (1) {
        level = gpio_get_level(PUSH_BUTTON_PIN);

        if (level == 0 && prev_level == 1) {
            supla_channel_emit_action(at_channel, SUPLA_ACTION_CAP_SHORT_PRESS_x1);
        }
        prev_level = level;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static esp_err_t supla_device_init(void)
{
#if defined(CONFIG_IDF_TARGET_ESP8266)
    supla_dev = supla_dev_create("ESP8266", NULL);
#elif defined(CONFIG_IDF_TARGET_ESP32)
    supla_dev = supla_dev_create("ESP32", NULL);
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
    supla_dev = supla_dev_create("ESP32S2", NULL);
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    supla_dev = supla_dev_create("ESP32S3", NULL);
#elif defined(CONFIG_IDF_TARGET_ESP32C2)
    supla_dev = supla_dev_create("ESP32C2", NULL);
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    supla_dev = supla_dev_create("ESP32C3", NULL);
#elif defined(CONFIG_IDF_TARGET_ESP32C5)
    supla_dev = supla_dev_create("ESP32C5", NULL);
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
    supla_dev = supla_dev_create("ESP32C6", NULL);
#elif defined(CONFIG_IDF_TARGET_ESP32C61)
    supla_dev = supla_dev_create("ESP32C61", NULL);
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
    supla_dev = supla_dev_create("ESP32P4", NULL);
#elif defined(CONFIG_IDF_TARGET_ESP32H2)
    supla_dev = supla_dev_create("ESP32H2", NULL);
#else
    supla_dev = supla_dev_create("ESPXX", NULL);
#endif

    supla_esp_generate_hostname(supla_dev, hostname, sizeof(hostname));
    supla_dev_set_flags(supla_dev, SUPLA_DEVICE_FLAG_CALCFG_ENTER_CFG_MODE);
    supla_dev_set_common_channel_state_callback(supla_dev, supla_esp_get_wifi_state);
    supla_dev_set_server_time_sync_callback(supla_dev, supla_esp_server_time_sync);
    supla_dev_set_server_req_restart_callback(supla_dev, supla_esp_restart_callback);

    supla_esp_nvs_config_init(&supla_config);
    supla_dev_set_config(supla_dev, &supla_config);

    relay_channel = supla_channel_create(&relay_channel_config);
    at_channel = supla_channel_create(&at_channel_config);

    supla_dev_add_channel(supla_dev, relay_channel);
    supla_dev_add_channel(supla_dev, at_channel);

    supla_dev_set_common_channel_state_callback(supla_dev, supla_esp_get_wifi_state);
    supla_dev_set_server_time_sync_callback(supla_dev, supla_esp_server_time_sync);

    return ESP_OK;
}

static void net_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            break;
        case WIFI_EVENT_STA_CONNECTED: {
            wifi_event_sta_connected_t *info = data;

            ESP_LOGI(TAG, "Connected to SSID: %s", info->ssid);
        } break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *info = data;

            ESP_LOGE(TAG, "Station disconnected(reason : %d)", info->reason);
            //          if (info->reason == WIFI_REASON_BASIC_RATE_NOT_SUPPORT) {
            //              /*Switch to 802.11 bgn mode */
            //              esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
            //          }
            if (info->reason == WIFI_REASON_ASSOC_LEAVE)
                break; // disconnected by user

            /* Let the chip cool down for a while */
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_wifi_connect();
        } break;
        default:
            break;
        }
    } else if (base == IP_EVENT) {
        switch (id) {
        case IP_EVENT_STA_GOT_IP: {
            ip_event_got_ip_t *event = data;
            ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
            supla_dev_start(supla_dev);
        } break;
        default:
            break;
        }
    }
}

void app_main()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated or version mismatch → erase and retry
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(supla_esp_nvs_config_init(&supla_config));

    ESP_ERROR_CHECK(io_init());
    ESP_ERROR_CHECK(supla_device_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(wifi_init(net_event_handler));

    if (supla_config.email[0] == 0) {
        wifi_set_access_point_mode(hostname);
        webserver_init(&supla_dev);
    } else {
        wifi_set_station_mode();
    }

    xTaskCreate(&io_task, "io", 2048, NULL, 1, NULL);
    while (1) {
        supla_dev_iterate(supla_dev);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
