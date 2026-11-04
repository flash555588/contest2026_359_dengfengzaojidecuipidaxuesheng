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
 * Text measurement using the system's LVGL fonts.
 *
 * Glyphs come from the firmware's shared SD-card TrueType font service
 * (glass_sd_font_get, Tiny TTF with CJK coverage). When that font is not
 * available the built-in Montserrat fonts are used (Latin only).
 * Measurement and plotting use the same font choice, so layout matches
 * rendering exactly.
 */

#include <stddef.h>
#include <string.h>

#include "utils/utf8.h"
#include "netsurf/browser.h"
#include "netsurf/plot_style.h"
#include "netsurf/layout.h"

#include "openvela/openvela.h"

/* Firmware font service (desktop): UI thread only, NULL until ready. */
extern lv_font_t *glass_sd_font_get(unsigned pixels);

static const unsigned sd_sizes[] = {12, 14, 16, 20, 28, 32, 34, 40, 48, 64, 72};
#define SD_SIZE_COUNT (sizeof(sd_sizes) / sizeof(sd_sizes[0]))

/* Per-font ASCII advance cache; widths are measured once per font. */
struct width_cache {
	const lv_font_t *font;
	uint8_t width[128];
	uint8_t known[128 / 8];
};
#define WIDTH_CACHE_FONTS 16
static struct width_cache width_cache[WIDTH_CACHE_FONTS];

static unsigned font_pixels(const plot_font_style_t *fstyle)
{
	/* fstyle->size is points in 22.10 fixed point */
	int64_t px = ((int64_t)fstyle->size * browser_get_dpi() +
		      36 * PLOT_STYLE_SCALE) / (72 * PLOT_STYLE_SCALE);
	if (px < 8) {
		px = 8;
	}
	if (px > 72) {
		px = 72;
	}
	return (unsigned)px;
}

static unsigned nearest(unsigned px, const unsigned *sizes, size_t count)
{
	unsigned best = sizes[0];
	unsigned diff = px > best ? px - best : best - px;
	for (size_t i = 1; i < count; i++) {
		unsigned d = px > sizes[i] ? px - sizes[i] : sizes[i] - px;
		if (d < diff || (d == diff && sizes[i] < best)) {
			best = sizes[i];
			diff = d;
		}
	}
	return best;
}

const lv_font_t *ov_font_for(const plot_font_style_t *fstyle)
{
	unsigned px = font_pixels(fstyle);
	lv_font_t *font = glass_sd_font_get(nearest(px, sd_sizes, SD_SIZE_COUNT));

	if (font != NULL) {
		return font;
	}
	if (px >= 19) {
		return &lv_font_montserrat_24;
	}
	return &lv_font_montserrat_14;
}

bool ov_font_bold(const plot_font_style_t *fstyle)
{
	return fstyle->weight >= 600;
}

static struct width_cache *cache_for(const lv_font_t *font)
{
	static unsigned next_slot;
	for (unsigned i = 0; i < WIDTH_CACHE_FONTS; i++) {
		if (width_cache[i].font == font) {
			return &width_cache[i];
		}
	}
	struct width_cache *c = &width_cache[next_slot++ % WIDTH_CACHE_FONTS];
	memset(c, 0, sizeof(*c));
	c->font = font;
	return c;
}

static int glyph_width(const lv_font_t *font, struct width_cache *cache,
		       uint32_t cp)
{
	int w;

	if (cp < 128 && (cache->known[cp / 8] & (1u << (cp % 8)))) {
		return cache->width[cp];
	}
	w = lv_font_get_glyph_width(font, cp, 0);
	if (w == 0 && cp > ' ') {
		/* missing glyph: reserve the placeholder box LVGL draws */
		w = lv_font_get_line_height(font) / 2;
	}
	if (cp < 128 && w < 256) {
		cache->width[cp] = (uint8_t)w;
		cache->known[cp / 8] |= (uint8_t)(1u << (cp % 8));
	}
	return w;
}

static int text_width(const plot_font_style_t *fstyle, const char *string,
		      size_t length)
{
	const lv_font_t *font = ov_font_for(fstyle);
	struct width_cache *cache = cache_for(font);
	size_t offset = 0;
	int width = 0;

	while (offset < length) {
		uint32_t cp = utf8_to_ucs4(string + offset, length - offset);
		width += glyph_width(font, cache, cp);
		offset = utf8_next(string, length, offset);
	}
	if (width > 0 && ov_font_bold(fstyle)) {
		width += 1; /* synthetic bold is drawn one pixel wider */
	}
	return width;
}

static nserror ov_font_width(const plot_font_style_t *fstyle,
			     const char *string, size_t length, int *width)
{
	*width = text_width(fstyle, string, length);
	return NSERROR_OK;
}

static nserror ov_font_position(const plot_font_style_t *fstyle,
				const char *string, size_t length, int x,
				size_t *char_offset, int *actual_x)
{
	const lv_font_t *font = ov_font_for(fstyle);
	struct width_cache *cache = cache_for(font);
	size_t offset = 0;
	int cur = 0;

	while (offset < length) {
		uint32_t cp = utf8_to_ucs4(string + offset, length - offset);
		int w = glyph_width(font, cache, cp);
		if (cur + w / 2 > x) {
			break;
		}
		cur += w;
		offset = utf8_next(string, length, offset);
	}
	*char_offset = offset;
	*actual_x = cur;
	return NSERROR_OK;
}

/** Characters with East Asian line breaking (break allowed around them). */
static bool is_cjk(uint32_t cp)
{
	return (cp >= 0x2e80 && cp <= 0x9fff) ||  /* radicals .. CJK unified */
	       (cp >= 0xac00 && cp <= 0xd7af) ||  /* Hangul syllables */
	       (cp >= 0xf900 && cp <= 0xfaff) ||  /* compatibility ideographs */
	       (cp >= 0xff00 && cp <= 0xffef) ||  /* fullwidth forms */
	       (cp >= 0x20000 && cp <= 0x2fa1f);
}

/** Punctuation that must not start a line. */
static bool no_break_before(uint32_t cp)
{
	switch (cp) {
	case 0x3001: case 0x3002: case 0xff0c: case 0xff0e: case 0xff01:
	case 0xff1f: case 0xff1a: case 0xff1b: case 0xff09: case 0x300b:
	case 0x300d: case 0x300f: case 0x3011: case 0x201d: case 0x2019:
	case 0x30fc:
		return true;
	default:
		return false;
	}
}

/** Punctuation that must not end a line. */
static bool no_break_after(uint32_t cp)
{
	switch (cp) {
	case 0xff08: case 0x300a: case 0x300c: case 0x300e: case 0x3010:
	case 0x201c: case 0x2018:
		return true;
	default:
		return false;
	}
}

/*
 * Split rules (see netsurf/layout.h): return the break opportunity closest
 * to x with actual_x <= x, else the first one beyond x; length means no
 * split. A space break returns the space's offset (layout skips it); an
 * ideographic break returns the offset of the following character.
 */
static nserror ov_font_split(const plot_font_style_t *fstyle,
			     const char *string, size_t length, int x,
			     size_t *char_offset, int *actual_x)
{
	const lv_font_t *font = ov_font_for(fstyle);
	struct width_cache *cache = cache_for(font);
	size_t offset = 0, best = 0;
	int cur = 0, best_x = 0;
	uint32_t prev = 0;

	while (offset < length) {
		uint32_t cp = utf8_to_ucs4(string + offset, length - offset);
		bool opportunity = false;

		if (offset > 0) {
			if (cp == ' ') {
				opportunity = true;
			} else if ((is_cjk(cp) || is_cjk(prev)) && prev != ' ' &&
				   !no_break_before(cp) && !no_break_after(prev)) {
				opportunity = true;
			}
		}
		if (opportunity) {
			if (cur <= x) {
				best = offset;
				best_x = cur;
			} else {
				break;
			}
		}
		cur += glyph_width(font, cache, cp);
		prev = cp;
		offset = utf8_next(string, length, offset);
	}

	if (offset >= length && cur <= x) {
		/* everything fits */
		*char_offset = length;
		*actual_x = cur;
	} else if (best != 0) {
		*char_offset = best;
		*actual_x = best_x;
	} else if (offset < length) {
		/* first opportunity is beyond x */
		*char_offset = offset;
		*actual_x = cur;
	} else {
		*char_offset = length;
		*actual_x = cur;
	}
	return NSERROR_OK;
}

static struct gui_layout_table layout_table = {
	.width = ov_font_width,
	.position = ov_font_position,
	.split = ov_font_split,
};

struct gui_layout_table *ov_layout_table = &layout_table;
