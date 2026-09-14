/* SPDX-License-Identifier: MIT */
#ifndef OPENVELA_ESPHOME_SECURE_TRANSPORT_H
#define OPENVELA_ESPHOME_SECURE_TRANSPORT_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct esphome_secure;
/* Borrow fd; does not close it. key is canonical base64. No plaintext fallback.
 * Functions are used only on the owning worker. Any error requires closing
 * the connection, except the caller's separate pre-read poll timeout.
 */
int esphome_secure_open(struct esphome_secure **out, int fd,
                        const char *key, const char *expected_name,
                        int timeout_ms);
int esphome_secure_send(struct esphome_secure *ctx, uint32_t type,
                        const void *body, size_t length, int timeout_ms);
int esphome_secure_receive(struct esphome_secure *ctx, uint32_t *type,
                           void *body, size_t capacity, size_t *length,
                           int timeout_ms);
void esphome_secure_free(struct esphome_secure *ctx);
/* Validation and explicit zeroing; safe before socket connection. */
int esphome_secure_validate_key(const char *key);
void esphome_secure_wipe(void *data, size_t size);
#endif
