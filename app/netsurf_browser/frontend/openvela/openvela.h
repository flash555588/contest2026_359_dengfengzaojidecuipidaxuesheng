/*
 * Copyright 2026 openvela NetSurf port contributors
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

/**
 * \file
 * openvela frontend internal interfaces.
 *
 * The frontend runs entirely on the LVGL (UI) thread. Only the HTTP
 * fetcher uses worker threads, and they never touch NetSurf or LVGL.
 */

#ifndef NETSURF_OPENVELA_H
#define NETSURF_OPENVELA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lvgl/lvgl.h>

#include "utils/errors.h"
#include "netsurf/plot_style.h"

struct browser_window;
struct gui_window;
struct plotter_table;

/* Operation tables registered with the core. */
extern struct gui_window_table *ov_window_table;
extern struct gui_bitmap_table *ov_bitmap_table;
extern struct gui_layout_table *ov_layout_table;
extern struct gui_fetch_table *ov_fetch_table;
extern const struct plotter_table ov_plotters;

/* Scheduler (schedule.c). */
nserror ov_schedule(int t, void (*callback)(void *p), void *p);
/** Run due callbacks; returns ms to the next one or -1 if none. */
int ov_schedule_run(void);
void ov_schedule_clear(void);

/* Plotting context passed through redraw_context.priv (plot.c). */
struct ov_plot_ctx {
	lv_layer_t *layer;
	lv_area_t bounds; /**< canvas area, inclusive */
	lv_area_t clip;   /**< current clip, inclusive */
	bool empty;       /**< current clip is empty */
};

/* Fonts (layout.c). */
const lv_font_t *ov_font_for(const plot_font_style_t *fstyle);
bool ov_font_bold(const plot_font_style_t *fstyle);

/* Bitmaps (bitmap.c). */
const lv_image_dsc_t *ov_bitmap_image(void *bitmap);
int ov_bitmap_width(void *bitmap);
int ov_bitmap_height(void *bitmap);

/* HTTP(S) fetcher (http.c). */
nserror ov_http_register(void);
/** Request every worker to stop. Never blocks. */
void ov_http_abort_all(void);
/** Reap finished workers; true when no worker thread exists. */
bool ov_http_idle(void);
/** True when the core holds no http(s) fetch. */
bool ov_http_empty(void);
/** Release fetcher globals; only valid when ov_http_idle(). */
void ov_http_finalise(void);

/* Image content handlers built on firmware decoders (image.c). */
nserror ov_image_register(void);

/* Embedded resources (resources.c, generated data in res_data.c). */
nserror ov_resource_data(const char *path, const uint8_t **data, size_t *len);
const uint8_t *ov_messages(size_t *len);

/* Application memory budget, computed once at core start (gui.c). */
struct ov_budget {
	size_t cache;       /**< llcache limit (image cache gets 1/4) */
	size_t image_bytes; /**< largest decoded image accepted */
	size_t fetch_queue; /**< bytes buffered per fetch before backpressure */
	size_t body_limit;  /**< largest response body accepted */
};
extern struct ov_budget ov_budget;

/* Window <-> shell glue (window.c). */
struct ov_view {
	lv_obj_t *obj;          /**< lv_image showing buf */
	lv_draw_buf_t *buf;
	int width, height;
};
/** Attach the canvas the next created gui_window will draw into. */
void ov_window_set_view(const struct ov_view *view);
struct gui_window *ov_window_current(void);
struct browser_window *ov_window_bw(struct gui_window *gw);
/** Flush pending invalidations to the canvas. UI thread. */
void ov_window_redraw(struct gui_window *gw);
void ov_window_scroll_by(struct gui_window *gw, int dx, int dy);
void ov_window_click(struct gui_window *gw, int x, int y);
/** Shell state snapshot; strings stay valid until the next core call. */
struct ov_window_state {
	const char *url;
	const char *title;
	const char *status;
	bool loading;
	bool can_back, can_forward;
	bool wants_input;
	int scroll_y, extent_y;
};
void ov_window_state(struct gui_window *gw, struct ov_window_state *st);
void ov_window_input_done(struct gui_window *gw);
void ov_window_detach_view(void);

#endif
