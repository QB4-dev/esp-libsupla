#ifndef WEBSERVER_H_
#define WEBSERVER_H_

#include <esp_err.h>

#include "esp-supla.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t webserver_init(supla_dev_t **dev);
void webserver_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // WEBSERVER_H_