/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "esp_hosted_rpc.pb-c.h"
static int scenario, freed;
static Rpc response;
static RpcRespWifiStaGetRssi payload;
static Rpc *rpc_transact(const Rpc *req, const char *label)
{
  assert(!strcmp(label,"WifiStaGetRssi"));
  assert(req->msg_type==RPC_TYPE__Req && req->msg_id==RPC_ID__Req_WifiStaGetRssi);
  assert(req->payload_case==RPC__PAYLOAD_REQ_WIFI_STA_GET_RSSI && req->req_wifi_sta_get_rssi);
  unsigned char packed[128];size_t size=rpc__pack(req,packed);
  Rpc *decoded=rpc__unpack(NULL,size,packed);assert(decoded);
  assert(decoded->payload_case==RPC__PAYLOAD_REQ_WIFI_STA_GET_RSSI);
  rpc__free_unpacked(decoded,NULL);
  if(scenario==1)return NULL;
  memset(&response,0,sizeof(response));memset(&payload,0,sizeof(payload));
  response.payload_case=RPC__PAYLOAD_RESP_WIFI_STA_GET_RSSI;
  response.resp_wifi_sta_get_rssi=&payload;payload.rssi=-48;
  if(scenario==2)response.payload_case=RPC__PAYLOAD__NOT_SET;
  if(scenario==3)response.resp_wifi_sta_get_rssi=NULL;
  if(scenario==4)payload.resp=1;
  if(scenario==5)payload.rssi=0;
  if(scenario==6)payload.rssi=-128;
  if(scenario==7)payload.rssi=-87;
  return &response;
}
static void release_response(Rpc *rpc,ProtobufCAllocator *allocator)
{assert(rpc==&response && !allocator);freed++;}
#define rpc__free_unpacked release_response
#include "status_rssi.inc"
#undef rpc__free_unpacked
int main(void)
{
  assert(esp_hosted_rpc_wifi_rssi(NULL)==-EINVAL);
  for(scenario=0;scenario<8;scenario++){
    int dbm=123, before=freed;
    int ret=esp_hosted_rpc_wifi_rssi(&dbm);
    if(scenario==0||scenario==7)assert(ret==0 && dbm==(scenario?-87:-48));
    else {assert(ret==(scenario==1?-ETIMEDOUT:-EIO));assert(dbm==123);}
    assert(freed==before+(scenario!=1));
  }
  puts("STATUS_RSSI_OK: protobuf wire request, strong/weak values, timeout, malformed/error responses, bounds, response cleanup");
}
