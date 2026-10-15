/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BILI_SPOOL_H
#define BILI_SPOOL_H
#include "bili_flv.h"
#include <stdatomic.h>
#include <stdbool.h>
struct bili_spool;
struct bili_spool *bili_spool_open(const char *directory,bili_read_fn source,void *ctx,atomic_bool *cancel);
int bili_spool_read(struct bili_spool *,uint8_t *,size_t);
void bili_spool_close(struct bili_spool *);
#endif
