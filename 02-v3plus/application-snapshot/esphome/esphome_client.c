/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 openvela ESPHome app contributors
 * Port of the connection/enumeration/subscription/command workflow in
 * aioesphomeapi's Python client. See reference/ and THIRD_PARTY.md.
 */
#include "esphome_client.h"
#include "esphome_model.h"
#include "native_transport.h"
#include "protocol_ids.h"
#include "secure_transport.h"

#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifndef ESPHOME_IO_TIMEOUT_MS
#define ESPHOME_IO_TIMEOUT_MS 3000
#endif
#ifndef ESPHOME_PING_INTERVAL_MS
#define ESPHOME_PING_INTERVAL_MS 15000
#endif
#ifndef ESPHOME_PING_TIMEOUT_MS
#define ESPHOME_PING_TIMEOUT_MS 10000
#endif
#ifndef ESPHOME_DISCOVERY_TIMEOUT_MS
#define ESPHOME_DISCOVERY_TIMEOUT_MS 60000
#endif
#define RECEIVE_SIZE 16384
#define COMMAND_QUEUE_SIZE 8

struct command
{
  uint8_t data[64];
  size_t size;
  uint32_t type;
};

struct connection
{
  int fd;
  struct esphome_secure *secure;
  uint8_t *receive;
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct esphome_snapshot g_model;
static struct command g_commands[COMMAND_QUEUE_SIZE];
static unsigned g_head;
static unsigned g_count;
static bool g_stop;

static int64_t now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static bool stopping(void)
{
  bool stop;
  pthread_mutex_lock(&g_lock);
  stop = g_stop;
  pthread_mutex_unlock(&g_lock);
  return stop;
}

static void publish(enum esphome_status status)
{
  pthread_mutex_lock(&g_lock);
  if (!g_stop) g_model.status = status;
  ++g_model.revision;
  pthread_mutex_unlock(&g_lock);
}

static int send_message(struct connection *c, uint32_t type,
                        const void *data, size_t size)
{
  if (stopping()) return -ECANCELED;
  if (c->secure)
    return esphome_secure_send(c->secure, type, data, size,
                               ESPHOME_IO_TIMEOUT_MS);
  return esphome_tcp_send(c->fd, type, data, size, ESPHOME_IO_TIMEOUT_MS);
}

/* Poll before entering frame decoding. A quiet connection may be retried;
 * a timeout after consuming any frame byte is fatal to avoid desynchronizing. */
static int receive_message(struct connection *c, uint32_t *type, size_t *size)
{
  struct pollfd pfd = {.fd = c->fd, .events = POLLIN};
  int ret;
  if (stopping()) return -ECANCELED;
  ret = poll(&pfd, 1, 100);
  if (ret < 0) return errno == EINTR ? 0 : -errno;
  if (!ret) return 0;
  if (!(pfd.revents & POLLIN)) return -ECONNRESET;
  if (c->secure)
    ret = esphome_secure_receive(c->secure, type, c->receive, RECEIVE_SIZE,
                                 size, ESPHOME_IO_TIMEOUT_MS);
  else
    ret = esphome_tcp_receive(c->fd, type, c->receive, RECEIVE_SIZE,
                              size, ESPHOME_IO_TIMEOUT_MS);
  return ret < 0 ? ret : 1;
}

static int auth_response(const void *body, size_t size)
{
  struct eh_pb_reader r = {body, size, 0};
  struct eh_pb_field f;
  int ret;
  while ((ret = eh_pb_next(&r, &f)) > 0)
    if (f.number == EH_AuthenticationResponse_invalid_password)
      {
        uint32_t bad;
        if (eh_pb_u32(&f, &bad) < 0) return -EPROTO;
        if (bad) return -EACCES;
      }
  return ret;
}

static int common_message(struct connection *c, uint32_t type, size_t size)
{
  if (type == EH_PingRequest)
    return send_message(c, EH_PingResponse, NULL, 0);
  if (type == EH_DisconnectRequest)
    {
      send_message(c, EH_DisconnectResponse, NULL, 0);
      return -ECONNRESET;
    }
  if (type == EH_DisconnectResponse) return -ECONNRESET;
  if (type == EH_AuthenticationResponse) return auth_response(c->receive, size);
  return 0;
}

static int check_name(const struct eh_pb_field *f, const char *expected)
{
  if (f->wire != 2) return -EPROTO;
  /* Compare raw bytes before display truncation/sanitization. */
  if (expected[0] && (strlen(expected) != f->length ||
      memcmp(expected, f->bytes, f->length))) return -EHOSTUNREACH;
  return 0;
}

static int hello_response(struct connection *c, size_t size,
                          const char *expected)
{
  struct eh_pb_reader r = {c->receive, size, 0};
  struct eh_pb_field f;
  uint32_t major = 0;
  bool name_seen = false;
  int ret;
  while ((ret = eh_pb_next(&r, &f)) > 0)
    {
      if (f.number == EH_HelloResponse_api_version_major)
        {
          if (eh_pb_u32(&f, &major) < 0) return -EPROTO;
        }
      if (f.number == EH_HelloResponse_name)
        {
          ret = check_name(&f, expected);
          if (ret < 0) return ret;
          name_seen = true;
        }
    }
  if (ret < 0) return ret;
  if (major != 1) return -EPROTONOSUPPORT;
  if (expected[0] && !name_seen) return -EHOSTUNREACH;
  return 0;
}

static int device_info(struct connection *c, size_t size, const char *expected)
{
  struct eh_pb_reader r = {c->receive, size, 0};
  struct eh_pb_field f;
  char name[ESPHOME_NAME_SIZE] = {0};
  char version[48] = {0};
  bool name_seen = false;
  int ret;
  while ((ret = eh_pb_next(&r, &f)) > 0)
    {
      if (f.number == EH_DeviceInfoResponse_name)
        {
          ret = check_name(&f, expected);
          if (ret < 0) return ret;
          if (eh_pb_string(&f, name, sizeof(name)) < 0) return -EPROTO;
          name_seen = true;
        }
      else if (f.number == EH_DeviceInfoResponse_esphome_version)
        {
          if (eh_pb_string(&f, version, sizeof(version)) < 0) return -EPROTO;
        }
      else if (f.number == EH_DeviceInfoResponse_uses_password)
        {
          uint32_t uses_password;
          if (eh_pb_u32(&f, &uses_password) < 0) return -EPROTO;
          if (uses_password) return -EACCES;
        }
    }
  if (ret < 0) return ret;
  if (expected[0] && !name_seen) return -EHOSTUNREACH;
  pthread_mutex_lock(&g_lock);
  memcpy(g_model.device_name, name, sizeof(name));
  memcpy(g_model.version, version, sizeof(version));
  ++g_model.revision;
  pthread_mutex_unlock(&g_lock);
  return 0;
}

static int connect_api(struct connection *c, const struct esphome_config *cfg)
{
  uint8_t hello[80];
  struct eh_pb_writer w = {hello, sizeof(hello), 0, 0};
  int64_t deadline;
  bool info_done = false;
  bool list_done = false;
  int ret;
  eh_pb_put_string(&w, EH_HelloRequest_client_info, "openvela ESPHome App");
  eh_pb_put_uint(&w, EH_HelloRequest_api_version_major, 1);
  /* 1.13 retains object IDs; no claim to implement newer optional features. */
  eh_pb_put_uint(&w, EH_HelloRequest_api_version_minor, 13);
  if (w.error) return w.error;
  ret = send_message(c, EH_HelloRequest, hello, w.size);
  if (ret < 0) return ret;
  /* As in Python _connect_hello_login, send empty legacy auth without waiting
   * for its reply. Modern servers reserve/ignore it; old servers need it. */
  ret = send_message(c, EH_AuthenticationRequest, NULL, 0);
  if (ret < 0) return ret;
  deadline = now_ms() + 10000;
  for (;;)
    {
      uint32_t type;
      size_t size;
      if (now_ms() >= deadline) return -ETIMEDOUT;
      ret = receive_message(c, &type, &size);
      if (ret < 0) return ret;
      if (!ret) continue;
      ret = common_message(c, type, size);
      if (ret < 0) return ret;
      if (type == EH_HelloResponse)
        {
          ret = hello_response(c, size, cfg->expected_name);
          if (ret < 0) return ret;
          break;
        }
    }
  publish(ESPHOME_DISCOVERING);
  ret = send_message(c, EH_DeviceInfoRequest, NULL, 0);
  if (ret < 0) return ret;
  ret = send_message(c, EH_ListEntitiesRequest, NULL, 0);
  if (ret < 0) return ret;
  deadline = now_ms() + ESPHOME_DISCOVERY_TIMEOUT_MS;
  while (!info_done || !list_done)
    {
      uint32_t type;
      size_t size;
      if (now_ms() >= deadline) return -ETIMEDOUT;
      ret = receive_message(c, &type, &size);
      if (ret < 0) return ret;
      if (!ret) continue;
      ret = common_message(c, type, size);
      if (ret < 0) return ret;
      if (type == EH_DeviceInfoResponse)
        {
          ret = device_info(c, size, cfg->expected_name);
          if (ret < 0) return ret;
          info_done = true;
        }
      else if (type == EH_ListEntitiesDoneResponse) list_done = true;
      else
        {
          pthread_mutex_lock(&g_lock);
          ret = eh_model_info(&g_model, type, c->receive, size);
          pthread_mutex_unlock(&g_lock);
          if (ret < 0) return ret;
        }
    }
  ret = send_message(c, EH_SubscribeStatesRequest, NULL, 0);
  if (ret < 0) return ret;
  publish(ESPHOME_READY);
  return 0;
}

static int run_api(struct connection *c)
{
  int64_t ping_due = now_ms() + ESPHOME_PING_INTERVAL_MS;
  int64_t ping_deadline = 0;
  while (!stopping())
    {
      struct command command;
      bool pending = false;
      uint32_t type;
      size_t size;
      int ret;
      pthread_mutex_lock(&g_lock);
      if (g_count && !g_stop)
        {
          command = g_commands[g_head];
          g_head = (g_head + 1) % COMMAND_QUEUE_SIZE;
          --g_count;
          pending = true;
        }
      pthread_mutex_unlock(&g_lock);
      if (pending)
        {
          ret = send_message(c, command.type, command.data, command.size);
          if (ret < 0) return ret;
        }
      if (ping_deadline && now_ms() >= ping_deadline) return -ETIMEDOUT;
      if (!ping_deadline && now_ms() >= ping_due)
        {
          ret = send_message(c, EH_PingRequest, NULL, 0);
          if (ret < 0) return ret;
          ping_deadline = now_ms() + ESPHOME_PING_TIMEOUT_MS;
        }
      ret = receive_message(c, &type, &size);
      if (ret < 0) return ret;
      if (!ret) continue;
      ret = common_message(c, type, size);
      if (ret < 0) return ret;
      if (type == EH_PingResponse)
        {
          ping_deadline = 0;
          ping_due = now_ms() + ESPHOME_PING_INTERVAL_MS;
        }
      pthread_mutex_lock(&g_lock);
      ret = eh_model_state(&g_model, type, c->receive, size);
      pthread_mutex_unlock(&g_lock);
      if (ret < 0) return ret;
    }
  return -ECANCELED;
}

static void *worker(void *arg)
{
  struct esphome_config *cfg = arg;
  struct connection c = {.fd = -1};
  int ret = -ENOMEM;
  c.receive = malloc(RECEIVE_SIZE);
  if (!c.receive) goto done;
  if (stopping()) {ret = -ECANCELED; goto done;}
  c.fd = esphome_tcp_connect(cfg->address, cfg->port, ESPHOME_IO_TIMEOUT_MS);
  if (c.fd < 0) {ret = c.fd; goto done;}
  if (cfg->noise_psk[0])
    {
      ret = esphome_secure_open(&c.secure, c.fd, cfg->noise_psk,
                                cfg->expected_name, ESPHOME_IO_TIMEOUT_MS);
      esphome_secure_wipe(cfg->noise_psk, sizeof(cfg->noise_psk));
      if (ret < 0) goto done;
    }
  ret = connect_api(&c, cfg);
  if (ret < 0) goto done;
  esphome_secure_wipe(cfg, sizeof(*cfg));
  ret = run_api(&c);
done:
  /* Error or cancellation may leave a partial frame: close immediately.
   * The worker is the only socket owner, preventing descriptor-reuse races. */
  if (c.fd >= 0) {shutdown(c.fd, SHUT_RDWR); close(c.fd);}
  esphome_secure_free(c.secure);
  if (c.receive) {esphome_secure_wipe(c.receive, RECEIVE_SIZE); free(c.receive);}
  esphome_secure_wipe(cfg, sizeof(*cfg));
  free(cfg);
  pthread_mutex_lock(&g_lock);
  g_model.status = g_stop ? ESPHOME_IDLE : ESPHOME_ERROR;
  g_model.error = g_stop ? 0 : ret;
  g_model.running = false;
  for (size_t i = 0; i < g_model.count; ++i) g_model.entities[i].has_state = false;
  g_count = 0;
  ++g_model.revision;
  pthread_mutex_unlock(&g_lock);
  return NULL;
}

int esphome_client_start(const struct esphome_config *config)
{
  struct esphome_config *copy;
  struct in_addr address;
  pthread_attr_t attr;
  pthread_t thread;
  uint32_t revision;
  int ret;
  if (!config || !config->port ||
      !memchr(config->address, 0, sizeof(config->address)) ||
      !memchr(config->noise_psk, 0, sizeof(config->noise_psk)) ||
      !memchr(config->expected_name, 0, sizeof(config->expected_name)) ||
      inet_pton(AF_INET, config->address, &address) != 1) return -EINVAL;
  if (config->noise_psk[0])
    {
      ret = esphome_secure_validate_key(config->noise_psk);
      if (ret < 0) return ret;
    }
  else if (!config->allow_plaintext) return -EACCES;
  copy = malloc(sizeof(*copy));
  if (!copy) return -ENOMEM;
  *copy = *config;
  ret = pthread_attr_init(&attr);
  if (ret) goto fail;
  ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
#ifdef __NuttX__
  if (!ret) ret = pthread_attr_setstacksize(&attr, 24576);
#endif
  if (ret) {pthread_attr_destroy(&attr); goto fail;}
  pthread_mutex_lock(&g_lock);
  if (g_model.running)
    {
      pthread_mutex_unlock(&g_lock);
      pthread_attr_destroy(&attr);
      ret = EBUSY;
      goto fail;
    }
  revision = g_model.revision + 1;
  memset(&g_model, 0, sizeof(g_model));
  g_model.revision = revision;
  g_model.status = ESPHOME_CONNECTING;
  g_model.running = true;
  g_model.encrypted = config->noise_psk[0] != 0;
  g_stop = false;
  g_head = g_count = 0;
  ret = pthread_create(&thread, &attr, worker, copy);
  if (ret)
    {
      g_model.running = false;
      g_model.status = ESPHOME_ERROR;
      g_model.error = -ret;
    }
  pthread_mutex_unlock(&g_lock);
  pthread_attr_destroy(&attr);
  if (!ret) return 0;
fail:
  esphome_secure_wipe(copy, sizeof(*copy));
  free(copy);
  return -ret;
}

void esphome_client_stop(void)
{
  pthread_mutex_lock(&g_lock);
  g_stop = true;
  g_count = 0;
  g_model.status = g_model.running ? ESPHOME_STOPPING : ESPHOME_IDLE;
  for (size_t i = 0; i < g_model.count; ++i) g_model.entities[i].has_state = false;
  ++g_model.revision;
  pthread_mutex_unlock(&g_lock);
}

void esphome_client_snapshot(struct esphome_snapshot *snapshot)
{
  if (!snapshot) return;
  pthread_mutex_lock(&g_lock);
  *snapshot = g_model;
  pthread_mutex_unlock(&g_lock);
}

int esphome_client_command(uint32_t key, uint32_t device_id, bool state,
                           bool has_brightness, float brightness)
{
  const struct esphome_entity *entity = NULL;
  struct command command = {0};
  struct eh_pb_writer w = {command.data, sizeof(command.data), 0, 0};
  int ret = -ENOENT;
  pthread_mutex_lock(&g_lock);
  if (g_stop || g_model.status != ESPHOME_READY)
    {ret = -ENOTCONN; goto out;}
  for (size_t i = 0; i < g_model.count; ++i)
    {
      const struct esphome_entity *candidate = &g_model.entities[i];
      if (candidate->key != key || candidate->device_id != device_id) continue;
      if (candidate->kind != ESPHOME_LIGHT && candidate->kind != ESPHOME_SWITCH)
        continue;
      if (entity) {ret = -EEXIST; goto out;}
      entity = candidate;
    }
  if (!entity) goto out;
  if (g_count == COMMAND_QUEUE_SIZE) {ret = -EBUSY; goto out;}
  ret = eh_model_command(entity, state, has_brightness, brightness, &w,
                         &command.type);
  if (ret < 0) goto out;
  command.size = w.size;
  g_commands[(g_head + g_count) % COMMAND_QUEUE_SIZE] = command;
  ++g_count;
out:
  pthread_mutex_unlock(&g_lock);
  return ret;
}

const char *esphome_client_error(int error)
{
  switch (-error)
    {
      case 0: return "";
      case EINVAL: return "请检查 IPv4 地址、端口和密钥格式";
      case EACCES: return "认证失败：请检查加密密钥；旧版密码认证不受支持";
      case EHOSTUNREACH: return "设备名称不匹配，或网络不可达";
      case ENETUNREACH: return "网络不可达，请检查设备网络连接";
      case ECONNREFUSED: return "连接被拒绝，请检查设备地址和 API 端口";
      case ETIMEDOUT: return "连接或响应超时，可以重试";
      case ECONNRESET: return "设备已断开连接，可以重试";
      case EPROTO: return "设备返回了无效协议数据";
      case EPROTONOSUPPORT: return "设备的 API 版本不受支持";
      case ENOTSUP: return "设备加密模式或实体功能不匹配";
      case EMSGSIZE: return "设备报文超过容量限制";
      case ENOMEM: return "运行内存不足";
      case EBUSY: return "正在处理，请稍后再试";
      case ENOTCONN: return "设备尚未连接";
      case EAGAIN: return "正在等待设备状态";
      case EEXIST: return "实体标识冲突，已停止操作";
      case ECANCELED: return "连接已取消";
      case ENOENT: return "实体不存在，或系统随机源不可用";
      default: return "通信失败，请检查网络和设备配置";
    }
}
