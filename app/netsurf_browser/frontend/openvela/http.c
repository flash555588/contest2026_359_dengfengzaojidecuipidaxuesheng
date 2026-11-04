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
 * http: and https: fetcher using the firmware's webclient and the shared
 * mbedTLS transport (glass_https).
 *
 * Each started fetch runs webclient_perform() on its own joinable worker
 * thread. Workers only append header lines and body chunks to the job's
 * queue; every NetSurf call happens in poll() on the UI thread. Queued
 * bytes per fetch are bounded (backpressure), and a fetch is freed only
 * after its worker has been joined, so no worker can outlive the module.
 *
 * webclient follows redirects internally, but NetSurf must see them to
 * resolve relative URLs. The header callback stops a 3xx response at its
 * Location header and the fetch is reported as FETCH_REDIRECT.
 */

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <netutils/webclient.h>
#include "glass_https.h"

#include "utils/corestrings.h"
#include "utils/errors.h"
#include "utils/log.h"
#include "utils/messages.h"
#include "utils/nsoption.h"
#include "utils/nsurl.h"
#include "utils/ring.h"
#include "utils/useragent.h"
#include "content/fetch.h"
#include "content/fetchers.h"
#include "content/urldb.h"

#include "openvela/openvela.h"

#define OV_CA_PATH "/sdcard/netsurf/ca-bundle.pem"
#define OV_WORKER_STACK (32 * 1024)
#define OV_WIRE_BUFFER 4096
#define OV_MAX_HEADERS 24

enum chunk_kind { CHUNK_HEADER, CHUNK_DATA };

struct chunk {
	struct chunk *next;
	enum chunk_kind kind;
	size_t len;
	char data[];
};

struct job {
	struct job *r_next, *r_prev;	/* ring of jobs (UI thread) */
	struct fetch *parent;
	nsurl *url;
	bool https;
	bool only_2xx;

	/* request, immutable once the worker starts */
	char *method;
	char *body;
	size_t body_len;
	char *headers[OV_MAX_HEADERS];
	unsigned nheaders;

	/* shared with the worker, guarded by lock */
	struct chunk *head, *tail;
	size_t queued;
	size_t received;
	unsigned status;
	char *location;
	int result;
	bool finished;
	atomic_bool abort;

	/* worker-local */
	struct webclient_context *client;

	/* UI thread only */
	pthread_t thread;
	bool thread_live;	/* created and not yet joined */
	bool stopping;		/* core asked to abort */
	bool code_sent;
	bool locked;		/* inside fetch_send_callback */
};

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t drained = PTHREAD_COND_INITIALIZER;
static struct job *ring;
static unsigned live_workers;
static bool polling;

static unsigned char *ca_bundle;
static size_t ca_size;
static bool ca_tried;

/* ------------------------------------------------------------ worker */

static void push_chunk(struct job *job, enum chunk_kind kind,
		       const char *data, size_t len)
{
	struct chunk *c = malloc(sizeof(*c) + len + 1);
	if (c == NULL) {
		atomic_store(&job->abort, true);
		return;
	}
	c->next = NULL;
	c->kind = kind;
	c->len = len;
	memcpy(c->data, data, len);
	c->data[len] = '\0';

	pthread_mutex_lock(&lock);
	if (job->tail != NULL) {
		job->tail->next = c;
	} else {
		job->head = c;
	}
	job->tail = c;
	job->queued += len;
	pthread_mutex_unlock(&lock);
}

static int header_cb(const char *line, bool truncated, void *arg)
{
	struct job *job = arg;
	unsigned status = job->client->http_status;

	(void)truncated;
	if (atomic_load(&job->abort)) {
		return -ECANCELED;
	}
	pthread_mutex_lock(&lock);
	job->status = status;
	pthread_mutex_unlock(&lock);

	if (status / 100 == 3 && status != 304 && status != 305 &&
	    strncasecmp(line, "Location:", 9) == 0) {
		const char *v = line + 9;
		while (*v == ' ' || *v == '\t') {
			v++;
		}
		char *loc = strdup(v);
		if (loc != NULL) {
			size_t n = strlen(loc);
			while (n > 0 && (loc[n - 1] == '\r' || loc[n - 1] == '\n' ||
					 loc[n - 1] == ' ')) {
				loc[--n] = '\0';
			}
		}
		pthread_mutex_lock(&lock);
		free(job->location);
		job->location = loc;
		pthread_mutex_unlock(&lock);
		/* stop before webclient follows the redirect itself */
		return -ECANCELED;
	}
	push_chunk(job, CHUNK_HEADER, line, strlen(line));
	return 0;
}

static int sink_cb(char **buffer, int offset, int datend, int *buflen, void *arg)
{
	struct job *job = arg;
	size_t len = datend > offset ? (size_t)(datend - offset) : 0;

	(void)buflen;
	if (atomic_load(&job->abort)) {
		return -ECANCELED;
	}
	if (len == 0) {
		return 0;
	}

	pthread_mutex_lock(&lock);
	job->received += len;
	if (job->received > ov_budget.body_limit) {
		pthread_mutex_unlock(&lock);
		return -EFBIG;
	}
	/* backpressure: wait for the UI thread to consume queued data */
	while (job->queued > ov_budget.fetch_queue && !atomic_load(&job->abort)) {
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_nsec += 100 * 1000 * 1000;
		if (ts.tv_nsec >= 1000000000) {
			ts.tv_sec++;
			ts.tv_nsec -= 1000000000;
		}
		pthread_cond_timedwait(&drained, &lock, &ts);
	}
	pthread_mutex_unlock(&lock);

	if (atomic_load(&job->abort)) {
		return -ECANCELED;
	}
	push_chunk(job, CHUNK_DATA, *buffer + offset, len);
	return 0;
}

static void *worker(void *arg)
{
	struct job *job = arg;
	struct webclient_context client;
	struct glass_https_bounded_request tls = {0};
	char *wire = malloc(OV_WIRE_BUFFER);
	int ret = -ENOMEM;

	if (wire != NULL) {
		webclient_set_defaults(&client);
		client.protocol_version = WEBCLIENT_PROTOCOL_VERSION_HTTP_1_1;
		client.method = job->method;
		client.url = nsurl_access(job->url);
		client.headers = (const char * const *)job->headers;
		client.nheaders = job->nheaders;
		client.timeout_sec = 20;
		client.buffer = wire;
		client.buflen = OV_WIRE_BUFFER;
		client.header_callback = header_cb;
		client.header_callback_arg = job;
		client.sink_callback = sink_cb;
		client.sink_callback_arg = job;
		if (job->https) {
			tls.request.ca_pem = ca_bundle;
			tls.request.ca_size = ca_size;
			tls.request.deadline_ms = glass_https_milliseconds() + 60000;
			tls.request.idle_timeout_ms = 20000;
			tls.request.abort_flag = &job->abort;
			tls.connect_timeout_ms = 15000;
			client.tls_ops = glass_https_bounded_tls_ops();
			client.tls_ctx = &tls;
		}
		if (job->body != NULL) {
			webclient_set_static_body(&client, job->body, job->body_len);
		}
		job->client = &client;
		ret = webclient_perform(&client);
		job->client = NULL;
		free(wire);
	}

	pthread_mutex_lock(&lock);
	job->result = ret;
	job->finished = true;
	pthread_mutex_unlock(&lock);
	return NULL;
}

/* ------------------------------------------------------------ UI side */

static bool load_ca_bundle(void)
{
	FILE *f;
	long size;

	if (ca_bundle != NULL) {
		return true;
	}
	if (ca_tried) {
		return false;
	}
	ca_tried = true;
	f = fopen(OV_CA_PATH, "rb");
	if (f == NULL) {
		NSLOG(fetch, ERROR, "missing %s", OV_CA_PATH);
		return false;
	}
	if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0 &&
	    size < 4 * 1024 * 1024 && fseek(f, 0, SEEK_SET) == 0) {
		ca_bundle = malloc((size_t)size + 1);
		if (ca_bundle != NULL &&
		    fread(ca_bundle, 1, (size_t)size, f) == (size_t)size) {
			ca_bundle[size] = '\0';
			ca_size = (size_t)size + 1;
		} else {
			free(ca_bundle);
			ca_bundle = NULL;
		}
	}
	fclose(f);
	/* also performs the one-time PSA crypto initialisation */
	if (ca_bundle != NULL && glass_https_validate_ca(ca_bundle, ca_size) != 0) {
		NSLOG(fetch, ERROR, "invalid CA bundle");
		free(ca_bundle);
		ca_bundle = NULL;
	}
	return ca_bundle != NULL;
}

static bool add_header(struct job *job, const char *fmt, const char *value)
{
	if (job->nheaders >= OV_MAX_HEADERS) {
		return false;
	}
	size_t len = strlen(fmt) + strlen(value) + 1;
	char *h = malloc(len);
	if (h == NULL) {
		return false;
	}
	snprintf(h, len, fmt, value);
	job->headers[job->nheaders++] = h;
	return true;
}

static char *build_multipart(const struct fetch_multipart_data *part,
			     const char *boundary, size_t *out_len)
{
	size_t cap = 256, len = 0;
	char *buf = malloc(cap);

	if (buf == NULL) {
		return NULL;
	}
	for (; part != NULL; part = part->next) {
		const char *value = part->file ? "" : part->value;
		size_t need = strlen(boundary) + strlen(part->name) +
			      strlen(value) + 128;
		if (len + need > cap) {
			cap = (len + need) * 2;
			char *n = realloc(buf, cap);
			if (n == NULL) {
				free(buf);
				return NULL;
			}
			buf = n;
		}
		len += snprintf(buf + len, cap - len,
				"--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n%s\r\n",
				boundary, part->name, value);
	}
	if (len + strlen(boundary) + 8 > cap) {
		char *n = realloc(buf, len + strlen(boundary) + 8);
		if (n == NULL) {
			free(buf);
			return NULL;
		}
		buf = n;
	}
	len += sprintf(buf + len, "--%s--\r\n", boundary);
	*out_len = len;
	return buf;
}

static void job_free_memory(struct job *job)
{
	struct chunk *c = job->head;
	while (c != NULL) {
		struct chunk *next = c->next;
		free(c);
		c = next;
	}
	for (unsigned i = 0; i < job->nheaders; i++) {
		free(job->headers[i]);
	}
	free(job->method);
	free(job->body);
	free(job->location);
	if (job->url != NULL) {
		nsurl_unref(job->url);
	}
	free(job);
}

static bool ov_http_initialise(lwc_string *scheme)
{
	(void)scheme;
	return true;
}

static void ov_http_finalise_scheme(lwc_string *scheme)
{
	(void)scheme;
}

static bool ov_http_acceptable(const nsurl *url)
{
	(void)url;
	return true;
}

static void *ov_http_setup(struct fetch *parent, nsurl *url, bool only_2xx,
			   bool downgrade_tls, const char *post_urlenc,
			   const struct fetch_multipart_data *post_multipart,
			   const char **headers)
{
	struct job *job;
	lwc_string *scheme;
	bool match = false;
	char *cookie;

	(void)downgrade_tls;
	job = calloc(1, sizeof(*job));
	if (job == NULL) {
		return NULL;
	}
	atomic_init(&job->abort, false);
	job->parent = parent;
	job->url = nsurl_ref(url);
	job->only_2xx = only_2xx;
	scheme = nsurl_get_component(url, NSURL_SCHEME);
	if (scheme != NULL) {
		lwc_string_caseless_isequal(scheme, corestring_lwc_https, &match);
		lwc_string_unref(scheme);
	}
	job->https = match;

	/* The firmware webclient adds its default User-Agent only when the
	 * caller sends none (tools/openvela_webclient_request.py). */
	add_header(job, "User-Agent: %s", user_agent_string());
	add_header(job, "Accept-Encoding: %s", "identity");
	if (nsoption_charp(accept_language) != NULL) {
		add_header(job, "Accept-Language: %s", nsoption_charp(accept_language));
	}
	cookie = urldb_get_cookie(url, true);
	if (cookie != NULL) {
		add_header(job, "Cookie: %s", cookie);
		free(cookie);
	}
	for (int i = 0; headers != NULL && headers[i] != NULL; i++) {
		add_header(job, "%s", headers[i]);
	}

	if (post_urlenc != NULL) {
		job->method = strdup("POST");
		job->body = strdup(post_urlenc);
		job->body_len = job->body != NULL ? strlen(job->body) : 0;
		add_header(job, "Content-Type: %s", "application/x-www-form-urlencoded");
	} else if (post_multipart != NULL) {
		static const char boundary[] = "----NetSurfOpenvelaBoundary7d1f";
		job->method = strdup("POST");
		job->body = build_multipart(post_multipart, boundary, &job->body_len);
		add_header(job, "Content-Type: multipart/form-data; boundary=%s", boundary);
	} else {
		job->method = strdup("GET");
	}
	if (job->method == NULL || (post_urlenc != NULL && job->body == NULL) ||
	    (post_multipart != NULL && job->body == NULL)) {
		job_free_memory(job);
		return NULL;
	}

	RING_INSERT(ring, job);
	return job;
}

static bool ov_http_start(void *ctx)
{
	struct job *job = ctx;
	pthread_attr_t attr;
	struct sched_param param;
	int policy, err;

	if (job->https && !load_ca_bundle()) {
		/* reported from poll as a normal fetch error */
		pthread_mutex_lock(&lock);
		job->result = -ENOENT;
		job->finished = true;
		pthread_mutex_unlock(&lock);
		return true;
	}

	if (pthread_attr_init(&attr) != 0) {
		return false;
	}
	pthread_attr_setstacksize(&attr, OV_WORKER_STACK);
	if (pthread_getschedparam(pthread_self(), &policy, &param) == 0 &&
	    param.sched_priority > sched_get_priority_min(policy)) {
		/* network work stays below the UI thread */
		param.sched_priority--;
		pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
		pthread_attr_setschedpolicy(&attr, policy);
		pthread_attr_setschedparam(&attr, &param);
	}
	err = pthread_create(&job->thread, &attr, worker, job);
	pthread_attr_destroy(&attr);
	if (err != 0) {
		NSLOG(fetch, ERROR, "worker create failed %d", err);
		return false; /* the core requeues the fetch */
	}
	job->thread_live = true;
	live_workers++;
	return true;
}

static void reap(struct job *job)
{
	if (job->thread_live) {
		pthread_join(job->thread, NULL);
		job->thread_live = false;
		live_workers--;
	}
}

static void release(struct job *job)
{
	/* fetch_free() calls ov_http_free(), which frees the job */
	struct fetch *parent = job->parent;
	fetch_remove_from_queues(parent);
	fetch_free(parent);
}

static void ov_http_abort(void *ctx)
{
	struct job *job = ctx;

	/* Like the data: fetcher, only flag here; poll() joins the worker
	 * and frees the fetch, so the core never sees it vanish mid-call. */
	atomic_store(&job->abort, true);
	pthread_mutex_lock(&lock);
	pthread_cond_broadcast(&drained);
	pthread_mutex_unlock(&lock);
	job->stopping = true;
}

static void ov_http_free(void *ctx)
{
	struct job *job = ctx;

	/* only reached after the worker was joined */
	RING_REMOVE(ring, job);
	job_free_memory(job);
}

static void send_msg(struct job *job, fetch_msg *msg)
{
	job->locked = true;
	fetch_send_callback(msg, job->parent);
	job->locked = false;
}

static void send_code(struct job *job, unsigned status)
{
	if (!job->code_sent && status != 0) {
		fetch_set_http_code(job->parent, status);
		job->code_sent = true;
	}
}

static const char *error_text(int result)
{
	switch (result) {
	case -ENOENT:
		return "HTTPS 需要 /sdcard/netsurf/ca-bundle.pem";
	case -ETIMEDOUT:
		return "连接超时";
	case -EFBIG:
		return "页面过大，已超出浏览器内存预算";
	case -ECONNREFUSED:
		return "连接被拒绝";
	case -EHOSTUNREACH:
	case -ENETUNREACH:
		return "网络不可达";
	case -EINVAL:
		return "证书或 TLS 握手失败";
	default:
		return "网络请求失败";
	}
}

/** Deliver queued data; returns true when the job was released. */
static bool process(struct job *job)
{
	struct chunk *c;
	unsigned status;
	bool finished;
	int result;
	char *location;
	fetch_msg msg;

	for (;;) {
		pthread_mutex_lock(&lock);
		c = job->head;
		if (c != NULL) {
			job->head = c->next;
			if (job->head == NULL) {
				job->tail = NULL;
			}
			job->queued -= c->len;
			pthread_cond_broadcast(&drained);
		}
		status = job->status;
		finished = job->finished;
		result = job->result;
		location = job->location;
		pthread_mutex_unlock(&lock);

		if (job->stopping) {
			free(c);
			if (finished || !job->thread_live) {
				reap(job);
				release(job);
				return true;
			}
			return false;
		}
		if (c == NULL) {
			break;
		}
		send_code(job, status);
		if (c->kind == CHUNK_HEADER) {
			if (strncasecmp(c->data, "Set-Cookie:", 11) == 0) {
				const char *v = c->data + 11;
				while (*v == ' ') {
					v++;
				}
				fetch_set_cookie(job->parent, v);
			}
			msg.type = FETCH_HEADER;
		} else {
			if (job->only_2xx && status / 100 != 2) {
				free(c);
				atomic_store(&job->abort, true);
				msg.type = FETCH_ERROR;
				msg.data.error = messages_get("Not2xx");
				send_msg(job, &msg);
				job->stopping = true;
				continue;
			}
			msg.type = FETCH_DATA;
		}
		msg.data.header_or_data.buf = (const uint8_t *)c->data;
		msg.data.header_or_data.len = c->len;
		send_msg(job, &msg);
		free(c);
	}

	if (!finished) {
		return false;
	}
	reap(job);
	send_code(job, status);
	if (location != NULL) {
		msg.type = FETCH_REDIRECT;
		msg.data.redirect = location;
	} else if (status == 304) {
		msg.type = FETCH_NOTMODIFIED;
	} else if (result == 0 && status != 0) {
		if (job->only_2xx && status / 100 != 2) {
			msg.type = FETCH_ERROR;
			msg.data.error = messages_get("Not2xx");
		} else {
			msg.type = FETCH_FINISHED;
		}
	} else {
		msg.type = FETCH_ERROR;
		msg.data.error = error_text(result);
	}
	send_msg(job, &msg);
	release(job);
	return true;
}

static void ov_http_poll(lwc_string *scheme)
{
	struct job *snapshot[32];
	unsigned count = 0;
	struct job *job;

	(void)scheme;
	if (polling || ring == NULL) {
		return;
	}
	/* While polling, aborts are deferred and only the job being processed
	 * can be released, so a snapshot of the ring stays valid. Jobs added
	 * by callbacks are handled on the next poll. */
	job = ring;
	do {
		snapshot[count++] = job;
		job = job->r_next;
	} while (job != ring && count < sizeof(snapshot) / sizeof(snapshot[0]));

	polling = true;
	for (unsigned i = 0; i < count; i++) {
		process(snapshot[i]);
	}
	polling = false;
}

nserror ov_http_register(void)
{
	static const struct fetcher_operation_table ops = {
		.initialise = ov_http_initialise,
		.acceptable = ov_http_acceptable,
		.setup = ov_http_setup,
		.start = ov_http_start,
		.abort = ov_http_abort,
		.free = ov_http_free,
		.poll = ov_http_poll,
		.finalise = ov_http_finalise_scheme,
	};
	nserror ret;

	ret = fetcher_add(lwc_string_ref(corestring_lwc_http), &ops);
	if (ret == NSERROR_OK) {
		ret = fetcher_add(lwc_string_ref(corestring_lwc_https), &ops);
	}
	return ret;
}

void ov_http_abort_all(void)
{
	struct job *job = ring;
	if (job == NULL) {
		return;
	}
	do {
		atomic_store(&job->abort, true);
		job = job->r_next;
	} while (job != ring);
	pthread_mutex_lock(&lock);
	pthread_cond_broadcast(&drained);
	pthread_mutex_unlock(&lock);
}

bool ov_http_idle(void)
{
	struct job *job = ring;
	if (job != NULL) {
		do {
			bool finished;
			pthread_mutex_lock(&lock);
			finished = job->finished;
			pthread_mutex_unlock(&lock);
			if (finished) {
				reap(job);
			}
			job = job->r_next;
		} while (job != ring);
	}
	return live_workers == 0;
}

bool ov_http_empty(void)
{
	return ring == NULL;
}

void ov_http_finalise(void)
{
	/* netsurf_exit() released every fetch; drop anything left over */
	while (ring != NULL) {
		struct job *job = ring;
		RING_REMOVE(ring, job);
		reap(job);
		job_free_memory(job);
	}
	free(ca_bundle);
	ca_bundle = NULL;
	ca_size = 0;
	ca_tried = false;
}
