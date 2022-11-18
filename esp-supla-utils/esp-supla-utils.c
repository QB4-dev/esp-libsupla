/*
 * Copyright (c) 2022 <qb4.dev@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "esp-supla-utils.h"

#include <time.h>
#include <string.h>
#include <sys/param.h>

#include <esp_system.h>
#include <esp_log.h>
#include <esp_err.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <cJSON.h>
#include <mdns.h>

static const char *TAG = "SUPLA-ESP";
static const char *NVS_STORAGE = "supla_nvs";

static char* btox(char *hex, const char *bb, int len) 
{
	const char xx[]= "0123456789ABCDEF";
	for(int i=len; i >= 0; --i)
		hex[i] = xx[(bb[i>>1] >> ((1 - (i&1)) << 2)) & 0x0F];
	hex[len]=0x00;
	return hex;
}

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
			ESP_LOG_BUFFER_HEX(TAG,supla_conf->auth_key,SUPLA_AUTHKEY_SIZE);
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
			ESP_LOG_BUFFER_HEX(TAG,supla_conf->guid,SUPLA_GUID_SIZE);
			nvs_set_blob(nvs,"guid",supla_conf->guid,SUPLA_GUID_SIZE);
			nvs_commit(nvs);
			nvs_close(nvs);
		}else{
			ESP_LOGE(TAG, "nvs open error %s",esp_err_to_name(rc));
			return rc;
		}
	}
	return ESP_OK;
}

esp_err_t supla_esp_nvs_config_write(struct supla_config *supla_conf)
{
	nvs_handle nvs;
	esp_err_t rc;

	rc = nvs_open(NVS_STORAGE,NVS_READWRITE,&nvs);
	if(rc == ESP_OK){
		nvs_set_str(nvs,"email",supla_conf->email);
		nvs_set_blob(nvs,"auth_key",supla_conf->auth_key,SUPLA_AUTHKEY_SIZE);
		nvs_set_blob(nvs,"guid",supla_conf->guid,SUPLA_GUID_SIZE);
		nvs_set_str(nvs,"server",supla_conf->server);
		nvs_set_i8(nvs,"ssl",supla_conf->ssl);
		nvs_set_i32(nvs,"port",supla_conf->port);
		nvs_set_i32(nvs,"activity_timeout",supla_conf->activity_timeout);
		nvs_commit(nvs);
		nvs_close(nvs);
	} else {
		ESP_LOGW(TAG, "nvs open error %s",esp_err_to_name(rc));
		return rc;
	}
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

esp_err_t supla_esp_generate_hostname(const supla_dev_t *dev, char* buf, size_t len)
{
	uint8_t mac[6];
	const char *dev_name;

	if(!dev)
		return ESP_ERR_INVALID_ARG;

	dev_name = supla_dev_get_name(dev);

	esp_efuse_mac_get_default(mac);
	if(strlen(dev_name) + 5 > len)
		return ESP_ERR_INVALID_SIZE;

	snprintf(buf,len,"%s-%02X%02X",dev_name,mac[4],mac[5]);
	return ESP_OK;
}

esp_err_t supla_esp_set_hostname(const supla_dev_t *dev, tcpip_adapter_if_t tcpip_if)
{

	char hostname[32];
	esp_err_t rc;

	if(!dev)
		return ESP_ERR_INVALID_ARG;

	rc = supla_esp_generate_hostname(dev,hostname,sizeof(hostname));
	if(rc != ESP_OK)
		return rc;
#if LIBSUPLA_ARCH == LIBSUPLA_ARCH_ESP32
	switch (tcpip_if) {
	case TCPIP_ADAPTER_IF_AP:{
		esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
		rc = esp_netif_set_hostname(ap_netif, hostname);
		}break;
	case TCPIP_ADAPTER_IF_STA:{
		esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
		rc = esp_netif_set_hostname(sta_netif, hostname);
		}break;
	default:
		break;
	}
#else
	rc = tcpip_adapter_set_hostname(tcpip_if,hostname);
#endif	
	if(rc != ESP_OK)
		return rc;

	ESP_LOGI(TAG, "device hostname set: %s",hostname);
	return ESP_OK;
}

esp_err_t supla_esp_init_mdns(const supla_dev_t *dev)
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

static esp_err_t send_json_response(cJSON *js, httpd_req_t *req)
{
	char *js_txt = cJSON_Print(js);
	cJSON_Delete(js);

	httpd_resp_set_type(req,HTTPD_TYPE_JSON);
	httpd_resp_send(req, js_txt, -1);
	free(js_txt);
	return ESP_OK;
}

static cJSON *json_error(int code, const char *title)
{
	cJSON* js_err = cJSON_CreateObject();
	cJSON_AddNumberToObject(js_err,"code",code);
	cJSON_AddItemToObject(js_err,"title",cJSON_CreateString(title));
	return js_err;
}

static cJSON *supla_config_to_json(struct supla_config *supla_conf)
{
	char guid_hex[SUPLA_GUID_HEXSIZE];
	char auth_hex[SUPLA_AUTHKEY_HEXSIZE];
	cJSON *js;

	if(!supla_conf)
		return NULL;

	js = cJSON_CreateObject();
	cJSON_AddStringToObject(js,"email",supla_conf->email);
	cJSON_AddStringToObject(js,"server",supla_conf->server);
	cJSON_AddStringToObject(js,"guid",btox(guid_hex,supla_conf->guid,sizeof(supla_conf->guid)));
	cJSON_AddStringToObject(js,"auth_key",btox(auth_hex,supla_conf->auth_key,sizeof(supla_conf->auth_key)));
	cJSON_AddBoolToObject(js,"ssl",supla_conf->ssl);
	cJSON_AddNumberToObject(js,"port",supla_conf->port);
	cJSON_AddNumberToObject(js,"activity_timeout",supla_conf->activity_timeout);
	return js;
}

esp_err_t supla_config_httpd_handler(httpd_req_t *req)
{
	cJSON *js;
	char *url_query;
	size_t qlen;
	char *req_data;
	char value[128];
	char save_config = 0;
	int bytes_recv = 0;
	int rc;

	struct supla_config *supla_config = req->user_ctx;

	if(!supla_config){
		js = cJSON_CreateObject();
		cJSON_AddItemToObject(js,"error",json_error(ESP_ERR_NOT_FOUND,"SUPLA config not found"));
		return send_json_response(js,req);
	}

	qlen = httpd_req_get_url_query_len(req) + 1;
	if (qlen > 1) {
		url_query = malloc(qlen);
		if (httpd_req_get_url_query_str(req, url_query, qlen) == ESP_OK) {
			if (httpd_query_key_value(url_query, "action", value, sizeof(value)) == ESP_OK) {

				if(!strcmp(value,"erase")){
					supla_esp_nvs_config_erase();

				} else if(!strcmp(value,"save")){
					save_config = true;
				}
			}
		}
		free(url_query);
	}

	if(req->content_len){
		req_data = calloc(1,req->content_len+1);
		if(!req_data)
			return ESP_ERR_NO_MEM;

		for(int bytes_left=req->content_len; bytes_left > 0; ) {
			if ((rc = httpd_req_recv(req, req_data+bytes_recv, bytes_left)) <= 0) {
				if (rc == HTTPD_SOCK_ERR_TIMEOUT)
					continue;
				else
					return ESP_FAIL;
			}
			bytes_recv += rc;
			bytes_left -= rc;
		}

		if(httpd_query_key_value(req_data,"email",value,sizeof(value)) == ESP_OK)
			strncpy(supla_config->email,value,sizeof(supla_config->email));

		if(httpd_query_key_value(req_data,"server",value,sizeof(value)) == ESP_OK)
			strncpy(supla_config->server,value,sizeof(supla_config->server));

		supla_config->ssl = 0;
		if(httpd_query_key_value(req_data,"ssl",value,sizeof(value)) == ESP_OK)
			supla_config->ssl = !strcmp("on",value);

		if(httpd_query_key_value(req_data,"port",value,sizeof(value)) == ESP_OK)
			supla_config->port = atoi(value);

		if(httpd_query_key_value(req_data,"activity_timeout",value,sizeof(value)) == ESP_OK)
			supla_config->activity_timeout = atoi(value);

		free(req_data);
	}

	if(save_config){
		rc = supla_esp_nvs_config_write(supla_config);
		if(rc == 0)
			ESP_LOGI(TAG,"SUPLA nvs config write OK");
		else
			ESP_LOGE(TAG,"SUPLA nvs config write ERR:%s(%d)", esp_err_to_name(rc),rc);
	}

	js = cJSON_CreateObject();
	cJSON_AddItemToObject(js,"data",supla_config_to_json(supla_config));
	return send_json_response(js,req);
}

static cJSON *supla_dev_state_to_json(supla_dev_t *dev)
{
	cJSON *js;
	supla_dev_state_t state;
	struct supla_config config;
	time_t uptime;
	time_t conn_uptime;

	supla_dev_get_state(dev,&state);
	supla_dev_get_config(dev,&config);
	supla_dev_get_uptime(dev,&uptime);
	supla_dev_get_connection_uptime(dev,&conn_uptime);

	js = cJSON_CreateObject();
	cJSON_AddStringToObject(js,"name",supla_dev_get_name(dev));
	cJSON_AddStringToObject(js,"software_ver",supla_dev_get_software_version(dev));
	cJSON_AddStringToObject(js,"state",supla_dev_state_str(state));
	cJSON_AddItemToObject(js,"config",supla_config_to_json(&config));
	cJSON_AddNumberToObject(js,"uptime",uptime);
	cJSON_AddNumberToObject(js,"connection_uptime",conn_uptime);
	return js;
}

esp_err_t supla_dev_httpd_handler(httpd_req_t *req)
{
	cJSON *js;
	supla_dev_t *dev = *(supla_dev_t **)req->user_ctx;
	if(!dev){
		js = cJSON_CreateObject();
		cJSON_AddItemToObject(js,"error",json_error(ESP_ERR_NOT_FOUND,"SUPLA dev not found"));
	} else {
		js = cJSON_CreateObject();
		cJSON_AddItemToObject(js,"data",supla_dev_state_to_json(dev));
	}
	return send_json_response(js,req);
}



