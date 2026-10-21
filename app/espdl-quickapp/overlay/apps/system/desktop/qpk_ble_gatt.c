/* SPDX-License-Identifier: Apache-2.0 */
#include "qpk_hardware.h"
#include "glass_ble_central.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int integer(const cJSON *a,const char *key,int fallback,int low,int high)
{
  const cJSON *v=cJSON_GetObjectItemCaseSensitive(a,key);if(!v)return fallback;
  return cJSON_IsNumber(v)&&v->valuedouble>=low&&v->valuedouble<=high&&v->valuedouble==v->valueint?v->valueint:-1;
}
int qpk_ble_gatt_execute(const char *op,const cJSON *a,cJSON *out,struct qpk_hw_progress *progress)
{
  const char *names[]={"services","characteristics","descriptors","read","write","subscribe","pair","mtu"};
  int operation=-1;for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++)if(!strcmp(op,names[i]))operation=i;
  bool notification=!strcmp(op,"notification");if(operation<0&&!notification)return -ENOSYS;
  int generation=integer(a,"generation",-1,1,INT_MAX);if(generation<0)return -EINVAL;
  const cJSON *field; cJSON_ArrayForEach(field,a){
    if(!field->string)return -EINVAL;
    bool allowed=!strcmp(field->string,"generation")||
      ((operation==GLASS_GATT_CHARACTERISTICS||operation==GLASS_GATT_DESCRIPTORS)&&(!strcmp(field->string,"start")||!strcmp(field->string,"end")))||
      ((operation==GLASS_GATT_READ||operation==GLASS_GATT_WRITE||operation==GLASS_GATT_SUBSCRIBE)&&!strcmp(field->string,"handle"))||
      (operation==GLASS_GATT_WRITE&&(!strcmp(field->string,"data")||!strcmp(field->string,"response")))||
      (operation==GLASS_GATT_SUBSCRIBE&&!strcmp(field->string,"mode"));
    if(!allowed)return -EINVAL;
  }
  struct glass_gatt_request *request=calloc(1,sizeof(*request));
  struct glass_gatt_result *result=calloc(1,sizeof(*result));
  if(!request||!result){free(request);free(result);return -ENOMEM;}
  request->generation=generation;request->operation=operation;
  int ret=-EINVAL;
  if(operation==GLASS_GATT_CHARACTERISTICS||operation==GLASS_GATT_DESCRIPTORS){
    int start=integer(a,"start",-1,1,65535),end=integer(a,"end",-1,1,65535);
    if(start<0||end<start)goto done;request->start=start;request->end=end;
  }
  if(operation==GLASS_GATT_READ||operation==GLASS_GATT_WRITE||operation==GLASS_GATT_SUBSCRIBE){
    int handle=integer(a,"handle",-1,1,65535);if(handle<0)goto done;request->handle=handle;
  }
  if(operation==GLASS_GATT_WRITE){
    const cJSON *data=cJSON_GetObjectItemCaseSensitive(a,"data"),*response=cJSON_GetObjectItemCaseSensitive(a,"response");
    if(!cJSON_IsArray(data)||cJSON_GetArraySize(data)>GLASS_GATT_BYTES||(response&&!cJSON_IsBool(response)))goto done;
    cJSON_ArrayForEach(field,data){if(!cJSON_IsNumber(field)||field->valuedouble<0||field->valuedouble>255||field->valuedouble!=field->valueint)goto done;
      request->data[request->length++]=field->valueint;}
    request->response=!response||cJSON_IsTrue(response);
  }
  if(operation==GLASS_GATT_SUBSCRIBE){int mode=integer(a,"mode",-1,0,2);if(mode<0)goto done;
    request->data[0]=mode;request->length=2;request->response=true;}
  ret=notification?glass_ble_notification(generation,result):glass_ble_gatt(request,result,&progress->cancel);
  if(ret)goto done;
  cJSON_AddNumberToObject(out,"generation",result->generation);cJSON_AddNumberToObject(out,"mtu",result->mtu);
  if(operation<=GLASS_GATT_DESCRIPTORS&&!notification){
    cJSON *items=cJSON_AddArrayToObject(out,"items");cJSON_AddBoolToObject(out,"truncated",result->truncated);
    for(unsigned i=0;i<result->count&&i<GLASS_GATT_ITEMS;i++){const struct glass_gatt_item *v=result->items+i;cJSON *item=cJSON_CreateObject();
      cJSON_AddStringToObject(item,"uuid",v->uuid);cJSON_AddNumberToObject(item,"start",v->start);
      cJSON_AddNumberToObject(item,"end",v->end);cJSON_AddNumberToObject(item,"valueHandle",v->value);
      cJSON_AddNumberToObject(item,"properties",v->properties);cJSON_AddItemToArray(items,item);}
  }
  if(operation==GLASS_GATT_READ||notification){
    cJSON *data=cJSON_AddArrayToObject(out,"data");for(size_t i=0;i<result->length&&i<GLASS_GATT_BYTES;i++)cJSON_AddItemToArray(data,cJSON_CreateNumber(result->data[i]));
    cJSON_AddNumberToObject(out,"bytes",result->length);cJSON_AddNumberToObject(out,"handle",result->handle);
    if(notification){cJSON_AddBoolToObject(out,"indication",result->indication);cJSON_AddNumberToObject(out,"dropped",result->dropped);}
  }
  if(operation==GLASS_GATT_WRITE){cJSON_AddNumberToObject(out,"bytes",request->length);
    cJSON_AddStringToObject(out,"scope",request->response?"att_write_response_not_application_readback":"submitted_without_response");}
done:free(request);free(result);return ret;
}
