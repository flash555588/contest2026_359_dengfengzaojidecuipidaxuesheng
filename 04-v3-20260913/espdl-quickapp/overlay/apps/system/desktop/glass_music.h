/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MUSIC_FAVORITES_MAX 20
struct music_song {
  char name[128], artist[96], album[128], query[128];
  char url[2048];
};
enum music_state { MUSIC_IDLE, MUSIC_LOADING, MUSIC_PLAYING, MUSIC_PAUSED,
                   MUSIC_STOPPED, MUSIC_ENDED, MUSIC_ERROR };
struct music_snapshot {
  struct music_song current, result;
  struct music_song favorites[MUSIC_FAVORITES_MAX];
  unsigned revision, search_revision, position_ms, volume, favorite_count;
  enum music_state state;
  bool searching, has_result, initialized;
  int error, search_error, storage_error;
};
void glass_music_start(void);
void glass_music_get(struct music_snapshot *out);
int glass_music_search(const char *query);
int glass_music_play(const struct music_song *song);
void glass_music_toggle(void);
void glass_music_stop(void);
int glass_music_stop_wait(unsigned timeout_ms);
void glass_music_volume(unsigned volume);
int glass_music_favorite(const struct music_song *song);
bool glass_music_is_favorite(const struct music_snapshot *state, const struct music_song *song);
int music_song_parse(const char *body, size_t size, const char *query, struct music_song *song);
int music_search_url(const char *query, const char *key, char *url, size_t cap);

typedef bool (*music_cancel_cb)(void *);
typedef int (*music_data_cb)(void *, const unsigned char *, size_t);
int music_http_get(const char *url, music_data_cb sink, void *ctx,
                   music_cancel_cb cancelled, unsigned *status);
