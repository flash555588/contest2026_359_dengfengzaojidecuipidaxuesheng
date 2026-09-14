/****************************************************************************
 * apps/system/desktop/qpk_homeassistant.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "qpk_homeassistant.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define HA_HOST_MAX 128
#define HA_PATH_MAX 256
#define HA_TOKEN_MAX 512
#define HA_BODY_MAX 2048
#define HA_REQUEST_MAX 4096
#define HA_RESPONSE_MAX 65536
#define HA_TIMEOUT_MS 5000
#define HA_POLL_MS 100

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct ha_request_s
{
  char host[HA_HOST_MAX];
  char path[HA_PATH_MAX];
  char token[HA_TOKEN_MAX];
  char body[HA_BODY_MAX];
  int port;
  bool post;
};

struct ha_state_s
{
  pthread_mutex_t lock;
  bool cancel_requested;
  bool running;
  bool done;
  int status;
  int error;
  char *response;
  struct ha_request_s request;
};

static struct ha_state_s g_ha =
{
  .lock = PTHREAD_MUTEX_INITIALIZER
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool ha_component_valid(const char *text, bool dot)
{
  size_t i;

  if (text == NULL || text[0] == '\0')
    {
      return false;
    }

  for (i = 0; text[i] != '\0'; i++)
    {
      if (!isalnum((unsigned char)text[i]) && text[i] != '_' &&
          text[i] != '-' && (!dot || text[i] != '.'))
        {
          return false;
        }
    }

  return i < 96;
}

static int ha_parse_url(const char *url, char *host, size_t host_size,
                        int *port)
{
  const char *start;
  const char *end;
  const char *colon;
  size_t length;

  if (url == NULL || strncmp(url, "http://", 7) != 0)
    {
      return -EPROTONOSUPPORT;
    }

  start = url + 7;
  end = strchr(start, '/');
  if (end == NULL)
    {
      end = start + strlen(start);
    }

  colon = memchr(start, ':', end - start);
  length = (colon == NULL ? end : colon) - start;
  if (length == 0 || length >= host_size)
    {
      return -EINVAL;
    }

  memcpy(host, start, length);
  host[length] = '\0';
  for (length = 0; host[length] != '\0'; length++)
    {
      if (!isalnum((unsigned char)host[length]) && host[length] != '.' &&
          host[length] != '-' && host[length] != '_')
        {
          return -EINVAL;
        }
    }

  *port = colon == NULL ? 8123 : atoi(colon + 1);
  return *port > 0 && *port <= 65535 ? OK : -EINVAL;
}

static void ha_wipe(void *data, size_t size)
{
  volatile unsigned char *bytes = data;
  while (size-- > 0) *bytes++ = 0;
}

static bool ha_cancelled(void)
{
  pthread_mutex_lock(&g_ha.lock);
  bool cancelled = g_ha.cancel_requested;
  pthread_mutex_unlock(&g_ha.lock);
  return cancelled;
}

static int ha_now(uint64_t *milliseconds)
{
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) return -errno;
  *milliseconds = (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
  return OK;
}

static int ha_check(uint64_t deadline)
{
  uint64_t now;
  if (ha_cancelled()) return -ECANCELED;
  int ret = ha_now(&now);
  if (ret < 0) return ret;
  return now >= deadline ? -ETIMEDOUT : OK;
}

static int ha_wait(int fd, short events, uint64_t deadline)
{
  for (;;)
    {
      uint64_t now;
      if (ha_cancelled()) return -ECANCELED;
      int ret = ha_now(&now);
      if (ret < 0) return ret;
      if (now >= deadline) return -ETIMEDOUT;
      int timeout = deadline - now < HA_POLL_MS ?
                    (int)(deadline - now) : HA_POLL_MS;
      struct pollfd pollfd = {.fd = fd, .events = events};
      ret = poll(&pollfd, 1, timeout);
      if (ret < 0)
        {
          if (errno == EINTR) continue;
          return -errno;
        }
      if (ret == 0) continue;
      if (pollfd.revents & POLLNVAL) return -EBADF;
      if (pollfd.revents & (events | POLLERR | POLLHUP))
        return ha_check(deadline);
    }
}

static int ha_send_all(int fd, const char *data, size_t length, uint64_t deadline)
{
  while (length > 0)
    {
      int ret = ha_check(deadline);
      if (ret < 0) return ret;
      int flags = 0;
#ifdef MSG_NOSIGNAL
      flags = MSG_NOSIGNAL;
#endif
      ssize_t sent = send(fd, data, length, flags);
      if (sent < 0)
        {
          if (errno == EINTR) continue;
          if (errno != EAGAIN && errno != EWOULDBLOCK) return -errno;
          ret = ha_wait(fd, POLLOUT, deadline);
          if (ret < 0) return ret;
          continue;
        }
      if (sent == 0) return -EIO;
      data += sent;
      length -= (size_t)sent;
    }
  return OK;
}

static int ha_connect(const struct addrinfo *address, uint64_t deadline)
{
  int ret = ha_check(deadline);
  if (ret < 0) return ret;
  int fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
  if (fd < 0) return -errno;
  int flags = fcntl(fd, F_GETFL);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
      ret = -errno;
      goto failed;
    }
  if (connect(fd, address->ai_addr, address->ai_addrlen) < 0)
    {
      if (errno != EINPROGRESS && errno != EALREADY && errno != EINTR)
        {
          ret = -errno;
          goto failed;
        }
      ret = ha_wait(fd, POLLOUT, deadline);
      if (ret < 0) goto failed;
      int error = 0;
      socklen_t length = sizeof(error);
      if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0)
        {
          ret = -errno;
          goto failed;
        }
      if (error) { ret = -error; goto failed; }
    }
  ret = ha_check(deadline);
  if (ret >= 0) return fd;
failed:
  close(fd);
  return ret;
}

static int ha_decode_chunked(char *body, size_t *length)
{
  char *input = body;
  char *output = body;
  char *end = body + *length;

  while (input < end)
    {
      char *line_end = strstr(input, "\r\n");
      unsigned long chunk;

      if (line_end == NULL)
        {
          return -EPROTO;
        }

      *line_end = '\0';
      chunk = strtoul(input, NULL, 16);
      input = line_end + 2;
      if (chunk == 0)
        {
          *length = output - body;
          body[*length] = '\0';
          return OK;
        }

      if (chunk > (unsigned long)(end - input) || output + chunk > end)
        {
          return -EOVERFLOW;
        }

      memmove(output, input, chunk);
      output += chunk;
      input += chunk;
      if (input + 2 > end || input[0] != '\r' || input[1] != '\n')
        {
          return -EPROTO;
        }

      input += 2;
    }

  return -EPROTO;
}

static int ha_request_run(const struct ha_request_s *request, int *status,
                          char **response_out, size_t response_size)
{
  struct addrinfo hints;
  struct addrinfo *addresses = NULL;
  struct addrinfo *address;
  uint64_t deadline;
  char port_text[8];
  char message[HA_REQUEST_MAX];
  char *response = NULL;
  char *headers;
  char *body;
  size_t used = 0;
  size_t body_length;
  ssize_t received;
  int fd = -1;
  int ret;

  *response_out = NULL;
  ret = ha_now(&deadline);
  if (ret < 0) return ret;
  deadline += HA_TIMEOUT_MS;
  ret = ha_check(deadline);
  if (ret < 0) return ret;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  snprintf(port_text, sizeof(port_text), "%d", request->port);
  ret = getaddrinfo(request->host, port_text, &hints, &addresses);
  if (ret != 0)
    {
      ret = ha_cancelled() ? -ECANCELED : -EHOSTUNREACH;
      goto finished;
    }

  ret = -ECONNREFUSED;
  for (address = addresses; address != NULL; address = address->ai_next)
    {
      ret = ha_connect(address, deadline);
      if (ret >= 0)
        {
          fd = ret;
          break;
        }
      if (ret == -ECANCELED || ret == -ETIMEDOUT) break;
    }

  freeaddrinfo(addresses);
  addresses = NULL;
  if (fd < 0) goto finished;
  /* Do not retain a 64 KiB response allocation while DNS is still pending. */
  response = malloc(response_size);
  if (response == NULL)
    {
      ret = -ENOMEM;
      goto finished;
    }

  if (request->post)
    {
      ret = snprintf(message, sizeof(message),
                     "POST %s HTTP/1.1\r\nHost: %s:%d\r\n"
                     "Authorization: Bearer %s\r\n"
                     "Accept: application/json\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %lu\r\nConnection: close\r\n\r\n%s",
                     request->path, request->host, request->port,
                     request->token,
                     (unsigned long)strlen(request->body), request->body);
    }
  else
    {
      ret = snprintf(message, sizeof(message),
                     "GET %s HTTP/1.1\r\nHost: %s:%d\r\n"
                     "Authorization: Bearer %s\r\n"
                     "Accept: application/json\r\nConnection: close\r\n\r\n",
                     request->path, request->host, request->port,
                     request->token);
    }

  if (ret < 0 || (size_t)ret >= sizeof(message))
    {
      ret = -E2BIG;
      goto finished;
    }

  ret = ha_send_all(fd, message, (size_t)ret, deadline);
  if (ret < 0) goto finished;

  while (used + 1 < response_size)
    {
      ret = ha_check(deadline);
      if (ret < 0) goto finished;
      received = recv(fd, response + used, response_size - used - 1, 0);
      if (received == 0)
        {
          break;
        }

      if (received < 0)
        {
          if (errno == EINTR) continue;
          if (errno != EAGAIN && errno != EWOULDBLOCK)
            {
              ret = -errno;
              goto finished;
            }
          ret = ha_wait(fd, POLLIN, deadline);
          if (ret < 0) goto finished;
          continue;
        }

      used += received;
    }

  response[used] = '\0';
  if (used + 1 == response_size)
    {
      ret = -EOVERFLOW;
      goto finished;
    }

  if (sscanf(response, "HTTP/%*u.%*u %d", status) != 1)
    {
      ret = -EPROTO;
      goto finished;
    }

  headers = strstr(response, "\r\n\r\n");
  if (headers == NULL)
    {
      ret = -EPROTO;
      goto finished;
    }

  body = headers + 4;
  body_length = used - (body - response);
  if (strcasestr(response, "transfer-encoding: chunked") != NULL)
    {
      ret = ha_decode_chunked(body, &body_length);
      if (ret < 0)
        {
          goto finished;
        }
    }

  memmove(response, body, body_length);
  response[body_length] = '\0';
  ret = ha_check(deadline);
finished:
  if (addresses != NULL) freeaddrinfo(addresses);
  if (fd >= 0 && close(fd) < 0 && ret >= 0) ret = -errno;
  ha_wipe(message, sizeof(message));
  if (ret < 0) free(response);
  else *response_out = response;
  return ret;
}

static void *ha_worker(pthread_addr_t arg)
{
  char *response = NULL;
  int status = 0;
  int ret;

  (void)arg;
  /* The request stays immutable until running becomes false. No second
   * request or UI stop may overwrite it while this worker uses it.
   */
  ret = ha_request_run(&g_ha.request, &status, &response, HA_RESPONSE_MAX);

  pthread_mutex_lock(&g_ha.lock);
  if (g_ha.cancel_requested)
    {
      pthread_mutex_unlock(&g_ha.lock);
      free(response);
      response = NULL;
      pthread_mutex_lock(&g_ha.lock);
    }
  g_ha.error = g_ha.cancel_requested ? 0 : ret;
  g_ha.status = g_ha.cancel_requested ? 0 : status;
  g_ha.response = response;
  g_ha.done = !g_ha.cancel_requested;
  ha_wipe(&g_ha.request, sizeof(g_ha.request));
  g_ha.running = false;
  pthread_mutex_unlock(&g_ha.lock);
  return NULL;
}

static int ha_start(const char *base_url, const char *token,
                    const char *path, const char *body)
{
  pthread_attr_t attr;
  bool attr_initialized = false;
  int ret;

  if (token == NULL || token[0] == '\0' || strlen(token) >= HA_TOKEN_MAX ||
      strchr(token, '\r') != NULL || strchr(token, '\n') != NULL)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_ha.lock);
  if (g_ha.running)
    {
      pthread_mutex_unlock(&g_ha.lock);
      return -EBUSY;
    }

  memset(&g_ha.request, 0, sizeof(g_ha.request));
  ret = ha_parse_url(base_url, g_ha.request.host,
                     sizeof(g_ha.request.host), &g_ha.request.port);
  if (ret < 0 || strlcpy(g_ha.request.path, path,
                         sizeof(g_ha.request.path)) >=
                 sizeof(g_ha.request.path))
    {
      pthread_mutex_unlock(&g_ha.lock);
      return ret < 0 ? ret : -E2BIG;
    }

  strlcpy(g_ha.request.token, token, sizeof(g_ha.request.token));
  if (body != NULL)
    {
      strlcpy(g_ha.request.body, body, sizeof(g_ha.request.body));
      g_ha.request.post = true;
    }

  g_ha.done = false;
  g_ha.status = 0;
  g_ha.error = 0;
  free(g_ha.response);
  g_ha.response = NULL;
  g_ha.cancel_requested = false;
  g_ha.running = true;
  pthread_mutex_unlock(&g_ha.lock);

  ret = pthread_attr_init(&attr);
  if (ret == 0)
    {
      attr_initialized = true;
#ifdef __NuttX__
      size_t stack = 12288;
#else
      size_t stack = 64 * 1024;
#endif
#ifdef PTHREAD_STACK_MIN
      if (stack < (size_t)PTHREAD_STACK_MIN) stack = PTHREAD_STACK_MIN;
#endif
      ret = pthread_attr_setstacksize(&attr, stack);
      if (ret == 0) ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    }

  if (ret == 0)
    {
      pthread_t thread;
      ret = pthread_create(&thread, &attr, ha_worker, NULL);
    }

  if (attr_initialized)
    {
      pthread_attr_destroy(&attr);
    }

  if (ret != 0)
    {
      pthread_mutex_lock(&g_ha.lock);
      g_ha.running = false;
      ha_wipe(&g_ha.request, sizeof(g_ha.request));
      pthread_mutex_unlock(&g_ha.lock);
      return -ret;
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int qpk_ha_get_state(const char *base_url, const char *token,
                     const char *entity_id)
{
  char path[HA_PATH_MAX];

  if (!ha_component_valid(entity_id, true))
    {
      return -EINVAL;
    }

  snprintf(path, sizeof(path), "/api/states/%s", entity_id);
  return ha_start(base_url, token, path, NULL);
}

int qpk_ha_get(const char *base_url, const char *token, const char *resource)
{
  const char *path;

  if (resource == NULL) return -EINVAL;
  if (strcmp(resource, "config") == 0)
    {
      path = "/api/config";
    }
  else if (strcmp(resource, "states") == 0)
    {
      path = "/api/states";
    }
  else if (strcmp(resource, "services") == 0)
    {
      path = "/api/services";
    }
  else
    {
      return -EINVAL;
    }

  return ha_start(base_url, token, path, NULL);
}

int qpk_ha_call_service(const char *base_url, const char *token,
                        const char *domain, const char *service,
                        const char *data)
{
  char path[HA_PATH_MAX];

  if (!ha_component_valid(domain, false) ||
      !ha_component_valid(service, false) ||
      data == NULL || data[0] != '{' || strlen(data) >= HA_BODY_MAX)
    {
      return -EINVAL;
    }

  snprintf(path, sizeof(path), "/api/services/%s/%s", domain, service);
  return ha_start(base_url, token, path, data);
}

void qpk_ha_poll(struct qpk_ha_result_s *result)
{
  if (result == NULL) return;
  memset(result, 0, sizeof(*result));

  pthread_mutex_lock(&g_ha.lock);
  result->busy = g_ha.running;
  result->done = g_ha.done;
  result->status = g_ha.status;
  result->error = g_ha.error;
  result->body = g_ha.response;
  g_ha.response = NULL;
  g_ha.done = false;
  pthread_mutex_unlock(&g_ha.lock);
}

void qpk_ha_stop(void)
{
  char *response;
  pthread_mutex_lock(&g_ha.lock);
  g_ha.cancel_requested = true;
  g_ha.done = false;
  g_ha.error = 0;
  g_ha.status = 0;
  response = g_ha.response;
  g_ha.response = NULL;
  if (!g_ha.running) ha_wipe(&g_ha.request, sizeof(g_ha.request));
  pthread_mutex_unlock(&g_ha.lock);
  free(response);
}
