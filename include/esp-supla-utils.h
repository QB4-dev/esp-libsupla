/*
 * Copyright (c) 2022 <qb4.dev@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ESP_SUPLA_UTILS_H_
#define ESP_SUPLA_UTILS_H_

#include <libsupla/device.h>

int supla_esp_nvs_config_init(struct supla_config *supla_conf);
int supla_esp_nvs_config_erase(void);

int supla_esp_get_wifi_state(TDSC_ChannelState *state);

int supla_esp_server_time_sync(supla_dev_t *dev, TSDC_UserLocalTimeResult *lt);

#endif /* ESP_SUPLA_UTILS_H_ */
