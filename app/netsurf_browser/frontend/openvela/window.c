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
 * Single browser window drawn into an LVGL canvas.
 *
 * Invalidations are accumulated as one dirty rectangle in canvas
 * coordinates and redrawn from the UI tick. Vertical scrolling moves the
 * existing pixels and redraws only the exposed strip.
 */

#include <stdlib.h>
#include <string.h>

#include "utils/errors.h"
#include "utils/nsurl.h"
#include "utils/utils.h"
#include "netsurf/types.h"
#include "netsurf/window.h"
#include "netsurf/browser_window.h"
#include "netsurf/plotters.h"
#include "netsurf/mouse.h"
#include "desktop/browser_history.h"

#include "openvela/openvela.h"

struct gui_window {
	struct browser_window *bw;
	struct ov_view view;
	bool has_view;
	int sx, sy;
	lv_area_t dirty;
	bool dirty_valid;
	char *title;
	char *url;
	char *status;
	bool loading;
	bool wants_input;
};

static struct gui_window *current;
static struct ov_view pending_view;
static bool pending_valid;

void ov_window_set_view(const struct ov_view *view)
{
	pending_view = *view;
	pending_valid = true;
	if (current != NULL) {
		current->view = *view;
		current->has_view = true;
		current->dirty_valid = false;
		if (current->bw != NULL) {
			browser_window_schedule_reformat(current->bw);
		}
		ov_window_table->invalidate(current, NULL);
	}
}

void ov_window_detach_view(void)
{
	pending_valid = false;
	if (current != NULL) {
		current->has_view = false;
		current->dirty_valid = false;
	}
}

struct gui_window *ov_window_current(void)
{
	return current;
}

struct browser_window *ov_window_bw(struct gui_window *gw)
{
	return gw != NULL ? gw->bw : NULL;
}

static void replace(char **slot, const char *value)
{
	free(*slot);
	*slot = value != NULL ? strdup(value) : NULL;
}

static struct gui_window *ov_window_create(struct browser_window *bw,
					   struct gui_window *existing,
					   gui_window_create_flags flags)
{
	struct gui_window *gw;

	(void)existing;
	(void)flags;
	if (current != NULL) {
		/* single view: popups and new windows reuse it */
		return NULL;
	}
	gw = calloc(1, sizeof(*gw));
	if (gw == NULL) {
		return NULL;
	}
	gw->bw = bw;
	if (pending_valid) {
		gw->view = pending_view;
		gw->has_view = true;
	}
	current = gw;
	return gw;
}

static void ov_window_destroy(struct gui_window *gw)
{
	if (gw == current) {
		current = NULL;
	}
	free(gw->title);
	free(gw->url);
	free(gw->status);
	free(gw);
}

static void add_dirty(struct gui_window *gw, const lv_area_t *area)
{
	lv_area_t full = {0, 0, gw->view.width - 1, gw->view.height - 1};
	lv_area_t clipped;

	if (!lv_area_intersect(&clipped, area, &full)) {
		return;
	}
	if (gw->dirty_valid) {
		lv_area_join(&gw->dirty, &gw->dirty, &clipped);
	} else {
		gw->dirty = clipped;
		gw->dirty_valid = true;
	}
}

static nserror ov_window_invalidate(struct gui_window *gw, const struct rect *rect)
{
	lv_area_t area;

	if (!gw->has_view) {
		return NSERROR_OK;
	}
	if (rect == NULL) {
		area.x1 = 0;
		area.y1 = 0;
		area.x2 = gw->view.width - 1;
		area.y2 = gw->view.height - 1;
	} else {
		/* rect is in document coordinates */
		area.x1 = rect->x0 - gw->sx;
		area.y1 = rect->y0 - gw->sy;
		area.x2 = rect->x1 - gw->sx - 1;
		area.y2 = rect->y1 - gw->sy - 1;
	}
	add_dirty(gw, &area);
	return NSERROR_OK;
}

static bool ov_window_get_scroll(struct gui_window *gw, int *sx, int *sy)
{
	*sx = gw->sx;
	*sy = gw->sy;
	return true;
}

static void clamp_scroll(struct gui_window *gw, int *sx, int *sy)
{
	int w = 0, h = 0;

	if (browser_window_get_extents(gw->bw, true, &w, &h) != NSERROR_OK) {
		w = gw->view.width;
		h = gw->view.height;
	}
	if (*sx > w - gw->view.width) *sx = w - gw->view.width;
	if (*sy > h - gw->view.height) *sy = h - gw->view.height;
	if (*sx < 0) *sx = 0;
	if (*sy < 0) *sy = 0;
}

/** Move to a new scroll offset, reusing on-screen pixels where possible. */
static void scroll_to(struct gui_window *gw, int nx, int ny)
{
	int dy;

	clamp_scroll(gw, &nx, &ny);
	if (nx == gw->sx && ny == gw->sy) {
		return;
	}
	dy = ny - gw->sy;
	if (!gw->has_view) {
		gw->sx = nx;
		gw->sy = ny;
		return;
	}
	if (nx == gw->sx && abs(dy) < gw->view.height && !gw->dirty_valid) {
		uint8_t *base = gw->view.buf->data;
		uint32_t stride = gw->view.buf->header.stride;
		int rows = gw->view.height - abs(dy);
		lv_area_t strip;

		if (dy > 0) {
			memmove(base, base + (size_t)dy * stride, (size_t)rows * stride);
			strip.y1 = rows;
			strip.y2 = gw->view.height - 1;
		} else {
			memmove(base + (size_t)(-dy) * stride, base, (size_t)rows * stride);
			strip.y1 = 0;
			strip.y2 = -dy - 1;
		}
		strip.x1 = 0;
		strip.x2 = gw->view.width - 1;
		gw->sy = ny;
		add_dirty(gw, &strip);
		lv_obj_invalidate(gw->view.obj);
		return;
	}
	gw->sx = nx;
	gw->sy = ny;
	ov_window_invalidate(gw, NULL);
}

static nserror ov_window_set_scroll(struct gui_window *gw, const struct rect *rect)
{
	scroll_to(gw, rect->x0, rect->y0);
	return NSERROR_OK;
}

static nserror ov_window_get_dimensions(struct gui_window *gw, int *width, int *height)
{
	*width = gw->has_view ? gw->view.width : 800;
	*height = gw->has_view ? gw->view.height : 480;
	return NSERROR_OK;
}

static nserror ov_window_event(struct gui_window *gw, enum gui_window_event event)
{
	int sx, sy;

	switch (event) {
	case GW_EVENT_UPDATE_EXTENT:
		sx = gw->sx;
		sy = gw->sy;
		clamp_scroll(gw, &sx, &sy);
		if (sx != gw->sx || sy != gw->sy) {
			gw->sx = sx;
			gw->sy = sy;
			ov_window_invalidate(gw, NULL);
		}
		break;
	case GW_EVENT_START_THROBBER:
		gw->loading = true;
		break;
	case GW_EVENT_STOP_THROBBER:
		gw->loading = false;
		break;
	case GW_EVENT_NEW_CONTENT:
		gw->sx = 0;
		gw->sy = 0;
		gw->wants_input = false;
		ov_window_invalidate(gw, NULL);
		break;
	case GW_EVENT_REMOVE_CARET:
		gw->wants_input = false;
		break;
	default:
		break;
	}
	return NSERROR_OK;
}

static void ov_window_set_title(struct gui_window *gw, const char *title)
{
	replace(&gw->title, title);
}

static nserror ov_window_set_url(struct gui_window *gw, struct nsurl *url)
{
	replace(&gw->url, nsurl_access(url));
	return NSERROR_OK;
}

static void ov_window_set_status(struct gui_window *gw, const char *text)
{
	replace(&gw->status, text);
}

static void ov_window_place_caret(struct gui_window *gw, int x, int y,
				  int height, const struct rect *clip)
{
	(void)x; (void)y; (void)height; (void)clip;
	/* a text field took focus: the shell opens the system IME */
	gw->wants_input = true;
}

static struct gui_window_table window_table = {
	.create = ov_window_create,
	.destroy = ov_window_destroy,
	.invalidate = ov_window_invalidate,
	.get_scroll = ov_window_get_scroll,
	.set_scroll = ov_window_set_scroll,
	.get_dimensions = ov_window_get_dimensions,
	.event = ov_window_event,
	.set_title = ov_window_set_title,
	.set_url = ov_window_set_url,
	.set_status = ov_window_set_status,
	.place_caret = ov_window_place_caret,
};

struct gui_window_table *ov_window_table = &window_table;

void ov_window_redraw(struct gui_window *gw)
{
	struct ov_plot_ctx pctx;
	struct redraw_context ctx = {
		.interactive = true,
		.background_images = true,
		.plot = &ov_plotters,
		.priv = &pctx,
	};
	struct rect clip;
	lv_layer_t layer;

	if (gw == NULL || !gw->has_view || !gw->dirty_valid) {
		return;
	}
	if (!browser_window_redraw_ready(gw->bw)) {
		return;
	}

	/* Offscreen layer over the view's draw buffer (what lv_canvas does;
	 * the canvas widget is not part of this firmware image). */
	memset(&layer, 0, sizeof(layer));
	layer.draw_buf = gw->view.buf;
	layer.color_format = gw->view.buf->header.cf;
	layer.buf_area.x2 = gw->view.width - 1;
	layer.buf_area.y2 = gw->view.height - 1;
	layer.phy_clip_area = layer.buf_area;
	pctx.layer = &layer;
	pctx.bounds.x1 = 0;
	pctx.bounds.y1 = 0;
	pctx.bounds.x2 = gw->view.width - 1;
	pctx.bounds.y2 = gw->view.height - 1;
	pctx.clip = gw->dirty;
	pctx.empty = false;
	layer._clip_area = gw->dirty;

	clip.x0 = gw->dirty.x1;
	clip.y0 = gw->dirty.y1;
	clip.x1 = gw->dirty.x2 + 1;
	clip.y1 = gw->dirty.y2 + 1;
	gw->dirty_valid = false;

	browser_window_redraw(gw->bw, -gw->sx, -gw->sy, &clip, &ctx);

	/* LV_USE_OS=0: draw units run synchronously in dispatch */
	lv_display_t *disp = lv_obj_get_display(gw->view.obj);
	while (layer.draw_task_head != NULL) {
		lv_draw_dispatch_wait_for_request();
		if (!lv_draw_dispatch_layer(disp, &layer)) {
			lv_draw_dispatch_request();
		}
	}
	lv_obj_invalidate(gw->view.obj);
}

void ov_window_scroll_by(struct gui_window *gw, int dx, int dy)
{
	if (gw != NULL) {
		scroll_to(gw, gw->sx + dx, gw->sy + dy);
	}
}

void ov_window_click(struct gui_window *gw, int x, int y)
{
	if (gw == NULL || gw->bw == NULL) {
		return;
	}
	x += gw->sx;
	y += gw->sy;
	browser_window_mouse_click(gw->bw, BROWSER_MOUSE_PRESS_1, x, y);
	browser_window_mouse_click(gw->bw, BROWSER_MOUSE_CLICK_1, x, y);
}

void ov_window_state(struct gui_window *gw, struct ov_window_state *st)
{
	int w = 0, h = 0;

	memset(st, 0, sizeof(*st));
	if (gw == NULL) {
		return;
	}
	st->url = gw->url;
	st->title = gw->title;
	st->status = gw->status;
	st->loading = gw->loading;
	st->wants_input = gw->wants_input;
	st->can_back = browser_window_back_available(gw->bw);
	st->can_forward = browser_window_forward_available(gw->bw);
	st->scroll_y = gw->sy;
	if (browser_window_get_extents(gw->bw, true, &w, &h) == NSERROR_OK) {
		st->extent_y = h;
	}
}

void ov_window_input_done(struct gui_window *gw)
{
	if (gw != NULL) {
		gw->wants_input = false;
	}
}
