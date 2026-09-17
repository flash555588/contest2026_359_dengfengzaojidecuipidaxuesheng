/* SPDX-License-Identifier: Apache-2.0 */
#include "hass_service.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
  if (argc != 2 || strcmp(argv[1], "status"))
    { puts("Usage: hass status"); return 1; }
  bool configured, busy;
  unsigned clients;
  hass_status(&configured, &busy, &clients);
  printf("configured=%s busy=%s clients=%u\n", configured ? "yes" : "no",
         busy ? "yes" : "no", clients);
  return 0;
}
