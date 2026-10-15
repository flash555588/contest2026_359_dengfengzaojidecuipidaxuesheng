/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BILI_API_H
#define BILI_API_H
#include <stddef.h>
#define BILI_URL_MAX 2048
#define BILI_MAX_LINES 8
struct bili_streams {
  char urls[BILI_MAX_LINES][BILI_URL_MAX];
  int count;
  int qn;
  char referer[96], quality[48];
};
int bili_room_id(const char *json, unsigned long *id);
int bili_play_info(const char *json, struct bili_streams *out);
int bili_low_play_info(const char *json, struct bili_streams *out);
int bili_valid_room(const char *text);
#endif
