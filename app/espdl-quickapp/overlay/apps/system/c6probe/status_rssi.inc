/* SPDX-License-Identifier: Apache-2.0 */
/* Included at the end of esp_hosted_rpc.c to reuse its serialized mailbox. */
#include "status_rssi.h"
int esp_hosted_rpc_wifi_rssi(int *dbm)
{
  if(!dbm)return -EINVAL;
  RpcReqWifiStaGetRssi payload=RPC__REQ__WIFI_STA_GET_RSSI__INIT;
  Rpc req={PROTOBUF_C_MESSAGE_INIT(&rpc__descriptor)};
  req.msg_type=RPC_TYPE__Req;req.msg_id=RPC_ID__Req_WifiStaGetRssi;req.uid=1;
  req.payload_case=RPC__PAYLOAD_REQ_WIFI_STA_GET_RSSI;req.req_wifi_sta_get_rssi=&payload;
  Rpc *resp=rpc_transact(&req,"WifiStaGetRssi");if(!resp)return -ETIMEDOUT;
  int ret=-EIO;
  if(resp->payload_case==RPC__PAYLOAD_RESP_WIFI_STA_GET_RSSI&&resp->resp_wifi_sta_get_rssi&&
     resp->resp_wifi_sta_get_rssi->resp==0){
    int value=resp->resp_wifi_sta_get_rssi->rssi;
    if(value>=-127&&value<0){*dbm=value;ret=0;}
  }
  rpc__free_unpacked(resp,NULL);return ret;
}
