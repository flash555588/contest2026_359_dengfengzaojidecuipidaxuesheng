/* SPDX-License-Identifier: Apache-2.0
 * In-process smart home MCP tools for the board server.
 *
 * Every tool runs through the shared Home Assistant service with the same
 * whitelist, registered-service table and read-back verification as the native
 * panel. No tool opens a page or touches LVGL, and no result contains the
 * configured address or token: the chat dialog renders the returned card and
 * the smart home app stays closed. */
#include "glass_agent.h"
#include "glass_board_home.h"
#include "../hass/hass_service.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

#define HOME_PAGE 16
#define HOME_ENTITY_BYTES 96
#define HOME_TOOL_WAIT_MS 12000
#define HOME_SUBMIT_RETRY_MS 2000
#define HOME_VERIFY_ATTEMPTS 4
#define HOME_VERIFY_INTERVAL_MS 1000

static const char home_tool_catalog[] =
"{\"name\":\"home_status\",\"description\":\"Read smart home connection status: configured, busy and states-cache freshness. Never returns the address or token.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}},"
"{\"name\":\"home_list\",\"description\":\"List smart home entities with name and current state, 16 per page via offset. Read-only; use it to find exact entity ids before control.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"offset\":{\"type\":\"integer\",\"minimum\":0}},\"additionalProperties\":false}},"
"{\"name\":\"home_state\",\"description\":\"Read one entity's current state by exact entity id (domain.object_id). Read-only.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"entity\":{\"type\":\"string\"}},\"required\":[\"entity\"],\"additionalProperties\":false}},"
"{\"name\":\"home_control\",\"description\":\"Turn one light, switch, input_boolean or fan on or off; optional brightness 1-100 for lights only. Verifies by reading the entity back and renders a result card in the chat instead of opening any page.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"entity\":{\"type\":\"string\"},\"on\":{\"type\":\"boolean\"},\"brightness\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100}},\"required\":[\"entity\",\"on\"],\"additionalProperties\":false}},"
"{\"name\":\"home_service\",\"description\":\"Run one documented Home Assistant service on one entity, for example scene turn_on, cover set_cover_position with {\\\"position\\\":60} or climate set_temperature. Unknown fields are rejected.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"entity\":{\"type\":\"string\"},\"service\":{\"type\":\"string\"},\"arguments\":{\"type\":\"object\"}},\"required\":[\"entity\",\"service\"],\"additionalProperties\":false}}";

static void home_sleep_ms(unsigned ms) { usleep((useconds_t)ms * 1000); }

static uint64_t home_now(void)
{
  struct timespec time;
  if (clock_gettime(CLOCK_MONOTONIC, &time) < 0) return 0;
  return (uint64_t)time.tv_sec * 1000 + time.tv_nsec / 1000000;
}

static char *home_error(const char *code)
{
  cJSON *root = cJSON_CreateObject();
  if (!root) return NULL;
  cJSON_AddBoolToObject(root, "ok", false);
  cJSON_AddStringToObject(root, "error", code);
  char *text = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return text;
}

static char *home_result(cJSON *root)
{
  char *text = root ? cJSON_PrintUnformatted(root) : NULL;
  cJSON_Delete(root);
  return text;
}

static const char *home_string(const cJSON *args, const char *key)
{
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(args, key);
  return cJSON_IsString(value) ? value->valuestring : "";
}

static bool home_args_only(const cJSON *args, const char *keys)
{
  if (!cJSON_IsObject(args)) return false;
  for (const cJSON *value = args->child; value; value = value->next) {
    if (cJSON_GetObjectItemCaseSensitive(args, value->string) != value) return false;
    if (!value->string || strlen(value->string) > 31) return false;
    for (const char *p = value->string; *p; p++)
      if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) return false;
    char field[64];
    snprintf(field, sizeof(field), ",%s,", value->string);
    if (!strstr(keys, field)) return false;
  }
  return true;
}

static bool home_integer(const cJSON *args, const char *key, int low, int high, int *out)
{
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(args, key);
  if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) || value->valuedouble < low || value->valuedouble > high ||
      value->valuedouble != (int)value->valuedouble) return false;
  *out = (int)value->valuedouble;
  return true;
}

static bool home_entity_valid(const char *entity)
{
  if (!entity || strlen(entity) >= HOME_ENTITY_BYTES || *entity < 'a' || *entity > 'z') return false;
  unsigned dots = 0;
  for (const char *p = entity; *p; p++) {
    if (*p == '.') { if (p == entity || !p[1] || ++dots > 1) return false; }
    else if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) return false;
  }
  return dots == 1;
}

static bool home_identity(const cJSON *body, const char *entity)
{
  const cJSON *id = cJSON_GetObjectItemCaseSensitive(body, "entity_id");
  return cJSON_IsString(id) && !strcmp(id->valuestring, entity);
}

/* Bounded, UTF-8-safe copy for anything that reaches the dialog card. */
static void home_text(char *out, size_t capacity, const char *text)
{
  size_t used = 0;
  if (!text) text = "";
  while (*text && used + 1 < capacity) {
    unsigned char code = (unsigned char)*text;
    size_t size = code < 0x80 ? 1 : (code >= 0xc2 && code <= 0xdf) ? 2 :
                  (code >= 0xe0 && code <= 0xef) ? 3 : (code >= 0xf0 && code <= 0xf4) ? 4 : 0;
    if (!size) { text++; continue; }
    if (size > 1) {
      if (used + size + 1 > capacity) break;
      bool valid = true;
      for (size_t i = 1; i < size; i++) {
        if (!text[i] || ((unsigned char)text[i] & 0xc0) != 0x80) { valid = false; break; }
      }
      if (!valid) { text++; continue; }
      memcpy(out + used, text, size); used += size; text += size;
      continue;
    }
    out[used++] = code >= 0x20 && code != 0x7f ? (char)code : ' ';
    text++;
  }
  out[used] = 0;
  while (used && out[used - 1] == ' ') out[--used] = 0;
}

/* Same wording as the native device cards. */
static const char *home_label(const char *value)
{
  static const char *const labels[][2] =
    {{"on", "已开启"}, {"off", "已关闭"}, {"unavailable", "设备离线"},
     {"unknown", "状态未知"}, {"idle", "空闲"}, {"open", "已打开"},
     {"closed", "已关闭"}, {"opening", "正在打开"}, {"closing", "正在关闭"},
     {"playing", "正在播放"}, {"paused", "已暂停"}, {"standby", "待机"},
     {"locked", "已上锁"}, {"unlocked", "已解锁"}, {"heat", "制热"},
     {"cool", "制冷"}, {"auto", "自动"}, {"cleaning", "清扫中"},
     {"docked", "已回充"}, {"returning", "正在回充"}};
  for (unsigned i = 0; i < sizeof(labels) / sizeof(labels[0]); i++)
    if (!strcmp(value, labels[i][0])) return labels[i][1];
  return NULL;
}

static const char *home_kind(const char *domain)
{
  if (!strcmp(domain, "light")) return "light";
  if (!strcmp(domain, "switch") || !strcmp(domain, "input_boolean")) return "switch";
  if (!strcmp(domain, "fan")) return "fan";
  if (!strcmp(domain, "sensor")) return "sensor";
  if (!strcmp(domain, "binary_sensor")) return "binary";
  if (!strcmp(domain, "climate")) return "climate";
  if (!strcmp(domain, "media_player")) return "media";
  if (!strcmp(domain, "cover")) return "cover";
  if (!strcmp(domain, "vacuum")) return "vacuum";
  if (!strcmp(domain, "camera")) return "camera";
  if (!strcmp(domain, "humidifier")) return "humidifier";
  if (!strcmp(domain, "water_heater")) return "heater";
  if (!strcmp(domain, "lock")) return "lock";
  if (!strcmp(domain, "scene")) return "scene";
  if (!strcmp(domain, "script")) return "script";
  return "generic";
}

static const char *home_domain(const char *entity, char *out, size_t capacity)
{
  const char *dot = strchr(entity, '.');
  size_t size = dot ? (size_t)(dot - entity) : 0;
  if (size >= capacity) size = capacity - 1;
  memcpy(out, entity, size); out[size] = 0;
  return out;
}

/* One dialog card from a Home Assistant entity state. */
static cJSON *home_card(const cJSON *body)
{
  const cJSON *id = cJSON_GetObjectItemCaseSensitive(body, "entity_id");
  const cJSON *state = cJSON_GetObjectItemCaseSensitive(body, "state");
  if (!cJSON_IsString(id) || !cJSON_IsString(state)) return NULL;
  char domain[24], name[64], raw[64], text[80];
  home_domain(id->valuestring, domain, sizeof(domain));
  const cJSON *attributes = cJSON_GetObjectItemCaseSensitive(body, "attributes");
  const cJSON *friendly = cJSON_GetObjectItemCaseSensitive(attributes, "friendly_name");
  const char *fallback = strchr(id->valuestring, '.');
  home_text(name, sizeof(name), cJSON_IsString(friendly) ? friendly->valuestring :
            fallback ? fallback + 1 : id->valuestring);
  home_text(raw, sizeof(raw), state->valuestring);
  const char *label = home_label(raw);
  const cJSON *unit = cJSON_GetObjectItemCaseSensitive(attributes, "unit_of_measurement");
  if (!label && cJSON_IsString(unit) && unit->valuestring[0]) {
    char safe[16];
    home_text(safe, sizeof(safe), unit->valuestring);
    snprintf(text, sizeof(text), "%s %s", raw, safe);
  } else snprintf(text, sizeof(text), "%s", label ? label : raw);
  int level = -1;
  const cJSON *brightness = cJSON_GetObjectItemCaseSensitive(attributes, "brightness");
  const cJSON *percentage = cJSON_GetObjectItemCaseSensitive(attributes, "percentage");
  const cJSON *position = cJSON_GetObjectItemCaseSensitive(attributes, "current_position");
  if (!strcmp(domain, "light") && cJSON_IsNumber(brightness) &&
      brightness->valuedouble >= 0 && brightness->valuedouble <= 255)
    level = (int)(brightness->valuedouble * 100 / 255 + .5);
  else if (!strcmp(domain, "fan") && cJSON_IsNumber(percentage) &&
           percentage->valuedouble >= 0 && percentage->valuedouble <= 100)
    level = (int)percentage->valuedouble;
  else if (!strcmp(domain, "cover") && cJSON_IsNumber(position) &&
           position->valuedouble >= 0 && position->valuedouble <= 100)
    level = (int)position->valuedouble;
  static const char *const active[] =
    {"on", "open", "opening", "playing", "cleaning", "locking", "unlocking",
     "heat", "cool", "auto", "home"};
  bool on = false;
  for (unsigned i = 0; i < sizeof(active) / sizeof(active[0]); i++)
    if (!strcmp(state->valuestring, active[i])) on = true;
  cJSON *card = cJSON_CreateObject();
  if (!card) return NULL;
  if (!cJSON_AddStringToObject(card, "entity", id->valuestring) ||
      !cJSON_AddStringToObject(card, "kind", home_kind(domain)) ||
      !cJSON_AddStringToObject(card, "name", name) ||
      !cJSON_AddStringToObject(card, "state", text) ||
      !cJSON_AddNumberToObject(card, "level", level) ||
      !cJSON_AddBoolToObject(card, "on", on)) { cJSON_Delete(card); return NULL; }
  return card;
}

/* Card for action-only domains (scene/script/button): the accepted call is the
 * visible outcome; the read only supplies the display name when available. */
static cJSON *home_action_card(const char *entity, const cJSON *body, const char *verb)
{
  char domain[24], name[64];
  home_domain(entity, domain, sizeof(domain));
  const char *friendly = "";
  if (body) {
    const cJSON *attributes = cJSON_GetObjectItemCaseSensitive(body, "attributes");
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(attributes, "friendly_name");
    if (cJSON_IsString(value)) friendly = value->valuestring;
  }
  const char *fallback = strchr(entity, '.');
  home_text(name, sizeof(name), friendly[0] ? friendly : fallback ? fallback + 1 : entity);
  cJSON *card = cJSON_CreateObject();
  if (!card) return NULL;
  if (!cJSON_AddStringToObject(card, "entity", entity) ||
      !cJSON_AddStringToObject(card, "kind", home_kind(domain)) ||
      !cJSON_AddStringToObject(card, "name", name) ||
      !cJSON_AddStringToObject(card, "state", verb) ||
      !cJSON_AddNumberToObject(card, "level", -1) ||
      !cJSON_AddBoolToObject(card, "on", false)) { cJSON_Delete(card); return NULL; }
  return card;
}

static const char *home_error_code(int ret)
{
  switch (-ret) {
    case ENOTCONN: return "not_configured";
    case EBUSY: return "smart_home_busy";
    case EINVAL: return "invalid_arguments";
    case EACCES: return "entity_not_allowed";
    case ESTALE: return "configuration_changed";
    case ECANCELED: return "cancelled";
    case ETIMEDOUT: return "timeout";
    case ENOENT: return "not_found";
    default: return "smart_home_unavailable";
  }
}

static const char *home_http_code(int status)
{
  if (status == 404) return "not_found";
  if (status == 401 || status == 403) return "authentication_failed";
  if (status >= 400) return "request_rejected";
  return "request_failed";
}

static bool home_finish(int client, int request, struct hass_result_s *result,
                        unsigned timeout_ms, atomic_bool *cancel)
{
  uint64_t deadline = home_now() + timeout_ms;
  for (;;) {
    if (cancel && atomic_load(cancel)) return false;
    if (hass_poll((uint32_t)client, result)) return false;
    if (result->done && (int)result->request_id == request) return true;
    if (result->done) { hass_result_free(result); return false; }
    if (home_now() >= deadline) { hass_result_free(result); return false; }
    home_sleep_ms(50);
  }
}

/* Submits one request, waiting out a busy service, then waits for completion.
 * The configuration revision is pinned so a mid-flight reconfigure is refused
 * instead of applied to the wrong endpoint. */
static int home_submit_wait_until(int client, const char *resource, const char *entity, bool control,
                            bool on, int brightness, const char *action, const char *data,
                            struct hass_result_s *result, atomic_bool *cancel,
                            uint32_t revision, uint64_t deadline)
{
  memset(result, 0, sizeof(*result));
  if (control && !revision) return -ENOTCONN;
  uint64_t retry_until = home_now() + HOME_SUBMIT_RETRY_MS;
  int request;
  for (;;) {
    if (cancel && atomic_load(cancel)) return -ECANCELED;
    if (home_now() >= deadline) return -ETIMEDOUT;
    if (hass_configuration_revision() != revision) return -ESTALE;
    request = !control ? (entity ? hass_get_state((uint32_t)client, entity) : hass_get((uint32_t)client, resource))
              : action ? hass_call_service_checked((uint32_t)client, entity, action, data, revision)
                       : hass_control_checked((uint32_t)client, entity, on, brightness, revision);
    if (request > 0) break;
    if (request != -EBUSY || home_now() >= retry_until) return request;
    if (cancel && atomic_load(cancel)) return -ECANCELED;
    home_sleep_ms(50);
  }
  uint64_t now = home_now();
  if (now >= deadline || !home_finish(client, request, result, (unsigned)(deadline - now), cancel))
    return cancel && atomic_load(cancel) ? -ECANCELED : -ETIMEDOUT;
  if (hass_configuration_revision() != revision) { hass_result_free(result); return -ESTALE; }
  return 0;
}

static int home_submit_wait(int client, const char *resource, const char *entity, bool control,
                            bool on, int brightness, const char *action, const char *data,
                            struct hass_result_s *result, atomic_bool *cancel)
{
  return home_submit_wait_until(client, resource, entity, control, on, brightness, action, data,
    result, cancel, hass_configuration_revision(), home_now() + HOME_TOOL_WAIT_MS);
}

static bool home_matches(const char *body, const char *entity, bool on, int level)
{
  cJSON *parsed = body ? cJSON_Parse(body) : NULL;
  const cJSON *state = cJSON_GetObjectItemCaseSensitive(parsed, "state");
  bool ok = home_identity(parsed, entity) && cJSON_IsString(state) &&
            !strcmp(state->valuestring, on ? "on" : "off");
  if (ok && level >= 0) {
    const cJSON *attributes = cJSON_GetObjectItemCaseSensitive(parsed, "attributes");
    const cJSON *brightness = cJSON_GetObjectItemCaseSensitive(attributes, "brightness");
    int percent = -1;
    if (cJSON_IsNumber(brightness) && brightness->valuedouble >= 0 && brightness->valuedouble <= 255)
      percent = (int)(brightness->valuedouble * 100 / 255 + .5);
    ok = percent >= level - 1 && percent <= level + 1;
  }
  cJSON_Delete(parsed);
  return ok;
}

/* The native panel's window: at most four reads inside eight seconds, and a
 * matching read is the only evidence a control succeeded. */
static char *home_verify(int client, const char *entity, bool on, int level, atomic_bool *cancel,
                         uint32_t revision)
{
  uint64_t deadline = home_now() + 8000;
  for (unsigned attempt = 0; attempt < HOME_VERIFY_ATTEMPTS; attempt++) {
    if (attempt) home_sleep_ms(HOME_VERIFY_INTERVAL_MS);
    if (cancel && atomic_load(cancel)) return NULL;
    struct hass_result_s read;
    if (home_submit_wait_until(client, NULL, entity, false, false, -1, NULL, NULL, &read, cancel,
                              revision, deadline)) { hass_result_free(&read); break; }
    if (read.error || read.status != 200 || !read.body) { hass_result_free(&read); continue; }
    char *body = read.body;
    read.body = NULL;
    if (home_matches(body, entity, on, level)) return body;
    free(body);
  }
  return NULL;
}

static char *home_read_once(int client, const char *entity, atomic_bool *cancel, uint32_t revision)
{
  struct hass_result_s read;
  if (home_submit_wait_until(client, NULL, entity, false, false, -1, NULL, NULL, &read, cancel,
                            revision, home_now() + 8000)) { hass_result_free(&read); return NULL; }
  if (read.error || read.status != 200 || !read.body) { hass_result_free(&read); return NULL; }
  char *body = read.body;
  read.body = NULL;
  cJSON *parsed = body ? cJSON_Parse(body) : NULL;
  bool valid = home_identity(parsed, entity);
  cJSON_Delete(parsed);
  if (!valid) { free(body); return NULL; }
  return body;
}

static char *home_status_tool(void)
{
  bool configured = false, busy = false;
  unsigned clients = 0;
  hass_status(&configured, &busy, &clients);
  cJSON *root = cJSON_CreateObject();
  if (!root) return NULL;
  cJSON_AddBoolToObject(root, "ok", true);
  cJSON_AddBoolToObject(root, "configured", configured);
  cJSON_AddBoolToObject(root, "busy", busy);
  if (configured) {
    int client = hass_open(HASS_READ);
    if (client > 0) {
      bool fresh = false;
      uint32_t age = 0;
      if (!hass_cache_info((uint32_t)client, &fresh, &age)) {
        cJSON_AddBoolToObject(root, "states_cached", fresh);
        if (fresh) cJSON_AddNumberToObject(root, "states_cache_age_ms", age);
      }
      hass_close((uint32_t)client);
    }
  } else cJSON_AddStringToObject(root, "hint", "Home Assistant 尚未配置；请用户在设备设置中填写地址和令牌。");
  return home_result(root);
}

static char *home_list_tool(const cJSON *args, atomic_bool *cancel)
{
  if (!home_args_only(args, ",offset,")) return home_error("invalid_arguments");
  int offset = 0;
  if (cJSON_GetObjectItemCaseSensitive(args, "offset") && !home_integer(args, "offset", 0, 100000, &offset))
    return home_error("invalid_arguments");
  int client = hass_open(HASS_READ);
  if (client <= 0) return home_error("smart_home_unavailable");
  struct hass_result_s result;
  int ret = home_submit_wait(client, "states", NULL, false, false, -1, NULL, NULL, &result, cancel);
  if (ret) { hass_close((uint32_t)client); return home_error(home_error_code(ret)); }
  if (result.error || result.status < 200 || result.status >= 300) {
    const char *code = result.error ? home_error_code(result.error) : home_http_code(result.status);
    hass_result_free(&result); hass_close((uint32_t)client);
    return home_error(code);
  }
  cJSON *states = cJSON_Parse(result.body);
  hass_result_free(&result);
  if (!cJSON_IsArray(states)) { cJSON_Delete(states); hass_close((uint32_t)client); return home_error("states_unavailable"); }
  cJSON *root = cJSON_CreateObject(), *items = root ? cJSON_AddArrayToObject(root, "items") : NULL;
  if (!root || !items) { cJSON_Delete(states); cJSON_Delete(root); hass_close((uint32_t)client); return NULL; }
  cJSON_AddBoolToObject(root, "ok", true);
  cJSON_AddNumberToObject(root, "offset", offset);
  unsigned total = (unsigned)cJSON_GetArraySize(states);
  cJSON_AddNumberToObject(root, "total", total);
  for (unsigned i = (unsigned)offset; i < total && i < (unsigned)offset + HOME_PAGE; i++) {
    const cJSON *item = cJSON_GetArrayItem(states, (int)i);
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "entity_id");
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(item, "state");
    if (!cJSON_IsString(id) || !cJSON_IsString(state)) continue;
    const cJSON *attributes = cJSON_GetObjectItemCaseSensitive(item, "attributes");
    const cJSON *friendly = cJSON_GetObjectItemCaseSensitive(attributes, "friendly_name");
    char name[64], value[48];
    home_text(name, sizeof(name), cJSON_IsString(friendly) ? friendly->valuestring : id->valuestring);
    home_text(value, sizeof(value), state->valuestring);
    cJSON *entry = cJSON_CreateObject();
    if (!entry || !cJSON_AddStringToObject(entry, "entity", id->valuestring) ||
        !cJSON_AddStringToObject(entry, "name", name) ||
        !cJSON_AddStringToObject(entry, "state", value)) { cJSON_Delete(entry); continue; }
    cJSON_AddItemToArray(items, entry);
  }
  if ((unsigned)offset + HOME_PAGE < total) cJSON_AddBoolToObject(root, "more", true);
  cJSON_Delete(states); hass_close((uint32_t)client);
  return home_result(root);
}

static char *home_state_tool(const cJSON *args, atomic_bool *cancel)
{
  const char *entity = home_string(args, "entity");
  if (!home_args_only(args, ",entity,") || !home_entity_valid(entity))
    return home_error("invalid_arguments");
  int client = hass_open(HASS_READ);
  if (client <= 0) return home_error("smart_home_unavailable");
  struct hass_result_s result;
  int ret = home_submit_wait(client, NULL, entity, false, false, -1, NULL, NULL, &result, cancel);
  if (ret) { hass_close((uint32_t)client); return home_error(home_error_code(ret)); }
  if (result.error || result.status < 200 || result.status >= 300) {
    const char *code = result.error ? home_error_code(result.error) : home_http_code(result.status);
    hass_result_free(&result); hass_close((uint32_t)client);
    return home_error(code);
  }
  cJSON *body = cJSON_Parse(result.body);
  hass_result_free(&result);
  cJSON *card = home_identity(body, entity) ? home_card(body) : NULL;
  if (!card) { cJSON_Delete(body); hass_close((uint32_t)client); return home_error("unreadable_response"); }
  cJSON *root = cJSON_CreateObject();
  if (!root) { cJSON_Delete(card); cJSON_Delete(body); hass_close((uint32_t)client); return NULL; }
  const cJSON *name = cJSON_GetObjectItemCaseSensitive(card, "name");
  const cJSON *state = cJSON_GetObjectItemCaseSensitive(body, "state");
  char value[48];
  if (cJSON_IsString(state)) home_text(value, sizeof(value), state->valuestring); else value[0] = 0;
  cJSON_AddBoolToObject(root, "ok", true);
  cJSON_AddStringToObject(root, "entity", entity);
  if (cJSON_IsString(name)) cJSON_AddStringToObject(root, "name", name->valuestring);
  if (value[0]) cJSON_AddStringToObject(root, "state", value);
  cJSON_AddBoolToObject(root, "available", strcmp(value, "unavailable") != 0);
  cJSON_AddItemToObject(root, "card", card);
  cJSON_Delete(body); hass_close((uint32_t)client);
  return home_result(root);
}

static char *home_control_tool(const cJSON *args, atomic_bool *cancel)
{
  const char *entity = home_string(args, "entity");
  const cJSON *on = cJSON_GetObjectItemCaseSensitive(args, "on");
  const cJSON *brightness = cJSON_GetObjectItemCaseSensitive(args, "brightness");
  if (!home_args_only(args, ",entity,,on,,brightness,") || !home_entity_valid(entity) ||
      !cJSON_IsBool(on)) return home_error("invalid_arguments");
  int level = -1;
  if (brightness && (!home_integer(args, "brightness", 1, 100, &level) || !cJSON_IsTrue(on)))
    return home_error("invalid_arguments");
  int client = hass_open(HASS_READ | HASS_CONTROL);
  if (client <= 0) return home_error("smart_home_unavailable");
  struct hass_result_s result;
  uint32_t revision = hass_configuration_revision();
  int ret = home_submit_wait_until(client, NULL, entity, true, cJSON_IsTrue(on), level,
                             NULL, NULL, &result, cancel, revision, home_now() + HOME_TOOL_WAIT_MS);
  if (ret) { hass_close((uint32_t)client); return home_error(home_error_code(ret)); }
  if (result.error || result.status < 200 || result.status >= 300) {
    const char *code = result.error ? home_error_code(result.error) : home_http_code(result.status);
    hass_result_free(&result); hass_close((uint32_t)client);
    return home_error(code);
  }
  hass_result_free(&result);
  char *observed = home_verify(client, entity, cJSON_IsTrue(on), level, cancel, revision);
  cJSON *root = cJSON_CreateObject();
  if (!root) { free(observed); hass_close((uint32_t)client); return NULL; }
  cJSON_AddBoolToObject(root, "ok", true);
  cJSON_AddStringToObject(root, "entity", entity);
  cJSON_AddBoolToObject(root, "on", cJSON_IsTrue(on));
  if (level >= 0) cJSON_AddNumberToObject(root, "brightness", level);
  cJSON *body = observed ? cJSON_Parse(observed) : NULL;
  cJSON *card = cJSON_IsObject(body) ? home_card(body) : NULL;
  if (card) {
    cJSON_AddBoolToObject(root, "verified", true);
    cJSON_AddItemToObject(root, "card", card);
  } else {
    cJSON_AddBoolToObject(root, "verified", false);
    cJSON_AddStringToObject(root, "note", "已发送控制，但没有读到一致状态，结果未确认；不要自动重发。");
    cJSON_AddItemToObject(root, "card", home_action_card(entity, NULL, "结果未确认"));
  }
  cJSON_Delete(body); free(observed); hass_close((uint32_t)client);
  return home_result(root);
}

static bool home_service_name(const char *service)
{
  size_t size = strlen(service);
  if (!size || size > 31) return false;
  for (const char *p = service; *p; p++)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) return false;
  return true;
}

/* Only properties with an explicit state mapping can confirm completion. */
static bool home_service_matches(const cJSON *observed, const char *service, const cJSON *args)
{
  const char *state = home_string(observed, "state");
  if (!state[0] || !strcmp(state, "unknown") || !strcmp(state, "unavailable")) return false;
  if (!strcmp(service, "turn_off")) return !strcmp(state, "off");
  bool matched = false;
  if (!strcmp(service, "turn_on")) { if (strcmp(state, "on")) return false; matched = true; }
  const cJSON *attributes = cJSON_GetObjectItemCaseSensitive(observed, "attributes");
  for (const cJSON *p = args ? args->child : NULL; p; p = p->next) {
    const char *key = p->string;
    if (!strcmp(key, "entity_id")) return false;
    const char *mapped = !strcmp(key, "position") ? "current_position" :
                         !strcmp(key, "brightness_pct") ? "brightness" : key;
    const cJSON *actual = (!strcmp(key, "value") || !strcmp(key, "option") || !strcmp(key, "hvac_mode"))
                         ? cJSON_GetObjectItemCaseSensitive(observed, "state")
                         : cJSON_GetObjectItemCaseSensitive(attributes, mapped);
    if (cJSON_IsNumber(p)) {
      double got;
      if (cJSON_IsNumber(actual)) got = actual->valuedouble;
      else if (cJSON_IsString(actual)) {
        char *end; got = strtod(actual->valuestring, &end);
        if (end == actual->valuestring || *end) return false;
      } else return false;
      if (!strcmp(key, "brightness_pct")) got = got * 100 / 255;
      double tolerance = !strcmp(key, "brightness_pct") ? 1.0 : 0.01;
      if (!isfinite(got) || fabs(got - p->valuedouble) > tolerance) return false;
    } else if (cJSON_IsString(p)) {
      if (!cJSON_IsString(actual) || strcmp(p->valuestring, actual->valuestring)) return false;
    } else if (cJSON_IsBool(p)) {
      if (!cJSON_IsBool(actual) || cJSON_IsTrue(p) != cJSON_IsTrue(actual)) return false;
    } else return false;
    matched = true;
  }
  return matched;
}

static char *home_service_tool(const cJSON *args, atomic_bool *cancel)
{
  const char *entity = home_string(args, "entity");
  const char *service = home_string(args, "service");
  const cJSON *arguments = cJSON_GetObjectItemCaseSensitive(args, "arguments");
  if (!home_args_only(args, ",entity,,service,,arguments,") || !home_entity_valid(entity) ||
      !home_service_name(service) ||
      (arguments && !cJSON_IsObject(arguments))) return home_error("invalid_arguments");
  char domain[24];
  home_domain(entity, domain, sizeof(domain));
  if (!strcmp(domain, "lock") || !strcmp(domain, "alarm_control_panel")) return home_error("entity_not_allowed");
  /* Compose the exact body the native panel sends; the shared service validates
   * domain, service, entity and every parameter against its rule table. */
  char *body = NULL;
  {
    char *printed = cJSON_IsObject(arguments) ? cJSON_PrintUnformatted(arguments) : strdup("{}");
    if (!printed) return NULL;
    size_t capacity = strlen(printed) + strlen(entity) + 32;
    body = malloc(capacity);
    if (body) {
      if (printed[1] == '}') snprintf(body, capacity, "{\"entity_id\":\"%s\"}", entity);
      else snprintf(body, capacity, "{\"entity_id\":\"%s\",%.*s}", entity, (int)(strlen(printed) - 2), printed + 1);
    }
    free(printed);
    if (!body) return NULL;
  }
  if (strlen(body) > 1023) { free(body); return home_error("invalid_arguments"); }
  int client = hass_open(HASS_READ | HASS_CONTROL);
  if (client <= 0) { free(body); return home_error("smart_home_unavailable"); }
  struct hass_result_s result;
  uint32_t revision = hass_configuration_revision();
  int ret = home_submit_wait_until(client, NULL, entity, true, false, -1, service, body, &result, cancel,
                                  revision, home_now() + HOME_TOOL_WAIT_MS);
  free(body);
  if (ret) { hass_close((uint32_t)client); return home_error(home_error_code(ret)); }
  if (result.error || result.status < 200 || result.status >= 300) {
    const char *code = result.error ? home_error_code(result.error) : home_http_code(result.status);
    hass_result_free(&result); hass_close((uint32_t)client);
    return home_error(code);
  }
  hass_result_free(&result);
  bool action_only = !strcmp(domain, "scene") || !strcmp(domain, "script") ||
                     !strcmp(domain, "button") || !strcmp(domain, "input_button") ||
                     !strcmp(domain, "automation");
  char *observed = home_read_once(client, entity, cancel, revision);
  cJSON *readback = observed ? cJSON_Parse(observed) : NULL;
  cJSON *card = NULL;
  bool verified = !action_only && cJSON_IsObject(readback) && home_service_matches(readback, service, arguments);
  if (verified) card = home_card(readback);
  else card = home_action_card(entity, readback, action_only ? "已受理" : "结果未确认");
  cJSON *root = cJSON_CreateObject();
  if (!root) { cJSON_Delete(card); cJSON_Delete(readback); free(observed); hass_close((uint32_t)client); return NULL; }
  cJSON_AddBoolToObject(root, "ok", true);
  cJSON_AddStringToObject(root, "entity", entity);
  cJSON_AddStringToObject(root, "service", service);
  cJSON_AddBoolToObject(root, "accepted", true);
  cJSON_AddBoolToObject(root, "verified", verified);
  if (card) cJSON_AddItemToObject(root, "card", card);
  if (!verified) cJSON_AddStringToObject(root, "note", "服务已被受理，执行结果未确认；不要自动重发。");
  cJSON_Delete(readback); free(observed); hass_close((uint32_t)client);
  return home_result(root);
}

static char *home_call(const char *tool, const cJSON *args, atomic_bool *cancel)
{
  if (!strcmp(tool, "home_control") || !strcmp(tool, "home_service")) {
    cJSON *config = NULL;
    int loaded = portal_agent_load(&config);
    const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(config, "home_control_enabled");
    bool allowed = !loaded && cJSON_IsNumber(enabled) && enabled->valuedouble == 1;
    cJSON_Delete(config);
    if (!allowed) return home_error("home_control_disabled");
  }
  if (!strcmp(tool, "home_status"))
    return cJSON_GetArraySize(args) == 0 ? home_status_tool() : home_error("invalid_arguments");
  if (!strcmp(tool, "home_list")) return home_list_tool(args, cancel);
  if (!strcmp(tool, "home_state")) return home_state_tool(args, cancel);
  if (!strcmp(tool, "home_control")) return home_control_tool(args, cancel);
  if (!strcmp(tool, "home_service")) return home_service_tool(args, cancel);
  return home_error("invalid_arguments");
}

static const struct glass_board_home home_binding = { home_tool_catalog, home_call };
void glass_board_home_register(void) { glass_agent_set_board_home(&home_binding); }
