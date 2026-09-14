#include "native_transport.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define FRAME_LIMIT 16384u

static int64_t now_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int wait_io(int fd, short events, int64_t deadline)
{
  struct pollfd pfd = {.fd = fd, .events = events};
  for (;;)
    {
      int64_t remaining = deadline - now_ms();
      int ret;
      if (remaining <= 0) return -ETIMEDOUT;
      ret = poll(&pfd, 1, remaining > INT_MAX ? INT_MAX : (int)remaining);
      if (ret < 0 && errno == EINTR) continue;
      if (ret < 0) return -errno;
      if (ret == 0) return -ETIMEDOUT;
      if (pfd.revents & events) return 0;
      return -ECONNRESET;
    }
}

static int transfer(int fd, void *data, size_t length, bool sending,
                    int64_t deadline)
{
  uint8_t *bytes = data;
  while (length)
    {
      int ret = wait_io(fd, sending ? POLLOUT : POLLIN, deadline);
      ssize_t count;
      if (ret < 0) return ret;
      if (sending)
        {
#ifdef MSG_NOSIGNAL
          count = send(fd, bytes, length, MSG_NOSIGNAL);
#else
          count = send(fd, bytes, length, 0);
#endif
        }
      else count = recv(fd, bytes, length, 0);
      if (count < 0 && (errno == EINTR || errno == EAGAIN ||
                        errno == EWOULDBLOCK)) continue;
      if (count < 0) return -errno;
      if (count == 0) return -ECONNRESET;
      bytes += count;
      length -= count;
    }
  return 0;
}

static size_t encode_varint(uint8_t *out, uint32_t value)
{
  size_t n = 0;
  do
    {
      out[n] = value & 127;
      value >>= 7;
      if (value) out[n] |= 128;
      n++;
    }
  while (value);
  return n;
}

static int read_varint(int fd, uint32_t *value, int64_t deadline)
{
  *value = 0;
  for (unsigned i = 0; i < 5; i++)
    {
      uint8_t byte;
      int ret = transfer(fd, &byte, 1, false, deadline);
      if (ret < 0) return ret;
      if (i == 4 && (byte & 0xf0)) return -EPROTO;
      *value |= (uint32_t)(byte & 127) << (7 * i);
      if (!(byte & 128)) return 0;
    }
  return -EPROTO;
}

int esphome_tcp_connect(const char *address, uint16_t port, int timeout_ms)
{
  struct sockaddr_in peer = {0};
  int fd;
  int ret;
  int error = 0;
  socklen_t size = sizeof(error);
  if (!address || !port || timeout_ms <= 0) return -EINVAL;
  peer.sin_family = AF_INET;
  peer.sin_port = htons(port);
  if (inet_pton(AF_INET, address, &peer.sin_addr) != 1) return -EINVAL;
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -errno;
  if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {ret = -errno; goto fail;}
  ret = connect(fd, (struct sockaddr *)&peer, sizeof(peer));
  if (ret == 0) return fd;
  if (errno != EINPROGRESS) {ret = -errno; goto fail;}
  ret = wait_io(fd, POLLOUT, now_ms() + timeout_ms);
  if (ret < 0) goto fail;
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0)
    {ret = -errno; goto fail;}
  if (error) {ret = -error; goto fail;}
  return fd;
fail:
  close(fd);
  return ret;
}

int esphome_tcp_send(int fd, uint32_t type, const void *body, size_t length,
                     int timeout_ms)
{
  uint8_t header[11] = {0};
  size_t count;
  int ret;
  int64_t deadline;
  if (fd < 0 || !type || length > FRAME_LIMIT || (!body && length) ||
      timeout_ms <= 0) return -EINVAL;
  deadline = now_ms() + timeout_ms;
  count = 1 + encode_varint(header + 1, (uint32_t)length);
  count += encode_varint(header + count, type);
  ret = transfer(fd, header, count, true, deadline);
  return ret < 0 ? ret : transfer(fd, (void *)body, length, true, deadline);
}

int esphome_tcp_receive(int fd, uint32_t *type, void *body, size_t capacity,
                        size_t *length, int timeout_ms)
{
  uint8_t marker;
  uint32_t size;
  int ret;
  int64_t deadline;
  if (fd < 0 || !type || !length || (!body && capacity) || timeout_ms <= 0)
    return -EINVAL;
  *length = 0;
  *type = 0;
  deadline = now_ms() + timeout_ms;
  ret = transfer(fd, &marker, 1, false, deadline);
  if (ret < 0) return ret;
  if (marker == 1) return -ENOTSUP; /* Noise requires a separate transport. */
  if (marker != 0) return -EPROTO;
  ret = read_varint(fd, &size, deadline);
  if (ret < 0) return ret;
  ret = read_varint(fd, type, deadline);
  if (ret < 0) return ret;
  if (!*type) return -EPROTO;
  if (size > FRAME_LIMIT || size > capacity) return -EMSGSIZE;
  ret = transfer(fd, body, size, false, deadline);
  if (ret == 0) *length = size;
  return ret;
}
