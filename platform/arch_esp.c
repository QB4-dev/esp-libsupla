/*
 * Copyright (c) 2022 <qb4.dev@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "port/util.h"
#include "port/net.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <esp_log.h>

#ifdef CONFIG_ESP_LIBSUPLA_USE_ESP_TLS
#include <esp_tls.h>

#ifdef CONFIG_IDF_TARGET_ESP8266
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

typedef struct {
    struct esp_tls *tls;
    int sockfd;
} tls_link_t;
#endif

static const char *TAG = "SUPLA-LINK";

uint64_t supla_time_getmonotonictime_milliseconds(void)
{
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    return (uint64_t)((current_time.tv_sec * 1000) + (current_time.tv_nsec / 1000000));
}

#ifdef CONFIG_ESP_LIBSUPLA_USE_ESP_TLS

static void set_keepalive(int sockfd)
{
    int keepalive = 1;
    int idle = 60;
    int interval = 10;
    int count = 3;

    setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
    setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));
}

int supla_cloud_connect(supla_link_t *link, const char *host, int port, unsigned char ssl)
{
    if (!link || !host)
        return SUPLA_RESULT_FALSE;

    tls_link_t *ctx = calloc(1, sizeof(tls_link_t));
    if (!ctx)
        return SUPLA_RESULT_FALSE;

    ctx->sockfd = -1;
    if (ssl) {
        ctx->tls = esp_tls_init();
        if (!ctx->tls) {
            free(ctx);
            return SUPLA_RESULT_FALSE;
        }

        esp_tls_cfg_t cfg = { 0 };
        cfg.cacert_buf = server_cert_pem_start;
        cfg.cacert_bytes = server_cert_pem_end - server_cert_pem_start;
        cfg.non_block = false;
        cfg.timeout_ms = 10000;

        int rc = esp_tls_conn_new_sync(host, strlen(host), port, &cfg, ctx->tls);
        if (rc != 1) {
            ESP_LOGE(TAG, "Connection failed: %s:%d (ssl=%d)", host, port, ssl);
            esp_tls_conn_destroy(ctx->tls);
            free(ctx);
            return SUPLA_RESULT_FALSE;
        }

        if (esp_tls_get_conn_sockfd(ctx->tls, &ctx->sockfd) == ESP_OK && ctx->sockfd >= 0)
            fcntl(ctx->sockfd, F_SETFL, O_NONBLOCK);

        if (ctx->sockfd >= 0)
            set_keepalive(ctx->sockfd);
    } else {
        struct addrinfo hints;
        struct addrinfo *result, *rp = NULL;

        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_NUMERICSERV;

        int rc = getaddrinfo(host, NULL, &hints, &result);
        if (rc != 0) {
            free(ctx);
            return SUPLA_RESULT_FALSE;
        }

        for (rp = result; rp != NULL; rp = rp->ai_next) {
            ctx->sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (ctx->sockfd == -1)
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
                close(ctx->sockfd);
                ctx->sockfd = -1;
                continue;
            }

            rc = connect(ctx->sockfd, rp->ai_addr, rp->ai_addrlen);
            if (rc != -1 || errno == EINPROGRESS) {
                fcntl(ctx->sockfd, F_SETFL, O_NONBLOCK);
                set_keepalive(ctx->sockfd);
                break;
            }

            close(ctx->sockfd);
            ctx->sockfd = -1;
        }

        freeaddrinfo(result);

        if (ctx->sockfd == -1) {
            free(ctx);
            return SUPLA_RESULT_FALSE;
        }
    }

    *link = (supla_link_t)ctx;
    return SUPLA_RESULT_TRUE;
}

int supla_cloud_send(supla_link_t link, void *buf, int count)
{
    if (!link || !buf || count <= 0)
        return SUPLA_RESULT_FALSE;

    tls_link_t *ctx = (tls_link_t *)link;

    if (ctx->tls) {
        int ret = esp_tls_conn_write(ctx->tls, (const unsigned char *)buf, count);
        if (ret == ESP_TLS_ERR_SSL_WANT_READ || ret == ESP_TLS_ERR_SSL_WANT_WRITE)
            return -1;

        if (ret == 0)
            return SUPLA_RESULT_FALSE;

        return ret;
    }

    return send(ctx->sockfd, buf, count, MSG_NOSIGNAL);
}

int supla_cloud_recv(supla_link_t link, void *buf, int count)
{
    if (!link || !buf || count <= 0)
        return SUPLA_RESULT_FALSE;

    tls_link_t *ctx = (tls_link_t *)link;

    if (ctx->tls) {
        int ret = esp_tls_conn_read(ctx->tls, buf, count);
        if (ret == ESP_TLS_ERR_SSL_WANT_READ || ret == ESP_TLS_ERR_SSL_WANT_WRITE)
            return -1;

        if (ret < 0) {
            ESP_LOGE(TAG, "Read error: %d", ret);
            return -1;
        }

        if (ret == 0)
            ESP_LOGW(TAG, "Connection closed by peer");

        return ret;
    }

    return recv(ctx->sockfd, buf, count, MSG_DONTWAIT);
}

int supla_cloud_disconnect(supla_link_t *link)
{
    if (!link || !*link)
        return SUPLA_RESULT_FALSE;

    tls_link_t *ctx = (tls_link_t *)(*link);

    if (ctx->tls) {
        esp_tls_conn_destroy(ctx->tls);
    } else if (ctx->sockfd != -1) {
        shutdown(ctx->sockfd, SHUT_RDWR);
        close(ctx->sockfd);
    }

    free(ctx);
    *link = NULL;

    ESP_LOGI(TAG, "Disconnected");
    return SUPLA_RESULT_TRUE;
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

    if (ssl) {
        port = (port != 2016) ? port : 2015; //switch to default unencrypted port
        ESP_LOGW(TAG, "esp_tls support is disabled, switching to port=%d", port);
    }

    link_data_t *ssd = calloc(1, sizeof(link_data_t));
    if (ssd) {
        ssd->sfd = -1;
    } else {
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
            *link = ssd;
            return SUPLA_RESULT_TRUE;
        } else {
            if (errno == EINPROGRESS) {
                freeaddrinfo(result);
                *link = ssd;
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
    if (!link)
        return -1;
    return send(ssd->sfd, buf, count, MSG_NOSIGNAL);
}

int supla_cloud_recv(supla_link_t link, void *buf, int count)
{
    link_data_t *ssd = link;
    if (!link)
        return -1;
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
    ssd = NULL;
    return 0;
}

#endif /* CONFIG_ESP_LIBSUPLA_USE_ESP_TLS */
