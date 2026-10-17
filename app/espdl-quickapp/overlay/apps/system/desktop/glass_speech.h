/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
enum speech_phase {SPEECH_IDLE,SPEECH_LOADING,SPEECH_WAKE,SPEECH_COMMAND,SPEECH_SPEAKING,SPEECH_STOPPING,SPEECH_ERROR};
struct speech_snapshot {
  uint32_t revision,request;enum speech_phase phase;bool active,wake_ready;int error,command;
  unsigned level;char text[128];
  unsigned wake_evaluations,wake_events,frames;float wake_score;
  /* Retained command event: status/level updates cannot replay an action. */
  uint32_t command_event,command_ms;int last_command;
};
int glass_speech_listen(void);
/* One-shot custom wake only. Releases capture/model before active becomes
 * false; request identifies this owner even after a newer session starts. */
int glass_speech_wait_wake(uint32_t *request);
void glass_speech_stop_request(uint32_t request);
int glass_speech_say(const char *text);
const char *glass_speech_backend(void);
int glass_speech_probe(void);
/* Optional official English fixture. Start from the persistent UI task. */
int glass_speech_nano(int mode);
int glass_speech_zh_q8(int mode);
int glass_speech_esp_tts(int mode);
int glass_speech_p4tts(int mode);
void glass_speech_stop(void);
/* Evict idle TTS weights; active synthesis defers eviction until return. */
void glass_speech_clear_cache(void);
void glass_speech_get(struct speech_snapshot *out);
int glass_speech_cli(int argc,char **argv);
