/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BILI_HTTP_H
#define BILI_HTTP_H
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
struct bili_http;
struct bili_http *bili_http_open(const char *url, const char *cookie,
                                atomic_bool *cancel, int *error);
struct bili_http *bili_http_open_media(const char *url, const char *cookie,
                                      unsigned long room_id,
                                      atomic_bool *cancel, int *error);
int bili_http_read(struct bili_http *h, uint8_t *dst, size_t n);
void bili_http_close(struct bili_http *h);
int bili_http_json(const char *url, const char *cookie, atomic_bool *cancel,
                   char **json);
struct bili_http *bili_http_open_site(const char *url,const char *cookie,const char *referer,atomic_bool *cancel,int *error);
int bili_http_json_site(const char *url,const char *cookie,const char *referer,atomic_bool *cancel,char **json);
#endif
