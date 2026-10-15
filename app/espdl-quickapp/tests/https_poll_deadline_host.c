/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <errno.h>
#include <poll.h>
struct glass_https_request { int64_t deadline_ms; atomic_bool *abort_flag; };
struct webclient_tls_connection {struct {int fd;} net; int ssl;struct glass_https_request request;};
static int64_t now;
static int calls,mode,last_timeout;
static int64_t glass_https_milliseconds(void){return now;}
static int mbedtls_ssl_get_bytes_avail(void *ssl){(void)ssl;return 0;}
static int fake_poll(struct pollfd *fds,nfds_t count,int timeout)
{
  (void)fds;(void)count;calls++;last_timeout=timeout;
  if(mode==1){now+=5;errno=EINTR;return -1;}
  now+=timeout;return 0;
}
#define poll fake_poll
#include "https_poll_subset.inc"
int main(void)
{
  struct webclient_tls_connection c={.net.fd=1,.request.deadline_ms=50};
  assert(glass_https_poll(&c,POLLIN,500)==-ETIMEDOUT&&now==50&&last_timeout==50&&calls==1);
  puts("HTTPS_REQUEST_DEADLINE_CAP_OK");
  now=0;calls=0;mode=1;c.request.deadline_ms=1000;
  assert(glass_https_poll(&c,POLLIN,20)==0&&now==20&&calls==4&&last_timeout==5);
  puts("HTTPS_EINTR_DOES_NOT_RESTART_TIMEOUT_OK");
  now=0;calls=0;mode=0;c.request.deadline_ms=1000;
  assert(glass_https_poll(&c,POLLIN,0)==0&&now==0&&last_timeout==0);
  puts("HTTPS_NONBLOCKING_POLL_OK");
  atomic_bool cancelled=true;c.request.abort_flag=&cancelled;calls=0;
  assert(glass_https_poll(&c,POLLIN,20)==-ECANCELED&&calls==0);
  puts("HTTPS_CANCELLATION_OK");return 0;
}
