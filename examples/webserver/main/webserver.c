#include "webserver.h"

#include <string.h>
#include <inttypes.h>

#include <esp_log.h>
#include <esp_system.h>
#include <esp_http_server.h>

static const char *TAG = "WEBSRV";
static httpd_handle_t server = NULL;

static httpd_uri_t basic_get_handler = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = supla_dev_basic_httpd_handler //
};

static httpd_uri_t basic_post_handler = {
    .uri = "/",
    .method = HTTP_POST,
    .handler = supla_dev_basic_httpd_handler //
};

esp_err_t webserver_init(supla_dev_t **dev)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.lru_purge_enable = true;
    config.stack_size = 4 * 4096;

    ESP_LOGI(TAG, "initializing");

    /* start HTTP server */
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    basic_get_handler.user_ctx = dev;
    basic_post_handler.user_ctx = dev;

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &basic_get_handler));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &basic_post_handler));

    ESP_LOGI(TAG, "server started on port %d, free mem: %" PRIu32 " bytes", config.server_port,
             esp_get_free_heap_size());
    return ESP_OK;
}

void webserver_deinit(void)
{
    if (server) {
        ESP_LOGI(TAG, "Stopping HTTP server");
        httpd_stop(server);
        server = NULL;
    }
}