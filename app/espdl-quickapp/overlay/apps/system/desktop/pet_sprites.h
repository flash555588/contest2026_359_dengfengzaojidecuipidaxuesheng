/****************************************************************************
 * apps/system/desktop/pet_sprites.h
 *
 * Validated SD-card sprite bundle loader for the native desktop pet.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#pragma once

#include <stddef.h>
#include <stdint.h>

#define PET_SPRITE_VIEW_COUNT 4
#define PET_SPRITE_SIZE_COUNT 3

enum pet_sprite_view_e
{
  PET_SPRITE_DOWN = 0,
  PET_SPRITE_UP,
  PET_SPRITE_LEFT,
  PET_SPRITE_RIGHT
};

struct pet_sprite_image_s
{
  uint16_t width;
  uint16_t height;
  uint32_t data_size;
  const uint8_t *data;
};

struct pet_sprite_bundle_s
{
  uint8_t *storage;
  size_t storage_size;
  struct pet_sprite_image_s images[PET_SPRITE_VIEW_COUNT]
                                          [PET_SPRITE_SIZE_COUNT];
};

int pet_sprites_load(const char *path, struct pet_sprite_bundle_s *bundle,
                     char *error, size_t error_size);
void pet_sprites_unload(struct pet_sprite_bundle_s *bundle);
const struct pet_sprite_image_s *
pet_sprites_get(const struct pet_sprite_bundle_s *bundle,
                enum pet_sprite_view_e view, unsigned size_index);
