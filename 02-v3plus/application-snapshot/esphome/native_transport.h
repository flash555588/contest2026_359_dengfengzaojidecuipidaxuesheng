#ifndef ESPHOME_NATIVE_TRANSPORT_H
#define ESPHOME_NATIVE_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

/* Numeric IPv4 only. No discovery, authentication, or encryption is implied. */
int esphome_tcp_connect(const char *address, uint16_t port, int timeout_ms);
int esphome_tcp_send(int fd, uint32_t type, const void *body, size_t length,
                     int timeout_ms);
int esphome_tcp_receive(int fd, uint32_t *type, void *body, size_t capacity,
                        size_t *length, int timeout_ms);
/* Close the connection after any send/receive failure; frames may be partial. */
#endif
