/* SPDX-License-Identifier: Apache-2.0 */
#include "bili_http.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include "glass_https.h"
#include "bili_roots.h"
#define JSON_LIMIT (256 * 1024)
#define IO_TIMEOUT 10000
struct bili_http {
  int fd;
  bool tls, chunked, chunk_crlf, eof;
  size_t chunk_left;
  uint8_t read_buffer[16384];
  size_t read_position,read_size;
  long long remaining;
  atomic_bool *cancel;
  struct webclient_tls_connection *connection;
  struct glass_https_diagnostic diagnostic;
};
static int waitfd(struct bili_http *h, short events)
{
  struct pollfd p = {h->fd, events, 0};
  int elapsed;
  for (elapsed = 0; elapsed < IO_TIMEOUT; elapsed += 100) {
    int r;
    if (atomic_load(h->cancel)) return -ECANCELED;
    r = poll(&p, 1, 100);
    if (r < 0 && errno != EINTR) return -errno;
    if (r > 0) return 0;
  }
  return -ETIMEDOUT;
}
static int plain_send(struct bili_http *h, const uint8_t *p, size_t n)
{
  for (;;) {
    if (atomic_load(h->cancel)) return -ECANCELED;
    int r = send(h->fd, p, n, 0);
    if (r >= 0) return r;
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return -errno;
    int w = waitfd(h, POLLOUT); if (w) return w;
  }
}
static int transport_read(struct bili_http *h, uint8_t *p, size_t n)
{
  if (h->tls) return glass_https_tls_ops()->recv(NULL, h->connection, p, n);
  for (;;) {
    if (atomic_load(h->cancel)) return -ECANCELED;
    int r = recv(h->fd, p, n, 0);
    if (r >= 0) return r;
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return -errno;
    int w = waitfd(h, POLLIN); if (w) return w;
  }
}
/* C6 socket/TLS calls cross the network transport. Serve FLV tag headers,
 * chunk delimiters and HTTP header characters from the same bounded cache. */
static int raw_read(struct bili_http *h,uint8_t *p,size_t n)
{
  if(atomic_load(h->cancel))return -ECANCELED;
  if(!n)return 0;
  if(h->read_position==h->read_size){
    int r=transport_read(h,h->read_buffer,sizeof(h->read_buffer));
    if(r<=0)return r;
    h->read_position=0;h->read_size=r;
  }
  size_t bytes=h->read_size-h->read_position;if(bytes>n)bytes=n;
  memcpy(p,h->read_buffer+h->read_position,bytes);h->read_position+=bytes;
  return bytes;
}
static int raw_exact(struct bili_http *h, uint8_t *p, size_t n)
{
  while (n) {
    int r = raw_read(h, p, n);
    if (r <= 0) return r ? r : -EPIPE;
    p += r; n -= r;
  }
  return 0;
}
static int line(struct bili_http *h, char *p, size_t n)
{
  size_t i = 0;
  for (;;) {
    uint8_t c;
    int r = raw_exact(h, &c, 1);
    if (r) return r;
    if (c == '\n') {
      if (!i || p[i - 1] != '\r') return -EPROTO;
      p[i - 1] = 0;
      return 0;
    }
    if (i + 1 >= n) return -EFBIG;
    p[i++] = c;
  }
}
int bili_http_read(struct bili_http *h, uint8_t *dst, size_t n)
{
  int r;
  if (!n || h->eof) return 0;
  if (h->chunked) {
    if (!h->chunk_left) {
      char l[128], *end;
      unsigned long v;
      if (h->chunk_crlf) {
        uint8_t crlf[2];
        if ((r = raw_exact(h, crlf, 2))) return r;
        if (memcmp(crlf, "\r\n", 2)) return -EPROTO;
      }
      if ((r = line(h, l, sizeof(l)))) return r;
      errno = 0;
      if (!((l[0] >= '0' && l[0] <= '9') ||
            (l[0] >= 'a' && l[0] <= 'f') || (l[0] >= 'A' && l[0] <= 'F')))
        return -EPROTO;
      v = strtoul(l, &end, 16);
      if (errno || (*end && *end != ';') || v > INT_MAX) return -EPROTO;
      h->chunk_left = v;
      h->chunk_crlf = true;
      if (!v) { h->eof = true; return 0; }
    }
    if (n > h->chunk_left) n = h->chunk_left;
  } else if (h->remaining >= 0) {
    if (!h->remaining) { h->eof = true; return 0; }
    if ((unsigned long long)h->remaining < n) n = h->remaining;
  }
  r = raw_read(h, dst, n);
  if (r > 0) {
    if (h->chunked) h->chunk_left -= r;
    else if (h->remaining >= 0) h->remaining -= r;
  } else if (!r && (h->chunked || h->remaining > 0)) return -EPIPE;
  return r;
}
void bili_http_close(struct bili_http *h)
{
  if (!h) return;
  if (h->fd >= 0) close(h->fd);
  if (h->connection) glass_https_tls_ops()->close(NULL, h->connection);
  free(h);
}
static struct bili_http *open_request(const char *url, const char *cookie,
                                      const char *referer,
                                      atomic_bool *cancel, int *error,
                                      unsigned redirects)
{
  struct bili_http *h = calloc(1, sizeof(*h));
  struct addrinfo hints = {0}, *ai = NULL, *a;
  char host[256], port[8], l[4096], location[2048] = {0};
  const char *p, *path;
  size_t hostlen, sent = 0;
  int r = -EINVAL, status, lines;
  if (!h) { *error = -ENOMEM; return NULL; }
  h->fd = -1; h->remaining = -1; h->cancel = cancel;
  if (!strncmp(url, "https://", 8)) { h->tls = true; p = url + 8; }
  else if (!strncmp(url, "http://", 7)) p = url + 7;
  else goto fail;
  if (strpbrk(url, "\r\n") || strpbrk(cookie, "\r\n") || strpbrk(referer, "\r\n")) goto fail;
  path = strchr(p, '/');
  if (!path) path = p + strlen(p);
  hostlen = path - p;
  if (!hostlen || hostlen >= sizeof(host) || memchr(p, '@', hostlen) ||
      memchr(p, ':', hostlen)) goto fail;
  memcpy(host, p, hostlen); host[hostlen] = 0;
  snprintf(port, sizeof(port), "%d", h->tls ? 443 : 80);
  if (h->tls) {
    struct glass_https_request request = {
      .ca_pem = (const unsigned char *)bili_roots,
      .ca_size = sizeof(bili_roots),
      .deadline_ms = glass_https_milliseconds() + IO_TIMEOUT,
      .idle_timeout_ms = IO_TIMEOUT,
      .abort_flag = cancel,
      .diagnostic = &h->diagnostic
    };
    r = glass_https_tls_ops()->connect(&request, host, port, 10, &h->connection);
    if (r) {
      fprintf(stderr, "bili: TLS stage=%s error=%d tls=%d verify=0x%lx\n",
              h->diagnostic.stage ? h->diagnostic.stage : "init", r,
              h->diagnostic.tls_error,
              (unsigned long)h->diagnostic.verify_flags);
      goto fail;
    }
  } else {
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    /* System resolver timeout is independent of the 10s socket timeout. */
    r = getaddrinfo(host, port, &hints, &ai);
    if (r) { r = -EHOSTUNREACH; goto fail; }
    r = -ECONNREFUSED;
    for (a = ai; a; a = a->ai_next) {
      int e = 0; socklen_t elen = sizeof(e);
      if (atomic_load(cancel)) { r = -ECANCELED; break; }
      h->fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
      if (h->fd < 0) continue;
      if (fcntl(h->fd, F_SETFL, O_NONBLOCK) < 0) {
        close(h->fd); h->fd = -1; continue;
      }
      if (connect(h->fd, a->ai_addr, a->ai_addrlen) == 0) { r = 0; break; }
      if (errno == EINPROGRESS && !(r = waitfd(h, POLLOUT)) &&
          !getsockopt(h->fd, SOL_SOCKET, SO_ERROR, &e, &elen) && !e) break;
      r = e ? -e : (r < 0 ? r : -ECONNREFUSED);
      close(h->fd); h->fd = -1;
    }
    freeaddrinfo(ai); ai = NULL;
    if (h->fd < 0 || r < 0) goto fail;
  }
  r = snprintf(l, sizeof(l),
       "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n"
       "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
       "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/138.0.0.0 Safari/537.36\r\n"
       "Referer: %s\r\n"
       "Accept-Encoding: identity\r\n"
       "%s%s%s\r\n", *path ? path : "/", host, referer,
       *cookie ? "Cookie: " : "", cookie, *cookie ? "\r\n" : "");
  if (r < 0 || (size_t)r >= sizeof(l)) { r = -EFBIG; goto fail; }
  while (sent < strlen(l)) {
    r = h->tls ? glass_https_tls_ops()->send(NULL, h->connection,
                                              (uint8_t *)l + sent, strlen(l) - sent) :
                 plain_send(h, (uint8_t *)l + sent, strlen(l) - sent);
    if (r > 0) sent += r;
    else { if (!r) r = -EPIPE; goto fail; }
  }
  if ((r = line(h, l, sizeof(l)))) goto fail;
  status = 0;
  if (sscanf(l, "HTTP/%*u.%*u %d", &status) != 1 ||
      (status != 200 && status != 301 && status != 302 && status != 303 &&
       status != 307 && status != 308)) {
    fprintf(stderr, "bili: HTTP response status=%d\n", status);
    r = -EPROTO; goto fail;
  }
  for (lines = 0; lines < 64; lines++) {
    if ((r = line(h, l, sizeof(l)))) goto fail;
    if (!*l) break;
    if (!strncasecmp(l, "Location:", 9)) {
      p = l + 9; while (*p == ' ') p++;
      if (strlen(p) >= sizeof(location)) { r = -EFBIG; goto fail; }
      strcpy(location, p);
    }
    if (!strncasecmp(l, "Transfer-Encoding:", 18)) {
      p = l + 18; while (*p == ' ') p++;
      if (strcasecmp(p, "chunked")) { r = -ENOTSUP; goto fail; }
      h->chunked = true;
    }
    if (!strncasecmp(l, "Content-Encoding:", 17)) {
      p = l + 17; while (*p == ' ') p++;
      if (strcasecmp(p, "identity")) { r = -ENOTSUP; goto fail; }
    }
    if (!strncasecmp(l, "Content-Length:", 15)) {
      char *end;
      errno = 0; h->remaining = strtoll(l + 15, &end, 10);
      if (errno || end == l + 15 || *end || h->remaining < 0) {
        r = -EPROTO; goto fail;
      }
    }
  }
  if (lines == 64) { r = -EFBIG; goto fail; }
  if (status != 200) {
    char next[2048];
    bool tls = h->tls;
    if (!*location || redirects >= 3) { r = -ELOOP; goto fail; }
    if (location[0] == '/' && location[1] != '/') {
      r = snprintf(next, sizeof(next), "%s://%s%s", tls ? "https" : "http", host, location);
      if (r < 0 || (size_t)r >= sizeof(next)) { r = -EFBIG; goto fail; }
    } else if (!strncmp(location, "//", 2)) {
      r = snprintf(next, sizeof(next), "%s:%s", tls ? "https" : "http", location);
      if (r < 0 || (size_t)r >= sizeof(next)) { r = -EFBIG; goto fail; }
    } else strcpy(next, location);
    /* Never downgrade a verified HTTPS request to plaintext. */
    if (tls && strncmp(next, "https://", 8)) { r = -EACCES; goto fail; }
    bili_http_close(h);
    return open_request(next, cookie, referer, cancel, error, redirects + 1);
  }
  *error = 0; return h;
fail:
  if (ai) freeaddrinfo(ai);
  *error = r < 0 ? r : -EIO;
  bili_http_close(h); return NULL;
}
struct bili_http *bili_http_open(const char *url, const char *cookie,
                                atomic_bool *cancel, int *error)
{
  return open_request(url, cookie, "https://live.bilibili.com/", cancel, error, 0);
}
struct bili_http *bili_http_open_media(const char *url, const char *cookie,
                                      unsigned long room_id,
                                      atomic_bool *cancel, int *error)
{
  char referer[96];
  snprintf(referer, sizeof(referer), "https://live.bilibili.com/%lu", room_id);
  return open_request(url, cookie, referer, cancel, error, 0);
}
struct bili_http *bili_http_open_site(const char *url,const char *cookie,const char *referer,atomic_bool *cancel,int *error)
{ return open_request(url,cookie,referer,cancel,error,0); }
int bili_http_json_site(const char *url, const char *cookie, const char *referer, atomic_bool *cancel,
                   char **json)
{
  int r;
  size_t used = 0;
  char *p;
  struct bili_http *h = bili_http_open_site(url, cookie, referer, cancel, &r);
  *json = NULL;
  if (!h) return r;
  p = malloc(JSON_LIMIT + 1);
  if (!p) { bili_http_close(h); return -ENOMEM; }
  while (used < JSON_LIMIT) {
    r = bili_http_read(h, (uint8_t *)p + used, JSON_LIMIT - used);
    if (r <= 0) break;
    used += r;
  }
  if (used == JSON_LIMIT) r = -EFBIG;
  bili_http_close(h);
  if (r < 0) { free(p); return r; }
  p[used] = 0; *json = p; return 0;
}

int bili_http_json(const char *url,const char *cookie,atomic_bool *cancel,char **json)
{ return bili_http_json_site(url,cookie,"https://live.bilibili.com/",cancel,json); }
