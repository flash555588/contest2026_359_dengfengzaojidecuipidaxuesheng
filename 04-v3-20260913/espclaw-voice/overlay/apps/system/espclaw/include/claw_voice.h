/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CLAW_VOICE_H
#define CLAW_VOICE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#define CLAW_VOICE_TEXT 4096
#define CLAW_VOICE_MAX_SECONDS 15
#define CLAW_VOICE_RATE 16000
enum claw_voice_state {
  CLAW_VOICE_IDLE, CLAW_VOICE_PREPARING, CLAW_VOICE_RECORDING,
  CLAW_VOICE_TRANSCRIBING, CLAW_VOICE_ASKING, CLAW_VOICE_STOPPING,
  CLAW_VOICE_DONE, CLAW_VOICE_ERROR, CLAW_VOICE_CANCELLED
};
struct claw_voice_snapshot {
  enum claw_voice_state state;
  bool busy;
  unsigned int milliseconds;
  unsigned int peak;
  int error;
  char detail[128];
  char transcript[CLAW_VOICE_TEXT + 1];
  char answer[CLAW_VOICE_TEXT + 1];
};
/* Recording is explicitly initiated. No hotword or background microphone. */
int claw_voice_start(unsigned int seconds, bool ask);
void claw_voice_cancel(void);
void claw_voice_finish_recording(void);
void claw_voice_read(struct claw_voice_snapshot *out);
int claw_voice_command(int argc, char **argv);
int claw_voice_capture(unsigned int seconds, atomic_bool *cancel,
                       atomic_bool *finish, unsigned char **wav, size_t *size);
void claw_voice_progress(unsigned int milliseconds, unsigned int peak);
void claw_voice_wav_header(unsigned char out[44], size_t pcm_size);
int claw_voice_multipart(const char *model, const unsigned char *wav, size_t size,
                          unsigned char **body, size_t *length);
const char *claw_voice_content_type(void);
int claw_voice_parse_transcript(const char *json, char *out, size_t capacity);
bool claw_voice_valid_url(const char *value);
bool claw_voice_valid_text(const char *value, size_t limit);
#endif
