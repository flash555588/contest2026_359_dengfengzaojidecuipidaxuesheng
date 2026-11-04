/* SPDX-License-Identifier: Apache-2.0 */
/* Native module ABI. Shared by the firmware loader and every native module.
 *
 * A native module is an ELF relocatable installed inside an application
 * directory as native/module.elf, next to native/host.abi (the 64-hex value
 * of OV_NATIVE_HOST_ABI it was built against). The firmware loads it from
 * SD or internal storage on demand; nothing is linked into Flash.
 *
 * The firmware imposes no memory, size or feature policy on a module. Each
 * module budgets its own heap, threads and caches. Loading only checks that
 * the module was built for this firmware and belongs to the launching app;
 * it is compatibility checking, not a sandbox for arbitrary native code.
 */
#ifndef OV_NATIVE_MODULE_H
#define OV_NATIVE_MODULE_H
#include <stdbool.h>
#include <stdint.h>
#include <lvgl/lvgl.h>
#include <quickjs.h>

#define OV_NATIVE_MODULE_ABI 2u
#define OV_NATIVE_MODULE_SYMBOL "ov_native_module_query"
#define OV_NATIVE_MODULE_FILE "native/module.elf"
#define OV_NATIVE_HOST_ABI_FILE "native/host.abi"

struct ov_native_module {
	uint32_t abi;           /* OV_NATIVE_MODULE_ABI */
	uint32_t bytes;         /* sizeof(struct ov_native_module) */
	const char *host_abi;   /* OV_NATIVE_HOST_ABI used at build time */
	const char *package;    /* must equal the launching app's manifest package */
	/* UI thread. root is the app's content area; system is the app's JS
	 * `system` object and may be ignored. Return false on failure. */
	bool (*attach)(JSContext *context, JSValue system, lv_obj_t *root,
		const char *package);
	/* UI thread. Release every LVGL/JS reference obtained in attach. */
	void (*detach)(void);
	/* Optional, UI thread, called only while detached. Return true only after
	 * every module-owned thread, timer and callback has ended; the firmware
	 * then unloads the code and frees its memory. A later launch reloads the
	 * file. NULL, or returning false, keeps the module resident; attach may
	 * then be called again on the same instance. */
	bool (*quiesce)(void);
};
typedef const struct ov_native_module *(*ov_native_module_query_fn)(void);
/* Each module defines this symbol (OV_NATIVE_MODULE_SYMBOL). */
const struct ov_native_module *ov_native_module_query(void);
#endif
