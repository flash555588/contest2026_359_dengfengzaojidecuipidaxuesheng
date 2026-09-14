/* SPDX-License-Identifier: Apache-2.0 */
#include "claw_tls.h"
#include "claw_webclient.h"
#include "claw_connect.h"
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

struct webclient_tls_connection {
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_x509_crt ca;
};
static const unsigned char *trusted_ca;
static size_t trusted_ca_size;

static int close_connection(void *ctx, struct webclient_tls_connection *conn)
{
    (void)ctx;
    if (!conn) return 0;
    mbedtls_net_free(&conn->net);
    mbedtls_ssl_free(&conn->ssl);
    mbedtls_ssl_config_free(&conn->config);
    mbedtls_x509_crt_free(&conn->ca);
    free(conn);
    return 0;
}

static int connect_verified(void *ctx, const char *host, const char *port,
                             unsigned int timeout,
                             struct webclient_tls_connection **out)
{
    (void)ctx;
    if (out) *out = NULL;
    if (!out || !host || !*host || !port || !trusted_ca ||
        !timeout || timeout > 3600) return -EINVAL;
    struct webclient_tls_connection *conn = calloc(1, sizeof(*conn));
    if (!conn) return -ENOMEM;
    mbedtls_net_init(&conn->net);
    mbedtls_ssl_init(&conn->ssl);
    mbedtls_ssl_config_init(&conn->config);
    mbedtls_x509_crt_init(&conn->ca);
    int result = -EPROTO;
    if (mbedtls_x509_crt_parse(&conn->ca, trusted_ca, trusted_ca_size)) goto fail;
    if (mbedtls_ssl_config_defaults(&conn->config, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT)) goto fail;
    mbedtls_ssl_conf_authmode(&conn->config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conn->config, &conn->ca, NULL);
    mbedtls_ssl_conf_read_timeout(&conn->config, timeout * 1000);
    if (mbedtls_ssl_setup(&conn->ssl, &conn->config)) goto fail;
    if (mbedtls_ssl_set_hostname(&conn->ssl, host)) goto fail;
    int socket_fd = claw_connect_tcp(host, port, timeout * 1000);
    if (socket_fd < 0) {
        result = socket_fd;
        goto fail;
    }
    conn->net.fd = socket_fd;
    struct timeval limit = {.tv_sec = timeout, .tv_usec = 0};
    if (setsockopt(conn->net.fd, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit)) ||
        setsockopt(conn->net.fd, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit))) {
        result = -errno;
        goto fail;
    }
    mbedtls_ssl_set_bio(&conn->ssl, &conn->net, mbedtls_net_send,
                        mbedtls_net_recv, mbedtls_net_recv_timeout);
    int rc = mbedtls_ssl_handshake(&conn->ssl);
    if (rc) {
        result = rc == MBEDTLS_ERR_SSL_TIMEOUT ? -ETIMEDOUT : -EACCES;
        goto fail;
    }
    if (mbedtls_ssl_get_verify_result(&conn->ssl)) {
        result = -EACCES;
        goto fail;
    }
    *out = conn;
    return 0;
fail:
    close_connection(NULL, conn);
    return result;
}

static ssize_t tls_send(void *ctx, struct webclient_tls_connection *conn,
                         const void *data, size_t size)
{
    (void)ctx;
    int rc = mbedtls_ssl_write(&conn->ssl, data, size);
    return rc >= 0 ? rc : rc == MBEDTLS_ERR_SSL_TIMEOUT ? -ETIMEDOUT : -EIO;
}

static ssize_t tls_recv(void *ctx, struct webclient_tls_connection *conn,
                         void *data, size_t size)
{
    (void)ctx;
    int rc = mbedtls_ssl_read(&conn->ssl, data, size);
    if (rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
    return rc >= 0 ? rc : rc == MBEDTLS_ERR_SSL_TIMEOUT ? -ETIMEDOUT : -EIO;
}

int claw_tls_initialize(const unsigned char *ca_pem, size_t ca_size)
{
    static const struct webclient_tls_ops ops = {
        .connect = connect_verified, .send = tls_send,
        .recv = tls_recv, .close = close_connection
    };
    if (!ca_pem || ca_size < 2 || ca_pem[ca_size - 1] != 0) return -EINVAL;
    if (trusted_ca) return -EALREADY;
    if (psa_crypto_init() != PSA_SUCCESS) return -EIO;
    mbedtls_x509_crt check;
    mbedtls_x509_crt_init(&check);
    int rc = mbedtls_x509_crt_parse(&check, ca_pem, ca_size);
    mbedtls_x509_crt_free(&check);
    if (rc) return -EINVAL;
    trusted_ca = ca_pem;
    trusted_ca_size = ca_size;
    return claw_webclient_set_tls(&ops, NULL);
}
