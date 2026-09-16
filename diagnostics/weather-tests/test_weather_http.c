/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_weather.h"
#include <netutils/webclient.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct response { const char *text; size_t length; int connect_error; int read_error; unsigned opened, closed; };
struct webclient_tls_connection { size_t offset; };

static int connect_test(void *arg, const char *host, const char *port,
                        unsigned timeout, struct webclient_tls_connection **out)
{
  struct response *response = arg;
  assert(!strcmp(host, "uapis.cn") && !strcmp(port, "443") && timeout == 10);
  *out = NULL;
  if (response->connect_error) return response->connect_error;
  *out = calloc(1, sizeof(**out));
  assert(*out);
  response->opened++;
  return 0;
}
static ssize_t send_test(void *arg, struct webclient_tls_connection *conn,
                         const void *data, size_t length)
{ (void)arg; (void)conn; (void)data; return length > 11 ? 11 : length; }
static ssize_t recv_test(void *arg, struct webclient_tls_connection *conn,
                         void *data, size_t length)
{
  struct response *response = arg;
  if (response->read_error) return response->read_error;
  size_t remaining = response->length - conn->offset;
  if (length > 17) length = 17;
  if (length > remaining) length = remaining;
  memcpy(data, response->text + conn->offset, length);
  conn->offset += length;
  return length;
}
static int close_test(void *arg, struct webclient_tls_connection *conn)
{
  struct response *response = arg;
  response->closed++;
  assert(response->closed == response->opened);
  free(conn);
  return 0;
}
static void check(struct response *response, int expected)
{
  static const struct webclient_tls_ops ops = {
    .connect = connect_test, .send = send_test, .recv = recv_test, .close = close_test};
  struct glass_weather_data data, before;
  memset(&data, 0x5a, sizeof(data));
  before = data;
  int result = glass_weather_http(&data, &ops, response);
  if (result != expected) fprintf(stderr, "expected=%d actual=%d response=%.100s\n", expected, result, response->text);
  assert(result == expected);
  assert(response->opened == response->closed);
  if (expected) assert(!memcmp(&data, &before, sizeof(data)));
  else assert(data.aqi == 14 && data.temperature == 18 && data.humidity == 93);
}
int main(void)
{
  const char *body = "{\"city\":\"成都市\",\"temperature\":18,\"humidity\":93,\"aqi\":14}";
  char text[6000];
  for (unsigned i = 0; i < 40; i++)
    {
      snprintf(text, sizeof(text), "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n%s", strlen(body), body);
      struct response response = {.text = text, .length = strlen(text)};
      check(&response, 0);
      response.read_error = -EPROTO;
      check(&response, -EPROTO);
      response.read_error = 0;
      response.connect_error = -EACCES;
      check(&response, -EACCES);
    }
  snprintf(text, sizeof(text), "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n%zx\r\n%s\r\n0\r\n\r\n", strlen(body), body);
  struct response response = {.text = text, .length = strlen(text)};
  check(&response, 0);
  const char *cases[] = {
    "HTTP/1.1 429 Too Many Requests\r\nContent-Length: 2\r\n\r\n{}",
    "HTTP/1.1 500 Error\r\nContent-Length: 2\r\n\r\n{}",
    "HTTP/1.1 302 Found\r\nLocation: http://uapis.cn/unsafe\r\nContent-Length: 0\r\n\r\n",
    "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 2\r\n\r\n{}",
    "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n{\"city\":",
  };
  int errors[] = {-EAGAIN, -EREMOTEIO, -EACCES, -ENOTSUP, -EPROTO};
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
      response.text = cases[i]; response.length = strlen(cases[i]);
      check(&response, errors[i]);
    }
  int header = snprintf(text, sizeof(text), "HTTP/1.1 200 OK\r\nContent-Length: 5000\r\n\r\n");
  memset(text + header, ' ', 5000);
  response.text = text; response.length = header + 5000;
  check(&response, -EFBIG);
  puts("PASS: actual NuttX webclient + production weather HTTP: repeated success/failure, connection ownership, short I/O, chunked data, HTTP errors, redirect rejection and size limits");
  return 0;
}
