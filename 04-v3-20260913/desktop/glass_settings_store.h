/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GLASS_SETTINGS_STORE_H
#define GLASS_SETTINGS_STORE_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifndef GLASS_SETTINGS_PREFIX
#define GLASS_SETTINGS_PREFIX "/data/glass-settings"
#endif
static uint32_t g_settings_generation;
static int g_settings_slot = -1;
static uint32_t glass_settings_check(const uint8_t *data)
{
  uint32_t hash = 2166136261u;
  for (int i = 0; i < 12; i++) hash = (hash ^ data[i]) * 16777619u;
  return hash;
}
static int glass_settings_read(int slot, uint8_t record[16])
{
  char path[96];
  snprintf(path, sizeof(path), "%s-%d.bin", GLASS_SETTINGS_PREFIX, slot);
  FILE *file = fopen(path, "rb");
  if (!file) return -1;
  size_t size = fread(record, 1, 16, file);
  int extra = fgetc(file);
  int io_error = ferror(file);
  if (fclose(file)) io_error = 1;
  if (size != 16 || extra != EOF || io_error) return -1;
  uint32_t stored;
  memcpy(&stored, record + 12, 4);
  return memcmp(record, "GLS1", 4) == 0 && record[8] <= 1 &&
    record[9] < 3 && record[10] < 3 && record[11] <= 1 &&
    stored == glass_settings_check(record) ? 0 : -1;
}
static int glass_settings_load(uint8_t values[4])
{
  uint8_t record[16] = {0};
  g_settings_slot = -1;
  g_settings_generation = 0;
  for (int slot = 0; slot < 2; slot++)
    {
      if (glass_settings_read(slot, record)) continue;
      uint32_t generation;
      memcpy(&generation, record + 4, 4);
      if (g_settings_slot < 0 || (int32_t)(generation - g_settings_generation) > 0)
        {
          memcpy(values, record + 8, 4);
          g_settings_slot = slot;
          g_settings_generation = generation;
        }
    }
  return g_settings_slot < 0 ? -1 : 0;
}
static int glass_settings_save(const uint8_t values[4])
{
  uint8_t record[16] = {'G', 'L', 'S', '1'};
  if (values[0] > 1 || values[1] > 2 || values[2] > 2 || values[3] > 1) return -1;
  uint32_t generation = g_settings_generation + 1;
  memcpy(record + 4, &generation, 4);
  memcpy(record + 8, values, 4);
  uint32_t check = glass_settings_check(record);
  memcpy(record + 12, &check, 4);
  int slot = g_settings_slot == 0 ? 1 : 0;
  char path[96];
  snprintf(path, sizeof(path), "%s-%d.bin", GLASS_SETTINGS_PREFIX, slot);
  FILE *file = fopen(path, "wb");
  if (!file) return -1;
  int failed = fwrite(record, 1, 16, file) != 16;
  if (fflush(file)) failed = 1;
  if (fsync(fileno(file))) failed = 1;
  if (fclose(file)) failed = 1;
  uint8_t verify[16] = {0};
  if (failed || glass_settings_read(slot, verify) || memcmp(record, verify, 16)) return -1;
  g_settings_slot = slot;
  g_settings_generation = generation;
  return 0;
}
#endif
