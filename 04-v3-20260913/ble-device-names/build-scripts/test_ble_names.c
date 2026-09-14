/* Exercise the production advertising parser with real AD encodings. */
#include "glass_ble_name.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
  struct glass_ble_device d = {0};
  const uint8_t shortened[] = {4, 8, 'B', 'o', 'x'};
  const uint8_t response[] = {2, 1, 6, 8, 9, 'B', 'o', 'x', ' ', 'P', 'r', 'o'};
  const uint8_t no_name[] = {2, 1, 6};
  const uint8_t broken[] = {20, 9, 'x'};
  glass_ble_update_name(&d, shortened, sizeof(shortened));
  assert(!strcmp(d.name, "Box") && !d.name_complete);
  glass_ble_update_name(&d, response, sizeof(response));
  assert(!strcmp(d.name, "Box Pro") && d.name_complete);
  glass_ble_update_name(&d, shortened, sizeof(shortened));
  glass_ble_update_name(&d, no_name, sizeof(no_name));
  glass_ble_update_name(&d, broken, sizeof(broken));
  assert(!strcmp(d.name, "Box Pro"));

  const uint8_t chinese[] = {7, 9, 0xe9, 0x9f, 0xb3, 0xe7, 0xae, 0xb1};
  glass_ble_update_name(&d, chinese, sizeof(chinese));
  assert(!strcmp(d.name, "\xe9\x9f\xb3\xe7\xae\xb1"));
  char text[GLASS_BLE_NAME_MAX + 1];
  const uint8_t controls[] = {'a', '\n', 'b', 0, 0xff, ' '};
  glass_ble_name_copy(text, controls, sizeof(controls));
  assert(!strcmp(text, "a b ?"));

  uint8_t long_name[100];
  memset(long_name, 'A', sizeof(long_name));
  long_name[62] = 0xe9; long_name[63] = 0x9f; long_name[64] = 0xb3;
  assert(glass_ble_name_copy(text, long_name, sizeof(long_name)) == 62);
  assert(text[62] == 0);
  const uint8_t invalid[] = {0xe0, 0x80, 0x80, 0xed, 0xa0, 0x80, 0xf4, 0x90, 0x80, 0x80};
  glass_ble_name_copy(text, invalid, sizeof(invalid));
  assert(!strcmp(text, "??????????"));

  /* Every truncated prefix is safe and cannot invent a complete name. */
  for (size_t size = 0; size < sizeof(response); size++)
    {
      memset(&d, 0, sizeof(d));
      glass_ble_update_name(&d, response, size);
      assert(!d.name[0]);
    }
  /* Deterministic malformed packets, with sanitizers checking all bounds. */
  uint32_t seed = 1;
  for (size_t size = 0; size <= 255; size++)
    for (unsigned trial = 0; trial < 100; trial++)
      {
        uint8_t *packet = malloc(size ? size : 1);
        assert(packet);
        for (size_t i = 0; i < size; i++)
          { seed = seed * 1664525 + 1013904223; packet[i] = seed >> 24; }
        memset(&d, 0, sizeof(d));
        glass_ble_update_name(&d, packet, size);
        assert(memchr(d.name, 0, sizeof(d.name)));
        free(packet);
      }
  puts("PASS: scan-response merge, name priority, Chinese UTF-8, controls, truncation and 25600 malformed packets");
  return 0;
}
