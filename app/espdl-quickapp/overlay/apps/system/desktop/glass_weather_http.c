/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "glass_weather.h"
#include <netutils/webclient.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct weather_body
{
  size_t used;
  char body[GLASS_WEATHER_BODY_MAX + 1];
  char buffer[2048];
};

static int body_sink(char **buffer, int offset, int end, int *length, void *arg)
{
  struct weather_body *request = arg;
  if (offset < 0 || end < offset || end > *length) return -EINVAL;
  size_t bytes = end - offset;
  if (bytes > GLASS_WEATHER_BODY_MAX - request->used) return -EFBIG;
  memcpy(request->body + request->used, *buffer + offset, bytes);
  request->used += bytes;
  request->body[request->used] = 0;
  return 0;
}

static int header_sink(const char *line, bool truncated, void *arg)
{
  (void)arg;
  (void)truncated;
  /* The fixed API has no redirects. Never follow a downgrade to plaintext. */
  if (!strncasecmp(line, "location:", 9)) return -EACCES;
  if (!strncasecmp(line, "content-encoding:", 17) &&
      !strstr(line, "identity")) return -ENOTSUP;
  return 0;
}

int glass_weather_http(struct glass_weather_data *out,
                        const struct webclient_tls_ops *tls, void *tls_context)
{
  static const char *const headers[] = {"Accept: application/json", "Accept-Encoding: identity"};
  struct weather_body *request = calloc(1, sizeof(*request));
  if (!request) return -ENOMEM;
  struct webclient_context client;
  webclient_set_defaults(&client);
  client.url = "https://uapis.cn/api/v1/misc/weather?extended=true";
  client.protocol_version = WEBCLIENT_PROTOCOL_VERSION_HTTP_1_1;
  client.headers = headers;
  client.nheaders = sizeof(headers) / sizeof(headers[0]);
  client.timeout_sec = 10;
  client.buffer = request->buffer;
  client.buflen = sizeof(request->buffer);
  client.tls_ops = tls;
  client.tls_ctx = tls_context;
  client.sink_callback = body_sink;
  client.sink_callback_arg = request;
  client.header_callback = header_sink;
  int ret = webclient_perform(&client);
  printf("[weather] HTTP %u bytes=%u ret=%d\n", client.http_status,
         (unsigned)request->used, ret);
  if (!ret && client.http_status != 200)
    ret = client.http_status == 429 ? -EAGAIN : -EREMOTEIO;
  if (!ret) ret = glass_weather_parse(request->body, request->used, out);
  /* Blocking perform releases its work/connection on success AND error.
   * abort is only legal after a pending nonblocking -EAGAIN. */
  free(request);
  return ret;
}
