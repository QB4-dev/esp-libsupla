/*
 * Copyright (c) 2022 <qb4.dev@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ESP_SUPLA_UTILS_H_
#define ESP_SUPLA_UTILS_H_

#include <libsupla/device.h>
#include <esp_err.h>


//TODO documentation
esp_err_t supla_esp_nvs_config_init(struct supla_config *supla_conf);

//TODO documentation
esp_err_t supla_esp_nvs_config_erase(void);

//TODO documentation
esp_err_t supla_esp_generate_hostname(supla_dev_t *dev, char* buf, size_t len);

//TODO documentation
esp_err_t supla_esp_init_mdns(supla_dev_t *dev);


//TODO config handlers GET/POST

//TODO documentation
int supla_esp_get_wifi_state(TDSC_ChannelState *state);

//TODO documentation
int supla_esp_server_time_sync(supla_dev_t *dev, TSDC_UserLocalTimeResult *lt);




//TODO set device name as hostname
//TODO start wifi_AP with devce name+mac

#endif /* ESP_SUPLA_UTILS_H_ */
