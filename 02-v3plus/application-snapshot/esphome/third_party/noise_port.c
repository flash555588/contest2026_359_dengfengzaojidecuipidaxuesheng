/* SPDX-License-Identifier: MIT
 * Fallible OS entropy adapter for the vendored reference X25519 backend.
 */
#ifdef __NuttX__
#include <nuttx/config.h>
#if !defined(CONFIG_DEV_RANDOM) || !defined(CONFIG_ARCH_HAVE_RNG)
#error "ESPHome Noise requires CONFIG_DEV_RANDOM and a hardware RNG driver"
#endif
#endif

#include "noise_port.h"
#include <noise/protocol.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/random.h>
#endif

int esphome_noise_random(void *bytes, size_t size)
{
  uint8_t *cursor = bytes;
  size_t remaining = size;
  unsigned attempts = 0;
#ifndef __linux__
  /* On this NuttX board /dev/random is the hardware driver. Never use
   * /dev/urandom, which can be configured with a non-cryptographic PRNG.
   */
  int fd = open("/dev/random", O_RDONLY | O_NONBLOCK);
  if (fd < 0) goto fail;
#endif
  while (remaining && attempts++ < 64)
    {
#ifdef __linux__
      ssize_t count = getrandom(cursor, remaining, GRND_NONBLOCK);
#else
      ssize_t count = read(fd, cursor, remaining);
#endif
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) break;
      cursor += count;
      remaining -= (size_t)count;
    }
#ifndef __linux__
  close(fd);
#endif
  if (!remaining) return NOISE_ERROR_NONE;
#ifndef __linux__
fail:
#endif
  noise_clean(bytes, size);
  return NOISE_ERROR_SYSTEM;
}
