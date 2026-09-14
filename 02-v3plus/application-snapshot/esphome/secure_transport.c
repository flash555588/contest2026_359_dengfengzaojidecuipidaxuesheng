/* SPDX-License-Identifier: MIT
 * ESPHome encrypted native API transport for openvela/POSIX.
 * Wire reference: aioesphomeapi 46475a3b8767bd28c4487fc263444798a34b5ede,
 * aioesphomeapi/_frame_helper/noise.py. See THIRD_PARTY.md.
 * Cryptographic operations are performed by the vendored MIT noise-c.
 */
#include "secure_transport.h"
#include <noise/protocol.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#define BODY_LIMIT 16384u
#define TAG_SIZE 16u
#define INNER_HEADER_SIZE 4u
#define FRAME_LIMIT (BODY_LIMIT + INNER_HEADER_SIZE + TAG_SIZE)
#define HELLO_LIMIT 256u
#define HANDSHAKE_SIZE 49u /* status + X25519 public key + authentication tag */

struct esphome_secure
{
  int fd;
  int failure;
  NoiseCipherState *send;
  NoiseCipherState *receive;
  /* One worker owns a context. Keep large messages off the task stack. */
  uint8_t frame[FRAME_LIMIT];
};

void esphome_secure_wipe(void *data, size_t size)
{
  if (data) noise_clean(data, size);
}

static int base64_digit(unsigned char c)
{
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

static int decode_key(const char *key, uint8_t raw[32])
{
  unsigned accumulator = 0;
  unsigned bits = 0;
  size_t written = 0;
  size_t i;
  if (!key) return -EINVAL;
  /* Canonical encoding: 43 base64 digits, one '=', no whitespace. */
  for (i = 0; i < 44 && key[i]; ++i) {}
  if (i != 44 || key[44] || key[43] != '=') return -EINVAL;
  for (i = 0; i < 43; ++i)
    {
      int digit = base64_digit((unsigned char)key[i]);
      if (digit < 0) return -EINVAL;
      accumulator = (accumulator << 6) | (unsigned)digit;
      bits += 6;
      if (bits >= 8)
        {
          bits -= 8;
          raw[written++] = (uint8_t)(accumulator >> bits);
        }
    }
  if (written != 32 || (accumulator & 3)) return -EINVAL;
  /* The all-zero value is ESPHome's unconfigured/provisioning sentinel. */
  return noise_is_zero(raw, 32) ? -EINVAL : 0;
}

int esphome_secure_validate_key(const char *key)
{
  uint8_t raw[32] = {0};
  int ret = decode_key(key, raw);
  esphome_secure_wipe(raw, sizeof(raw));
  return ret;
}

static int noise_error(int code)
{
  switch (code)
    {
      case NOISE_ERROR_NONE: return 0;
      case NOISE_ERROR_NO_MEMORY: return -ENOMEM;
      case NOISE_ERROR_MAC_FAILURE:
      case NOISE_ERROR_INVALID_PUBLIC_KEY: return -EACCES;
      case NOISE_ERROR_INVALID_NONCE: return -EOVERFLOW;
      case NOISE_ERROR_SYSTEM: return -EIO;
      default: return -EPROTO;
    }
}

static int fail(struct esphome_secure *ctx, int error)
{
  if (ctx)
    {
      if (!ctx->failure) ctx->failure = error;
      if (ctx->send) noise_cipherstate_free(ctx->send);
      if (ctx->receive) noise_cipherstate_free(ctx->receive);
      ctx->send = NULL;
      ctx->receive = NULL;
      esphome_secure_wipe(ctx->frame, sizeof(ctx->frame));
      return ctx->failure;
    }
  return error;
}

void esphome_secure_free(struct esphome_secure *ctx)
{
  if (!ctx) return;
  fail(ctx, -ECONNRESET);
  esphome_secure_wipe(ctx, sizeof(*ctx));
  free(ctx);
}

static int monotonic_ms(int64_t *value)
{
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return -errno;
  *value = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
  return 0;
}

static int make_deadline(int timeout_ms, int64_t *deadline)
{
  int ret;
  if (timeout_ms <= 0) return -EINVAL;
  ret = monotonic_ms(deadline);
  if (!ret) *deadline += timeout_ms;
  return ret;
}

static int transfer(int fd, uint8_t *bytes, size_t size, bool sending,
                    int64_t deadline)
{
  struct pollfd pfd = {.fd = fd, .events = sending ? POLLOUT : POLLIN};
  while (size)
    {
      int64_t now;
      ssize_t count;
      int ret = monotonic_ms(&now);
      if (ret < 0) return ret;
      if (now >= deadline) return -ETIMEDOUT;
      ret = poll(&pfd, 1, deadline - now > INT_MAX ?
                 INT_MAX : (int)(deadline - now));
      if (ret < 0 && errno == EINTR) continue;
      if (ret < 0) return -errno;
      if (!ret) return -ETIMEDOUT;
      /* Read a final buffered frame even when POLLHUP is also set. */
      if (!(pfd.revents & pfd.events)) return -ECONNRESET;
      if (sending)
        {
#ifdef MSG_NOSIGNAL
          count = send(fd, bytes, size, MSG_NOSIGNAL);
#else
          count = send(fd, bytes, size, 0);
#endif
        }
      else count = recv(fd, bytes, size, 0);
      if (count < 0 && (errno == EINTR || errno == EAGAIN ||
                        errno == EWOULDBLOCK)) continue;
      if (count < 0) return -errno;
      if (!count) return -ECONNRESET;
      size -= (size_t)count;
      bytes += count;
    }
  return 0;
}

static int write_frame(struct esphome_secure *ctx, size_t size,
                       int64_t deadline)
{
  uint8_t header[3] = {1, (uint8_t)(size >> 8), (uint8_t)size};
  int ret = transfer(ctx->fd, header, sizeof(header), true, deadline);
  return ret < 0 ? ret : transfer(ctx->fd, ctx->frame, size, true, deadline);
}

static int read_frame(struct esphome_secure *ctx, size_t limit,
                      size_t *size, int64_t deadline)
{
  uint8_t header[3];
  int ret = transfer(ctx->fd, header, sizeof(header), false, deadline);
  if (ret < 0) return ret;
  /* 0x00 is plaintext. Neither it nor any other protocol is accepted. */
  if (header[0] != 1) return -EPROTO;
  *size = ((size_t)header[1] << 8) | header[2];
  if (*size > limit) return -EMSGSIZE;
  return transfer(ctx->fd, ctx->frame, *size, false, deadline);
}

static int check_hello(const uint8_t *frame, size_t size,
                       const char *expected_name)
{
  const uint8_t *end;
  size_t name_size;
  if (!size || frame[0] != 1) return -EPROTO;
  if (!expected_name || !*expected_name) return 0;
  end = memchr(frame + 1, 0, size - 1);
  /* An expected name also requires the peer to provide the extension. */
  if (!end) return -EACCES;
  name_size = (size_t)(end - frame - 1);
  if (strlen(expected_name) != name_size ||
      memcmp(expected_name, frame + 1, name_size)) return -EACCES;
  return 0;
}

int esphome_secure_open(struct esphome_secure **out, int fd,
                        const char *key, const char *expected_name,
                        int timeout_ms)
{
  static const uint8_t prologue[] = "NoiseAPIInit\0\0";
  NoiseHandshakeState *handshake = NULL;
  struct esphome_secure *ctx = NULL;
  NoiseBuffer buffer;
  uint8_t raw[32] = {0};
  int64_t deadline;
  size_t size;
  int flags;
  int ret;
  if (!out) return -EINVAL;
  *out = NULL;
  if (fd < 0) return -EINVAL;
  ret = make_deadline(timeout_ms, &deadline);
  if (ret < 0) return ret;
  ret = decode_key(key, raw);
  if (ret < 0) goto done;
  ctx = calloc(1, sizeof(*ctx));
  if (!ctx) {ret = -ENOMEM; goto done;}
  ctx->fd = fd;
  /* Exclusive owner keeps this borrowed descriptor nonblocking until close.
   * poll() alone cannot bound a subsequent blocking send on a full socket.
   */
  flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {ret = -errno; goto done;}
  ret = noise_error(noise_handshakestate_new_by_name(&handshake,
            "Noise_NNpsk0_25519_ChaChaPoly_SHA256", NOISE_ROLE_INITIATOR));
  if (ret < 0) goto done;
  ret = noise_error(noise_handshakestate_set_pre_shared_key(handshake,
                                                           raw, sizeof(raw)));
  esphome_secure_wipe(raw, sizeof(raw));
  if (ret < 0) goto done;
  ret = noise_error(noise_handshakestate_set_prologue(handshake, prologue,
                                                     sizeof(prologue) - 1));
  if (ret < 0) goto done;
  ret = noise_error(noise_handshakestate_start(handshake));
  if (ret < 0) goto done;
  /* aioesphomeapi pipelines empty ClientHello and the first handshake. */
  ctx->frame[0] = 0;
  noise_buffer_set_output(buffer, ctx->frame + 1, HANDSHAKE_SIZE - 1);
  ret = noise_error(noise_handshakestate_write_message(handshake, &buffer, NULL));
  if (ret < 0) goto done;
  if (buffer.size != HANDSHAKE_SIZE - 1) {ret = -EPROTO; goto done;}
  ret = write_frame(ctx, 0, deadline);
  if (ret < 0) goto done;
  ret = write_frame(ctx, buffer.size + 1, deadline);
  if (ret < 0) goto done;
  ret = read_frame(ctx, HELLO_LIMIT, &size, deadline);
  if (ret < 0) goto done;
  ret = check_hello(ctx->frame, size, expected_name);
  if (ret < 0) goto done;
  ret = read_frame(ctx, HELLO_LIMIT, &size, deadline);
  if (ret < 0) goto done;
  if (!size) {ret = -EPROTO; goto done;}
  if (ctx->frame[0] != 0) {ret = -EACCES; goto done;}
  if (size != HANDSHAKE_SIZE) {ret = -EPROTO; goto done;}
  noise_buffer_set_input(buffer, ctx->frame + 1, size - 1);
  ret = noise_error(noise_handshakestate_read_message(handshake, &buffer, NULL));
  if (ret < 0) goto done;
  if (noise_handshakestate_get_action(handshake) != NOISE_ACTION_SPLIT)
    {ret = -EPROTO; goto done;}
  ret = noise_error(noise_handshakestate_split(handshake, &ctx->send,
                                              &ctx->receive));
  if (ret < 0) goto done;
  if (!noise_cipherstate_has_key(ctx->send) ||
      !noise_cipherstate_has_key(ctx->receive)) {ret = -EPROTO; goto done;}
  esphome_secure_wipe(ctx->frame, sizeof(ctx->frame));
  *out = ctx;
  ctx = NULL;
done:
  esphome_secure_wipe(raw, sizeof(raw));
  if (handshake) noise_handshakestate_free(handshake);
  esphome_secure_free(ctx);
  return ret;
}

int esphome_secure_send(struct esphome_secure *ctx, uint32_t type,
                        const void *body, size_t length, int timeout_ms)
{
  NoiseBuffer buffer;
  int64_t deadline;
  int ret;
  if (!ctx) return -EINVAL;
  if (ctx->failure) return ctx->failure;
  if (!type || type > UINT16_MAX || (!body && length)) return fail(ctx, -EINVAL);
  if (length > BODY_LIMIT) return fail(ctx, -EMSGSIZE);
  ret = make_deadline(timeout_ms, &deadline);
  if (ret < 0) return fail(ctx, ret);
  ctx->frame[0] = (uint8_t)(type >> 8);
  ctx->frame[1] = (uint8_t)type;
  ctx->frame[2] = (uint8_t)(length >> 8);
  ctx->frame[3] = (uint8_t)length;
  if (length) memcpy(ctx->frame + INNER_HEADER_SIZE, body, length);
  noise_buffer_set_inout(buffer, ctx->frame, length + INNER_HEADER_SIZE,
                         sizeof(ctx->frame));
  ret = noise_error(noise_cipherstate_encrypt(ctx->send, &buffer));
  if (ret < 0) return fail(ctx, ret);
  ret = write_frame(ctx, buffer.size, deadline);
  if (ret < 0) return fail(ctx, ret);
  esphome_secure_wipe(ctx->frame, sizeof(ctx->frame));
  return 0;
}

int esphome_secure_receive(struct esphome_secure *ctx, uint32_t *type,
                           void *body, size_t capacity, size_t *length,
                           int timeout_ms)
{
  NoiseBuffer buffer;
  int64_t deadline;
  size_t size;
  size_t declared;
  uint32_t decoded_type;
  int ret;
  if (length) *length = 0;
  if (type) *type = 0;
  if (!ctx) return -EINVAL;
  if (ctx->failure) return ctx->failure;
  if (!length || !type || (!body && capacity)) return fail(ctx, -EINVAL);
  ret = make_deadline(timeout_ms, &deadline);
  if (ret < 0) return fail(ctx, ret);
  ret = read_frame(ctx, FRAME_LIMIT, &size, deadline);
  if (ret < 0) return fail(ctx, ret);
  if (size < INNER_HEADER_SIZE + TAG_SIZE) return fail(ctx, -EPROTO);
  noise_buffer_set_input(buffer, ctx->frame, size);
  ret = noise_error(noise_cipherstate_decrypt(ctx->receive, &buffer));
  if (ret < 0) return fail(ctx, ret);
  decoded_type = ((uint32_t)ctx->frame[0] << 8) | ctx->frame[1];
  declared = ((size_t)ctx->frame[2] << 8) | ctx->frame[3];
  size = buffer.size - INNER_HEADER_SIZE;
  /* Bound by the authenticated actual length; reject inconsistent headers. */
  if (!decoded_type || declared != size) return fail(ctx, -EPROTO);
  if (size > capacity) return fail(ctx, -EMSGSIZE);
  /* Never publish unverified or partially received bytes to the caller. */
  if (size) memcpy(body, ctx->frame + INNER_HEADER_SIZE, size);
  *type = decoded_type;
  *length = size;
  esphome_secure_wipe(ctx->frame, sizeof(ctx->frame));
  return 0;
}
