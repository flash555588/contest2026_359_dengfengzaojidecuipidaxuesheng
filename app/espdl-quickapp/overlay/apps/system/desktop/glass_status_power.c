/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "glass_status.h"
#if defined(CONFIG_BATTERY_GAUGE)
#include <nuttx/power/battery_ioctl.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#endif
void glass_status_power_read(int *percent, bool *charging)
{
  *percent=-1;*charging=false;
#if defined(CONFIG_BATTERY_GAUGE)
  static const char *paths[]={"/dev/batgauge0","/dev/battery0","/dev/bat0"};
  for(unsigned i=0;i<sizeof(paths)/sizeof(paths[0]);i++){
    int fd=open(paths[i],O_RDONLY|O_NONBLOCK);if(fd<0)continue;
    int capacity=-1,state=BATTERY_UNKNOWN;
    if(ioctl(fd,BATIOC_CAPACITY,(unsigned long)(uintptr_t)&capacity)==0&&capacity>=0&&capacity<=100)*percent=capacity;
    if(ioctl(fd,BATIOC_STATE,(unsigned long)(uintptr_t)&state)==0)*charging=state==BATTERY_CHARGING;
    close(fd);if(*percent>=0)return;
  }
#endif
}
