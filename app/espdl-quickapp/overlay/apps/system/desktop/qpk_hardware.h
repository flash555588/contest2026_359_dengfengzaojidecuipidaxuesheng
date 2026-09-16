/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <cJSON.h>

struct qpk_hw_session;
struct qpk_hw_progress {
  atomic_bool cancel, finish;
  atomic_uint bytes, elapsed_ms, peak;
};
struct qpk_hw_session *qpk_hw_create(const char *package);
void qpk_hw_release(struct qpk_hw_session *session);
/* For short-lived diagnostic tasks; the UI uses the nonblocking release. */
void qpk_hw_release_wait(struct qpk_hw_session *session);
int qpk_hw_submit(struct qpk_hw_session *session, const char *operation, const char *arguments);
char *qpk_hw_poll(struct qpk_hw_session *session, unsigned id);
int qpk_hw_cancel(struct qpk_hw_session *session, unsigned id, bool finish);
char *qpk_hw_capabilities(void);

/* Platform calls run on a dedicated worker, never on the LVGL/JS thread. */
void *qpk_hw_platform_create(const char *package);
void qpk_hw_platform_destroy(void *platform);
int qpk_hw_platform_execute(void *platform, const char *operation,
                            const cJSON *arguments, cJSON *result,
                            struct qpk_hw_progress *progress);
