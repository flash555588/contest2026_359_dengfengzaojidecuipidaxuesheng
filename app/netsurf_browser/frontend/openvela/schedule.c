/*
 * Copyright 2008 Vincent Sanders <vince@simtec.co.uk>
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
 * Callback scheduler, adapted from the framebuffer frontend.
 * Driven by an LVGL timer on the UI thread.
 */

#include <stdlib.h>
#include <sys/time.h>

#include "utils/sys_time.h"
#include "utils/log.h"

#include "openvela/openvela.h"

struct nscallback {
	struct nscallback *next;
	struct timeval tv;
	void (*callback)(void *p);
	void *p;
};

static struct nscallback *schedule_list = NULL;

static void schedule_remove(void (*callback)(void *p), void *p)
{
	struct nscallback *cur = schedule_list, *prev = NULL;

	while (cur != NULL) {
		if (cur->callback == callback && cur->p == p) {
			struct nscallback *unlink = cur;
			cur = cur->next;
			if (prev == NULL) {
				schedule_list = cur;
			} else {
				prev->next = cur;
			}
			free(unlink);
		} else {
			prev = cur;
			cur = cur->next;
		}
	}
}

nserror ov_schedule(int tival, void (*callback)(void *p), void *p)
{
	struct nscallback *nscb;
	struct timeval tv;

	schedule_remove(callback, p);
	if (tival < 0) {
		return NSERROR_OK;
	}

	nscb = calloc(1, sizeof(*nscb));
	if (nscb == NULL) {
		return NSERROR_NOMEM;
	}

	tv.tv_sec = tival / 1000;
	tv.tv_usec = (tival % 1000) * 1000;
	gettimeofday(&nscb->tv, NULL);
	timeradd(&nscb->tv, &tv, &nscb->tv);

	nscb->callback = callback;
	nscb->p = p;
	nscb->next = schedule_list;
	schedule_list = nscb;

	return NSERROR_OK;
}

int ov_schedule_run(void)
{
	struct timeval tv, nexttime, rettime;
	struct nscallback *cur, *prev;

	if (schedule_list == NULL) {
		return -1;
	}

	cur = schedule_list;
	prev = NULL;
	nexttime = cur->tv;
	gettimeofday(&tv, NULL);

	while (cur != NULL) {
		if (timercmp(&tv, &cur->tv, >)) {
			struct nscallback *unlink = cur;

			if (prev == NULL) {
				schedule_list = unlink->next;
			} else {
				prev->next = unlink->next;
			}
			unlink->callback(unlink->p);
			free(unlink);

			/* the callback may have modified the list */
			if (schedule_list == NULL) {
				return -1;
			}
			cur = schedule_list;
			prev = NULL;
			nexttime = cur->tv;
		} else {
			if (timercmp(&nexttime, &cur->tv, >)) {
				nexttime = cur->tv;
			}
			prev = cur;
			cur = cur->next;
		}
	}

	timersub(&nexttime, &tv, &rettime);
	return (rettime.tv_sec * 1000) + (rettime.tv_usec / 1000);
}

void ov_schedule_clear(void)
{
	while (schedule_list != NULL) {
		struct nscallback *next = schedule_list->next;
		free(schedule_list);
		schedule_list = next;
	}
}
