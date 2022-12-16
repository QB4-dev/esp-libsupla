/*
 * wifi.c
 *
 *  Created on: 8 wrz 2022
 *      Author: kuba
 */

#include "wifi.h"

#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

static const char *TAG = "wifi";

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1


static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
	if (event_base == WIFI_EVENT) {
		switch(event_id) {
		case WIFI_EVENT_STA_START:
			//esp_wifi_connect();
			break;
		case WIFI_EVENT_STA_CONNECTED:{
			wifi_event_sta_connected_t *info = event_data;

			ESP_LOGI(TAG, "Connected to SSID: %s",info->ssid);
			}break;
		case WIFI_EVENT_STA_DISCONNECTED:{
			wifi_event_sta_disconnected_t *info = event_data;

			ESP_LOGE(TAG, "Station disconnected(reason : %d)",info->reason);
//			if (info->reason == WIFI_REASON_BASIC_RATE_NOT_SUPPORT) {
//				/*Switch to 802.11 bgn mode */
//				esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
//			}
			esp_wifi_connect();
			}break;
		default:
			break;
		}
	} else if (event_base == IP_EVENT) {
		switch(event_id){
		case IP_EVENT_STA_GOT_IP:{
			ip_event_got_ip_t* event = event_data;
			ESP_LOGI(TAG, "got ip:%s", ip4addr_ntoa(&event->ip_info.ip));
			xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
			}break;
		default:
			break;
		}
	}
}


void wifi_init(void)
{
	s_wifi_event_group = xEventGroupCreate();
	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
	ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_init_sta(void)
{
	wifi_config_t wifi_config = {
		.sta = {
			.ssid = EXAMPLE_ESP_WIFI_SSID,
			.password = EXAMPLE_ESP_WIFI_PASS
		},
	};

	/* Setting a password implies station will connect to all security modes including WEP/WPA.
		* However these modes are deprecated and not advisable to be used. Incase your Access point
		* doesn't support WPA2, these mode can be enabled by commenting below line */

	if (strlen((char *)wifi_config.sta.password)) {
		wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	}

	ESP_ERROR_CHECK(esp_wifi_stop());
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA) );
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
	ESP_ERROR_CHECK(esp_wifi_start());
	ESP_ERROR_CHECK(esp_wifi_connect());

	ESP_LOGI(TAG, "wifi init station");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s",EXAMPLE_ESP_WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}
