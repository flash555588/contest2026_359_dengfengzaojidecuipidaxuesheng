/* SPDX-License-Identifier: MIT
 * openvela adaptation of the ESPHome Python API client workflow.
 * See THIRD_PARTY.md for the pinned reference sources.
 */
#ifndef OPENVELA_ESPHOME_CLIENT_H
#define OPENVELA_ESPHOME_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESPHOME_MAX_ENTITIES 64
#define ESPHOME_NAME_SIZE 96

enum esphome_entity_kind
{
  ESPHOME_SENSOR, ESPHOME_BINARY_SENSOR, ESPHOME_SWITCH, ESPHOME_LIGHT,
  ESPHOME_TEXT_SENSOR
};

enum esphome_status
{
  ESPHOME_IDLE, ESPHOME_CONNECTING, ESPHOME_DISCOVERING, ESPHOME_READY,
  ESPHOME_STOPPING, ESPHOME_ERROR
};

struct esphome_config
{
  char address[16];                 /* Numeric IPv4; no host DNS blocking. */
  uint16_t port;
  char noise_psk[45];               /* 32 bytes, canonical base64, RAM only. */
  char expected_name[64];           /* Optional exact node name check. */
  bool allow_plaintext;             /* Explicit opt-in; never a fallback. */
};

struct esphome_entity
{
  uint32_t key;
  uint32_t device_id;
  enum esphome_entity_kind kind;
  char name[ESPHOME_NAME_SIZE];
  char unit[24];
  char text[128];
  bool has_state;
  bool state;
  bool supports_brightness;
  float value;
  float brightness;
};

struct esphome_snapshot
{
  enum esphome_status status;
  int error;                        /* Negative errno; never secret text. */
  bool running;
  bool encrypted;
  bool truncated;
  uint32_t revision;
  char device_name[ESPHOME_NAME_SIZE];
  char version[48];
  size_t count;
  struct esphome_entity entities[ESPHOME_MAX_ENTITIES];
};

/* UI thread calls only: start/stop/command return without network I/O.
 * stop is asynchronous; running becomes false after the worker releases its
 * socket and secret material. start returns -EBUSY until that completes.
 */
int esphome_client_start(const struct esphome_config *config);
void esphome_client_stop(void);
void esphome_client_snapshot(struct esphome_snapshot *snapshot);
int esphome_client_command(uint32_t key, uint32_t device_id, bool state,
                           bool has_brightness, float brightness);
const char *esphome_client_error(int error);

#endif
