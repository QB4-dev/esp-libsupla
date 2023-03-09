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
#include "wifi.h"

#if CONFIG_IDF_TARGET_ESP8266
	#define PUSH_BUTTON_PIN  GPIO_NUM_0
	#define LED_PIN          GPIO_NUM_2
#elif CONFIG_IDF_TARGET_ESP32
	#define PUSH_BUTTON_PIN  GPIO_NUM_0
	#define LED_PIN          GPIO_NUM_27
#endif

static const char *TAG="APP";

static struct supla_config supla_config = {
	.email = CONFIG_SUPLA_EMAIL,
	.server = CONFIG_SUPLA_SERVER
};

static supla_dev_t *supla_dev;
static supla_channel_t *relay_channel;
static supla_channel_t *at_channel;

//RELAY
int led_set_value(supla_channel_t *ch, TSD_SuplaChannelNewValue *new_value)
{
	TRelayChannel_Value *relay_val = (TRelayChannel_Value*)new_value->value;

	supla_log(LOG_INFO,"Relay set value %d",relay_val->hi);
	gpio_set_level(LED_PIN,!relay_val->hi);
	return supla_channel_set_relay_value(ch,relay_val);
}

supla_channel_config_t relay_channel_config = {
	.type = SUPLA_CHANNELTYPE_RELAY,
	.supported_functions = 0xFF,
	.default_function = SUPLA_CHANNELFNC_LIGHTSWITCH,
	.flags = SUPLA_CHANNEL_FLAG_CHANNELSTATE,
	.on_set_value = led_set_value
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
	gpio_set_level(LED_PIN,1);
	return ESP_OK;
}

static void io_task(void *arg)
{
	int level,prev_level= 0;

	while(1){
		level = gpio_get_level(PUSH_BUTTON_PIN);

		if(level == 0 && prev_level == 1){
			supla_channel_emit_action(at_channel,SUPLA_ACTION_CAP_SHORT_PRESS_x1);
		}
		prev_level = level;
		vTaskDelay(100 / portTICK_RATE_MS);
	}
}

static void supla_task(void *arg)
{
	supla_dev_t *dev = arg;
	if(!dev){
		vTaskDelete(NULL);
		return;
	}

	relay_channel = supla_channel_create(&relay_channel_config);
	at_channel = supla_channel_create(&at_channel_config);

	supla_dev_add_channel(dev,relay_channel);
	supla_dev_add_channel(dev,at_channel);

	supla_dev_set_common_channel_state_callback(dev,supla_esp_get_wifi_state);
	supla_dev_set_server_time_sync_callback(dev,supla_esp_server_time_sync);

	if(supla_dev_set_config(dev,&supla_config) != SUPLA_RESULT_TRUE){
		vTaskDelete(NULL);
		return;
	}
        supla_dev_start(dev);
	while(1){
		supla_dev_iterate(dev);
		vTaskDelay(100 / portTICK_RATE_MS);
	}
}

void app_main()
{
	ESP_ERROR_CHECK(nvs_flash_init());
	ESP_ERROR_CHECK(supla_esp_nvs_config_init(&supla_config));

	io_init();
	wifi_init();
#if CONFIG_IDF_TARGET_ESP8266
	supla_dev = supla_dev_create("ESP8266",NULL);
#elif CONFIG_IDF_TARGET_ESP32
	supla_dev = supla_dev_create("ESP32",NULL);
#endif
    wifi_init_sta();
	xTaskCreate(&io_task, "io", 2048, NULL, 1, NULL);
	xTaskCreate(&supla_task, "supla", 8192, supla_dev, 1, NULL);
	while(1){
		ESP_LOGI(TAG, "Free heap size: '%d'", esp_get_free_heap_size());
		vTaskDelay(10000 / portTICK_RATE_MS);
	}
}
