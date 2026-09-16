/* Exercise the production worker and file store with a controllable AI peer. */
#include "glass_chat_backend.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static atomic_bool configured, release_peer;
static atomic_int mode, calls;
static char last_context[CHAT_CONTEXT_BYTES * 6 + 4096];
static struct chat_snapshot snapshot;

int glass_chat_load_settings(struct portal_ai_config *out)
{
  memset(out, 0, sizeof(*out));
  strcpy(out->model, "host-fixture");
  return atomic_load(&configured) ? 0 : -ENOENT;
}

enum chat_error glass_chat_request(const struct portal_ai_config *settings, const char *context,
                                  const char *prompt, uint32_t id, atomic_bool *cancel, char **reply)
{
  (void)settings; (void)prompt; (void)id; (void)cancel;
  snprintf(last_context, sizeof(last_context), "%s", context);
  int behavior = atomic_load(&mode);
  atomic_fetch_add(&calls, 1);
  if (behavior == 1) {
    /* Deliberately ignore cancellation: the worker must discard late data. */
    while (!atomic_load(&release_peer)) usleep(1000);
    *reply = strdup("LATE RESULT MUST BE DISCARDED");
  } else if (behavior == 4) {
    struct claw_stream_event e = {.kind=CLAW_STREAM_REASONING, .text="正在分析边界"}; glass_chat_stream(id, &e);
    e.kind=CLAW_STREAM_TEXT; e.text="已经收到的片段"; glass_chat_stream(id, &e);
    e.kind=CLAW_STREAM_USAGE; e.has_reasoning_tokens=true; e.reasoning_tokens=37; glass_chat_stream(id, &e);
    while (!atomic_load(&release_peer)) usleep(1000);
    return CHAT_ERROR_STREAM;
  } else if (behavior == 2) return CHAT_ERROR_AUTH;
  else if (behavior == 3) {
    *reply = calloc(1, 18001);
    for (unsigned i = 0; i < 6000; i++) memcpy(*reply + i * 3, "中", 3);
  } else *reply = strdup("这是测试服务返回的回答。\n可以继续提问。");
  return CHAT_ERROR_NONE;
}

static void update(void) { glass_chat_get(&snapshot); }
static void idle(void)
{
  for (unsigned i = 0; i < 5000; i++) {
    update(); if (snapshot.loaded && snapshot.phase == CHAT_IDLE) return;
    usleep(1000);
  }
  fprintf(stderr, "worker stalled: phase=%d count=%u storage=%d\n", snapshot.phase, snapshot.count, snapshot.storage_error);
  abort();
}
static void peer_entered(int before)
{
  for (unsigned i = 0; i < 5000; i++) {
    if (atomic_load(&calls) > before) return;
    usleep(1000);
  }
  assert(!"AI peer did not receive a request");
}
static struct chat_turn *last(void)
{ assert(snapshot.count); return &snapshot.turns[snapshot.count - 1]; }

static void validators(void)
{
  assert(glass_chat_utf8("你好\nhello\t世界", 100, true));
  assert(!glass_chat_utf8("bad\nsetting", 100, false));
  assert(!glass_chat_utf8("\xc0\x80", 10, true));
  assert(!glass_chat_utf8("\xed\xa0\x80", 10, true));
  assert(!glass_chat_utf8("\xf4\x90\x80\x80", 10, true));
  assert(!glass_chat_utf8("\xe4\xb8", 10, true));
  char copied[6]; glass_chat_copy_utf8(copied, sizeof(copied), "你好啊");
  assert(!strcmp(copied, "你"));
  assert(glass_chat_utf8(copied, 5, true));
}

int main(int argc, char **argv)
{
  assert(argc == 2);
  validators();
  assert(!glass_chat_start()); idle();
  if (!strcmp(argv[1], "reload")) {
    assert(snapshot.count == CHAT_MAX_TURNS);
    assert(last()->state == CHAT_DONE && !snapshot.storage_error);
    puts("PASS: conversation survives process restart"); return 0;
  }
  if (!strcmp(argv[1], "interrupt")) {
    last()->state = CHAT_WAITING; last()->reply[0] = 0;
    assert(!glass_chat_store_save(&snapshot));
    puts("Wrote interrupted-request fixture"); return 0;
  }
  if (!strcmp(argv[1], "recover")) {
    assert(last()->state == CHAT_FAILED && last()->error == CHAT_ERROR_INTERRUPTED);
    assert(!last()->reply[0]);
    puts("PASS: interrupted request is retryable after reboot"); return 0;
  }
  assert(!strcmp(argv[1], "run"));
  assert(!snapshot.count && !snapshot.configured);
  assert(glass_chat_send("hello") == -ENOKEY);
  assert(glass_chat_send("\xf0\x80\x80\x80") == -EINVAL);
  atomic_store(&configured, true); glass_chat_refresh_config();
  for (unsigned i = 0; i < 1000; i++) { update(); if (snapshot.configured) break; usleep(1000); }
  assert(snapshot.configured);
  assert(glass_chat_send("  \n\t ") == -EINVAL);
  assert(!glass_chat_send("  你好\n请介绍自己。  ")); idle();
  assert(snapshot.count == 1 && last()->state == CHAT_DONE);
  assert(!strcmp(last()->prompt, "你好\n请介绍自己。"));
  assert(!strcmp(last_context, "[]"));
  assert(!glass_chat_send("继续解释")); idle();
  cJSON *messages = cJSON_Parse(last_context);
  assert(cJSON_GetArraySize(messages) == 2);
  assert(!strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(messages, 0), "role")->valuestring, "user"));
  assert(!strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(messages, 1), "role")->valuestring, "assistant"));
  assert(!strstr(last_context, "继续解释")); cJSON_Delete(messages);

  atomic_store(&mode, 2); assert(!glass_chat_send("保留问题以便重试")); idle();
  assert(last()->state == CHAT_FAILED && last()->error == CHAT_ERROR_AUTH);
  unsigned count = snapshot.count;
  atomic_store(&mode, 0); assert(!glass_chat_retry()); idle();
  assert(snapshot.count == count && last()->state == CHAT_DONE);
  assert(!strstr(last_context, "保留问题以便重试"));

  int before = atomic_load(&calls);
  atomic_store(&mode, 1); atomic_store(&release_peer, false);
  assert(!glass_chat_send("取消测试")); peer_entered(before);
  assert(glass_chat_send("不要重复发送") == -EBUSY);
  assert(glass_chat_retry() == -EBUSY);
  glass_chat_cancel(); update(); assert(snapshot.phase == CHAT_STOPPING);
  atomic_store(&release_peer, true); idle();
  assert(last()->state == CHAT_CANCELLED && !last()->reply[0]);

  before = atomic_load(&calls); atomic_store(&release_peer, false);
  assert(!glass_chat_send("清空时仍在等待")); peer_entered(before);
  assert(!glass_chat_clear()); update(); assert(snapshot.phase == CHAT_CLEARING);
  atomic_store(&release_peer, true); idle();
  assert(!snapshot.count && !snapshot.storage_error);
  struct chat_snapshot *disk = calloc(1, sizeof(*disk));
  assert(!glass_chat_store_load(disk) && !disk->count);

  atomic_store(&mode, 4); atomic_store(&release_peer, false); before = atomic_load(&calls);
  assert(!glass_chat_send("实时显示测试")); peer_entered(before);
  for (unsigned wait = 0; wait < 1000; wait++) { update(); if (last()->has_reasoning_tokens) break; usleep(1000); }
  assert(snapshot.phase == CHAT_REQUESTING && !strcmp(last()->reply, "已经收到的片段"));
  assert(last()->reasoning_bytes && last()->reasoning_tokens == 37 && last()->text_bytes);
  atomic_store(&release_peer, true); idle();
  assert(last()->state == CHAT_FAILED && last()->error == CHAT_ERROR_STREAM && last()->reply[0]);
  puts("PASS: live content/reasoning/token updates arrive before completion, and a disconnect preserves partial output");

  atomic_store(&mode, 3); assert(!glass_chat_send("长回复")); idle();
  assert(last()->state == CHAT_DONE && last()->clipped);
  assert(glass_chat_utf8(last()->reply, CHAT_REPLY_BYTES, true));
  assert(strlen(last()->reply) == 8190);
  assert(last()->archive[0] && last()->archive_bytes == 18000);
  struct chat_page page = {0}; size_t complete_bytes = 0;
  for (unsigned p = 0;; p++) {
    assert(!glass_chat_page_request(last()->archive, p));
    for (unsigned wait = 0; wait < 1000; wait++) {
      glass_chat_page_get(&page); if (!page.busy) break; usleep(1000);
    }
    assert(!page.busy && !page.error && page.page == p);
    assert(glass_chat_utf8(page.text, CHAT_PAGE_BYTES, true));
    complete_bytes += strlen(page.text);
    if (p + 1 == page.pages) break;
  }
  assert(complete_bytes == 18000);
  assert(glass_chat_page_request("../ai.json", 0) == -EINVAL);
  assert(!glass_chat_store_load(disk) && !strcmp(disk->turns[disk->count-1].archive, last()->archive));
  puts("PASS: full long reply archive survives reload and UTF-8 pagination reconstructs all bytes");
  atomic_store(&mode, 0);
  for (unsigned i = 0; i < 8; i++) {
    char question[40]; snprintf(question, sizeof(question), "follow-up %u", i);
    assert(!glass_chat_send(question)); idle(); assert(last()->state == CHAT_DONE);
  }
  assert(snapshot.count == CHAT_MAX_TURNS && !strcmp(snapshot.turns[0].prompt, "follow-up 2"));
  assert(!glass_chat_store_load(disk) && disk->count == CHAT_MAX_TURNS);
  char *context = glass_chat_context(disk); assert(context);
  assert(!strstr(context, "取消测试") && !strstr(context, "LATE RESULT")); free(context);

  /* Make the older slot unwritable; the newest committed slot must survive. */
  const char *paths[] = {CHAT_DATA_ROOT "/chat/history.json", CHAT_DATA_ROOT "/chat/history.tmp"};
  unsigned sequence[2];
  for (unsigned i = 0; i < 2; i++) {
    FILE *file = fopen(paths[i], "rb"); assert(file);
    assert(!fseek(file, 0, SEEK_END)); long length = ftell(file); rewind(file);
    char *text = calloc(1, length + 1); assert(text);
    assert(fread(text, 1, length, file) == (size_t)length); fclose(file);
    cJSON *doc = cJSON_Parse(text); assert(doc); free(text);
    sequence[i] = cJSON_GetObjectItem(doc, "sequence")->valuedouble; cJSON_Delete(doc);
  }
  const char *target = paths[sequence[0] < sequence[1] ? 0 : 1];
  const char *held = CHAT_DATA_ROOT "/chat/held.json";
  assert(!rename(target, held)); assert(!mkdir(target, 0700));
  assert(!glass_chat_send("暂时保存失败")); idle();
  assert(snapshot.storage_error && last()->state == CHAT_DONE);
  memset(disk, 0, sizeof(*disk)); assert(!glass_chat_store_load(disk));
  assert(strcmp(disk->turns[disk->count - 1].prompt, "暂时保存失败"));
  assert(!rmdir(target)); assert(!rename(held, target));
  assert(!glass_chat_send("恢复保存")); idle(); assert(!snapshot.storage_error);
  free(disk);
  puts("PASS: UTF-8, configuration gate, multi-turn roles, no duplicate retry, cancellation, late-reply isolation, clear, history bounds, alternating save failure/recovery");
  return 0;
}
