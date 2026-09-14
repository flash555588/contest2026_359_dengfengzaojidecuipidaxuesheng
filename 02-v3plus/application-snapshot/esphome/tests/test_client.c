#include "esphome_client.h"
#include "native_transport.h"
#include "protobuf.h"
#include "protocol_ids.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static struct esphome_snapshot snapshot;
int main(void)
{
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr = {.sin_family = AF_INET,
                            .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  socklen_t addr_size = sizeof(addr);
  struct esphome_config cfg = {.address = "127.0.0.1", .port = 1};
  uint8_t bytes[256];
  uint32_t type;
  size_t size;
  struct eh_pb_writer w = {bytes, sizeof(bytes), 0, 0};
  struct timespec delay = {.tv_nsec = 10000000};
  assert(esphome_client_start(&cfg) == -EACCES);
  assert(listener >= 0);
  assert(bind(listener, (void *)&addr, sizeof(addr)) == 0);
  assert(listen(listener, 1) == 0);
  assert(getsockname(listener, (void *)&addr, &addr_size) == 0);
  cfg.port = ntohs(addr.sin_port);
  cfg.allow_plaintext = true;
  assert(esphome_client_start(&cfg) == 0);
  assert(esphome_client_start(&cfg) == -EBUSY);
  int peer = accept(listener, NULL, NULL);
  assert(peer >= 0);
  assert(esphome_tcp_receive(peer, &type, bytes, sizeof(bytes), &size, 1000) == 0);
  assert(type == EH_HelloRequest);
  assert(esphome_tcp_receive(peer, &type, bytes, sizeof(bytes), &size, 1000) == 0);
  assert(type == EH_AuthenticationRequest);
  eh_pb_put_uint(&w, EH_HelloResponse_api_version_major, 1);
  assert(esphome_tcp_send(peer, EH_HelloResponse, bytes, w.size, 1000) == 0);
  for (int i = 0; i < 2; ++i)
    assert(esphome_tcp_receive(peer, &type, bytes, sizeof(bytes), &size, 1000) == 0);
  assert(esphome_tcp_send(peer, EH_DeviceInfoResponse, NULL, 0, 1000) == 0);
  assert(esphome_tcp_send(peer, EH_ListEntitiesDoneResponse, NULL, 0, 1000) == 0);
  assert(esphome_tcp_receive(peer, &type, bytes, sizeof(bytes), &size, 1000) == 0);
  assert(type == EH_SubscribeStatesRequest);
  for (int i = 0; i < 100; ++i)
    {
      esphome_client_snapshot(&snapshot);
      if (snapshot.status == ESPHOME_READY) break;
      nanosleep(&delay, NULL);
    }
  assert(snapshot.status == ESPHOME_READY);
  esphome_client_stop();
  for (int i = 0; i < 100; ++i)
    {
      esphome_client_snapshot(&snapshot);
      if (!snapshot.running) break;
      nanosleep(&delay, NULL);
    }
  assert(!snapshot.running && snapshot.status == ESPHOME_IDLE);
  close(peer);
  close(listener);
  puts("client lifecycle tests passed");
  return 0;
}
