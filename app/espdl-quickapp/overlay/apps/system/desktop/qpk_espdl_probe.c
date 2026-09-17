/* SPDX-License-Identifier: Apache-2.0 */
#include "qpk_espdl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
int qpk_dl_probe(int argc,char **argv)
{
  if (argc==4 && !strcmp(argv[2],"test")) {
    struct qpk_dl_request r={.mode=atoi(argv[3]),.selftest=true};
    int ret=qpk_dl_start(&r);printf("espdl test request=%d\n",ret);
    if(ret<0)return 1;
    /* NuttX terminates a task's pthreads when the owning CLI task exits.
     * Keep the diagnostic owner alive until its worker releases everything. */
    struct qpk_dl_result status;
    do {usleep(100000);qpk_dl_status(&status);} while(status.busy);
    printf("espdl test completed error=%d\n",status.error);
    return status.error!=0;
  }
  if (argc==3 && !strcmp(argv[2],"stop")) qpk_dl_cancel();
  struct qpk_dl_result r;qpk_dl_status(&r);
  printf("espdl request=%lu mode=%d busy=%d stage=%d error=%d frame=%lu count=%u ms=%lu track=%d target=%d\n",
         (unsigned long)r.request,r.mode,r.busy,r.stage,r.error,(unsigned long)r.frame,
         r.count,(unsigned long)r.elapsed_ms,r.track.state,r.track.target);
  return 0;
}
