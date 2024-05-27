/*
 * Copyright (c) 2022 <qb4.dev@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "../include/esp-supla.h"

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

#if CONFIG_IDF_TARGET_ESP32
#include <esp_random.h>
#include <esp_mac.h>
#endif

static const char *TAG = "ESP-SUPLA";
static const char *NVS_STORAGE = "supla_nvs";

#define CHECK_ARG(VAL)                  \
    do {                                \
        if (!(VAL))                     \
            return ESP_ERR_INVALID_ARG; \
    } while (0)

static char *btox(char *hex, const char *bb, int len)
{
    const char xx[] = "0123456789ABCDEF";
    for (int i = len; i >= 0; --i)
        hex[i] = xx[(bb[i >> 1] >> ((1 - (i & 1)) << 2)) & 0x0F];
    hex[len] = 0x00;
    return hex;
}

esp_err_t supla_esp_nvs_config_init(struct supla_config *supla_conf)
{
    CHECK_ARG(supla_conf);
    size_t required_size;
    nvs_handle nvs;
    esp_err_t rc;
    const char empty_auth[SUPLA_AUTHKEY_SIZE] = { 0 };
    const char empty_guid[SUPLA_GUID_SIZE] = { 0 };

    nvs_flash_init();
    ESP_LOGI(TAG, "NVS config init");

    rc = nvs_open(NVS_STORAGE, NVS_READONLY, &nvs);
    if (rc == ESP_OK) {
        nvs_get_str(nvs, "email", supla_conf->email, &required_size);
        nvs_get_blob(nvs, "auth_key", supla_conf->auth_key, &required_size);
        nvs_get_blob(nvs, "guid", supla_conf->guid, &required_size);
        nvs_get_str(nvs, "server", supla_conf->server, &required_size);
        nvs_get_i8(nvs, "ssl", (int8_t *)&supla_conf->ssl);
        nvs_get_i32(nvs, "port", (int32_t *)&supla_conf->port);
        nvs_get_i32(nvs, "activity_timeout", (int32_t *)&supla_conf->activity_timeout);
        nvs_close(nvs);
    } else {
        ESP_LOGW(TAG, "nvs open error %s", esp_err_to_name(rc));
    }

    if (!memcmp(supla_conf->auth_key, empty_auth, SUPLA_AUTHKEY_SIZE)) {
        ESP_LOGW(TAG, "AUTHKEY not set, generate now...");
        rc = nvs_open(NVS_STORAGE, NVS_READWRITE, &nvs);
        if (rc == ESP_OK) {
            esp_fill_random(supla_conf->auth_key, SUPLA_AUTHKEY_SIZE);
            ESP_LOGI(TAG, "generated AUTHKEY");
            ESP_LOG_BUFFER_HEX(TAG, supla_conf->auth_key, SUPLA_AUTHKEY_SIZE);
            nvs_set_blob(nvs, "auth_key", supla_conf->auth_key, SUPLA_AUTHKEY_SIZE);
            nvs_commit(nvs);
            nvs_close(nvs);
        } else {
            ESP_LOGE(TAG, "nvs open error %s", esp_err_to_name(rc));
        }
    }

    if (!memcmp(supla_conf->guid, empty_guid, SUPLA_GUID_SIZE)) {
        ESP_LOGW(TAG, "GUID not set, generate now...");
        rc = nvs_open(NVS_STORAGE, NVS_READWRITE, &nvs);
        if (rc == ESP_OK) {
            esp_fill_random(supla_conf->guid, SUPLA_GUID_SIZE);
            ESP_LOGI(TAG, "generated GUID");
            ESP_LOG_BUFFER_HEX(TAG, supla_conf->guid, SUPLA_GUID_SIZE);
            nvs_set_blob(nvs, "guid", supla_conf->guid, SUPLA_GUID_SIZE);
            nvs_commit(nvs);
            nvs_close(nvs);
        } else {
            ESP_LOGE(TAG, "nvs open error %s", esp_err_to_name(rc));
            return rc;
        }
    }
    return ESP_OK;
}

esp_err_t supla_esp_nvs_config_write(struct supla_config *supla_conf)
{
    CHECK_ARG(supla_conf);
    nvs_handle nvs;
    esp_err_t rc;

    rc = nvs_open(NVS_STORAGE, NVS_READWRITE, &nvs);
    if (rc == ESP_OK) {
        nvs_set_str(nvs, "email", supla_conf->email);
        nvs_set_blob(nvs, "auth_key", supla_conf->auth_key, SUPLA_AUTHKEY_SIZE);
        nvs_set_blob(nvs, "guid", supla_conf->guid, SUPLA_GUID_SIZE);
        nvs_set_str(nvs, "server", supla_conf->server);
        nvs_set_i8(nvs, "ssl", supla_conf->ssl);
        nvs_set_i32(nvs, "port", supla_conf->port);
        nvs_set_i32(nvs, "activity_timeout", supla_conf->activity_timeout);
        nvs_commit(nvs);
        nvs_close(nvs);
    } else {
        ESP_LOGW(TAG, "nvs open error %s", esp_err_to_name(rc));
        return rc;
    }
    return ESP_OK;
}

esp_err_t supla_esp_nvs_config_erase(void)
{
    nvs_handle nvs;
    esp_err_t rc;
    rc = nvs_open(NVS_STORAGE, NVS_READWRITE, &nvs);
    if (rc == ESP_OK) {
        nvs_erase_all(nvs);
        ESP_LOGI(TAG, "nvs erased");
    } else {
        ESP_LOGE(TAG, "nvs open error %s", esp_err_to_name(rc));
    }
    return rc;
}

esp_err_t supla_esp_generate_hostname(const supla_dev_t *dev, char *buf, size_t len)
{
    CHECK_ARG(dev);
    CHECK_ARG(buf);

    char name[SUPLA_DEVICE_NAME_MAXSIZE];
    uint8_t mac[6];

    supla_dev_get_name(dev, name, sizeof(name));

    esp_efuse_mac_get_default(mac);
    if (strlen(name) + 5 > len)
        return ESP_ERR_INVALID_SIZE;

    //SSID must start with "SUPLA-" to use APP wizard
    snprintf(buf, len, "SUPLA-%s-%02X%02X", name, mac[4], mac[5]);
    return ESP_OK;
}

esp_err_t supla_esp_get_wifi_state(supla_dev_t *dev, TDSC_ChannelState *state)
{
    CHECK_ARG(dev);
    CHECK_ARG(state);
    wifi_ap_record_t wifi_info = { 0 };

    if (esp_efuse_mac_get_default(state->MAC) == ESP_OK)
        state->Fields |= SUPLA_CHANNELSTATE_FIELD_MAC;

    if (esp_wifi_sta_get_ap_info(&wifi_info) == ESP_OK) {
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

#if CONFIG_IDF_TARGET_ESP8266
    tcpip_adapter_ip_info_t ip_info = { 0 };
    if (tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info) == ESP_OK) {
        state->Fields |= SUPLA_CHANNELSTATE_FIELD_IPV4;
        state->IPv4 = ip_info.ip.addr;
    }
#elif CONFIG_IDF_TARGET_ESP32
    //esp_netif_ip_info_t ip_info = {};

#endif

    return ESP_OK;
}

int supla_esp_server_time_sync(supla_dev_t *dev, TSDC_UserLocalTimeResult *lt)
{
    CHECK_ARG(dev);
    CHECK_ARG(lt);
    struct tm tm;
    struct timeval timeval;

    tm.tm_year = lt->year - 1900;
    tm.tm_mon = lt->month - 1;
    tm.tm_mday = lt->day;

    tm.tm_hour = lt->hour;
    tm.tm_min = lt->min;
    tm.tm_sec = lt->sec;
    tm.tm_isdst = -1; //use timezone data to determine if DST is used

    timeval.tv_sec = mktime(&tm);
    timeval.tv_usec = 0;

    return settimeofday(&timeval, NULL);
}

static esp_err_t send_json_response(cJSON *js, httpd_req_t *req)
{
    CHECK_ARG(js);
    CHECK_ARG(req);

    char *js_txt = cJSON_Print(js);
    cJSON_Delete(js);

    httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    httpd_resp_send(req, js_txt, -1);
    free(js_txt);
    return ESP_OK;
}

static cJSON *json_error(int code, const char *title)
{
    cJSON *js_err = cJSON_CreateObject();
    cJSON_AddNumberToObject(js_err, "code", code);
    cJSON_AddItemToObject(js_err, "title", cJSON_CreateString(title));
    return js_err;
}

static cJSON *supla_dev_state_to_json(supla_dev_t *dev)
{
    cJSON *js;
    supla_dev_state_t state;
    char guid_hex[SUPLA_GUID_HEXSIZE];
    struct supla_config config;
    time_t uptime;
    time_t conn_uptime;
    char name[SUPLA_DEVICE_NAME_MAXSIZE];
    char soft_ver[SUPLA_DEVICE_NAME_MAXSIZE];

    if (!dev)
        return NULL;

    supla_dev_get_name(dev, name, sizeof(name));
    supla_dev_get_software_version(dev, soft_ver, sizeof(soft_ver));

    supla_dev_get_state(dev, &state);
    supla_dev_get_config(dev, &config);
    supla_dev_get_uptime(dev, &uptime);
    supla_dev_get_connection_uptime(dev, &conn_uptime);

    js = cJSON_CreateObject();
    cJSON_AddStringToObject(js, "name", name);
    cJSON_AddStringToObject(js, "software_ver", soft_ver);
    cJSON_AddStringToObject(js, "guid", btox(guid_hex, config.guid, sizeof(config.guid)));
    cJSON_AddStringToObject(js, "state", supla_dev_state_str(state));
    cJSON_AddNumberToObject(js, "uptime", (int)uptime);
    cJSON_AddNumberToObject(js, "connection_uptime", (int)conn_uptime);
    return js;
}

static cJSON *supla_dev_config_to_json(supla_dev_t *dev)
{
    cJSON *js;
    struct supla_config config;
    char guid_hex[SUPLA_GUID_HEXSIZE];
    char auth_hex[SUPLA_AUTHKEY_HEXSIZE];

    if (!dev)
        return NULL;

    supla_dev_get_config(dev, &config);

    js = cJSON_CreateObject();
    cJSON_AddStringToObject(js, "email", config.email);
    cJSON_AddStringToObject(js, "server", config.server);
    cJSON_AddStringToObject(js, "guid", btox(guid_hex, config.guid, sizeof(config.guid)));
    cJSON_AddStringToObject(js, "auth_key",
                            btox(auth_hex, config.auth_key, sizeof(config.auth_key)));
    cJSON_AddBoolToObject(js, "ssl", config.ssl);
    cJSON_AddNumberToObject(js, "port", config.port);
    cJSON_AddNumberToObject(js, "activity_timeout", config.activity_timeout);
    return js;
}

static esp_err_t supla_dev_post_config(supla_dev_t *dev, httpd_req_t *req)
{
    struct supla_config config;
    char *req_data;
    char value[128];
    int bytes_recv = 0;
    int rc;

    supla_dev_get_config(dev, &config);
    if (req->content_len) {
        req_data = calloc(1, req->content_len + 1);
        if (!req_data)
            return ESP_ERR_NO_MEM;

        for (int bytes_left = req->content_len; bytes_left > 0;) {
            if ((rc = httpd_req_recv(req, req_data + bytes_recv, bytes_left)) <= 0) {
                if (rc == HTTPD_SOCK_ERR_TIMEOUT)
                    continue;
                else
                    return ESP_FAIL;
            }
            bytes_recv += rc;
            bytes_left -= rc;
        }

        if (httpd_query_key_value(req_data, "email", value, sizeof(value)) == ESP_OK)
            strncpy(config.email, value, sizeof(config.email));

        if (httpd_query_key_value(req_data, "server", value, sizeof(value)) == ESP_OK)
            strncpy(config.server, value, sizeof(config.server));

        config.ssl = 0;
        if (httpd_query_key_value(req_data, "ssl", value, sizeof(value)) == ESP_OK)
            config.ssl = !strcmp("on", value);

        if (httpd_query_key_value(req_data, "port", value, sizeof(value)) == ESP_OK)
            config.port = atoi(value);

        if (httpd_query_key_value(req_data, "activity_timeout", value, sizeof(value)) == ESP_OK)
            config.activity_timeout = atoi(value);

        free(req_data);
    }

    rc = supla_esp_nvs_config_write(&config);
    if (rc == 0) {
        ESP_LOGI(TAG, "nvs write OK");
        supla_dev_stop(dev);
        supla_dev_set_config(dev, &config);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "nvs write ERR:%s(%d)", esp_err_to_name(rc), rc);
        return rc;
    }
}

static esp_err_t supla_dev_erase_config(supla_dev_t *dev)
{
    struct supla_config config = { 0 };
    int rc;

    rc = supla_esp_nvs_config_erase();
    if (rc != ESP_OK)
        return rc;

    rc = supla_esp_nvs_config_init(&config);
    if (rc != ESP_OK)
        return rc;

    supla_dev_stop(dev);
    supla_dev_set_config(dev, &config);
    supla_dev_start(dev);
    return ESP_OK;
}

esp_err_t supla_dev_httpd_handler(httpd_req_t *req)
{
    CHECK_ARG(req);
    cJSON *js;
    char *url_query;
    size_t qlen;
    char value[128];
    supla_dev_t *dev;

    js = cJSON_CreateObject();
    if (!req->user_ctx) {
        cJSON_AddItemToObject(js, "error",
                              json_error(ESP_ERR_NOT_FOUND, esp_err_to_name(ESP_ERR_NOT_FOUND)));
        return send_json_response(js, req);
    }

    dev = *(supla_dev_t **)req->user_ctx;
    //parse URL query
    qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen > 1) {
        url_query = malloc(qlen);
        if (httpd_req_get_url_query_str(req, url_query, qlen) == ESP_OK) {
            if (httpd_query_key_value(url_query, "action", value, sizeof(value)) == ESP_OK) {
                if (!strcmp(value, "get_config")) {
                    cJSON_AddItemToObject(js, "data", supla_dev_config_to_json(dev));
                } else if (!strcmp(value, "set_config")) {
                    supla_dev_post_config(dev, req);
                    cJSON_AddItemToObject(js, "data", supla_dev_config_to_json(dev));
                } else if (!strcmp(value, "erase_config")) {
                    supla_dev_erase_config(dev);
                    cJSON_AddItemToObject(js, "data", supla_dev_config_to_json(dev));
                }
            }
        }
        free(url_query);
    } else {
        cJSON_AddItemToObject(js, "data", supla_dev_state_to_json(dev));
    }
    return send_json_response(js, req);
}
