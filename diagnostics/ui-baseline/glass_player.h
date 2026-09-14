/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
enum glass_player_command { GLASS_PLAY = 1, GLASS_PAUSE, GLASS_RESUME,
                            GLASS_STOP, GLASS_VOLUME };
struct glass_player_status
{
  bool busy;
  int error;
  int state;
  unsigned volume;
};
int glass_player_submit(enum glass_player_command command, const char *path,
                        unsigned volume);
void glass_player_read(struct glass_player_status *status);
