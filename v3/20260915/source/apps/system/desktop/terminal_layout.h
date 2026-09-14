/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TERMINAL_LAYOUT_H
#define TERMINAL_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define TERM_MAX_APPS 15
#define TERM_AREA_W 960
#define TERM_AREA_H 300
#define TERM_MIN_SIZE 56
#define TERM_MAX_SIZE 112
#define TERM_LABEL_H 30
#define TERM_LAYOUT_MAGIC 0x5445524dU
#define TERM_LAYOUT_VERSION 2

struct term_item_s
{
  char key[64];
  int16_t x;
  int16_t y;
  uint16_t size;
  uint8_t pocket;
  uint8_t reserved;
};

struct term_layout_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  uint8_t light;
  uint8_t quiet;
  uint8_t reduced;
  uint8_t warm;
  uint32_t generation;
  struct term_item_s items[TERM_MAX_APPS];
  uint32_t checksum;
};

static inline int term_clamp(int value, int minimum, int maximum)
{
  return value < minimum ? minimum : value > maximum ? maximum : value;
}

static inline void term_constrain(struct term_item_s *item)
{
  item->size = term_clamp(item->size, TERM_MIN_SIZE, TERM_MAX_SIZE);
  item->x = term_clamp(item->x, 0, TERM_AREA_W - item->size);
  item->y = term_clamp(item->y, 0,
                      TERM_AREA_H - item->size - TERM_LABEL_H);
}

static inline uint32_t term_checksum(const struct term_layout_s *layout)
{
  const unsigned char *bytes = (const unsigned char *)layout;
  uint32_t hash = 2166136261U;
  unsigned int i;
  for (i = 0; i < sizeof(*layout) - sizeof(layout->checksum); i++)
    {
      hash = (hash ^ bytes[i]) * 16777619U;
    }
  return hash;
}

static inline bool term_layout_valid(const struct term_layout_s *layout)
{
  int i;
  int j;
  if (layout->magic != TERM_LAYOUT_MAGIC ||
      layout->version != TERM_LAYOUT_VERSION ||
      layout->count > TERM_MAX_APPS || layout->light > 1 ||
      layout->quiet > 1 || layout->reduced > 1 || layout->warm > 1 ||
      layout->checksum != term_checksum(layout))
    {
      return false;
    }
  for (i = 0; i < layout->count; i++)
    {
      const struct term_item_s *item = &layout->items[i];
      if (!item->key[0] || !memchr(item->key, 0, sizeof(item->key)) ||
          item->size < TERM_MIN_SIZE || item->size > TERM_MAX_SIZE ||
          item->pocket > 1 || item->x < 0 || item->y < 0 ||
          item->x + item->size > TERM_AREA_W ||
          item->y + item->size + TERM_LABEL_H > TERM_AREA_H)
        {
          return false;
        }
      for (j = 0; j < i; j++)
        {
          if (!strcmp(item->key, layout->items[j].key))
            {
              return false;
            }
        }
    }
  return true;
}
#endif
