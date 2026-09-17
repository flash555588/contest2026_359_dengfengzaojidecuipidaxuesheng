/* SPDX-License-Identifier: Apache-2.0 */
#ifndef QPK_ESPDL_H
#define QPK_ESPDL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define QPK_DL_WIDTH 320
#define QPK_DL_HEIGHT 240
#define QPK_DL_PIXELS (QPK_DL_WIDTH * QPK_DL_HEIGHT)
#define QPK_DL_MAX_RESULTS 10
enum qpk_dl_mode { QPK_DL_CLASSIFY, QPK_DL_FACE };
enum qpk_dl_stage { QPK_DL_IDLE, QPK_DL_CAPTURE, QPK_DL_LOAD, QPK_DL_INFER,
                    QPK_DL_DONE, QPK_DL_CANCELLED, QPK_DL_ERROR };
struct qpk_dl_item { char label[96]; float score; int x1, y1, x2, y2; };
enum qpk_dl_tracking { QPK_DL_SEARCHING, QPK_DL_TRACKING, QPK_DL_LOST };
struct qpk_dl_track {
  enum qpk_dl_tracking state;
  uint32_t id;
  int target, offset_x, offset_y; /* -1000..1000; positive means right/down */
};
struct qpk_dl_tracker {
  struct qpk_dl_item box;
  uint32_t id;
  unsigned missed;
  bool locked;
  int center_x, center_y;
};
struct qpk_dl_result {
  uint32_t request, frame;
  enum qpk_dl_stage stage;
  enum qpk_dl_mode mode;
  bool busy, preview;
  int error;
  uint32_t elapsed_ms;
  unsigned count;
  struct qpk_dl_track track;
  struct qpk_dl_item items[QPK_DL_MAX_RESULTS];
};
struct qpk_dl_request {
  enum qpk_dl_mode mode;
  unsigned device;
  uint32_t generation;
  bool selftest; /* diagnostic request, never supplied by the quick-app API */
};
#ifdef __cplusplus
extern "C" {
#endif
int qpk_dl_start(const struct qpk_dl_request *request);
void qpk_dl_cancel(void);
bool qpk_dl_should_cancel(void);
void qpk_dl_status(struct qpk_dl_result *result);
int qpk_dl_copy_preview(uint32_t request, uint32_t frame, uint16_t *pixels,
                        size_t count, struct qpk_dl_result *result);
int qpk_dl_backend_open(enum qpk_dl_mode mode, void **context);
int qpk_dl_backend_run(void *context, const uint16_t *pixels, struct qpk_dl_result *result);
void qpk_dl_backend_close(void *context);
int qpk_dl_backend_verify(void *context);
int qpk_dl_capture_open(const struct qpk_dl_request *request, void **context);
int qpk_dl_capture_next(void *context, uint16_t *pixels);
void qpk_dl_capture_close(void *context);
void qpk_dl_track_update(struct qpk_dl_tracker *tracker, struct qpk_dl_result *result);
void qpk_dl_set_stage(enum qpk_dl_stage stage);
int qpk_dl_model_load(unsigned asset, void **data, size_t *length);
void qpk_dl_model_free(void *data);
int qpk_dl_model_available(unsigned asset);
void qpk_dl_sha256(const void *data, size_t length, uint8_t digest[32]);
#ifdef __cplusplus
}
#endif
#endif
