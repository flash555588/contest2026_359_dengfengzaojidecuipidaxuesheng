/* SPDX-License-Identifier: Apache-2.0 */
#include "native_async.h"
#include <sched.h>
#include <stdio.h>
#include <string.h>

static void *load(void *arg)
{
	struct ov_native_async *a = arg;
	bool ok = ov_native_host_load(&a->host, a->loader, a->abi, a->package,
		a->name, a->module, a->abi_path);
	pthread_mutex_lock(&a->lock);
	a->state = ok ? OV_NATIVE_READY : OV_NATIVE_FAILED;
	pthread_mutex_unlock(&a->lock);
	return NULL;
}

int ov_native_async_init(struct ov_native_async *a)
{
	memset(a, 0, sizeof(*a));
	return pthread_mutex_init(&a->lock, NULL);
}

enum ov_native_load_state ov_native_async_state(struct ov_native_async *a)
{
	pthread_mutex_lock(&a->lock);
	enum ov_native_load_state state = a->state;
	pthread_mutex_unlock(&a->lock);
	return state;
}

void ov_native_async_reset(struct ov_native_async *a)
{
	pthread_mutex_lock(&a->lock);
	if (a->state != OV_NATIVE_LOADING && !a->host.api) {
		a->state = OV_NATIVE_EMPTY;
		memset(&a->host, 0, sizeof(a->host));
	}
	pthread_mutex_unlock(&a->lock);
}

bool ov_native_async_start(struct ov_native_async *a, const struct ov_native_loader *loader,
	const char *abi)
{
	enum ov_native_load_state current = ov_native_async_state(a);
	if (current == OV_NATIVE_LOADING || current == OV_NATIVE_READY) return true;
	a->abi = abi;
	a->loader = loader;
	a->host.error[0] = 0;
	pthread_attr_t attr;
	int error = pthread_attr_init(&attr);
	if (error) return false;
	/* Relocation runs below the UI priority so the screen stays responsive. */
	error = pthread_attr_setstacksize(&attr, 64u * 1024u);
	if (!error) error = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
#ifdef __NuttX__
	int policy;
	struct sched_param priority;
	if (!error) error = pthread_getschedparam(pthread_self(), &policy, &priority);
	if (!error && priority.sched_priority > sched_get_priority_min(policy)) {
		priority.sched_priority--;
		error = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
		if (!error) error = pthread_attr_setschedpolicy(&attr, policy);
		if (!error) error = pthread_attr_setschedparam(&attr, &priority);
	}
#endif
	pthread_mutex_lock(&a->lock);
	a->state = OV_NATIVE_LOADING;
	if (!error) {
		pthread_t thread;
		error = pthread_create(&thread, &attr, load, a);
	}
	if (error) {
		snprintf(a->host.error, sizeof(a->host.error), "原生模块加载线程启动失败 (%d)", error);
		a->state = OV_NATIVE_FAILED;
	}
	pthread_mutex_unlock(&a->lock);
	pthread_attr_destroy(&attr);
	return error == 0;
}
