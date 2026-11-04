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
 * image/jpeg and image/png content handlers built on existing system
 * decoders instead of libjpeg/libpng:
 *
 *  - JPEG: the ESP32-P4 hardware decoder (qpk_jpeg_hw_decode), falling
 *    back to the firmware's TJpgDec (qpk_jd_*). Both are baseline only.
 *  - PNG: the same vendored LodePNG decoder the system album uses,
 *    compiled into this module because the firmware does not export it.
 *
 * Decoding is deferred to the image cache like the upstream handlers, and
 * every decode is checked against this application's image budget.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "utils/utils.h"
#include "utils/log.h"
#include "utils/messages.h"
#include "netsurf/bitmap.h"
#include "content/llcache.h"
#include "content/content.h"
#include "content/content_protected.h"
#include "content/content_factory.h"
#include "desktop/gui_internal.h"
#include "image/image_cache.h"

#include "openvela/openvela.h"

/* ---------------------------------------------- firmware JPEG decoders */

/* qpk_tjpgd.h sets LV_USE_TJPGD for its own guard; LVGL's value is 0 */
#undef LV_USE_TJPGD
#include "qpk_tjpgd.h"

int qpk_jpeg_hw_decode(const uint8_t *input, size_t size, uint16_t *output,
		       unsigned width, unsigned height, unsigned stride);

/* ------------------------------------------------------- LodePNG (PNG) */

#define LODEPNG_NO_COMPILE_ENCODER
#define LODEPNG_NO_COMPILE_DISK
#define LODEPNG_NO_COMPILE_CPP
#define LODEPNG_NO_COMPILE_ANCILLARY_CHUNKS
#define LODEPNG_NO_COMPILE_ALLOCATORS
#include "lodepng.h"

void *lodepng_malloc(size_t size)
{
	return malloc(size);
}

void *lodepng_realloc(void *ptr, size_t new_size)
{
	return realloc(ptr, new_size);
}

void lodepng_free(void *ptr)
{
	free(ptr);
}

#include "lodepng.c"

/* -------------------------------------------------------------- common */

static nserror ov_image_create(const content_handler *handler,
			       lwc_string *imime_type,
			       const struct http_parameter *params,
			       llcache_handle *llcache,
			       const char *fallback_charset, bool quirks,
			       struct content **c)
{
	struct content *image = calloc(1, sizeof(struct content));
	nserror error;

	if (image == NULL) {
		return NSERROR_NOMEM;
	}
	error = content__init(image, handler, imime_type, params, llcache,
			      fallback_charset, quirks);
	if (error != NSERROR_OK) {
		free(image);
		return error;
	}
	*c = image;
	return NSERROR_OK;
}

static bool within_budget(unsigned width, unsigned height)
{
	return width > 0 && height > 0 && width <= 8192 && height <= 8192 &&
	       (uint64_t)width * height * 4 <= ov_budget.image_bytes;
}

static void finish_convert(struct content *c, const char *title_key)
{
	char *title = messages_get_buff(title_key,
		nsurl_access_leaf(llcache_handle_get_url(c->llcache)),
		c->width, c->height);
	if (title != NULL) {
		content__set_title(c, title);
		free(title);
	}
	content_set_ready(c);
	content_set_done(c);
	content_set_status(c, "");
}

static bool fail_convert(struct content *c, const char *why)
{
	union content_msg_data msg_data;
	msg_data.errordata.errorcode = NSERROR_UNKNOWN;
	msg_data.errordata.errormsg = why;
	content_broadcast(c, CONTENT_MSG_ERROR, &msg_data);
	return false;
}

/* ---------------------------------------------------------------- JPEG */

struct jpeg_info {
	unsigned width, height;
	bool baseline;
};

/** Find the frame header; true when dimensions were found. */
static bool jpeg_probe(const uint8_t *d, size_t n, struct jpeg_info *info)
{
	size_t p = 2;

	if (n < 4 || d[0] != 0xff || d[1] != 0xd8) {
		return false;
	}
	while (p + 4 <= n) {
		if (d[p] != 0xff) {
			p++;
			continue;
		}
		uint8_t m = d[p + 1];
		if (m == 0xff) {
			p++;
			continue;
		}
		if (m == 0xd8 || m == 0x01 || (m >= 0xd0 && m <= 0xd7)) {
			p += 2;
			continue;
		}
		size_t len = ((size_t)d[p + 2] << 8) | d[p + 3];
		if (len < 2) {
			return false;
		}
		bool sof = m >= 0xc0 && m <= 0xcf && m != 0xc4 && m != 0xc8 &&
			   m != 0xcc;
		if (sof) {
			if (p + 9 > n) {
				return false;
			}
			info->height = ((unsigned)d[p + 5] << 8) | d[p + 6];
			info->width = ((unsigned)d[p + 7] << 8) | d[p + 8];
			info->baseline = (m == 0xc0 || m == 0xc1);
			return info->width > 0 && info->height > 0;
		}
		if (m == 0xda) {
			return false; /* scan before any frame header */
		}
		p += 2 + len;
	}
	return false;
}

struct tjpg_io {
	const uint8_t *data;
	size_t size, pos;
	uint16_t *out;
	unsigned width;
};

static size_t tjpg_in(JDEC *jd, uint8_t *buf, size_t n)
{
	struct tjpg_io *io = jd->device;
	size_t left = io->size - io->pos;
	if (n > left) {
		n = left;
	}
	if (buf != NULL) {
		memcpy(buf, io->data + io->pos, n);
	}
	io->pos += n;
	return n;
}

static int tjpg_out(JDEC *jd, void *bitmap, JRECT *rect)
{
	struct tjpg_io *io = jd->device;
	const uint16_t *src = bitmap;
	unsigned w = rect->right - rect->left + 1;

	for (unsigned y = rect->top; y <= rect->bottom; y++) {
		memcpy(io->out + (size_t)y * io->width + rect->left, src, w * 2);
		src += w;
	}
	return 1;
}

static bool decode_tjpgd(const uint8_t *data, size_t size, uint16_t *out,
			 unsigned width)
{
	const size_t pool_size = 32 * 1024;
	void *pool = malloc(pool_size);
	struct tjpg_io io = {data, size, 0, out, width};
	JDEC jd;
	bool ok = false;

	if (pool == NULL) {
		return false;
	}
	if (qpk_jd_prepare(&jd, tjpg_in, pool, pool_size, &io) == JDR_OK &&
	    jd.width == width) {
		ok = qpk_jd_decomp(&jd, tjpg_out, 0) == JDR_OK;
	}
	free(pool);
	return ok;
}

static struct bitmap *jpeg_cache_convert(struct content *c)
{
	const uint8_t *data;
	size_t size;
	struct jpeg_info info;
	struct bitmap *bitmap;
	uint16_t *rgb565;
	uint8_t *pixels;
	size_t rowstride;
	bool ok;

	data = content__get_source_data(c, &size);
	if (data == NULL || !jpeg_probe(data, size, &info) || !info.baseline ||
	    !within_budget(info.width, info.height)) {
		return NULL;
	}
	rgb565 = malloc((size_t)info.width * info.height * 2);
	if (rgb565 == NULL) {
		return NULL;
	}
	ok = qpk_jpeg_hw_decode(data, size, rgb565, info.width, info.height,
				info.width) == 0;
	if (!ok) {
		ok = decode_tjpgd(data, size, rgb565, info.width);
	}
	if (!ok) {
		free(rgb565);
		return NULL;
	}

	bitmap = guit->bitmap->create(info.width, info.height, BITMAP_OPAQUE);
	if (bitmap == NULL) {
		free(rgb565);
		return NULL;
	}
	pixels = guit->bitmap->get_buffer(bitmap);
	rowstride = guit->bitmap->get_rowstride(bitmap);
	for (unsigned y = 0; y < info.height; y++) {
		const uint16_t *s = rgb565 + (size_t)y * info.width;
		uint8_t *d = pixels + rowstride * y;
		for (unsigned x = 0; x < info.width; x++, d += 4) {
			uint16_t v = s[x];
			uint8_t r = (v >> 11) & 0x1f, g = (v >> 5) & 0x3f, b = v & 0x1f;
			d[0] = (b << 3) | (b >> 2);  /* BGRA (LVGL ARGB8888) */
			d[1] = (g << 2) | (g >> 4);
			d[2] = (r << 3) | (r >> 2);
			d[3] = 0xff;
		}
	}
	free(rgb565);
	guit->bitmap->modified(bitmap);
	return bitmap;
}

static bool ov_jpeg_convert(struct content *c)
{
	const uint8_t *data;
	size_t size;
	struct jpeg_info info;

	data = content__get_source_data(c, &size);
	if (data == NULL || !jpeg_probe(data, size, &info)) {
		return fail_convert(c, "JPEG 数据无效");
	}
	c->width = info.width;
	c->height = info.height;
	c->size = (size_t)c->width * c->height * 4;
	/* progressive or oversized images keep their box but show nothing */
	image_cache_add(c, NULL, jpeg_cache_convert);
	finish_convert(c, "JPEGTitle");
	return true;
}

static nserror ov_jpeg_clone(const struct content *old, struct content **newc)
{
	struct content *c = calloc(1, sizeof(struct content));
	nserror error;

	if (c == NULL) {
		return NSERROR_NOMEM;
	}
	error = content__clone(old, c);
	if (error != NSERROR_OK) {
		content_destroy(c);
		return error;
	}
	if ((old->status == CONTENT_STATUS_READY ||
	     old->status == CONTENT_STATUS_DONE) && !ov_jpeg_convert(c)) {
		content_destroy(c);
		return NSERROR_CLONE_FAILED;
	}
	*newc = c;
	return NSERROR_OK;
}

static const content_handler ov_jpeg_content_handler = {
	.create = ov_image_create,
	.data_complete = ov_jpeg_convert,
	.destroy = image_cache_destroy,
	.redraw = image_cache_redraw,
	.clone = ov_jpeg_clone,
	.get_internal = image_cache_get_internal,
	.type = image_cache_content_type,
	.is_opaque = image_cache_is_opaque,
	.no_share = false,
};

static const char *ov_jpeg_types[] = {"image/jpeg", "image/jpg", "image/pjpeg"};

CONTENT_FACTORY_REGISTER_TYPES(ov_jpeg, ov_jpeg_types, ov_jpeg_content_handler);

/* ----------------------------------------------------------------- PNG */

static struct bitmap *png_cache_convert(struct content *c)
{
	const uint8_t *data;
	size_t size;
	unsigned width = 0, height = 0, error;
	unsigned char *rgba = NULL;
	LodePNGState state;
	struct bitmap *bitmap = NULL;

	data = content__get_source_data(c, &size);
	if (data == NULL) {
		return NULL;
	}
	lodepng_state_init(&state);
	error = lodepng_inspect(&width, &height, &state, data, size);
	if (error || !within_budget(width, height)) {
		lodepng_state_cleanup(&state);
		return NULL;
	}
	state.info_raw.colortype = LCT_RGBA;
	state.info_raw.bitdepth = 8;
	state.decoder.zlibsettings.max_output_size = ov_budget.image_bytes * 2;
	error = lodepng_decode(&rgba, &width, &height, &state, data, size);
	if (!error) {
		bool opaque = true;
		bitmap = guit->bitmap->create(width, height, BITMAP_NONE);
		if (bitmap != NULL) {
			uint8_t *pixels = guit->bitmap->get_buffer(bitmap);
			size_t rowstride = guit->bitmap->get_rowstride(bitmap);
			for (unsigned y = 0; y < height; y++) {
				const uint8_t *s = rgba + (size_t)y * width * 4;
				uint8_t *d = pixels + rowstride * y;
				for (unsigned x = 0; x < width; x++, s += 4, d += 4) {
					d[0] = s[2];
					d[1] = s[1];
					d[2] = s[0];
					d[3] = s[3];
					opaque = opaque && s[3] == 0xff;
				}
			}
			guit->bitmap->set_opaque(bitmap, opaque);
			guit->bitmap->modified(bitmap);
		}
	}
	lodepng_free(rgba);
	lodepng_state_cleanup(&state);
	return bitmap;
}

static bool ov_png_convert(struct content *c)
{
	const uint8_t *data;
	size_t size;
	unsigned width = 0, height = 0;
	LodePNGState state;
	unsigned error;

	data = content__get_source_data(c, &size);
	if (data == NULL) {
		return fail_convert(c, "PNG 数据无效");
	}
	lodepng_state_init(&state);
	error = lodepng_inspect(&width, &height, &state, data, size);
	lodepng_state_cleanup(&state);
	if (error) {
		return fail_convert(c, "PNG 数据无效");
	}
	c->width = width;
	c->height = height;
	c->size = (size_t)width * height * 4;
	image_cache_add(c, NULL, png_cache_convert);
	finish_convert(c, "PNGTitle");
	return true;
}

static nserror ov_png_clone(const struct content *old, struct content **newc)
{
	struct content *c = calloc(1, sizeof(struct content));
	nserror error;

	if (c == NULL) {
		return NSERROR_NOMEM;
	}
	error = content__clone(old, c);
	if (error != NSERROR_OK) {
		content_destroy(c);
		return error;
	}
	if ((old->status == CONTENT_STATUS_READY ||
	     old->status == CONTENT_STATUS_DONE) && !ov_png_convert(c)) {
		content_destroy(c);
		return NSERROR_CLONE_FAILED;
	}
	*newc = c;
	return NSERROR_OK;
}

static const content_handler ov_png_content_handler = {
	.create = ov_image_create,
	.data_complete = ov_png_convert,
	.destroy = image_cache_destroy,
	.redraw = image_cache_redraw,
	.clone = ov_png_clone,
	.get_internal = image_cache_get_internal,
	.type = image_cache_content_type,
	.is_opaque = image_cache_is_opaque,
	.no_share = false,
};

static const char *ov_png_types[] = {"image/png", "image/x-png"};

CONTENT_FACTORY_REGISTER_TYPES(ov_png, ov_png_types, ov_png_content_handler);

nserror ov_image_register(void)
{
	nserror ret = ov_jpeg_init();
	if (ret == NSERROR_OK) {
		ret = ov_png_init();
	}
	return ret;
}
