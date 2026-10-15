/* SPDX-License-Identifier: Apache-2.0
 * Bounded RAM storage and serial fixture import for standalone board tests.
 */
#include <poll.h>
#include <nuttx/drivers/ramdisk.h>

#if defined(CONFIG_BOARDCTL_MKRD) && defined(CONFIG_FS_FAT)
static void *g_test_disk;
static int desktop_test_disk(void)
{
  struct stat st;
  if (g_test_disk || stat("/dev/ram9", &st) == 0) return -EEXIST;
  void *buffer = calloc(4096, 512);
  if (!buffer) return -ENOMEM;
  int ret = ramdisk_register(9, buffer, 4096, 512, RDFLAG_WRENABLED);
  if (ret < 0) free(buffer);
  else g_test_disk = buffer; /* Borrowed by the driver until reboot. */
  printf("test-disk: %d (temporary /dev/ram9)\n", ret);
  return ret;
}
#endif

static int desktop_base64_digit(int c)
{
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  return c == '+' ? 62 : c == '/' ? 63 : -1;
}

static int desktop_receive(const char *path, const char *length)
{
  char *end;
  long wanted = strtol(length, &end, 10);
  if (!*length || *end || wanted <= 0 || wanted > 512 * 1024 ||
      strncmp(path, "/sdcard/", 8) || strstr(path, "..")) return -EINVAL;
  struct stat st;
  if (stat(path, &st) == 0) return -EEXIST;
  uint8_t *buffer = malloc(wanted);
  if (!buffer) return -ENOMEM;
  size_t count = 0;
  size_t encoded = 4 * (((size_t)wanted + 2) / 3);
  uint32_t value = 0;
  unsigned bits = 0;
  unsigned pads = 0;
  int ret = -ETIMEDOUT;
  struct timespec stamp;
  clock_gettime(CLOCK_MONOTONIC,&stamp);
  time_t deadline = stamp.tv_sec + 180;
  printf("recv: READY bytes=%ld chunk=48\n", wanted);
  fflush(stdout);
  for (size_t i = 0; i < encoded; )
    {
      struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
      clock_gettime(CLOCK_MONOTONIC,&stamp);
      if (stamp.tv_sec > deadline) goto done;
      int ready = poll(&pfd, 1, 1000);
      if (ready < 0) { if (errno == EINTR) continue; ret = -errno; goto done; }
      if (!ready) continue;
      unsigned char c;
      if (read(STDIN_FILENO, &c, 1) != 1) { ret = -EIO; goto done; }
      if (c == '\r' || c == '\n') continue;
      if (c == '=')
        {
          pads++;
          if (i < encoded - 2 || pads > 2) { ret = -EINVAL; goto done; }
        }
      else
        {
          int digit = desktop_base64_digit(c);
          if (digit < 0 || pads) { ret = -EINVAL; goto done; }
          value = (value << 6) | digit;
          bits += 6;
          if (bits >= 8)
            {
              bits -= 8;
              if (count >= (size_t)wanted) { ret = -EINVAL; goto done; }
              buffer[count++] = (uint8_t)(value >> bits);
            }
        }
      i++;
      if(i%48==0){printf("recv: ACK %lu\n",(unsigned long)(i/48));fflush(stdout);}
    }
  if (count != (size_t)wanted || pads != (3 - wanted % 3) % 3 ||
      (bits && (value & ((1u << bits) - 1)))) { ret = -EINVAL; goto done; }
  int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0) { ret = -errno; goto done; }
  size_t written = 0;
  while (written < count)
    {
      ssize_t n = write(fd, buffer + written, count - written);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) { ret = n < 0 ? -errno : -EIO; break; }
      written += n;
    }
  if (written == count) ret = 0;
  if (close(fd) < 0 && !ret) ret = -errno;
  if (ret < 0) unlink(path); /* Only our newly created file. */
  else
    {
      uint32_t crc = UINT32_MAX;
      for (size_t i = 0; i < count; i++)
        {
          crc ^= buffer[i];
          for (unsigned b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
        }
      printf("recv: OK bytes=%lu crc32=%08lx\n", (unsigned long)count,
             (unsigned long)~crc);
    }
done:
  if (ret < 0) printf("recv: ERROR %d\n", ret);
  free(buffer);
  return ret;
}
