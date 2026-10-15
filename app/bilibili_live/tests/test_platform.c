/* SPDX-License-Identifier: Apache-2.0 */
#include "live_platform.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
  struct bili_streams *s=calloc(1,sizeof(*s));assert(s);
  const char *cc="{\"data\":[{\"status\":1,\"ccid\":123,\"stream_list\":{\"original\":{\"vbr\":9000,\"streamname\":\"source\",\"CDN_FMT\":{\"ali\":\"q=source\"}},\"medium\":{\"vbr\":1000,\"streamname\":\"medium\",\"CDN_FMT\":{\"ali\":\"q=medium\"}},\"low\":{\"vbr\":600,\"streamname\":\"lowtc4\",\"CDN_FMT\":{\"ali\":\"q=low\"}},\"unavailable\":{\"vbr\":100,\"streamname\":\"unavailable\",\"CDN_FMT\":{}}}}]}";
  assert(live_parse_cc(cc,s)==0 && s->qn==600 && s->count==1);
  assert(!strcmp(s->urls[0],"https://alirtspull.cc.netease.com/live/lowtc4.flv?q=low"));
  assert(!strcmp(s->referer,"https://cc.163.com/123"));
  assert(live_parse_cc("{\"data\":[{\"status\":0}]}",s)==-ENODATA);
  assert(live_parse_cc("{\"data\":[{\"status\":1,\"stream_list\":{\"resolution\":{}}}]}",s)==-ENOTSUP);
  assert(live_parse_huya("{\"status\":200,\"data\":{\"liveStatus\":\"OFF\"}}","123",s)==-ENODATA);
  assert(live_parse_huya("{\"status\":200,\"data\":{\"liveStatus\":\"ON\",\"stream\":{}}}","123",s)==-ENOTSUP);
  assert(live_parse_huya("null","123",s)==-EPROTO);
  const char *huya="{\"status\":200,\"data\":{\"liveStatus\":\"ON\",\"stream\":{\"flv\":{\"rateArray\":[{\"iBitRate\":4000},{\"iBitRate\":500},{\"iBitRate\":100,\"iCodecType\":1}]},\"baseSteamInfoList\":[{\"sStreamName\":\"sample\",\"sFlvUrl\":\"http://example.invalid\",\"sFlvAntiCode\":\"fm=cHJlZml4XyQwXyQxXyQyXyQz&ctype=huya_webh5&t=100&wsTime=ffffffff&fs=bgct\"}]}}}";
  assert(live_parse_huya(huya,"123",s)==0 && s->qn==500 && s->count==1);
  assert(strstr(s->urls[0],"https://example.invalid/sample.flv?wsSecret=") && strstr(s->urls[0],"&ratio=500"));
  assert(!strstr(s->urls[0],"fm="));
  atomic_bool cancel=true;
  assert(live_resolve(LIVE_CC,"123","",&cancel,s)==-ECANCELED);
  assert(live_resolve(99,"123","",&cancel,s)==-EINVAL);
  free(s);puts("PASS platform quality selection, Huya signing, offline/malformed/unsupported responses, cancellation");return 0;
}
