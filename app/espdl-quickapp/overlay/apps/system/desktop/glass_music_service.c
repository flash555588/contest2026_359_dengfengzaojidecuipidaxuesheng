/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "glass_music.h"
#include "glass_portal.h"
#include "glass_music_pcm.h"
#include "qpk_storage.h"
#include <cJSON.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if __has_include("music_credentials.h")
#include "music_credentials.h"
#else
#define GLASS_MUSIC_API_KEY ""
#endif
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "music_vendor/minimp3.h"

#define RING_CAP (256 * 1024)
#define PREFILL (48 * 1024)
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static struct music_snapshot state = {.volume = 40};
static bool started, want_play, want_pause, want_search, want_favorite;
static bool player_active;
static unsigned play_generation, search_generation;
static struct music_song pending_song, pending_favorite;
static char pending_query[128];

struct transfer {
  unsigned generation;
  bool search;
  char *body;
  size_t used;
};

static bool transfer_cancelled(void *ctx)
{
  struct transfer *t = ctx;
  pthread_mutex_lock(&lock);
  bool cancelled = t->generation != (t->search ? search_generation : play_generation);
  pthread_mutex_unlock(&lock);
  return cancelled;
}

static int collect_json(void *ctx, const unsigned char *data, size_t size)
{
  struct transfer *t = ctx;
  if (size > 16384 - t->used) return -E2BIG;
  memcpy(t->body + t->used, data, size); t->used += size;
  return 0;
}

static int lookup(const char *query, struct music_song *song, unsigned generation, bool search)
{
  struct transfer t = {.generation = generation, .search = search};
  t.body = malloc(16385);
  char *url = malloc(2048);
  int ret = -ENOMEM;
  if (!t.body || !url) goto done;
  char custom_key[129];
  ret = music_search_url(query, portal_music_key(custom_key,sizeof(custom_key)) == 0 ? custom_key : GLASS_MUSIC_API_KEY, url, 2048);
  if (!ret) ret = music_http_get(url, collect_json, &t, transfer_cancelled, NULL);
  if (!ret) ret = music_song_parse(t.body, t.used, query, song);
done:
  free(t.body); free(url);
  return ret;
}

bool glass_music_is_favorite(const struct music_snapshot *s, const struct music_song *song)
{
  if (!song || !song->name[0]) return false;
  for (unsigned i = 0; i < s->favorite_count; i++)
    if (!strcmp(s->favorites[i].name, song->name) && !strcmp(s->favorites[i].artist, song->artist)) return true;
  return false;
}

static int save_favorites(struct music_song *songs, unsigned count)
{
  cJSON *array = cJSON_CreateArray();
  if (!array) return -ENOMEM;
  int ret = -ENOMEM;
  for (unsigned i = 0; i < count; i++) {
    cJSON *song = cJSON_CreateObject();
    if (!song) goto done;
    cJSON_AddItemToArray(array, song);
    if (!cJSON_AddStringToObject(song, "name", songs[i].name) ||
        !cJSON_AddStringToObject(song, "artist", songs[i].artist) ||
        !cJSON_AddStringToObject(song, "album", songs[i].album) ||
        !cJSON_AddStringToObject(song, "query", songs[i].query)) goto done;
  }
  char *json = cJSON_PrintUnformatted(array);
  if (json) {
    ret = qpk_storage_write("/data/qpk", "music", "favorites", json, strlen(json));
    cJSON_free(json);
  }
done:
  cJSON_Delete(array);
  return ret;
}

static void load_favorites(void)
{
  char *json = malloc(QPK_STORAGE_VALUE_MAX + 1);
  if (!json) return;
  size_t size = 0;
  int ret = qpk_storage_read("/data/qpk", "music", "favorites", json, QPK_STORAGE_VALUE_MAX + 1, &size);
  if (!ret) {
    json[size] = 0;
    cJSON *array = cJSON_ParseWithOpts(json, NULL, true);
    if (cJSON_IsArray(array) && cJSON_GetArraySize(array) <= MUSIC_FAVORITES_MAX) {
      cJSON *item;
      cJSON_ArrayForEach(item, array) {
        struct music_song song = {0};
        const char *keys[] = {"name", "artist", "album", "query"};
        char *values[] = {song.name, song.artist, song.album, song.query};
        size_t caps[] = {sizeof(song.name), sizeof(song.artist), sizeof(song.album), sizeof(song.query)};
        bool valid = cJSON_IsObject(item);
        for (unsigned i = 0; i < 4 && valid; i++) {
          cJSON *v = cJSON_GetObjectItemCaseSensitive(item, keys[i]);
          valid = cJSON_IsString(v) && strlen(v->valuestring) < caps[i];
          if (valid) strcpy(values[i], v->valuestring);
        }
        if (valid && song.name[0] && song.query[0]) state.favorites[state.favorite_count++] = song;
      }
    } else ret = -EINVAL;
    cJSON_Delete(array);
  }
  state.storage_error = ret == -ENOENT ? 0 : ret;
  free(json);
}

static void *search_worker(void *unused)
{
  (void)unused;
  pthread_setname_np(pthread_self(), "music_search");
  pthread_mutex_lock(&lock);
  load_favorites();
  state.initialized = true; state.revision++;
  pthread_mutex_unlock(&lock);
  for (;;) {
    pthread_mutex_lock(&lock);
    while (!want_search && !want_favorite) pthread_cond_wait(&wake, &lock);
    if (want_favorite) {
      struct music_song *songs = malloc(sizeof(state.favorites));
      unsigned count = state.favorite_count;
      struct music_song song = pending_favorite;
      want_favorite = false;
      if (songs) memcpy(songs, state.favorites, sizeof(state.favorites));
      pthread_mutex_unlock(&lock);
      int ret = -ENOMEM;
      if (songs) {
        unsigned i;
        for (i = 0; i < count; i++) if (!strcmp(songs[i].name, song.name) && !strcmp(songs[i].artist, song.artist)) break;
        if (i < count) { memmove(songs+i, songs+i+1, (count-i-1)*sizeof(*songs)); count--; ret = 0; }
        else if (count >= MUSIC_FAVORITES_MAX) ret = -ENOSPC;
        else { memmove(songs+1, songs, count*sizeof(*songs)); songs[0] = song; songs[0].url[0] = 0; count++; ret = 0; }
        if (!ret) ret = save_favorites(songs, count);
      }
      pthread_mutex_lock(&lock);
      if (!ret) { memcpy(state.favorites, songs, count*sizeof(*songs)); state.favorite_count = count; }
      state.storage_error = ret; state.revision++;
      pthread_mutex_unlock(&lock);
      free(songs);
      continue;
    }
    char query[128]; strcpy(query, pending_query);
    unsigned generation = search_generation;
    want_search = false;
    pthread_mutex_unlock(&lock);
    struct music_song song = {0};
    int ret = lookup(query, &song, generation, true);
    pthread_mutex_lock(&lock);
    if (generation == search_generation) {
      state.searching = false; state.search_error = ret; state.has_result = ret == 0;
      if (!ret) state.result = song;
      state.search_revision++; state.revision++;
      printf("[music] search result=%d\n", ret);
    }
    pthread_mutex_unlock(&lock);
  }
  return NULL;
}

struct stream {
  struct transfer transfer; /* generation first; shared cancellation callback */
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  unsigned char *ring;
  size_t read, used;
  bool done, abort;
  int error;
  const char *url;
};

static bool stream_cancelled(void *ctx)
{
  struct stream *s = ctx;
  pthread_mutex_lock(&s->mutex); bool abort = s->abort; pthread_mutex_unlock(&s->mutex);
  return abort || transfer_cancelled(&s->transfer);
}

static void stream_wait(struct stream *s)
{
  struct timespec t; clock_gettime(CLOCK_REALTIME, &t);
  t.tv_nsec += 100000000;
  if (t.tv_nsec >= 1000000000) { t.tv_nsec -= 1000000000; t.tv_sec++; }
  pthread_cond_timedwait(&s->condition, &s->mutex, &t);
}

static int stream_sink(void *ctx, const unsigned char *data, size_t size)
{
  struct stream *s = ctx;
  while (size) {
    if (stream_cancelled(s)) return -ECANCELED;
    pthread_mutex_lock(&s->mutex);
    if (s->used == RING_CAP) { stream_wait(s); pthread_mutex_unlock(&s->mutex); continue; }
    size_t pos = (s->read + s->used) % RING_CAP;
    size_t take = RING_CAP - s->used;
    if (take > RING_CAP-pos) take = RING_CAP-pos;
    if (take > size) take = size;
    memcpy(s->ring+pos, data, take); s->used += take;
    pthread_cond_broadcast(&s->condition);
    pthread_mutex_unlock(&s->mutex);
    data += take; size -= take;
  }
  return 0;
}

static void *network_worker(void *ctx)
{
  struct stream *s = ctx;
  pthread_setname_np(pthread_self(), "music_net");
  int ret = music_http_get(s->url, stream_sink, s, stream_cancelled, NULL);
  pthread_mutex_lock(&s->mutex);
  s->error = ret; s->done = true;
  pthread_cond_broadcast(&s->condition);
  pthread_mutex_unlock(&s->mutex);
  return NULL;
}

static int spawn(pthread_t *thread, void *(*entry)(void *), void *arg, size_t stack)
{
  pthread_attr_t attr; pthread_attr_init(&attr);
  int ret = pthread_attr_setstacksize(&attr, stack);
  if (!ret) ret = pthread_create(thread, &attr, entry, arg);
  pthread_attr_destroy(&attr);
  return ret ? -ret : 0;
}

static int play_stream(struct music_song *song, unsigned generation)
{
  struct stream s = {.transfer = {.generation = generation},
    .mutex = PTHREAD_MUTEX_INITIALIZER, .condition = PTHREAD_COND_INITIALIZER, .url = song->url};
  s.ring = malloc(RING_CAP);
  unsigned char *input = malloc(32768);
  mp3dec_t *decoder = calloc(1, sizeof(*decoder));
  int16_t *samples = malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(*samples));
  struct music_pcm pcm = {.fd = -1, .mq = (mqd_t)-1};
  int ret = -ENOMEM; bool network_started = false, paused = false;
  pthread_t network;
  if (!s.ring || !input || !decoder || !samples) goto done;
  mp3dec_init(decoder);
  ret = spawn(&network, network_worker, &s, 32768);
  if (ret) goto done;
  network_started = true;
  size_t buffered = 0, skipped = 0; unsigned volume = 0;
  bool prefilled = false;
  while (!stream_cancelled(&s)) {
    pthread_mutex_lock(&lock);
    bool pause = want_pause; unsigned desired_volume = state.volume;
    pthread_mutex_unlock(&lock);
    if (pause != paused) { ret = music_pcm_pause(&pcm, pause); if (ret) break; paused = pause; }
    if (paused) { usleep(20000); continue; }
    pthread_mutex_lock(&s.mutex);
    if (!prefilled && s.used < PREFILL && !s.done) { stream_wait(&s); pthread_mutex_unlock(&s.mutex); continue; }
    prefilled = true;
    while (s.used && buffered < 32768) {
      size_t take = RING_CAP-s.read;
      if (take > s.used) take = s.used;
      if (take > 32768-buffered) take = 32768-buffered;
      memcpy(input+buffered, s.ring+s.read, take);
      buffered += take; s.read = (s.read+take)%RING_CAP; s.used -= take;
    }
    pthread_cond_broadcast(&s.condition);
    bool eof = s.done && !s.used;
    int network_error = s.error;
    if (buffered < 16384 && !eof) { stream_wait(&s); pthread_mutex_unlock(&s.mutex); continue; }
    pthread_mutex_unlock(&s.mutex);
    if (!buffered) { ret = network_error; break; }
    mp3dec_frame_info_t info;
    int frames = mp3dec_decode_frame(decoder, input, buffered, samples, &info);
    if (info.frame_bytes < 0 || (size_t)info.frame_bytes > buffered) { ret = -EINVAL; break; }
    if (info.frame_bytes) { memmove(input, input+info.frame_bytes, buffered-info.frame_bytes); buffered -= info.frame_bytes; }
    else if (eof) { ret = network_error; break; }
    else { ret = -EINVAL; break; }
    if (!frames) { skipped += info.frame_bytes; if (skipped > 1024*1024) { ret = -EINVAL; break; } continue; }
    skipped = 0;
    if (pcm.fd < 0) {
      ret = music_pcm_open(&pcm, info.hz, desired_volume);
      if (ret) break;
      volume = desired_volume;
    }
    if (pcm.rate != (unsigned)info.hz) { ret = -EINVAL; break; }
    if (volume != desired_volume) { ret = music_pcm_volume(&pcm, desired_volume); if (ret) break; volume = desired_volume; }
    ret = music_pcm_write(&pcm, samples, frames, info.channels);
    if (ret) break;
    pthread_mutex_lock(&lock);
    if (generation == play_generation) {
      state.state = want_pause ? MUSIC_PAUSED : MUSIC_PLAYING;
      state.position_ms = (uint64_t)pcm.played_samples * 1000 / pcm.rate;
      state.revision++;
    }
    pthread_mutex_unlock(&lock);
  }
  if (!ret && !stream_cancelled(&s) && pcm.fd >= 0) ret = music_pcm_drain(&pcm);
  if (!ret && pcm.fd < 0) ret = -ENODATA;
done:
  pthread_mutex_lock(&s.mutex); s.abort = true; pthread_cond_broadcast(&s.condition); pthread_mutex_unlock(&s.mutex);
  music_pcm_close(&pcm);
  if (network_started) pthread_join(network, NULL);
  free(s.ring); free(input); free(decoder); free(samples);
  pthread_cond_destroy(&s.condition); pthread_mutex_destroy(&s.mutex);
  return ret;
}

static void *player_worker(void *unused)
{
  (void)unused; pthread_setname_np(pthread_self(), "music_player");
  for (;;) {
    pthread_mutex_lock(&lock);
    while (!want_play) pthread_cond_wait(&wake, &lock);
    unsigned generation = play_generation;
    struct music_song song = pending_song;
    want_play = false;
    player_active = true;
    pthread_mutex_unlock(&lock);
    int ret = lookup(song.query[0] ? song.query : song.name, &song, generation, false);
    pthread_mutex_lock(&lock);
    bool current = generation == play_generation;
    if (current && !ret) { state.current = song; state.revision++; }
    pthread_mutex_unlock(&lock);
    if (!ret && current) ret = play_stream(&song, generation);
    pthread_mutex_lock(&lock);
    if (generation == play_generation) { state.state = ret ? MUSIC_ERROR : MUSIC_ENDED; state.error = ret; state.revision++; }
    player_active = false;
    pthread_cond_broadcast(&wake);
    pthread_mutex_unlock(&lock);
    printf("[music] playback finished ret=%d\n", ret);
  }
  return NULL;
}

void glass_music_start(void)
{
  pthread_mutex_lock(&lock);
  if (started) { pthread_mutex_unlock(&lock); return; }
  started = true;
  pthread_mutex_unlock(&lock);
  pthread_t thread;
  int ret = spawn(&thread, search_worker, NULL, 32768);
  if (!ret) pthread_detach(thread);
  if (!ret) { ret = spawn(&thread, player_worker, NULL, 49152); if (!ret) pthread_detach(thread); }
  if (ret) { pthread_mutex_lock(&lock); state.error = ret; state.state = MUSIC_ERROR; state.revision++; pthread_mutex_unlock(&lock); }
}

void glass_music_get(struct music_snapshot *out)
{ pthread_mutex_lock(&lock); *out = state; pthread_mutex_unlock(&lock); }

int glass_music_search(const char *query)
{
  if (!query || !query[0] || strlen(query) >= sizeof(pending_query)) return -EINVAL;
  pthread_mutex_lock(&lock);
  strcpy(pending_query, query); search_generation++; want_search = true;
  state.searching = true; state.search_error = 0; state.has_result = false; state.revision++;
  pthread_cond_broadcast(&wake); pthread_mutex_unlock(&lock); return 0;
}

int glass_music_play(const struct music_song *song)
{
  if (!song || !song->name[0]) return -EINVAL;
  pthread_mutex_lock(&lock);
  pending_song = *song; state.current = *song; play_generation++; want_play = true; want_pause = false;
  state.state = MUSIC_LOADING; state.position_ms = 0; state.error = 0; state.revision++;
  pthread_cond_broadcast(&wake); pthread_mutex_unlock(&lock); return 0;
}

void glass_music_toggle(void)
{
  pthread_mutex_lock(&lock);
  if (state.state == MUSIC_PLAYING || state.state == MUSIC_PAUSED || state.state == MUSIC_LOADING) {
    want_pause = !want_pause;
    if (state.state != MUSIC_LOADING) state.state = want_pause ? MUSIC_PAUSED : MUSIC_PLAYING;
    state.revision++;
  } else if (state.current.name[0]) {
    pending_song = state.current; play_generation++; want_play = true; want_pause = false;
    state.state = MUSIC_LOADING; state.error = 0; state.position_ms = 0; state.revision++;
    pthread_cond_broadcast(&wake);
  }
  pthread_mutex_unlock(&lock);
}

void glass_music_stop(void)
{ pthread_mutex_lock(&lock); play_generation++; want_play = false; want_pause = false; state.state = MUSIC_STOPPED; state.revision++; pthread_cond_broadcast(&wake); pthread_mutex_unlock(&lock); }

int glass_music_stop_wait(unsigned timeout_ms)
{
  struct timespec deadline;
  clock_gettime(CLOCK_REALTIME, &deadline);
  deadline.tv_sec += timeout_ms / 1000;
  deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
  if (deadline.tv_nsec >= 1000000000L) {
    deadline.tv_sec++;
    deadline.tv_nsec -= 1000000000L;
  }

  pthread_mutex_lock(&lock);
  play_generation++;
  want_play = false;
  want_pause = false;
  state.state = MUSIC_STOPPED;
  state.revision++;
  pthread_cond_broadcast(&wake);

  int ret = 0;
  while (player_active && !ret) ret = pthread_cond_timedwait(&wake, &lock, &deadline);
  pthread_mutex_unlock(&lock);
  return ret == ETIMEDOUT ? -ETIMEDOUT : ret ? -ret : 0;
}

void glass_music_volume(unsigned volume)
{ pthread_mutex_lock(&lock); state.volume = volume > 100 ? 100 : volume; state.revision++; pthread_mutex_unlock(&lock); }

int glass_music_favorite(const struct music_song *song)
{
  if (!song || !song->name[0]) return -EINVAL;
  pthread_mutex_lock(&lock);
  if (want_favorite) { pthread_mutex_unlock(&lock); return -EBUSY; }
  pending_favorite = *song; want_favorite = true;
  pthread_cond_broadcast(&wake); pthread_mutex_unlock(&lock); return 0;
}
