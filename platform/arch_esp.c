/*
 * Copyright (c) 2022 <qb4.dev@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "port/util.h"
#include "port/net.h"

#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
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
    int sockfd;
    uint8_t is_tls;
#ifdef CONFIG_ESP_LIBSUPLA_USE_ESP_TLS
    struct esp_tls *tls;
#endif
} link_ctx_t;
#endif

static const char *TAG = "SUPLA-LINK";

uint64_t supla_time_getmonotonictime_milliseconds(void)
{
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    return (uint64_t)((current_time.tv_sec * 1000) + (current_time.tv_nsec / 1000000));
}

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

static int link_write(link_ctx_t *ctx, const void *buf, int len)
{
#ifdef CONFIG_ESP_LIBSUPLA_USE_ESP_TLS
    if (ctx->is_tls) {
        int ret = esp_tls_conn_write(ctx->tls, buf, len);
        if (ret == ESP_TLS_ERR_SSL_WANT_READ || ret == ESP_TLS_ERR_SSL_WANT_WRITE)
            return -1;
        return ret;
    }
#endif
    return send(ctx->sockfd, buf, len, MSG_NOSIGNAL);
}

static int link_read(link_ctx_t *ctx, void *buf, int len)
{
#ifdef CONFIG_ESP_LIBSUPLA_USE_ESP_TLS
    if (ctx->is_tls) {
        int ret = esp_tls_conn_read(ctx->tls, buf, len);
        if (ret == ESP_TLS_ERR_SSL_WANT_READ || ret == ESP_TLS_ERR_SSL_WANT_WRITE)
            return -1;
        return ret;
    }
#endif
    return recv(ctx->sockfd, buf, len, MSG_DONTWAIT);
}

static void link_close(link_ctx_t *ctx)
{
#ifdef CONFIG_ESP_LIBSUPLA_USE_ESP_TLS
    if (ctx->is_tls && ctx->tls) {
        esp_tls_conn_destroy(ctx->tls);
        return;
    }
#endif
    if (ctx->sockfd != -1) {
        shutdown(ctx->sockfd, SHUT_RDWR);
        close(ctx->sockfd);
    }
}

int supla_cloud_connect(supla_link_t *link, const char *host, int port, unsigned char ssl)
{
    if (!link || !host)
        return SUPLA_RESULT_FALSE;

    *link = NULL;

    link_ctx_t *ctx = calloc(1, sizeof(link_ctx_t));
    if (!ctx)
        return SUPLA_RESULT_FALSE;

    ctx->sockfd = -1;
    ctx->is_tls = ssl;

#ifdef CONFIG_ESP_LIBSUPLA_USE_ESP_TLS
    if (ssl) {
        ctx->tls = esp_tls_init();
        if (!ctx->tls) {
            free(ctx);
            return SUPLA_RESULT_FALSE;
        }

        esp_tls_cfg_t cfg = { 0 };
        cfg.cacert_buf = server_cert_pem_start;
        cfg.cacert_bytes = server_cert_pem_end - server_cert_pem_start;
        cfg.timeout_ms = 10000;
        cfg.common_name = host;

        int rc = esp_tls_conn_new_sync(host, strlen(host), port, &cfg, ctx->tls);
        if (rc != 1) {
            ESP_LOGE(TAG, "TLS connect failed: %s:%d", host, port);
            esp_tls_conn_destroy(ctx->tls);
            free(ctx);
            return SUPLA_RESULT_FALSE;
        }

        if (esp_tls_get_conn_sockfd(ctx->tls, &ctx->sockfd) == ESP_OK && ctx->sockfd >= 0) {
            fcntl(ctx->sockfd, F_SETFL, O_NONBLOCK);
            set_keepalive(ctx->sockfd);
        }

        *link = ctx;
        return SUPLA_RESULT_TRUE;
    }
#endif

    // Plain TCP fallback
    struct addrinfo hints = { 0 }, *result, *rp;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;

    if (getaddrinfo(host, NULL, &hints, &result) != 0) {
        free(ctx);
        return SUPLA_RESULT_FALSE;
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        ctx->sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (ctx->sockfd == -1)
            continue;

        if (rp->ai_family == AF_INET) {
            ((struct sockaddr_in *)rp->ai_addr)->sin_port = htons(port);
        }
#if LWIP_IPV6
        else if (rp->ai_family == AF_INET6) {
            ((struct sockaddr_in6 *)rp->ai_addr)->sin6_port = htons(port);
        }
#endif
        else {
            close(ctx->sockfd);
            ctx->sockfd = -1;
            continue;
        }

        if (connect(ctx->sockfd, rp->ai_addr, rp->ai_addrlen) != -1 || errno == EINPROGRESS) {
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

    *link = ctx;
    return SUPLA_RESULT_TRUE;
}

int supla_cloud_send(supla_link_t link, void *buf, int count)
{
    if (!link || !buf || count <= 0)
        return SUPLA_RESULT_FALSE;

    return link_write((link_ctx_t *)link, buf, count);
}

int supla_cloud_recv(supla_link_t link, void *buf, int count)
{
    if (!link || !buf || count <= 0)
        return SUPLA_RESULT_FALSE;

    return link_read((link_ctx_t *)link, buf, count);
}

int supla_cloud_disconnect(supla_link_t *link)
{
    if (!link || !*link)
        return SUPLA_RESULT_FALSE;

    link_ctx_t *ctx = *link;
    link_close(ctx);
    free(ctx);

    *link = NULL;
    return SUPLA_RESULT_TRUE;
}
