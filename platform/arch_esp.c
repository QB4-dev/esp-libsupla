/*
 * Copyright (c) 2022 <qb4.dev@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "port/util.h"
#include "port/net.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <esp_log.h>

#ifdef CONFIG_ESP_LIBSUPLA_USE_ESP_TLS
#include <esp_tls.h>

#ifndef CONFIG_IDF_TARGET_ESP32
// delete name variant is deprecated in ESP-IDF, however ESP8266 RTOS still
// use it.
#define esp_tls_conn_destroy esp_tls_conn_delete

// Latest ESP-IDF moved definition of esp_tls_t to private section and added
// methods to access members. This change is missing in esp8266, so below
// method is added to keep the same functionality
void esp_tls_get_error_handle(esp_tls_t *client, esp_tls_error_handle_t *errorHandle)
{
    *errorHandle = client->error_handle;
}
#endif

extern const uint8_t server_cert_pem_start[] asm("_binary_supla_org_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_supla_org_cert_pem_end");
#endif

static const char *TAG = "SUPLA";

uint64_t supla_time_getmonotonictime_milliseconds(void)
{
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    return (uint64_t)((current_time.tv_sec * 1000) + (current_time.tv_nsec / 1000000));
}

#ifdef CONFIG_ESP_LIBSUPLA_USE_ESP_TLS

int supla_cloud_connect(supla_link_t *link, const char *host, int port, unsigned char ssl)
{
    esp_tls_cfg_t cfg = {
        .cacert_pem_buf = server_cert_pem_start,
        .cacert_pem_bytes = server_cert_pem_end - server_cert_pem_start,
        .non_block = true,
        .timeout_ms = 10000 //
    };

    esp_tls_error_handle_t esp_tls_errh;
    int sockfd = 0;
    struct esp_tls *tls = esp_tls_init();

    if (tls != NULL) {
        *link = tls;
    } else {
        *link = NULL;
        return SUPLA_RESULT_FALSE;
    }

    int rc = esp_tls_conn_new_sync(host, strlen(host), port, ssl ? &cfg : NULL, tls);
    if (rc == 1) {
        if (esp_tls_get_conn_sockfd(tls, &sockfd) == ESP_OK)
            fcntl(sockfd, F_SETFL, O_NONBLOCK);

        return SUPLA_RESULT_TRUE;
    } else {
        esp_tls_get_error_handle(tls, &esp_tls_errh);

        ESP_LOGE(TAG, "esp_tls_conn_new_sync failed. Last errors: 0x%x 0x%x 0x%x",
                 esp_tls_errh->last_error, esp_tls_errh->esp_tls_error_code,
                 esp_tls_errh->esp_tls_flags);
        return SUPLA_RESULT_FALSE;
    }
}

int supla_cloud_send(supla_link_t link, void *buf, int count)
{
    return esp_tls_conn_write(link, buf, count);
}

int supla_cloud_recv(supla_link_t link, void *buf, int count)
{
    return esp_tls_conn_read(link, buf, count);
}

int supla_cloud_disconnect(supla_link_t *link)
{
    esp_tls_conn_destroy(*link);
    return 0;
}

#else

typedef struct {
    int sfd;
} link_data_t;

int supla_cloud_connect(supla_link_t *link, const char *host, int port, unsigned char ssl)
{
    struct addrinfo hints;
    struct addrinfo *result, *rp = NULL;
    int rc;

    if (ssl)
        ESP_LOGW(TAG, "esp_tls support is disabled, cannot create secure connection");

    link_data_t *ssd = calloc(1, sizeof(link_data_t));
    if (ssd) {
        *link = ssd;
        ssd->sfd = -1;
    } else {
        *link = NULL;
        return SUPLA_RESULT_FALSE;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;

    // Address resolution.
    rc = getaddrinfo(host, NULL, &hints, &result);
    if (rc != 0) {
        free(ssd);
        return rc;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        ssd->sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (ssd->sfd == -1)
            continue;

        switch (rp->ai_family) {
#if LWIP_IPV6
        case AF_INET6:
            ((struct sockaddr_in6 *)(rp->ai_addr))->sin6_port = htons(port);
            break;
#endif
        case AF_INET:
            ((struct sockaddr_in *)(rp->ai_addr))->sin_port = htons(port);
            break;
        default:
            free(ssd);
            return SUPLA_RESULT_FALSE;
        }

        // Attempt to connect.
        rc = connect(ssd->sfd, rp->ai_addr, rp->ai_addrlen);
        if (rc != -1) {
            freeaddrinfo(result);
            return SUPLA_RESULT_TRUE;
        } else {
            if (errno == EINPROGRESS) {
                freeaddrinfo(result);
                return SUPLA_RESULT_TRUE;
            } else {
                close(ssd->sfd);
            }
        }
    }
    freeaddrinfo(result);
    free(ssd);
    return SUPLA_RESULT_FALSE;
}

int supla_cloud_send(supla_link_t link, void *buf, int count)
{
    link_data_t *ssd = link;
    return send(ssd->sfd, buf, count, MSG_NOSIGNAL);
}

int supla_cloud_recv(supla_link_t link, void *buf, int count)
{
    link_data_t *ssd = link;
    return recv(ssd->sfd, buf, count, MSG_DONTWAIT);
}

int supla_cloud_disconnect(supla_link_t *link)
{
    if (!link)
        return EINVAL;

    link_data_t *ssd = *link;
    if (!ssd)
        return EINVAL;

    if (ssd->sfd != -1) {
        shutdown(ssd->sfd, SHUT_RDWR);
        close(ssd->sfd);
    }
    free(ssd);
    return 0;
}

#endif /* CONFIG_ESP_LIBSUPLA_USE_ESP_TLS */
