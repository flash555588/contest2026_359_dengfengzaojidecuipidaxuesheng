/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <string.h>
#include "ble_register.h"

int main(int argc, char **argv)
{
  if (argc != 2 || strcmp(argv[1], "register"))
    {
      fprintf(stderr, "Usage: c6ble register\n");
      return 1;
    }
  int ret = c6_ble_register("/dev/ttyHCI0");
  printf("c6ble: registration result=%d\n", ret);
  return ret < 0 ? 1 : 0;
}
