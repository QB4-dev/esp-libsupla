/*
 * wifi.h
 *
 *  Created on: 8 wrz 2022
 *      Author: kuba
 */

#ifndef MAIN_WIFI_H_
#define MAIN_WIFI_H_

#include <esp-supla-utils.h>

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

void wifi_init(void);
void wifi_init_sta(const supla_dev_t *dev);

#endif /* MAIN_WIFI_H_ */
