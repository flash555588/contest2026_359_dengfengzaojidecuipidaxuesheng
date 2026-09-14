/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GLASS_WALLPAPER_H
#define GLASS_WALLPAPER_H

#ifndef GLASS_WALLPAPER_PATH
#define GLASS_WALLPAPER_PATH "/data/desktop-wallpaper.rgb565"
#endif

/* Fixed-size, little-endian RGB565. Retained for the desktop lifetime. */
#define GLASS_WALLPAPER_WIDTH 884
#define GLASS_WALLPAPER_HEIGHT 288
#define GLASS_WALLPAPER_BYTES (GLASS_WALLPAPER_WIDTH * GLASS_WALLPAPER_HEIGHT * 2)

static lv_image_dsc_t g_wallpaper;

static bool glass_wallpaper_load(void)
{
  FILE *file;
  uint8_t *data;
  size_t count;
  int extra;
  bool failed;
  if (g_wallpaper.data != NULL) return true;
  file = fopen(GLASS_WALLPAPER_PATH, "rb");
  if (file == NULL) return false;
  data = malloc(GLASS_WALLPAPER_BYTES);
  if (data == NULL)
    {
      fclose(file);
      return false;
    }
  count = fread(data, 1, GLASS_WALLPAPER_BYTES, file);
  extra = fgetc(file);
  failed = ferror(file) != 0;
  if (fclose(file) != 0) failed = true;
  if (count != GLASS_WALLPAPER_BYTES || extra != EOF || failed)
    {
      free(data);
      return false;
    }
  g_wallpaper.header.magic = LV_IMAGE_HEADER_MAGIC;
  g_wallpaper.header.cf = LV_COLOR_FORMAT_RGB565;
  g_wallpaper.header.w = GLASS_WALLPAPER_WIDTH;
  g_wallpaper.header.h = GLASS_WALLPAPER_HEIGHT;
  g_wallpaper.header.stride = GLASS_WALLPAPER_WIDTH * 2;
  g_wallpaper.data_size = GLASS_WALLPAPER_BYTES;
  g_wallpaper.data = data;
  return true;
}
#endif
