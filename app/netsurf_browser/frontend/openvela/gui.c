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
 * openvela native module entry: NetSurf as a dynamically loaded app.
 *
 * The firmware's generic native loader calls attach() on the UI thread
 * with the Quick App's JS context and content area. This module installs
 * `system.browser` for the app.js shell and drives the NetSurf core from
 * an LVGL timer. Memory limits are this application's own policy and are
 * computed from the free heap when the core starts.
 */

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <quickjs.h>

#include "utils/errors.h"
#include "utils/log.h"
#include "utils/messages.h"
#include "utils/nsoption.h"
#include "utils/nsurl.h"
#include "utils/utf8.h"
#include "netsurf/bitmap.h"
#include "netsurf/browser.h"
#include "netsurf/browser_window.h"
#include "netsurf/keypress.h"
#include "netsurf/misc.h"
#include "netsurf/netsurf.h"
#include "desktop/browser_history.h"
#include "desktop/searchweb.h"

#include "native/native_module.h"
#include "native/native_host_abi.h"

#include "openvela/openvela.h"

#define OV_PACKAGE "com.netsurf.browser"
#define OV_TICK_MS 10
#define OV_DRAG_SLOP 10

struct ov_budget ov_budget;

static struct {
	bool core_up;
	lv_obj_t *root;
	lv_obj_t *canvas;
	lv_draw_buf_t draw_buf;
	uint8_t *pixels;
	lv_timer_t *tick;
	/* touch tracking */
	bool pressing, dragging;
	lv_point_t press, last;
	/* shell strings returned to JS */
	char error[96];
} ui;

/* ---------------------------------------------------------------- core */

static nserror ov_launch_url(struct nsurl *url)
{
	(void)url;
	return NSERROR_NO_FETCH_HANDLER;
}

static void ov_quit(void)
{
}

static struct gui_misc_table misc_table = {
	.schedule = ov_schedule,
	.quit = ov_quit,
	.launch_url = ov_launch_url,
};

static nserror set_defaults(struct nsoption_s *defaults)
{
	(void)defaults;
	return NSERROR_OK;
}

/** This application's memory policy, derived from the free heap. */
static void compute_budget(void)
{
	size_t free_bytes = (size_t)mallinfo().fordblks;
	size_t cache = free_bytes / 6;

	if (cache < 2u * 1024u * 1024u) cache = 2u * 1024u * 1024u;
	if (cache > 12u * 1024u * 1024u) cache = 12u * 1024u * 1024u;
	ov_budget.cache = cache;
	/* one decoded image may use at most a third of the cache */
	ov_budget.image_bytes = cache / 3;
	if (ov_budget.image_bytes < 1024u * 1024u) ov_budget.image_bytes = 1024u * 1024u;
	ov_budget.fetch_queue = 256u * 1024u;
	ov_budget.body_limit = free_bytes / 4;
	if (ov_budget.body_limit > 16u * 1024u * 1024u) ov_budget.body_limit = 16u * 1024u * 1024u;
	NSLOG(netsurf, INFO, "budget free=%zu cache=%zu image=%zu body=%zu",
	      free_bytes, ov_budget.cache, ov_budget.image_bytes,
	      ov_budget.body_limit);
}

static nserror core_start(void)
{
	/* netsurf_register() keeps this pointer (guit) for the core's lifetime */
	static struct netsurf_table table;
	const bitmap_fmt_t fmt = {.layout = BITMAP_LAYOUT_ARGB8888, .pma = false};
	const uint8_t *messages;
	size_t messages_len;
	nserror ret;

	memset(&table, 0, sizeof(table));
	table.misc = &misc_table;
	table.window = ov_window_table;
	table.fetch = ov_fetch_table;
	table.bitmap = ov_bitmap_table;
	table.layout = ov_layout_table;
	ret = netsurf_register(&table);
	if (ret != NSERROR_OK) {
		return ret;
	}
	compute_budget();

	ret = nsoption_init(set_defaults, &nsoptions, &nsoptions_default);
	if (ret != NSERROR_OK) {
		return ret;
	}
	nsoption_set_int(memory_cache_size, (int)ov_budget.cache);
	nsoption_set_uint(disc_cache_size, 0);
	nsoption_set_int(max_fetchers, 4);
	nsoption_set_int(max_fetchers_per_host, 2);
	nsoption_set_int(max_cached_fetch_handles, 2);
	nsoption_set_bool(enable_javascript, false);
	/* one view: target=_blank links open in place */
	nsoption_set_bool(target_blank, false);
	nsoption_set_charp(accept_language, strdup("zh-CN,zh,en"));
	nsoption_set_charp(accept_charset, strdup("utf-8,gbk,gb2312"));

	messages = ov_messages(&messages_len);
	if (messages != NULL) {
		messages_add_from_inline(messages, messages_len);
	}
	bitmap_set_format(&fmt);

	ret = netsurf_init(NULL);
	if (ret != NSERROR_OK) {
		return ret;
	}
	ret = ov_http_register();
	if (ret == NSERROR_OK) {
		ret = ov_image_register();
	}
	if (ret != NSERROR_OK) {
		netsurf_exit();
		return ret;
	}
	ui.core_up = true;
	return NSERROR_OK;
}

static void core_stop(void)
{
	if (!ui.core_up) {
		return;
	}
	netsurf_exit();
	ov_schedule_clear();
	ov_http_finalise();
	nsoption_finalise(nsoptions, nsoptions_default);
	ui.core_up = false;
}

/* ----------------------------------------------------------------- view */

static void tick(lv_timer_t *timer)
{
	(void)timer;
	ov_schedule_run();
	ov_window_redraw(ov_window_current());
}

static void canvas_event(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);
	lv_indev_t *indev = lv_indev_active();
	struct gui_window *gw = ov_window_current();
	lv_point_t pt;
	lv_area_t at;

	if (indev == NULL || gw == NULL) {
		return;
	}
	lv_indev_get_point(indev, &pt);
	lv_obj_get_coords(ui.canvas, &at);

	switch (code) {
	case LV_EVENT_PRESSED:
		ui.pressing = true;
		ui.dragging = false;
		ui.press = pt;
		ui.last = pt;
		break;
	case LV_EVENT_PRESSING:
		if (!ui.pressing) {
			break;
		}
		if (!ui.dragging &&
		    (abs(pt.x - ui.press.x) > OV_DRAG_SLOP ||
		     abs(pt.y - ui.press.y) > OV_DRAG_SLOP)) {
			ui.dragging = true;
		}
		if (ui.dragging) {
			ov_window_scroll_by(gw, ui.last.x - pt.x, ui.last.y - pt.y);
			ui.last = pt;
		}
		break;
	case LV_EVENT_RELEASED:
		if (ui.pressing && !ui.dragging) {
			ov_window_click(gw, pt.x - at.x1, pt.y - at.y1);
		}
		ui.pressing = false;
		ui.dragging = false;
		break;
	case LV_EVENT_PRESS_LOST:
		ui.pressing = false;
		ui.dragging = false;
		break;
	default:
		break;
	}
}

static void view_destroy(void)
{
	ov_window_detach_view();
	if (ui.canvas != NULL) {
		lv_image_cache_drop(&ui.draw_buf);
		lv_obj_delete(ui.canvas);
		ui.canvas = NULL;
	}
	free(ui.pixels);
	ui.pixels = NULL;
}

static bool view_create(int top)
{
	int width, height;
	uint32_t stride;
	size_t bytes;
	struct ov_view view;

	if (ui.canvas != NULL) {
		return true;
	}
	lv_obj_update_layout(ui.root);
	width = lv_obj_get_width(ui.root);
	height = lv_obj_get_height(ui.root) - top;
	if (width < 64 || height < 64) {
		snprintf(ui.error, sizeof(ui.error), "browser area too small");
		return false;
	}
	stride = (uint32_t)width * 2;
	bytes = (size_t)stride * (size_t)height;
	ui.pixels = malloc(bytes);
	if (ui.pixels == NULL) {
		snprintf(ui.error, sizeof(ui.error), "not enough memory for the page view");
		return false;
	}
	memset(ui.pixels, 0xff, bytes);
	if (lv_draw_buf_init(&ui.draw_buf, width, height, LV_COLOR_FORMAT_RGB565,
			     stride, ui.pixels, bytes) != LV_RESULT_OK) {
		free(ui.pixels);
		ui.pixels = NULL;
		return false;
	}

	ui.canvas = lv_image_create(ui.root);
	if (ui.canvas == NULL) {
		free(ui.pixels);
		ui.pixels = NULL;
		return false;
	}
	/* an lv_draw_buf_t is a valid image source; pixels are used in place */
	lv_image_set_src(ui.canvas, &ui.draw_buf);
	lv_obj_set_size(ui.canvas, width, height);
	lv_obj_set_pos(ui.canvas, 0, top);
	lv_obj_add_flag(ui.canvas, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_remove_flag(ui.canvas, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_remove_flag(ui.canvas, LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_remove_flag(ui.canvas, LV_OBJ_FLAG_SCROLL_CHAIN);
	lv_obj_add_event_cb(ui.canvas, canvas_event, LV_EVENT_ALL, NULL);

	view.obj = ui.canvas;
	view.buf = &ui.draw_buf;
	view.width = width;
	view.height = height;
	ov_window_set_view(&view);
	return true;
}

/* -------------------------------------------------------------- JS API */

static const char *arg_string(JSContext *ctx, int argc, JSValueConst *argv, int i)
{
	return i < argc ? JS_ToCString(ctx, argv[i]) : NULL;
}

static int arg_int(JSContext *ctx, int argc, JSValueConst *argv, int i, int def)
{
	int32_t v = def;
	if (i < argc) {
		JS_ToInt32(ctx, &v, argv[i]);
	}
	return v;
}

static nserror navigate_text(const char *text)
{
	struct gui_window *gw = ov_window_current();
	nsurl *url = NULL;
	nserror ret;

	if (text == NULL || text[0] == '\0') {
		return NSERROR_BAD_PARAMETER;
	}
	ret = search_web_omni(text, SEARCH_WEB_OMNI_NONE, &url);
	if (ret != NSERROR_OK) {
		return ret;
	}
	if (gw == NULL) {
		struct browser_window *bw;
		ret = browser_window_create(BW_CREATE_HISTORY, url, NULL, NULL, &bw);
	} else {
		ret = browser_window_navigate(ov_window_bw(gw), url, NULL,
					      BW_NAVIGATE_HISTORY, NULL, NULL, NULL);
	}
	nsurl_unref(url);
	return ret;
}

static JSValue result(JSContext *ctx, nserror ret)
{
	return JS_NewBool(ctx, ret == NSERROR_OK);
}

/* open(url, top): create the page view below the shell toolbar */
static JSValue js_open(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	(void)this_val;
	int top = arg_int(ctx, argc, argv, 1, 0);
	const char *url = arg_string(ctx, argc, argv, 0);
	nserror ret = NSERROR_INIT_FAILED;

	if (view_create(top < 0 ? 0 : top)) {
		ret = navigate_text(url != NULL ? url : "about:welcome");
	}
	if (url != NULL) {
		JS_FreeCString(ctx, url);
	}
	return result(ctx, ret);
}

static JSValue js_go(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	(void)this_val;
	const char *url = arg_string(ctx, argc, argv, 0);
	nserror ret = navigate_text(url);
	if (url != NULL) {
		JS_FreeCString(ctx, url);
	}
	return result(ctx, ret);
}

static struct browser_window *current_bw(void)
{
	return ov_window_bw(ov_window_current());
}

static JSValue js_back(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	(void)this_val; (void)argc; (void)argv;
	struct browser_window *bw = current_bw();
	if (bw == NULL || !browser_window_back_available(bw)) {
		return JS_FALSE;
	}
	return result(ctx, browser_window_history_back(bw, false));
}

static JSValue js_forward(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	(void)this_val; (void)argc; (void)argv;
	struct browser_window *bw = current_bw();
	if (bw == NULL || !browser_window_forward_available(bw)) {
		return JS_FALSE;
	}
	return result(ctx, browser_window_history_forward(bw, false));
}

static JSValue js_reload(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	(void)this_val; (void)argc; (void)argv;
	struct browser_window *bw = current_bw();
	return bw != NULL ? result(ctx, browser_window_reload(bw, true)) : JS_FALSE;
}

static JSValue js_stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	(void)ctx; (void)this_val; (void)argc; (void)argv;
	struct browser_window *bw = current_bw();
	if (bw != NULL) {
		browser_window_stop(bw);
	}
	return JS_UNDEFINED;
}

static JSValue js_scroll(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	(void)this_val;
	ov_window_scroll_by(ov_window_current(), 0, arg_int(ctx, argc, argv, 0, 0));
	return JS_UNDEFINED;
}

/* type(text, submit): send UTF-8 text to the focused form field */
static JSValue js_type(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	(void)this_val;
	struct gui_window *gw = ov_window_current();
	struct browser_window *bw = ov_window_bw(gw);
	const char *text = arg_string(ctx, argc, argv, 0);
	bool submit = argc > 1 && JS_ToBool(ctx, argv[1]) > 0;

	if (bw != NULL && text != NULL) {
		size_t len = strlen(text), off = 0;
		while (off < len) {
			browser_window_key_press(bw, utf8_to_ucs4(text + off, len - off));
			off = utf8_next(text, len, off);
		}
		if (submit) {
			browser_window_key_press(bw, NS_KEY_NL);
		}
	}
	ov_window_input_done(gw);
	if (text != NULL) {
		JS_FreeCString(ctx, text);
	}
	return JS_UNDEFINED;
}

static void set_string(JSContext *ctx, JSValue obj, const char *key, const char *v)
{
	JS_SetPropertyStr(ctx, obj, key, JS_NewString(ctx, v != NULL ? v : ""));
}

static JSValue js_state(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
	(void)this_val; (void)argc; (void)argv;
	struct ov_window_state st;
	JSValue obj = JS_NewObject(ctx);

	ov_window_state(ov_window_current(), &st);
	set_string(ctx, obj, "url", st.url);
	set_string(ctx, obj, "title", st.title);
	set_string(ctx, obj, "status", st.status);
	set_string(ctx, obj, "error", ui.error);
	JS_SetPropertyStr(ctx, obj, "loading", JS_NewBool(ctx, st.loading));
	JS_SetPropertyStr(ctx, obj, "canBack", JS_NewBool(ctx, st.can_back));
	JS_SetPropertyStr(ctx, obj, "canForward", JS_NewBool(ctx, st.can_forward));
	JS_SetPropertyStr(ctx, obj, "wantsInput", JS_NewBool(ctx, st.wants_input));
	JS_SetPropertyStr(ctx, obj, "scrollY", JS_NewInt32(ctx, st.scroll_y));
	JS_SetPropertyStr(ctx, obj, "extentY", JS_NewInt32(ctx, st.extent_y));
	return obj;
}

static void install_api(JSContext *ctx, JSValue system)
{
	static const struct {
		const char *name;
		JSCFunction *fn;
		int len;
	} fns[] = {
		{"open", js_open, 2}, {"go", js_go, 1}, {"back", js_back, 0},
		{"forward", js_forward, 0}, {"reload", js_reload, 0},
		{"stop", js_stop, 0}, {"scroll", js_scroll, 1},
		{"type", js_type, 2}, {"state", js_state, 0},
	};
	JSValue api = JS_NewObject(ctx);

	for (size_t i = 0; i < sizeof(fns) / sizeof(fns[0]); i++) {
		JS_SetPropertyStr(ctx, api, fns[i].name,
				  JS_NewCFunction(ctx, fns[i].fn, fns[i].name, fns[i].len));
	}
	JS_SetPropertyStr(ctx, system, "browser", api);
}

/* ------------------------------------------------------- module ABI */

static bool ov_attach(JSContext *ctx, JSValue system, lv_obj_t *root,
		      const char *package)
{
	(void)package;
	ui.error[0] = '\0';
	if (!ui.core_up && core_start() != NSERROR_OK) {
		return false;
	}
	ui.root = root;
	ui.tick = lv_timer_create(tick, OV_TICK_MS, NULL);
	if (ui.tick == NULL) {
		return false;
	}
	install_api(ctx, system);
	return true;
}

static void ov_detach(void)
{
	struct gui_window *gw = ov_window_current();

	if (ui.tick != NULL) {
		lv_timer_delete(ui.tick);
		ui.tick = NULL;
	}
	if (gw != NULL) {
		browser_window_destroy(ov_window_bw(gw));
	}
	view_destroy();
	ov_http_abort_all();
	ui.root = NULL;
}

static bool ov_quiesce(void)
{
	/* Give aborted fetches a short window to finish and be released by
	 * the core's own poll. A worker stuck in DNS keeps the module resident;
	 * the loader then reattaches the same instance on the next launch. */
	for (int i = 0; i < 20; i++) {
		ov_schedule_run();
		if (ov_http_idle() && ov_http_empty()) {
			break;
		}
		usleep(10 * 1000);
	}
	/* Unload only once no fetch worker can still run module code. */
	if (!ov_http_idle()) {
		return false;
	}
	core_stop();
	return true;
}

static const struct ov_native_module module = {
	.abi = OV_NATIVE_MODULE_ABI,
	.bytes = sizeof(struct ov_native_module),
	.host_abi = OV_NATIVE_HOST_ABI,
	.package = OV_PACKAGE,
	.attach = ov_attach,
	.detach = ov_detach,
	.quiesce = ov_quiesce,
};

__attribute__((visibility("default")))
const struct ov_native_module *ov_native_module_query(void)
{
	return &module;
}
