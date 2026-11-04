/* SPDX-License-Identifier: Apache-2.0 */
#include "native_host.h"
#include "../qpk_volume.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static bool fail(struct ov_native_host *host, const char *message)
{
	snprintf(host->error, sizeof(host->error), "%s", message);
	return false;
}

static bool app_dir(const char *entry, const char *root, size_t *length)
{
	size_t n = strlen(root);
	if (strncmp(entry, root, n) || entry[n] != '/') return false;
	const char *dir = entry + n + 1, *slash = strchr(dir, '/');
	if (!slash || slash == dir || dir[0] == '.' || strcmp(slash, "/app.js")) return false;
	*length = (size_t)(slash - entry);
	return true;
}

enum ov_native_locate_result ov_native_locate(const char *entry,
	char module[OV_NATIVE_PATH_MAX], char abi[OV_NATIVE_PATH_MAX])
{
	size_t n;
	if (!entry || !(app_dir(entry, QPK_VOLUME_SD_ROOT, &n) ||
		app_dir(entry, QPK_VOLUME_INTERNAL_ROOT, &n)))
		return OV_NATIVE_NONE; /* builtin and foreign entries never load code */
	int a = snprintf(module, OV_NATIVE_PATH_MAX, "%.*s/" OV_NATIVE_MODULE_FILE, (int)n, entry);
	int b = snprintf(abi, OV_NATIVE_PATH_MAX, "%.*s/" OV_NATIVE_HOST_ABI_FILE, (int)n, entry);
	if (a <= 0 || a >= OV_NATIVE_PATH_MAX || b <= 0 || b >= OV_NATIVE_PATH_MAX)
		return OV_NATIVE_INVALID;
	struct stat info;
	if (stat(module, &info)) return errno == ENOENT || errno == ENOTDIR ?
		OV_NATIVE_NONE : OV_NATIVE_INVALID;
	return S_ISREG(info.st_mode) && info.st_size > 0 ? OV_NATIVE_FOUND : OV_NATIVE_INVALID;
}

static bool read_abi(const char *path, char abi[65])
{
	char text[66] = {0};
	FILE *file = fopen(path, "rb");
	if (!file) return false;
	size_t n = fread(text, 1, sizeof(text) - 1, file);
	bool ok = !ferror(file);
	fclose(file);
	if (n == 65 && text[64] == '\n') text[64] = 0, n = 64;
	if (!ok || n != 64 || strlen(text) != 64) return false;
	memcpy(abi, text, 65);
	return true;
}

bool ov_native_host_load(struct ov_native_host *host, const struct ov_native_loader *loader,
	const char *host_abi, const char *package, const char *name,
	const char *module_path, const char *abi_path)
{
	if (!host || !loader || !loader->open || !loader->query || !loader->close ||
		!host_abi || !package || !package[0] || !name || !module_path || !abi_path)
		return false;
	host->error[0] = 0;
	if (host->api) return true;
	char abi[65];
	if (!read_abi(abi_path, abi)) return fail(host, "原生模块缺少兼容性文件");
	if (strlen(host_abi) != 64 || strcmp(abi, host_abi))
		return fail(host, "原生模块与系统 ABI 不兼容，请安装配套版本");
	void *handle = loader->open(module_path, name);
	if (!handle) return fail(host, "原生模块加载失败：请检查内存和模块文件");
	ov_native_module_query_fn query = loader->query(handle);
	const struct ov_native_module *api = query ? query() : NULL;
	if (!api || api->abi != OV_NATIVE_MODULE_ABI || api->bytes != sizeof(*api) ||
		!api->host_abi || strcmp(api->host_abi, host_abi) || !api->attach || !api->detach) {
		(void)loader->close(handle);
		return fail(host, "原生模块接口不兼容");
	}
	if (!api->package || strcmp(api->package, package)) {
		(void)loader->close(handle);
		return fail(host, "原生模块不属于此应用");
	}
	host->handle = handle;
	host->api = api;
	return true;
}

bool ov_native_host_attach(struct ov_native_host *host, JSContext *context, JSValue system,
	lv_obj_t *root, const char *package)
{
	if (!host || !host->api || host->attached) return false;
	if (!host->api->attach(context, system, root, package))
		return fail(host, "原生模块初始化失败");
	host->attached = true;
	return true;
}

void ov_native_host_detach(struct ov_native_host *host)
{
	if (host && host->api && host->attached) {
		host->api->detach();
		host->attached = false;
	}
}

bool ov_native_host_unload(struct ov_native_host *host, const struct ov_native_loader *loader)
{
	if (!host || !host->api || host->attached || !host->api->quiesce || !loader) return false;
	if (!host->api->quiesce()) return false;
	/* A failed removal keeps the instance; attach after quiesce is allowed. */
	if (loader->close(host->handle) != 0) return false;
	host->handle = NULL;
	host->api = NULL;
	return true;
}
