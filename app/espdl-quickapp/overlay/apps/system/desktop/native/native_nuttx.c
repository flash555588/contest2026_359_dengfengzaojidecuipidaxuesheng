/* SPDX-License-Identifier: Apache-2.0 */
/* Generic native module loader for installed apps. Any app directory that
 * contains native/module.elf gets its module loaded on launch; nothing here
 * knows about a particular application. */
#include "native_host.h"
#include "native_async.h"
#include "native_host_abi.h"
#include "native_cache.h"
#include <esp_cache.h>
#include <nuttx/lib/elf.h>
#include <nuttx/arch.h>
#include <nuttx/module.h>
#include <nuttx/symtab.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#if !defined(CONFIG_FS_PROCFS) || defined(CONFIG_FS_PROCFS_EXCLUDE_MODULE)
#error "Native modules require module textsize metadata"
#endif
#ifndef OV_NATIVE_SLOTS
#define OV_NATIVE_SLOTS 8
#endif

extern const struct symtab_s ov_native_exports[];
extern const int ov_native_nexports;

/* A slot is bound to one module file and package while used. */
struct slot { struct ov_native_async load; bool used; };
static struct slot *slots;
static struct {
	struct slot *slot;
	JSContext *context;
	JSValue system;
	lv_obj_t *root;
	lv_timer_t *timer;
	char package[OV_NATIVE_PACKAGE_MAX];
} page;
/* libelf's symbol table is process-global; serialize every insmod. */
static pthread_mutex_t insmod_lock = PTHREAD_MUTEX_INITIALIZER;

static bool sync_cache(void *ctx, uintptr_t addr, size_t bytes, bool instruction)
{
	(void)ctx;
	int flags = instruction ? ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_TYPE_INST :
		ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA;
	int err = esp_cache_msync((void *)addr, bytes, flags);
	if (err != ESP_OK)
		fprintf(stderr, "[native] cache sync %s at %p+%lu err=%d\n",
			instruction ? "inst" : "data", (void *)addr, (unsigned long)bytes, err);
	return err == ESP_OK;
}

static void *open_module(const char *path, const char *name)
{
	const struct symtab_s *previous;
	int previous_count;
	pthread_mutex_lock(&insmod_lock);
	libelf_getsymtab(&previous, &previous_count);
	libelf_setsymtab(ov_native_exports, ov_native_nexports);
	fprintf(stderr, "[native] loading %s as %s\n", path, name);
	void *handle = insmod(path, name);
	int error = errno;
	libelf_setsymtab(previous, previous_count);
	pthread_mutex_unlock(&insmod_lock);
	if (!handle) {
		fprintf(stderr, "[native] insmod failed errno=%d\n", error);
		return NULL;
	}
	struct module_s *module = handle;
	fprintf(stderr, "[native] %s text=%p bytes=%lu\n", name, module->textalloc,
		(unsigned long)module->textsize);
#ifdef CONFIG_OV_NATIVE_TEXT_POOL
	if (!up_textheap_heapmember(module->textalloc)) {
		(void)rmmod(handle);
		fprintf(stderr, "[native] executable pool mismatch\n");
		return NULL;
	}
#endif
	/* The executable alias lies above physical PSRAM, where the SDK's
	 * esp_cache_get_line_size_by_addr() reports 0. Synchronise in 4 KiB
	 * units instead: a multiple of every cache line size. NuttX sizes text in
	 * 4-byte units; the executable heap hands out whole 64 KiB pages to one
	 * module, so the rounded tail belongs to this allocation. */
	const size_t line = 4096;
	size_t bytes = (module->textsize + line - 1) & ~(line - 1);
#ifdef CONFIG_OV_NATIVE_TEXT_POOL
	if (bytes && !up_textheap_heapmember((uint8_t *)module->textalloc + bytes - 1)) {
		(void)rmmod(handle);
		fprintf(stderr, "[native] text tail outside executable allocation\n");
		return NULL;
	}
#endif
	if (!ov_native_cache_prepare((uintptr_t)module->textalloc, bytes,
		line, sync_cache, NULL)) {
		(void)rmmod(handle);
		fprintf(stderr, "[native] cache preparation failed\n");
		return NULL;
	}
	return handle;
}
static ov_native_module_query_fn query_module(void *handle)
{
	return (ov_native_module_query_fn)modsym(handle, OV_NATIVE_MODULE_SYMBOL);
}
static int close_module(void *handle) { return rmmod(handle); }
static const struct ov_native_loader loader = {open_module, query_module, close_module};

static bool ensure_slots(void)
{
	if (slots) return true;
	struct slot *created = calloc(OV_NATIVE_SLOTS, sizeof(*created));
	if (!created) return false;
	for (unsigned i = 0; i < OV_NATIVE_SLOTS; i++) {
		if (ov_native_async_init(&created[i].load)) {
			while (i--) pthread_mutex_destroy(&created[i].load.lock);
			free(created);
			return false;
		}
		snprintf(created[i].load.name, sizeof(created[i].load.name), "ovnative%u", i);
	}
	slots = created;
	return true;
}

/* Find the slot bound to this module file, or bind a free one. */
static struct slot *bind_slot(const char *package, const char *module,
	const char *abi_path, const char **error)
{
	struct slot *spare = NULL;
	for (unsigned i = 0; i < OV_NATIVE_SLOTS; i++) {
		struct slot *s = &slots[i];
		if (s->used && !strcmp(s->load.module, module)) {
			if (strcmp(s->load.package, package)) {
				*error = "原生模块已由另一个应用加载，请重启后再试";
				return NULL;
			}
			return s;
		}
		if (!spare && (!s->used || ov_native_async_state(&s->load) == OV_NATIVE_FAILED))
			spare = s; /* failed slots hold no code and may be rebound */
	}
	if (!spare) { *error = "常驻原生模块过多，请关闭其他原生应用或重启"; return NULL; }
	ov_native_async_reset(&spare->load);
	snprintf(spare->load.package, sizeof(spare->load.package), "%s", package);
	snprintf(spare->load.module, sizeof(spare->load.module), "%s", module);
	snprintf(spare->load.abi_path, sizeof(spare->load.abi_path), "%s", abi_path);
	spare->used = true;
	return spare;
}

static void publish_error(JSContext *context, JSValue system, const char *message)
{
	JS_SetPropertyStr(context, system, "nativeLoading", JS_FALSE);
	JS_SetPropertyStr(context, system, "nativeError", JS_NewString(context, message));
}

static void poll_load(lv_timer_t *timer)
{
	(void)timer;
	struct slot *s = page.slot;
	enum ov_native_load_state state = ov_native_async_state(&s->load);
	if (state == OV_NATIVE_LOADING) return;
	lv_timer_delete(page.timer);
	page.timer = NULL;
	/* Only the LVGL owner touches context, system, root or LVGL objects. */
	if (state != OV_NATIVE_READY) {
		publish_error(page.context, page.system,
			s->load.host.error[0] ? s->load.host.error : "原生模块加载失败");
		return;
	}
	if (!ov_native_host_attach(&s->load.host, page.context, page.system, page.root,
		page.package)) {
		publish_error(page.context, page.system, s->load.host.error);
		return;
	}
	JS_SetPropertyStr(page.context, page.system, "nativeLoading", JS_FALSE);
	fprintf(stderr, "[native] %s attached\n", s->load.name);
}

/* Unload every detached module whose quiesce reports that it has stopped. */
static void sweep(void)
{
	if (!slots) return;
	for (unsigned i = 0; i < OV_NATIVE_SLOTS; i++) {
		struct slot *s = &slots[i];
		if (!s->used || ov_native_async_state(&s->load) != OV_NATIVE_READY) continue;
		if (ov_native_host_unload(&s->load.host, &loader)) {
			fprintf(stderr, "[native] %s unloaded\n", s->load.name);
			ov_native_async_reset(&s->load);
			s->used = false;
		}
	}
}

bool ov_native_install(JSContext *context, JSValue system, lv_obj_t *root,
	const char *package, const char *entry)
{
	char module[OV_NATIVE_PATH_MAX], abi_path[OV_NATIVE_PATH_MAX];
	enum ov_native_locate_result found = ov_native_locate(entry, module, abi_path);
	if (found == OV_NATIVE_NONE) return true; /* plain Quick App */
	ov_native_stop();
	if (found == OV_NATIVE_INVALID) {
		publish_error(context, system, "原生模块文件无效");
		return false;
	}
	if (!package || !package[0] || strlen(package) >= sizeof(page.package)) {
		publish_error(context, system, "应用包名无效，无法加载原生模块");
		return false;
	}
	if (!ensure_slots()) {
		publish_error(context, system, "原生模块加载器内存不足");
		return false;
	}
	const char *error = NULL;
	struct slot *s = bind_slot(package, module, abi_path, &error);
	if (!s) { publish_error(context, system, error); return false; }
	page.slot = s;
	page.context = context;
	page.system = JS_DupValue(context, system);
	page.root = root;
	snprintf(page.package, sizeof(page.package), "%s", package);
	page.timer = lv_timer_create(poll_load, 50, NULL);
	if (!page.timer) {
		publish_error(context, system, "原生模块加载定时器创建失败");
		return false;
	}
	JS_SetPropertyStr(context, system, "nativeLoading", JS_TRUE);
	if (!ov_native_async_start(&s->load, &loader, OV_NATIVE_HOST_ABI)) {
		lv_timer_delete(page.timer);
		page.timer = NULL;
		publish_error(context, system, s->load.host.error);
		return false;
	}
	return true;
}

void ov_native_stop(void)
{
	if (page.timer) { lv_timer_delete(page.timer); page.timer = NULL; }
	/* A load in progress outlives the page, but all UI references expire now. */
	if (page.slot && ov_native_async_state(&page.slot->load) == OV_NATIVE_READY)
		ov_native_host_detach(&page.slot->load.host);
	if (page.context) JS_FreeValue(page.context, page.system);
	page.slot = NULL;
	page.context = NULL;
	page.root = NULL;
	page.package[0] = 0;
	sweep();
}
