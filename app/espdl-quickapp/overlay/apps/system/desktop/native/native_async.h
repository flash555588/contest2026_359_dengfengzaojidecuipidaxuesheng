/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OV_NATIVE_ASYNC_H
#define OV_NATIVE_ASYNC_H
#include "native_host.h"
#include <pthread.h>
enum ov_native_load_state { OV_NATIVE_EMPTY, OV_NATIVE_LOADING, OV_NATIVE_READY, OV_NATIVE_FAILED };
/* One per resident module slot. Never destroy while its worker or module
 * is resident. Request fields are written by the UI before start and read
 * by the worker; host is read by the UI only after LOADING has ended. */
struct ov_native_async {
	pthread_mutex_t lock;
	enum ov_native_load_state state;
	struct ov_native_host host;
	const struct ov_native_loader *loader;
	const char *abi;
	char name[16];
	char package[OV_NATIVE_PACKAGE_MAX];
	char module[OV_NATIVE_PATH_MAX];
	char abi_path[OV_NATIVE_PATH_MAX];
};
int ov_native_async_init(struct ov_native_async *);
/* Starts only from EMPTY or FAILED. LOADING and READY return true unchanged. */
bool ov_native_async_start(struct ov_native_async *, const struct ov_native_loader *,
	const char *abi);
/* After a successful unload: back to EMPTY so the slot may be reused. */
void ov_native_async_reset(struct ov_native_async *);
enum ov_native_load_state ov_native_async_state(struct ov_native_async *);
#endif
