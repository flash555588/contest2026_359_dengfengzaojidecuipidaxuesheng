#include "../overlay/apps/system/desktop/pet_sprites.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
  struct pet_sprite_bundle_s bundle = {0};
  char error[96];
  unsigned view;
  unsigned size_index;

  if (argc != 2)
    {
      fprintf(stderr, "usage: %s dafeiyu.lvbin\n", argv[0]);
      return 2;
    }

  if (pet_sprites_load(argv[1], &bundle, error, sizeof(error)) < 0)
    {
      fprintf(stderr, "load failed: %s\n", error);
      return 1;
    }

  for (view = 0; view < PET_SPRITE_VIEW_COUNT; view++)
    for (size_index = 0; size_index < PET_SPRITE_SIZE_COUNT; size_index++)
      {
        const struct pet_sprite_image_s *image =
          pet_sprites_get(&bundle, (enum pet_sprite_view_e)view, size_index);
        if (image == NULL || image->height != (uint16_t[]){66, 88, 110}[size_index] ||
            image->data_size != (uint32_t)image->width * image->height * 3u)
          {
            fprintf(stderr, "invalid image %u/%u\n", view, size_index);
            pet_sprites_unload(&bundle);
            return 1;
          }
      }

  printf("ok: %lu bytes\n", (unsigned long)bundle.storage_size);
  pet_sprites_unload(&bundle);
  return 0;
}
