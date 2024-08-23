/*
 * Copyright (c) 2022 <qb4.dev@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ESP_SUPLA_H_
#define ESP_SUPLA_H_

#include <libsupla/device.h>
#include <esp_http_server.h>
#include <esp_err.h>

/**
 * @brief Initialize SUPLA config in NVS memory. GUID and AUTHKEY
 * will be generated automatically if not set
 *
 * @param[in] supla_conf SUPLA connection config
 * @return
 *     - ESP_OK success
 */
esp_err_t supla_esp_nvs_config_init(struct supla_config *supla_conf);

/**
 * @brief Write SUPLA config to NVS memory
 *
 * @param[in] supla_conf SUPLA connection config
 * @return
 *     - ESP_OK success
 */
esp_err_t supla_esp_nvs_config_write(struct supla_config *supla_conf);

/**
 * @brief Erase SUPLA config from NVS memory
 *
 * @return
 *     - ESP_OK success
 */
esp_err_t supla_esp_nvs_config_erase(void);

/**
 * @brief generate SUPLA device hostname from device name and last two bytes
 * of MAC address like DEVNAME-XXXX

 * @param[in] dev SUPLA device instance
 * @param[out] buf output buffer
 * @param[out] len buffer length
 * @return
 *     - ESP_OK success
 *     - ESP_ERR_INVALID_ARG  no SUPLA device
 *     - ESP_ERR_INVALID_SIZE not enough space in buffer
 */
esp_err_t supla_esp_generate_hostname(const supla_dev_t *dev, char *buf, size_t len);

/**
 * @brief fill SUPLA channel state with wifi connection data
 * This function should be used by
 * supla_dev_set_common_channel_state_callback()
 *
 * @param[in] dev SUPLA device instance
 * @param[out] state common channel state
 * @return 0 on success
 */
int supla_esp_get_wifi_state(supla_dev_t *dev, TDSC_ChannelState *state);

/**
 * @brief set system time from SUPLA server
 * This function should be used by
 * supla_dev_set_server_time_sync_callback()
 *
 * @param[in] dev SUPLA device instance
 * @param[in] lt SUPLA server local time
 * @return settimeofday() result
 */
int supla_esp_server_time_sync(supla_dev_t *dev, TSDC_UserLocalTimeResult *lt);

//httpd device state handler GET/POST
//TODO documentation
esp_err_t supla_dev_httpd_handler(httpd_req_t *req);

esp_err_t supla_dev_basic_httpd_handler(httpd_req_t *req);

#endif /* ESP_SUPLA_H_ */
