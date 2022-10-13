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
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <cJSON.h>
#include <mdns.h>

static const char *TAG = "SUPLA-ESP";
static const char *NVS_STORAGE = "supla_nvs";

esp_err_t supla_esp_nvs_config_init(struct supla_config *supla_conf)
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
	if(!supla_conf->port)
		supla_conf->port = supla_conf->ssl ? 2016 : 2015;

	if(!supla_conf->activity_timeout)
		supla_conf->activity_timeout = 120;

	return ESP_OK;
}

esp_err_t supla_esp_nvs_config_erase(void)
{
	nvs_handle nvs;
	esp_err_t rc;
	rc = nvs_open(NVS_STORAGE,NVS_READWRITE,&nvs);
	if(rc == ESP_OK){
		nvs_erase_all(nvs);
		ESP_LOGI(TAG, "nvs config erased");
	}else{
		ESP_LOGE(TAG, "nvs open error %s",esp_err_to_name(rc));
	}
	return rc;
}

static char *bin2hex(char *hex, const char *bin, size_t len)
{
	int adv = 0;

	if(!bin || !hex)
	  return hex;

	hex[0] = 0;
	for(int i = 0; i < len; i++) {
		snprintf(&hex[adv], 3, "%02X", (unsigned char)bin[i]);
		adv += 2;
	}
	return hex;
}

esp_err_t supla_config_httpd_handler(httpd_req_t *req)
{
	cJSON *js = NULL;
	cJSON *js_data = NULL;
	cJSON *js_errors = NULL;
	cJSON *js_err = NULL;
	char *js_txt;
	char guid_hex[SUPLA_GUID_HEXSIZE];

	struct supla_config *supla_conf = req->user_ctx;

	js = cJSON_CreateObject();
	if(!js)
		return ESP_FAIL;

	if(supla_conf){
		js_data = cJSON_CreateObject();
		cJSON_AddStringToObject(js_data,"email",supla_conf->email);
		cJSON_AddStringToObject(js_data,"server",supla_conf->server);
		cJSON_AddStringToObject(js_data,"guid",bin2hex(guid_hex,supla_conf->guid,sizeof(supla_conf->guid)));
		cJSON_AddBoolToObject(js_data,"ssl",supla_conf->ssl);
		cJSON_AddNumberToObject(js_data,"port",supla_conf->port);
		cJSON_AddNumberToObject(js_data,"activity_timeout",supla_conf->activity_timeout);
		cJSON_AddItemToObject(js,"data",js_data);
		printf("port=%d\n",supla_conf->port);
	} else {
		js_errors = cJSON_CreateArray();
		js_err =  cJSON_CreateObject();
		cJSON_AddItemToObject(js_err,"title",cJSON_CreateString("SUPLA config not found"));
		cJSON_AddItemToArray(js_errors,js_err);
		cJSON_AddItemToObject(js,"errors",js_errors);
	}

	js_txt = cJSON_Print(js);
	cJSON_Delete(js);

	httpd_resp_set_type(req,HTTPD_TYPE_JSON);
	httpd_resp_send(req, js_txt, -1);
	free(js_txt);
	return ESP_OK;
}



esp_err_t supla_esp_generate_hostname(supla_dev_t *dev, char* buf, size_t len)
{
	uint8_t mac[6];
	const char *dev_name;

	if(!dev)
		return ESP_ERR_INVALID_ARG;

	dev_name = supla_dev_get_name(dev);

	esp_efuse_mac_get_default(mac);
	if(strlen(dev_name) + 18 > len)
		return ESP_ERR_INVALID_SIZE;

	snprintf(buf,len,"%s-%02X%02X",dev_name,mac[4],mac[5]);
	return ESP_OK;
}

esp_err_t supla_esp_set_hostname(supla_dev_t *dev, tcpip_adapter_if_t tcpip_if)
{
	char hostname[TCPIP_HOSTNAME_MAX_SIZE];
	esp_err_t rc;

	if(!dev)
		return ESP_ERR_INVALID_ARG;

	rc = supla_esp_generate_hostname(dev,hostname,sizeof(hostname));
	if(rc != ESP_OK)
		return rc;

	rc = tcpip_adapter_set_hostname(tcpip_if,hostname);
	if(rc != ESP_OK)
		return rc;

	ESP_LOGI(TAG, "device hostname set: %s",hostname);
	return ESP_OK;
}


esp_err_t supla_esp_init_mdns(supla_dev_t *dev)
{
#if ENABLE_MDNS == y
	char mdns_name[SUPLA_DEVICE_NAME_MAXSIZE+16];
	esp_err_t rc;

	if(!dev)
		return ESP_ERR_INVALID_ARG;

	rc = mdns_init();
	if(rc != ESP_OK)
		return rc;

	rc = supla_esp_generate_hostname(dev,mdns_name,sizeof(mdns_name));
	if(rc != ESP_OK)
		return rc;

	rc = mdns_hostname_set(mdns_name);
	if(rc != ESP_OK)
		return rc;

	ESP_LOGI(TAG, "mdns hostname: %s",mdns_name);
	return ESP_OK;
#else
	#warning "mDNS is disabled - .local acess will not work"
	return ESP_ERR_NOT_SUPPORTED;
#endif
}



esp_err_t supla_esp_get_wifi_state(supla_dev_t *dev, TDSC_ChannelState *state)
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
	return ESP_OK;
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


