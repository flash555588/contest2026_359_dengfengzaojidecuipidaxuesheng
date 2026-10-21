/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "qpk_hardware.h"
#include <netutils/netlib.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <errno.h>
int qpk_network_status(cJSON *out)
{
  cJSON *interfaces=cJSON_AddArrayToObject(out,"interfaces");if(!interfaces)return -ENOMEM;
  const char *names[]={"eth0","wlan0"};
  for(unsigned i=0;i<2;i++){
    uint8_t flags=0;struct in_addr address={0},mask={0},router={0};
    cJSON *row=cJSON_CreateObject();cJSON_AddStringToObject(row,"id",names[i]);
    int ret=netlib_getifstatus(names[i],&flags);
    cJSON_AddBoolToObject(row,"registered",ret==0);
    if(!ret){
      int ip=netlib_get_ipv4addr(names[i],&address),netmask=netlib_get_ipv4netmask(names[i],&mask),gateway=netlib_get_dripv4addr(names[i],&router);
      cJSON_AddBoolToObject(row,"up",!!(flags&IFF_UP));cJSON_AddBoolToObject(row,"link",!!(flags&IFF_RUNNING));
      cJSON_AddBoolToObject(row,"addressReady",!ip&&address.s_addr!=0);
      if(!ip)cJSON_AddStringToObject(row,"address",inet_ntoa(address));
      if(!netmask)cJSON_AddStringToObject(row,"netmask",inet_ntoa(mask));
      if(!gateway)cJSON_AddStringToObject(row,"gateway",inet_ntoa(router));
    }
    cJSON_AddItemToArray(interfaces,row);
  }
  cJSON_AddStringToObject(out,"scope","interface_link_and_address_not_internet_reachability");return 0;
}
