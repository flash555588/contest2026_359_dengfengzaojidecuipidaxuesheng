/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_weather.h"
#include "glass_https.h"
#include "weather_root_ca.inc"
#include <stdio.h>

int glass_weather_fetch(struct glass_weather_data *out)
{
  int ret=glass_https_initialize();
  if(ret) return ret;
  struct glass_https_diagnostic diagnostic={0};
  struct glass_https_request request={
    .ca_pem=weather_root_ca,.ca_size=sizeof(weather_root_ca),
    .deadline_ms=glass_https_milliseconds()+45000,.diagnostic=&diagnostic
  };
  ret=glass_weather_http(out,glass_https_tls_ops(),&request);
  if(ret) printf("[weather] HTTPS stage=%s ret=%d tls=%d verify=%lu\n",
    diagnostic.stage?diagnostic.stage:"request",ret,diagnostic.tls_error,
    (unsigned long)diagnostic.verify_flags);
  return ret;
}
