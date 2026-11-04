/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OV_NATIVE_HOST_H
#define OV_NATIVE_HOST_H
#include "native_module.h"
#include <stddef.h>

#define OV_NATIVE_PATH_MAX 192
#define OV_NATIVE_PACKAGE_MAX 64

struct ov_native_loader {
	void *(*open)(const char *path, const char *name);
	ov_native_module_query_fn (*query)(void *handle);
	int (*close)(void *handle); /* 0 once the code has been removed */
};
struct ov_native_host {
	void *handle;
	const struct ov_native_module *api;
	bool attached;
	char error[160];
};
enum ov_native_locate_result { OV_NATIVE_NONE, OV_NATIVE_FOUND, OV_NATIVE_INVALID };

/* Pure path policy. entry must be <volume root>/<app dir>/app.js. An app
 * without native/module.elf is OV_NATIVE_NONE and runs as a plain Quick App. */
enum ov_native_locate_result ov_native_locate(const char *entry,
	char module[OV_NATIVE_PATH_MAX], char abi[OV_NATIVE_PATH_MAX]);

/* Worker thread; makes no JS or LVGL calls. */
bool ov_native_host_load(struct ov_native_host *, const struct ov_native_loader *,
	const char *host_abi, const char *package, const char *name,
	const char *module_path, const char *abi_path);
/* UI thread only. */
bool ov_native_host_attach(struct ov_native_host *, JSContext *, JSValue,
	lv_obj_t *, const char *package);
void ov_native_host_detach(struct ov_native_host *);
/* UI thread, detached only. True once the module code is gone. */
bool ov_native_host_unload(struct ov_native_host *, const struct ov_native_loader *);

/* Quick App runtime hooks (firmware only). */
bool ov_native_install(JSContext *, JSValue system, lv_obj_t *root,
	const char *package, const char *entry);
void ov_native_stop(void);
#endif
