"""Exercise actual Hosted batch read/dispatch functions with a bounded SDIO FIFO."""
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).resolve().parents[2] / 'app/espdl-quickapp/overlay/apps/system/c6probe/esp_hosted.c'
text = source.read_text()
def function(name):
    start = text.index('static ', text.index(' * Name: '+name))
    opening = text.index('{', start)
    depth = 1
    end = opening+1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#define FAR
#define OK 0
#define LOG_ERR 3
#define syslog(...) ((void)0)
#define ESP_HOSTED_HEADER_SIZE 12
#define ESP_HOSTED_MAX_FRAME 16384
#define C6_RX_BATCH_SIZE (64u * 1024u)
#define C6_SDIO_BLOCK_SIZE 512
#define C6_CMD53_END_ADDR 0x1f800u
#define C6_RX_BYTE_MODULO 0x100000u
struct esp_hosted_header_s {
  uint8_t interface, flags;
  uint16_t len, offset, checksum, sequence;
  uint8_t reserved, type;
} __attribute__((packed));
static struct {void *sdio; uint32_t rx_bytes;} g_hosted;
static uint8_t g_dma_block[512];
static uint8_t *g_frame_buf;
static uint32_t g_rx_peak, g_rx_large_batches, pending_bytes;
static uint8_t fifo[C6_RX_BATCH_SIZE];
static unsigned reads, dispatched;
static int hosted_pending_rx_bytes(uint32_t *value) {*value=pending_bytes;return 0;}
static int read_fifo(uint32_t address, uint32_t bytes) {
  uint32_t start=C6_CMD53_END_ADDR-pending_bytes;
  assert(address>=start && address-start+bytes<=pending_bytes);
  assert(bytes<=sizeof(g_dma_block));
  memcpy(g_dma_block,fifo+address-start,bytes);reads++;return 0;
}
static int hosted_do_read_block(void *sdio,uint32_t address) {
  (void)sdio;return read_fifo(address,512);
}
static int hosted_do_read_tail(void *sdio,uint32_t address,uint32_t bytes) {
  (void)sdio;return read_fifo(address,bytes);
}
static void hosted_dispatch(uint8_t *frame,uint32_t bytes) {
  struct esp_hosted_header_s *h=(void *)frame;
  assert(bytes==h->offset+h->len && h->len==1488);
  for (unsigned i=h->offset;i<bytes;i++) assert(frame[i]==(uint8_t)dispatched);
  dispatched++;
}
'''
harness += function('hosted_dispatch_stream') + '\n' + function('hosted_read_frame')
harness += r'''
int main(void) {
  g_frame_buf=malloc(C6_RX_BATCH_SIZE);assert(g_frame_buf);
  for (unsigned count=1;count<=40;count++) {
    pending_bytes=count*1500;reads=dispatched=0;
    for (unsigned n=0;n<count;n++) {
      struct esp_hosted_header_s *h=(void *)(fifo+n*1500);
      memset(h,0,sizeof(*h));h->offset=12;h->len=1488;
      memset(fifo+n*1500+12,(uint8_t)n,1488);
    }
    g_hosted.rx_bytes=C6_RX_BYTE_MODULO-100;
    uint32_t bytes=0;assert(hosted_read_frame(&bytes)==0);
    assert(bytes==pending_bytes && !memcmp(fifo,g_frame_buf,bytes));
    assert(g_hosted.rx_bytes==(pending_bytes-100)%C6_RX_BYTE_MODULO);
    hosted_dispatch_stream(g_frame_buf,bytes);assert(dispatched==count);
    assert(reads==(pending_bytes+511)/512);
  }
  assert(g_rx_peak==60000 && g_rx_large_batches==30);
  pending_bytes=0;reads=0;uint32_t bytes=123;
  assert(hosted_read_frame(&bytes)==0 && bytes==0 && reads==0);
  pending_bytes=C6_RX_BATCH_SIZE+1;
  assert(hosted_read_frame(&bytes)==-EMSGSIZE && reads==0);
  free(g_frame_buf);
  puts("PASS actual SDIO read/dispatch: 1..40 Ethernet frames, 60KB batches, tails, counter wrap, empty FIFO and capacity guard");
}
'''
with tempfile.TemporaryDirectory(prefix='hosted-rx-test-') as folder:
    path=Path(folder)
    (path/'test.c').write_text(harness)
    subprocess.run(['cc','-std=c11','-O1','-g','-fsanitize=address,undefined',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
