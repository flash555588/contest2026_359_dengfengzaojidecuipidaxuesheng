/****************************************************************************
 * apps/system/desktop/pet_sprites.c
 *
 * Validated SD-card sprite bundle loader for the native desktop pet.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include "pet_sprites.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define PET_SPRITE_MAGIC       "DFYSPRT1"
#define PET_SPRITE_VERSION     1u
#define PET_SPRITE_COUNT       12u
#define PET_SPRITE_HEADER_SIZE 24u
#define PET_SPRITE_ENTRY_SIZE  20u
#define PET_SPRITE_MAX_BYTES   (512u * 1024u)
#define PET_SPRITE_MAX_SIDE    256u

static uint16_t read_u16(const uint8_t *p)
{
  return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static uint32_t read_u32(const uint8_t *p)
{
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
         (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t sprite_crc32(const uint8_t *data, size_t size)
{
  uint32_t crc = UINT32_MAX;
  size_t i;

  for (i = 0; i < size; i++)
    {
      unsigned bit;

      crc ^= data[i];
      for (bit = 0; bit < 8; bit++)
        {
          crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }

  return crc ^ UINT32_MAX;
}

static void set_error(char *error, size_t size, const char *message)
{
  if (error != NULL && size > 0)
    {
      snprintf(error, size, "%s", message);
    }
}

void pet_sprites_unload(struct pet_sprite_bundle_s *bundle)
{
  if (bundle == NULL)
    {
      return;
    }

  free(bundle->storage);
  memset(bundle, 0, sizeof(*bundle));
}

int pet_sprites_load(const char *path, struct pet_sprite_bundle_s *bundle,
                     char *error, size_t error_size)
{
  struct stat info;
  uint8_t *bytes = NULL;
  FILE *file = NULL;
  size_t size;
  size_t table_end = PET_SPRITE_HEADER_SIZE +
                     PET_SPRITE_COUNT * PET_SPRITE_ENTRY_SIZE;
  size_t next_offset = table_end;
  unsigned i;
  int ret = -EINVAL;

  if (path == NULL || bundle == NULL)
    {
      set_error(error, error_size, "invalid loader arguments");
      return -EINVAL;
    }

  if (stat(path, &info) < 0)
    {
      ret = -errno;
      set_error(error, error_size, "resource file is absent");
      return ret;
    }

  if (info.st_size < (off_t)table_end ||
      info.st_size > (off_t)PET_SPRITE_MAX_BYTES)
    {
      set_error(error, error_size, "resource file size is invalid");
      return -EFBIG;
    }

  size = (size_t)info.st_size;
  bytes = malloc(size);
  if (bytes == NULL)
    {
      set_error(error, error_size, "not enough memory for sprites");
      return -ENOMEM;
    }

  file = fopen(path, "rb");
  if (file == NULL)
    {
      ret = -errno;
      set_error(error, error_size, "cannot open resource file");
      goto fail;
    }

  if (fread(bytes, 1, size, file) != size || fgetc(file) != EOF)
    {
      set_error(error, error_size, "cannot read complete resource file");
      ret = -EIO;
      goto fail;
    }

  fclose(file);
  file = NULL;
  if (memcmp(bytes, PET_SPRITE_MAGIC, 8) != 0 ||
      read_u32(bytes + 8) != PET_SPRITE_VERSION ||
      read_u32(bytes + 12) != PET_SPRITE_COUNT ||
      read_u32(bytes + 16) != size)
    {
      set_error(error, error_size, "resource header is invalid");
      goto fail;
    }

  if (sprite_crc32(bytes + PET_SPRITE_HEADER_SIZE,
                   size - PET_SPRITE_HEADER_SIZE) != read_u32(bytes + 20))
    {
      set_error(error, error_size, "resource bundle CRC mismatch");
      goto fail;
    }

  memset(bundle, 0, sizeof(*bundle));
  for (i = 0; i < PET_SPRITE_COUNT; i++)
    {
      const uint8_t *entry = bytes + PET_SPRITE_HEADER_SIZE +
                             i * PET_SPRITE_ENTRY_SIZE;
      unsigned view = entry[0];
      unsigned size_index = entry[1];
      uint16_t width = read_u16(entry + 4);
      uint16_t height = read_u16(entry + 6);
      uint32_t offset = read_u32(entry + 8);
      uint32_t length = read_u32(entry + 12);
      uint64_t expected_length = (uint64_t)width * height * 3u;

      if (view != i / PET_SPRITE_SIZE_COUNT ||
          size_index != i % PET_SPRITE_SIZE_COUNT ||
          read_u16(entry + 2) != 0 || width == 0 || height == 0 ||
          width > PET_SPRITE_MAX_SIDE || height > PET_SPRITE_MAX_SIDE ||
          expected_length != length || offset != next_offset ||
          offset > size || length > size - offset ||
          sprite_crc32(bytes + offset, length) != read_u32(entry + 16))
        {
          set_error(error, error_size, "resource entry is invalid");
          goto fail_bundle;
        }

      bundle->images[view][size_index].width = width;
      bundle->images[view][size_index].height = height;
      bundle->images[view][size_index].data_size = length;
      bundle->images[view][size_index].data = bytes + offset;
      next_offset += length;
    }

  if (next_offset != size)
    {
      set_error(error, error_size, "resource bundle has trailing data");
      goto fail_bundle;
    }

  bundle->storage = bytes;
  bundle->storage_size = size;
  set_error(error, error_size, "ok");
  return 0;

fail_bundle:
  memset(bundle, 0, sizeof(*bundle));
fail:
  if (file != NULL)
    {
      fclose(file);
    }

  free(bytes);
  return ret;
}

const struct pet_sprite_image_s *
pet_sprites_get(const struct pet_sprite_bundle_s *bundle,
                enum pet_sprite_view_e view, unsigned size_index)
{
  if (bundle == NULL || bundle->storage == NULL ||
      (unsigned)view >= PET_SPRITE_VIEW_COUNT ||
      size_index >= PET_SPRITE_SIZE_COUNT)
    {
      return NULL;
    }

  return &bundle->images[view][size_index];
}
