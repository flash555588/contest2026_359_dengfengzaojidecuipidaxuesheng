/* SPDX-License-Identifier: Apache-2.0 */
/* Test HTTP body framing by including the transport in a separate test TU.
 * Network/TLS functions are linked normally; socketpair drives fragmented IO. */
#include "bili_http.c"
#include <assert.h>
#include <pthread.h>
struct writer { int fd; const char *body; };
static void *write_bytes(void *ctx)
{
  struct writer *w = ctx;
  for (size_t i=0;i<strlen(w->body);i++) assert(write(w->fd,w->body+i,1)==1);
  close(w->fd); return NULL;
}
static void test_body(const char *body, bool chunked, long long length,
                       const char *expected, int expected_end)
{
  struct bili_http h = {0};
  int fd[2], n; pthread_t thread; atomic_bool cancel = false;
  uint8_t out[100]; size_t used = 0;
  assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
  h.fd=fd[0]; h.cancel=&cancel; h.chunked=chunked; h.remaining=length;
  struct writer w={fd[1],body};
  assert(!pthread_create(&thread,NULL,write_bytes,&w));
  while ((n=bili_http_read(&h,out+used,sizeof(out)-used-1))>0) used+=n;
  out[used]=0; assert(!strcmp((char *)out,expected)); assert(n==expected_end);
  pthread_join(thread,NULL); close(h.fd);
}
static void test_cache(void){
  struct bili_http h={0};atomic_bool cancel=false;int fd[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,fd));
  assert(write(fd[1],"abcde",5)==5);close(fd[1]);h.fd=fd[0];h.cancel=&cancel;h.remaining=5;
  uint8_t b;assert(bili_http_read(&h,&b,1)==1&&b=='a');assert(h.read_size==5&&h.read_position==1);
  atomic_store(&cancel,true);assert(bili_http_read(&h,&b,1)==-ECANCELED);atomic_store(&cancel,false);
  for(unsigned i=1;i<5;i++)assert(bili_http_read(&h,&b,1)==1&&b=='a'+i);
  assert(!bili_http_read(&h,&b,1));close(fd[0]);
  puts("PASS cached small reads use one transport fill; cancel interrupts buffered data");
}
int main(void)
{
  test_cache();
  test_body("3\r\nabc\r\n2;ext=1\r\nde\r\n0\r\n\r\n",true,-1,"abcde",0);
  test_body("abcde",false,5,"abcde",0);
  test_body("abcde",false,-1,"abcde",0);
  test_body("abc",false,5,"abc",-EPIPE);
  test_body("-3\r\n",true,-1,"",-EPROTO);
  test_body("3\r\nab",true,-1,"ab",-EPIPE);
  puts("PASS: fragmented chunked/length/EOF HTTP, chunk extensions, truncation, malformed sizes");
  return 0;
}
