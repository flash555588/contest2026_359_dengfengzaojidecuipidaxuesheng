/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_grok.h"
unsigned glass_grok_emotion(const struct chat_activity_snapshot *chat,
                            const struct glass_voice_snapshot *voice)
{
  if (chat->phase == CHAT_STOPPING) return 41;
  if (chat->phase == CHAT_CLEARING || chat->phase == CHAT_LOADING) return 36;
  if (chat->phase == CHAT_QUEUED) return 31;
  if (chat->phase == CHAT_REQUESTING) {
    if (chat->activity == CHAT_ACTIVITY_REPLY) return 39;
    if (chat->activity == CHAT_ACTIVITY_TOOL) return 32;
    if (chat->activity == CHAT_ACTIVITY_CONNECT) return 36;
    return 30;
  }
  if (voice && voice->active) {
    if (voice->phase == GLASS_VOICE_LISTENING) return 35;
    if (voice->phase == GLASS_VOICE_FINISHING) return 32;
    return 36;
  }
  if (chat->turn_id) {
    if (chat->state == CHAT_FAILED) return 34;
    if (chat->state == CHAT_CANCELLED) return 41;
    if (chat->state == CHAT_DONE) return 33;
  }
  return 2;
}
