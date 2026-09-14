/****************************************************************************
 * apps/system/desktop/qpk_runtime.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#pragma once

#include <stddef.h>
#include <lvgl/lvgl.h>

typedef const lv_font_t *(*qpk_font_cb_t)(int size);
typedef void (*qpk_message_cb_t)(const char *message);

/* Lifecycle and UI calls belong to the LVGL thread. Stop the active runtime
 * before destroying its root; retired pages may be deleted after a handoff.
 */
int qpk_runtime_launch(lv_obj_t *root, const char *name,
                       const char *package, const char *version,
                       const char *filename, const char *source,
                       size_t source_len, qpk_font_cb_t font_cb,
                       qpk_message_cb_t toast_cb,
                       qpk_message_cb_t dialog_cb);
void qpk_runtime_stop(void);
bool qpk_runtime_running(void);
/* True while direct capture or its cleanup still owns the display pages. */
bool qpk_runtime_camera_active(void);
/* A hardware session cannot be replaced until cleanup has finished. */
bool qpk_runtime_camera_busy(void);
bool qpk_runtime_camera_capture(void);
/* Call from the UI loop even while LVGL is suspended. Handles worker failure
 * and bounded cleanup retries; returns a one-shot full-redraw request.
 */
bool qpk_runtime_camera_poll(void);
