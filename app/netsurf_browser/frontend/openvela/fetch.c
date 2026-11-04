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
 * Frontend fetch table: MIME types for local files and the embedded
 * resource: scheme (stylesheets, messages and built-in pages).
 */

#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "utils/errors.h"
#include "netsurf/fetch.h"

#include "openvela/openvela.h"

static const struct {
	const char *ext;
	const char *type;
} mime_types[] = {
	{"html", "text/html"}, {"htm", "text/html"}, {"xhtml", "text/html"},
	{"css", "text/css"}, {"txt", "text/plain"}, {"md", "text/plain"},
	{"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"},
	{"gif", "image/gif"}, {"bmp", "image/bmp"}, {"ico", "image/x-icon"},
	{"js", "application/javascript"}, {"json", "application/json"},
};

static const char *ov_fetch_filetype(const char *unix_path)
{
	const char *dot = strrchr(unix_path, '.');
	const char *slash = strrchr(unix_path, '/');

	if (dot != NULL && (slash == NULL || dot > slash)) {
		for (size_t i = 0; i < sizeof(mime_types) / sizeof(mime_types[0]); i++) {
			if (strcasecmp(dot + 1, mime_types[i].ext) == 0) {
				return mime_types[i].type;
			}
		}
	}
	return "text/plain";
}

static nserror ov_fetch_resource_data(const char *path, const uint8_t **data,
				      size_t *data_len)
{
	return ov_resource_data(path, data, data_len);
}

static nserror ov_fetch_release_resource_data(const uint8_t *data)
{
	(void)data; /* embedded, nothing to release */
	return NSERROR_OK;
}

static struct gui_fetch_table fetch_table = {
	.filetype = ov_fetch_filetype,
	.get_resource_data = ov_fetch_resource_data,
	.release_resource_data = ov_fetch_release_resource_data,
};

struct gui_fetch_table *ov_fetch_table = &fetch_table;
