/*
 * Copyright (c) 2022 <qb4.dev@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "esp-supla-utils.h"

#include <time.h>
#include <string.h>

#include <esp_system.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <nvs_flash.h>

static const char *TAG = "SUPLA-ESP";
static const char *NVS_STORAGE = "supla_nvs";

int supla_esp_nvs_config_init(struct supla_config *supla_conf)
{
	size_t required_size;
	nvs_handle nvs;
	esp_err_t rc;
	const char empty_auth[SUPLA_AUTHKEY_SIZE] = {0};
	const char empty_guid[SUPLA_GUID_SIZE] = {0};

	nvs_flash_init();
	ESP_LOGI(TAG,"NVS config init");
	rc = nvs_open(NVS_STORAGE,NVS_READONLY,&nvs);
	if(rc == ESP_OK){
		nvs_get_str(nvs,"email",supla_conf->email,&required_size);
		nvs_get_blob(nvs,"auth_key",supla_conf->auth_key,&required_size);
		nvs_get_blob(nvs,"guid",supla_conf->guid,&required_size);
		nvs_get_str(nvs,"server",supla_conf->server,&required_size);
		nvs_get_i8(nvs,"ssl",(int8_t*)&supla_conf->ssl);
		nvs_get_i32(nvs,"port",&supla_conf->port);
		nvs_get_i32(nvs,"activity_timeout",&supla_conf->activity_timeout);
		nvs_close(nvs);
	} else {
		ESP_LOGW(TAG, "nvs open error %s",esp_err_to_name(rc));
	}

	if(!memcmp(supla_conf->auth_key,empty_auth,SUPLA_AUTHKEY_SIZE)){
		ESP_LOGW(TAG,"AUTHKEY not set, generate now...");
		rc = nvs_open(NVS_STORAGE,NVS_READWRITE,&nvs);
		if(rc == ESP_OK){
			esp_fill_random(supla_conf->auth_key,SUPLA_AUTHKEY_SIZE);
			ESP_LOGI(TAG, "generated AUTHKEY");
			//ESP_LOG_BUFFER_HEX(TAG,supla_conf->auth_key,SUPLA_AUTHKEY_SIZE);
			nvs_set_blob(nvs,"auth_key",supla_conf->auth_key,SUPLA_AUTHKEY_SIZE);
			nvs_commit(nvs);
			nvs_close(nvs);
		}else{
			ESP_LOGE(TAG, "nvs open error %s",esp_err_to_name(rc));
		}
	}

	if(!memcmp(supla_conf->guid,empty_guid,SUPLA_GUID_SIZE)){
		ESP_LOGW(TAG,"GUID not set, generate now...");
		rc = nvs_open(NVS_STORAGE,NVS_READWRITE,&nvs);
		if(rc == ESP_OK){
			esp_fill_random(supla_conf->guid,SUPLA_GUID_SIZE);
			ESP_LOGI(TAG, "generated GUID");
			//ESP_LOG_BUFFER_HEX(TAG,supla_conf->guid,SUPLA_GUID_SIZE);
			nvs_set_blob(nvs,"guid",supla_conf->guid,SUPLA_GUID_SIZE);
			nvs_commit(nvs);
			nvs_close(nvs);
		}else{
			ESP_LOGE(TAG, "nvs open error %s",esp_err_to_name(rc));
		}
	}
	return 0;
}

int supla_esp_nvs_config_erase(void)
{
	nvs_handle nvs;
	esp_err_t rc;
	rc = nvs_open(NVS_STORAGE,NVS_READWRITE,&nvs);
	if(rc == ESP_OK){
		nvs_erase_all(&nvs);
		ESP_LOGE(TAG, "nvs config erased");
	}else{
		ESP_LOGE(TAG, "nvs open error %s",esp_err_to_name(rc));
	}
	return rc;
}

int supla_esp_get_wifi_state(TDSC_ChannelState *state)
{
	tcpip_adapter_ip_info_t ip_info = {0};
	wifi_ap_record_t wifi_info = {0};

	if(esp_efuse_mac_get_default(state->MAC) == ESP_OK)
		state->Fields |= SUPLA_CHANNELSTATE_FIELD_MAC;

	if(esp_wifi_sta_get_ap_info(&wifi_info) == ESP_OK){
		state->Fields |= SUPLA_CHANNELSTATE_FIELD_WIFIRSSI;
		state->WiFiRSSI = wifi_info.rssi;

		state->Fields |= SUPLA_CHANNELSTATE_FIELD_WIFISIGNALSTRENGTH;
		if (wifi_info.rssi > -50)
			state->WiFiSignalStrength = 100;
		else if (wifi_info.rssi <= -100)
			state->WiFiSignalStrength = 0;
		else
			state->WiFiSignalStrength = 2 * (wifi_info.rssi + 100);
	}

	if(tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA,&ip_info) == ESP_OK){
		state->Fields |= SUPLA_CHANNELSTATE_FIELD_IPV4;
		state->IPv4 = ip_info.ip.addr;
	}
	return 0;
}

int supla_esp_server_time_sync(supla_dev_t *dev, TSDC_UserLocalTimeResult *lt)
{
	struct tm tm;
	struct timeval timeval;

	tm.tm_year = lt->year-1900;
	tm.tm_mon  = lt->month-1;
	tm.tm_mday = lt->day;

	tm.tm_hour = lt->hour;
	tm.tm_min = lt->min;
	tm.tm_sec = lt->sec;

	timeval.tv_sec = mktime(&tm);
	timeval.tv_usec = 0;

	return settimeofday(&timeval,NULL);
}
