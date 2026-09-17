/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include "qpk_limits.h"

#define QPK_DRAFT_NAME_MAX 47
#define QPK_DRAFT_SLUG_MAX 20
#define QPK_EXAMPLE_PAGE_MAX 6144

struct qpk_draft_info {
  bool loaded, busy, discarding, removing;
  int remove_error;
  uint32_t revision, previewed_revision, saved_revision;
  int error;
  unsigned save_stage;
  uint32_t save_started_ms, save_elapsed_ms;
  char name[QPK_DRAFT_NAME_MAX + 1], slug[QPK_DRAFT_SLUG_MAX + 1];
  char installed_package[32], preview_error[256];
};

/* No LVGL in this module. Synchronous file work is for worker/core threads. */
int glass_qpk_initialize(void);
int glass_qpk_start(void); /* Asynchronous initialization for the UI. */
void glass_qpk_get(struct qpk_draft_info *out);
char *glass_qpk_preview_source(struct qpk_draft_info *out);
void glass_qpk_preview_result(uint32_t revision, const char *error);
int glass_qpk_save_async(uint32_t revision);
int glass_qpk_save(uint32_t revision);
int glass_qpk_discard_async(void);
int glass_qpk_discard(void);
bool glass_qpk_removable(const char *directory, const char *package);
int glass_qpk_remove_async(const char *directory, const char *package);

const char *glass_qpk_guide(void);
const char *glass_qpk_tools(void);
char *glass_qpk_list_examples(void);
char *glass_qpk_read_example(const char *example, const char *file,
                            unsigned start_line, unsigned max_lines);
char *glass_qpk_read_draft(void);
char *glass_qpk_write_draft(const char *json, atomic_bool *cancel);
const char *glass_qpk_error_text(int error);
