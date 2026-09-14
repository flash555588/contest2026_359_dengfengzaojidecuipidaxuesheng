#include "native_transport.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void pair(int fd[2])
{
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0);
  assert(fcntl(fd[0], F_SETFL, O_NONBLOCK) == 0);
  assert(fcntl(fd[1], F_SETFL, O_NONBLOCK) == 0);
}

int main(void)
{
  int fd[2];
  uint32_t type;
  size_t length;
  unsigned char body[256];
  unsigned char source[200];
  memset(source, 42, sizeof(source));
  pair(fd);
  assert(esphome_tcp_send(fd[0], 300, source, sizeof(source), 100) == 0);
  assert(esphome_tcp_receive(fd[1], &type, body, sizeof(body), &length, 100) == 0);
  assert(type == 300 && length == 200 && !memcmp(source, body, 200));
  assert(esphome_tcp_receive(fd[1], &type, body, sizeof(body), &length, 10) == -ETIMEDOUT);
  close(fd[0]); close(fd[1]);
  pair(fd);
  assert(write(fd[0], "\1", 1) == 1);
  assert(esphome_tcp_receive(fd[1], &type, body, sizeof(body), &length, 100) == -ENOTSUP);
  close(fd[0]); close(fd[1]);
  pair(fd);
  assert(write(fd[0], "\0\377\377\377\377\377", 6) == 6);
  assert(esphome_tcp_receive(fd[1], &type, body, sizeof(body), &length, 100) == -EPROTO);
  close(fd[0]); close(fd[1]);
  pair(fd);
  assert(esphome_tcp_send(fd[0], 10, source, 200, 100) == 0);
  assert(esphome_tcp_receive(fd[1], &type, body, 10, &length, 100) == -EMSGSIZE);
  close(fd[0]); close(fd[1]);
  assert(esphome_tcp_connect("invalid", 6053, 100) == -EINVAL);
  return 0;
}
