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
 * Bitmaps backed by LVGL image descriptors.
 *
 * The core decodes into straight-alpha BGRA bytes (BITMAP_LAYOUT_ARGB8888
 * on this little endian target), which is exactly LVGL's ARGB8888, so
 * plotting needs no conversion.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "utils/errors.h"
#include "netsurf/bitmap.h"

#include "openvela/openvela.h"

struct bitmap {
	lv_image_dsc_t img;
	uint8_t *pixels;
	int width;
	int height;
	bool opaque;
};

static void set_format(struct bitmap *b)
{
	b->img.header.cf = b->opaque ? LV_COLOR_FORMAT_XRGB8888 :
				       LV_COLOR_FORMAT_ARGB8888;
}

static void *ov_bitmap_create(int width, int height, enum gui_bitmap_flags flags)
{
	struct bitmap *b;
	size_t bytes;

	if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
		return NULL;
	}
	bytes = (size_t)width * (size_t)height * 4;
	if (bytes > ov_budget.image_bytes) {
		return NULL; /* the application's own per-image budget */
	}

	b = calloc(1, sizeof(*b));
	if (b == NULL) {
		return NULL;
	}
	b->pixels = (flags & BITMAP_CLEAR) ? calloc(1, bytes) : malloc(bytes);
	if (b->pixels == NULL) {
		free(b);
		return NULL;
	}
	b->width = width;
	b->height = height;
	b->opaque = (flags & BITMAP_OPAQUE) != 0;

	b->img.header.magic = LV_IMAGE_HEADER_MAGIC;
	b->img.header.w = width;
	b->img.header.h = height;
	b->img.header.stride = width * 4;
	b->img.data = b->pixels;
	b->img.data_size = bytes;
	set_format(b);
	return b;
}

static void ov_bitmap_destroy(void *bitmap)
{
	struct bitmap *b = bitmap;
	if (b == NULL) {
		return;
	}
	/* LVGL caches decoder state keyed by the descriptor address */
	lv_image_cache_drop(&b->img);
	free(b->pixels);
	free(b);
}

static void ov_bitmap_set_opaque(void *bitmap, bool opaque)
{
	struct bitmap *b = bitmap;
	b->opaque = opaque;
	set_format(b);
	lv_image_cache_drop(&b->img);
}

static bool ov_bitmap_get_opaque(void *bitmap)
{
	return ((struct bitmap *)bitmap)->opaque;
}

static unsigned char *ov_bitmap_get_buffer(void *bitmap)
{
	return ((struct bitmap *)bitmap)->pixels;
}

static size_t ov_bitmap_get_rowstride(void *bitmap)
{
	return (size_t)((struct bitmap *)bitmap)->width * 4;
}

static int ov_bitmap_get_width(void *bitmap)
{
	return ((struct bitmap *)bitmap)->width;
}

static int ov_bitmap_get_height(void *bitmap)
{
	return ((struct bitmap *)bitmap)->height;
}

static void ov_bitmap_modified(void *bitmap)
{
	lv_image_cache_drop(&((struct bitmap *)bitmap)->img);
}

static nserror ov_bitmap_render(struct bitmap *bitmap,
				struct hlcache_handle *content)
{
	(void)bitmap;
	(void)content;
	return NSERROR_NOT_IMPLEMENTED; /* no thumbnails */
}

const lv_image_dsc_t *ov_bitmap_image(void *bitmap)
{
	return &((struct bitmap *)bitmap)->img;
}

int ov_bitmap_width(void *bitmap)
{
	return ((struct bitmap *)bitmap)->width;
}

int ov_bitmap_height(void *bitmap)
{
	return ((struct bitmap *)bitmap)->height;
}

static struct gui_bitmap_table bitmap_table = {
	.create = ov_bitmap_create,
	.destroy = ov_bitmap_destroy,
	.set_opaque = ov_bitmap_set_opaque,
	.get_opaque = ov_bitmap_get_opaque,
	.get_buffer = ov_bitmap_get_buffer,
	.get_rowstride = ov_bitmap_get_rowstride,
	.get_width = ov_bitmap_get_width,
	.get_height = ov_bitmap_get_height,
	.modified = ov_bitmap_modified,
	.render = ov_bitmap_render,
};

struct gui_bitmap_table *ov_bitmap_table = &bitmap_table;
