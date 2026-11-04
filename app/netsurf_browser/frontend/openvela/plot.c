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
 * Plotters implemented with the LVGL draw API on a canvas layer.
 *
 * NetSurf rectangles are half open (x1/y1 exclusive); LVGL areas are
 * inclusive. Every draw call records the layer's current clip area.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "utils/errors.h"
#include "netsurf/plotters.h"
#include "netsurf/plot_style.h"
#include "netsurf/types.h"

#include "openvela/openvela.h"

#define OV_MAX_TILES 1024
#define OV_PATH_POINTS 512

static inline lv_color_t ns_colour(colour c)
{
	return lv_color_make(c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff);
}

static inline struct ov_plot_ctx *pctx(const struct redraw_context *ctx)
{
	return ctx->priv;
}

static nserror ov_plot_clip(const struct redraw_context *ctx,
			    const struct rect *clip)
{
	struct ov_plot_ctx *p = pctx(ctx);
	lv_area_t area = {clip->x0, clip->y0, clip->x1 - 1, clip->y1 - 1};

	p->empty = !lv_area_intersect(&p->clip, &area, &p->bounds);
	if (!p->empty) {
		p->layer->_clip_area = p->clip;
	}
	return NSERROR_OK;
}

static int stroke_px(const plot_style_t *style)
{
	int w = plot_style_fixed_to_int(style->stroke_width);
	return w < 1 ? 1 : w;
}

static void dash_for(const plot_style_t *style, lv_draw_line_dsc_t *d)
{
	if (style->stroke_type == PLOT_OP_TYPE_DOT) {
		d->dash_width = d->width;
		d->dash_gap = d->width;
	} else if (style->stroke_type == PLOT_OP_TYPE_DASH) {
		d->dash_width = d->width * 3;
		d->dash_gap = d->width * 3;
	}
}

static void draw_line(struct ov_plot_ctx *p, const plot_style_t *style,
		      float x0, float y0, float x1, float y1)
{
	lv_draw_line_dsc_t d;
	lv_draw_line_dsc_init(&d);
	d.color = ns_colour(style->stroke_colour);
	d.width = stroke_px(style);
	d.opa = LV_OPA_COVER;
	d.p1.x = x0;
	d.p1.y = y0;
	d.p2.x = x1;
	d.p2.y = y1;
	dash_for(style, &d);
	lv_draw_line(p->layer, &d);
}

static void fill_area(struct ov_plot_ctx *p, const lv_area_t *area,
		      colour c, int32_t radius)
{
	lv_draw_rect_dsc_t d;
	lv_draw_rect_dsc_init(&d);
	d.bg_color = ns_colour(c);
	d.bg_opa = LV_OPA_COVER;
	d.border_width = 0;
	d.radius = radius;
	lv_draw_rect(p->layer, &d, area);
}

static nserror ov_plot_rectangle(const struct redraw_context *ctx,
				 const plot_style_t *style,
				 const struct rect *r)
{
	struct ov_plot_ctx *p = pctx(ctx);
	lv_area_t area = {r->x0, r->y0, r->x1 - 1, r->y1 - 1};

	if (p->empty || area.x2 < area.x1 || area.y2 < area.y1) {
		return NSERROR_OK;
	}
	if (style->fill_type != PLOT_OP_TYPE_NONE) {
		fill_area(p, &area, style->fill_colour, 0);
	}
	if (style->stroke_type == PLOT_OP_TYPE_SOLID) {
		lv_draw_rect_dsc_t d;
		lv_draw_rect_dsc_init(&d);
		d.bg_opa = LV_OPA_TRANSP;
		d.border_color = ns_colour(style->stroke_colour);
		d.border_opa = LV_OPA_COVER;
		d.border_width = stroke_px(style);
		lv_draw_rect(p->layer, &d, &area);
	} else if (style->stroke_type != PLOT_OP_TYPE_NONE) {
		draw_line(p, style, r->x0, r->y0, r->x1, r->y0);
		draw_line(p, style, r->x1, r->y0, r->x1, r->y1);
		draw_line(p, style, r->x1, r->y1, r->x0, r->y1);
		draw_line(p, style, r->x0, r->y1, r->x0, r->y0);
	}
	return NSERROR_OK;
}

static nserror ov_plot_line(const struct redraw_context *ctx,
			    const plot_style_t *style, const struct rect *line)
{
	struct ov_plot_ctx *p = pctx(ctx);
	if (!p->empty && style->stroke_type != PLOT_OP_TYPE_NONE) {
		draw_line(p, style, line->x0, line->y0, line->x1, line->y1);
	}
	return NSERROR_OK;
}

static void fill_triangle(struct ov_plot_ctx *p, colour c,
			  float ax, float ay, float bx, float by,
			  float cx, float cy)
{
	lv_draw_triangle_dsc_t d;
	lv_draw_triangle_dsc_init(&d);
	d.bg_color = ns_colour(c);
	d.bg_opa = LV_OPA_COVER;
	d.p[0].x = ax; d.p[0].y = ay;
	d.p[1].x = bx; d.p[1].y = by;
	d.p[2].x = cx; d.p[2].y = cy;
	lv_draw_triangle(p->layer, &d);
}

static nserror ov_plot_polygon(const struct redraw_context *ctx,
			       const plot_style_t *style, const int *pts,
			       unsigned int n)
{
	struct ov_plot_ctx *p = pctx(ctx);
	if (p->empty || n < 3 || style->fill_type == PLOT_OP_TYPE_NONE) {
		return NSERROR_OK;
	}
	/* fan triangulation: exact for the convex shapes the core emits */
	for (unsigned int i = 1; i + 1 < n; i++) {
		fill_triangle(p, style->fill_colour, pts[0], pts[1],
			      pts[i * 2], pts[i * 2 + 1],
			      pts[i * 2 + 2], pts[i * 2 + 3]);
	}
	return NSERROR_OK;
}

static nserror ov_plot_disc(const struct redraw_context *ctx,
			    const plot_style_t *style, int x, int y, int radius)
{
	struct ov_plot_ctx *p = pctx(ctx);
	lv_area_t area = {x - radius, y - radius, x + radius, y + radius};

	if (p->empty || radius <= 0) {
		return NSERROR_OK;
	}
	if (style->fill_type != PLOT_OP_TYPE_NONE) {
		fill_area(p, &area, style->fill_colour, LV_RADIUS_CIRCLE);
	}
	if (style->stroke_type != PLOT_OP_TYPE_NONE) {
		lv_draw_rect_dsc_t d;
		lv_draw_rect_dsc_init(&d);
		d.bg_opa = LV_OPA_TRANSP;
		d.radius = LV_RADIUS_CIRCLE;
		d.border_color = ns_colour(style->stroke_colour);
		d.border_opa = LV_OPA_COVER;
		d.border_width = stroke_px(style);
		lv_draw_rect(p->layer, &d, &area);
	}
	return NSERROR_OK;
}

static nserror ov_plot_arc(const struct redraw_context *ctx,
			   const plot_style_t *style, int x, int y,
			   int radius, int angle1, int angle2)
{
	struct ov_plot_ctx *p = pctx(ctx);
	lv_draw_arc_dsc_t d;

	if (p->empty || radius <= 0) {
		return NSERROR_OK;
	}
	lv_draw_arc_dsc_init(&d);
	/* NetSurf: anticlockwise from 3 o'clock; LVGL: clockwise */
	d.start_angle = 360 - angle2;
	d.end_angle = 360 - angle1;
	d.center.x = x;
	d.center.y = y;
	d.radius = radius;
	d.width = stroke_px(style);
	d.opa = LV_OPA_COVER;
	d.color = ns_colour(style->stroke_type != PLOT_OP_TYPE_NONE ?
			    style->stroke_colour : style->fill_colour);
	lv_draw_arc(p->layer, &d);
	return NSERROR_OK;
}

static nserror ov_plot_path(const struct redraw_context *ctx,
			    const plot_style_t *style, const float *path,
			    unsigned int n, const float t[6])
{
	struct ov_plot_ctx *p = pctx(ctx);
	float *pts;
	unsigned int count = 0, start = 0, i = 0;
	float cx = 0, cy = 0;

	if (p->empty || n == 0) {
		return NSERROR_OK;
	}
	pts = malloc(sizeof(float) * 2 * OV_PATH_POINTS);
	if (pts == NULL) {
		return NSERROR_NOMEM;
	}

#define XF(x, y) (t[0] * (x) + t[2] * (y) + t[4])
#define YF(x, y) (t[1] * (x) + t[3] * (y) + t[5])
#define ADD(x, y) do { if (count < OV_PATH_POINTS) { \
		pts[count * 2] = XF(x, y); pts[count * 2 + 1] = YF(x, y); \
		count++; } } while (0)

	while (i < n) {
		unsigned int cmd = (unsigned int)path[i];
		bool flush = cmd == PLOTTER_PATH_MOVE ||
			     cmd == PLOTTER_PATH_CLOSE;

		if (flush && count > start) {
			unsigned int m = count - start;
			if (style->fill_type != PLOT_OP_TYPE_NONE && m >= 3) {
				for (unsigned int k = 1; k + 1 < m; k++) {
					fill_triangle(p, style->fill_colour,
						pts[start * 2], pts[start * 2 + 1],
						pts[(start + k) * 2], pts[(start + k) * 2 + 1],
						pts[(start + k + 1) * 2], pts[(start + k + 1) * 2 + 1]);
				}
			}
			if (style->stroke_type != PLOT_OP_TYPE_NONE) {
				for (unsigned int k = start; k + 1 < count; k++) {
					draw_line(p, style, pts[k * 2], pts[k * 2 + 1],
						  pts[k * 2 + 2], pts[k * 2 + 3]);
				}
				if (cmd == PLOTTER_PATH_CLOSE && m >= 2) {
					draw_line(p, style,
						  pts[(count - 1) * 2], pts[(count - 1) * 2 + 1],
						  pts[start * 2], pts[start * 2 + 1]);
				}
			}
			start = count;
		}

		if (cmd == PLOTTER_PATH_MOVE || cmd == PLOTTER_PATH_LINE) {
			if (i + 2 >= n) {
				break;
			}
			cx = path[i + 1];
			cy = path[i + 2];
			ADD(cx, cy);
			i += 3;
		} else if (cmd == PLOTTER_PATH_BEZIER) {
			if (i + 6 >= n) {
				break;
			}
			float x1 = path[i + 1], y1 = path[i + 2];
			float x2 = path[i + 3], y2 = path[i + 4];
			float x3 = path[i + 5], y3 = path[i + 6];
			for (int s = 1; s <= 8; s++) {
				float u = s / 8.0f, v = 1.0f - u;
				float bx = v * v * v * cx + 3 * v * v * u * x1 +
					   3 * v * u * u * x2 + u * u * u * x3;
				float by = v * v * v * cy + 3 * v * v * u * y1 +
					   3 * v * u * u * y2 + u * u * u * y3;
				ADD(bx, by);
			}
			cx = x3;
			cy = y3;
			i += 7;
		} else {
			i += 1; /* CLOSE or unknown */
		}
	}
	/* flush a trailing open subpath (stroke only) */
	if (count > start + 1 && style->stroke_type != PLOT_OP_TYPE_NONE) {
		for (unsigned int k = start; k + 1 < count; k++) {
			draw_line(p, style, pts[k * 2], pts[k * 2 + 1],
				  pts[k * 2 + 2], pts[k * 2 + 3]);
		}
	}
#undef ADD
#undef XF
#undef YF
	free(pts);
	return NSERROR_OK;
}

static void draw_image(struct ov_plot_ctx *p, const lv_image_dsc_t *img,
		       int x, int y, int32_t sx, int32_t sy, bool tile,
		       const lv_area_t *tile_region)
{
	lv_draw_image_dsc_t d;
	lv_area_t coords;

	lv_draw_image_dsc_init(&d);
	d.src = img;
	d.opa = LV_OPA_COVER;
	d.scale_x = sx;
	d.scale_y = sy;
	d.pivot.x = 0;
	d.pivot.y = 0;
	d.antialias = 0;
	coords.x1 = x;
	coords.y1 = y;
	coords.x2 = x + img->header.w - 1;
	coords.y2 = y + img->header.h - 1;
	if (tile) {
		d.tile = 1;
		d.image_area = coords;
		coords = *tile_region;
	}
	lv_draw_image(p->layer, &d, &coords);
}

static int tile_start(int origin, int step, int limit)
{
	/* first tile position <= limit, aligned to origin */
	if (origin > limit) {
		int n = (origin - limit + step - 1) / step;
		return origin - n * step;
	}
	return origin;
}

static nserror ov_plot_bitmap(const struct redraw_context *ctx,
			      struct bitmap *bitmap, int x, int y,
			      int width, int height, colour bg,
			      bitmap_flags_t flags)
{
	struct ov_plot_ctx *p = pctx(ctx);
	const lv_image_dsc_t *img;
	bool rx = (flags & BITMAPF_REPEAT_X) != 0;
	bool ry = (flags & BITMAPF_REPEAT_Y) != 0;
	int bw, bh;
	int32_t sx, sy;

	(void)bg;
	if (p->empty || width <= 0 || height <= 0 || bitmap == NULL) {
		return NSERROR_OK;
	}
	img = ov_bitmap_image(bitmap);
	bw = ov_bitmap_width(bitmap);
	bh = ov_bitmap_height(bitmap);
	sx = (int32_t)(((int64_t)width * LV_SCALE_NONE) / bw);
	sy = (int32_t)(((int64_t)height * LV_SCALE_NONE) / bh);
	if (sx < 1) sx = 1;
	if (sy < 1) sy = 1;

	if (!rx && !ry) {
		draw_image(p, img, x, y, sx, sy, false, NULL);
		return NSERROR_OK;
	}

	/* repeat region: the clip in tiled directions, the image otherwise */
	lv_area_t region = {
		rx ? p->clip.x1 : x, ry ? p->clip.y1 : y,
		rx ? p->clip.x2 : x + width - 1, ry ? p->clip.y2 : y + height - 1,
	};
	int x0 = rx ? tile_start(x, width, region.x1) : x;
	int y0 = ry ? tile_start(y, height, region.y1) : y;

	if (width == bw && height == bh) {
		/* LVGL tiles unscaled images within one draw task */
		draw_image(p, img, x0, y0, LV_SCALE_NONE, LV_SCALE_NONE,
			   true, &region);
		return NSERROR_OK;
	}

	int64_t tiles = (int64_t)((region.x2 - x0) / width + 1) *
			((region.y2 - y0) / height + 1);
	if (tiles > OV_MAX_TILES) {
		/* pathological scaled tiling: approximate with the clip */
		tiles = OV_MAX_TILES;
	}
	int drawn = 0;
	for (int ty = y0; ty <= region.y2 && drawn < tiles; ty += height) {
		for (int tx = x0; tx <= region.x2 && drawn < tiles; tx += width) {
			draw_image(p, img, tx, ty, sx, sy, false, NULL);
			drawn++;
		}
	}
	return NSERROR_OK;
}

static void draw_label(struct ov_plot_ctx *p, const lv_font_t *font,
		       lv_color_t color, int x, int top, const char *text)
{
	lv_draw_label_dsc_t d;
	lv_area_t area;

	lv_draw_label_dsc_init(&d);
	d.font = font;
	d.color = color;
	d.opa = LV_OPA_COVER;
	d.text = text;
	d.text_local = 1; /* LVGL keeps its own copy until the layer finishes */
	d.flag = LV_TEXT_FLAG_EXPAND;
	area.x1 = x;
	area.y1 = top;
	area.x2 = p->bounds.x2 + 4096;
	area.y2 = top + lv_font_get_line_height(font) - 1;
	lv_draw_label(p->layer, &d, &area);
}

static nserror ov_plot_text(const struct redraw_context *ctx,
			    const plot_font_style_t *fstyle, int x, int y,
			    const char *text, size_t length)
{
	struct ov_plot_ctx *p = pctx(ctx);
	const lv_font_t *font;
	char small[256];
	char *copy = small;
	int top;

	if (p->empty || length == 0) {
		return NSERROR_OK;
	}
	if (length >= sizeof(small)) {
		copy = malloc(length + 1);
		if (copy == NULL) {
			return NSERROR_NOMEM;
		}
	}
	memcpy(copy, text, length);
	copy[length] = '\0';

	font = ov_font_for(fstyle);
	/* y is the baseline; LVGL positions the line box top */
	top = y - (lv_font_get_line_height(font) - font->base_line);
	draw_label(p, font, ns_colour(fstyle->foreground), x, top, copy);
	if (ov_font_bold(fstyle)) {
		draw_label(p, font, ns_colour(fstyle->foreground), x + 1, top, copy);
	}
	if (copy != small) {
		free(copy);
	}
	return NSERROR_OK;
}

const struct plotter_table ov_plotters = {
	.clip = ov_plot_clip,
	.arc = ov_plot_arc,
	.disc = ov_plot_disc,
	.line = ov_plot_line,
	.rectangle = ov_plot_rectangle,
	.polygon = ov_plot_polygon,
	.path = ov_plot_path,
	.bitmap = ov_plot_bitmap,
	.text = ov_plot_text,
	.option_knockout = true,
};
