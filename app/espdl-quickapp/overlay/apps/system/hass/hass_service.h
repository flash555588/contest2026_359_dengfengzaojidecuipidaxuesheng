/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define HASS_READ 1u
#define HASS_CONTROL 2u
#define HASS_CONFIGURE 4u
#define HASS_MAX_CLIENTS 8

struct hass_result_s
{
  uint32_t request_id;
  bool busy;
  bool done;
  bool cached;
  int status;
  int error;
  char *body; /* Owned by caller; release with hass_result_free(). */
};

/* Grants are assigned by trusted native integration, never by JavaScript.
 * Handles prevent accidental cross-client consumption, not hostile native code.
 * Configuration is shared RAM state, independent of any UI session.
 */
int hass_open(unsigned grants);
int hass_close(uint32_t client);
int hass_configure(uint32_t client, const char *url, const char *token,
                   bool allow_plain_http);
int hass_clear_configuration(uint32_t client);
int hass_get(uint32_t client, const char *resource);
int hass_get_state(uint32_t client, const char *entity);
int hass_control(uint32_t client, const char *entity, bool on, int brightness);
int hass_poll(uint32_t client, struct hass_result_s *result);
void hass_result_free(struct hass_result_s *result);
void hass_status(bool *configured, bool *busy, unsigned *clients);
int hass_get_url(uint32_t client, char *url, size_t size);
int hass_cache_info(uint32_t client, bool *available, uint32_t *age_ms);
