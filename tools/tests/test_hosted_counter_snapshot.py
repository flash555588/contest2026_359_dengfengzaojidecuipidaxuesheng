"""Inject byte-boundary carries into actual SDIO counter read functions."""
from pathlib import Path
import re
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[2]
source = repo / 'app/espdl-quickapp/overlay/apps/system/c6probe/esp_hosted.c'
text = source.read_text()


def function(name):
    start = re.search(r'^static int ' + name + r'\(', text, re.M).start()
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#define FAR
#define OK 0
#define C6_SDIO_FUNCTION 1
#define C6_COUNTER_READ_TRIES 4
#define C6_REG_PACKET_LEN 0x60
#define C6_REG_TOKEN_RDATA 0x44
#define C6_RX_LEN_MASK 0xfffff
#define C6_RX_BYTE_MODULO 0x100000
#define C6_TX_BUFFER_MASK 0xfff
#define C6_TX_BUFFER_MODULO 0x1000
#define C6_TOKEN_SHIFT 16
struct sdio_dev_s { int unused; };
static struct { struct sdio_dev_s *sdio; uint32_t rx_bytes, tx_buffers; } g_hosted;
static uint32_t g_counter_retries, g_counter_exhausted;
static uint32_t reg, next_value, reg_address, reads, mutate_on, fail_on;
static bool churn;
static int sdio_io_rw_direct(struct sdio_dev_s *s, bool write, uint8_t func,
                             uint32_t address, uint8_t data, uint8_t *out) {
  (void)s; (void)data;
  assert(!write && func==1 && address>=reg_address && address<reg_address+4);
  reads++;
  if(reads==fail_on) return -ETIMEDOUT;
  *out=(uint8_t)(reg >> ((address-reg_address)*8));
  if(reads==mutate_on) reg=next_value;
  if(churn && reads%7==4) reg+=0x100;
  return 0;
}
static void setup(uint32_t address,uint32_t value,unsigned at,uint32_t next) {
  reg_address=address; reg=value; mutate_on=at; next_value=next;
  reads=fail_on=g_counter_retries=g_counter_exhausted=0; churn=false;
}
'''
for name in ('hosted_sdio_read_reg32', 'hosted_sdio_read_counter32',
             'hosted_pending_rx_bytes', 'hosted_tx_buffers_free'):
    code += '\n' + function(name)
code += r'''
int main(void) {
  uint32_t value;
  /* Negative controls execute the actual former byte-read primitive. */
  setup(0x60,0xff,1,0x100);
  assert(!hosted_sdio_read_reg32(NULL,0x60,&value));
  assert(value==0x1ff && reg==0x100);
  setup(0x44,0x00ff0000,3,0x01000000);
  assert(!hosted_sdio_read_reg32(NULL,0x44,&value));
  assert(value==0x01ff0000 && reg==0x01000000);
  puts("REPRODUCED old reads: RX 0xff->0x100 becomes 0x1ff; TX credit 255->256 becomes 511");
  const uint32_t rx_before[]={0xff,0xffff,0xfffff};
  const uint32_t rx_after[]={0x100,0x10000,0};
  for(unsigned n=0;n<3;n++) {
    setup(0x60,rx_before[n],4,rx_after[n]);
    assert(!hosted_sdio_read_counter32(NULL,0x60,0xfffff,&value));
    assert((value&0xfffff)==rx_after[n] && g_counter_retries==1);
  }
  const uint32_t tx_before[]={0x00ff0000,0x0fff0000};
  const uint32_t tx_after[]={0x01000000,0};
  for(unsigned n=0;n<2;n++) {
    setup(0x44,tx_before[n],2,tx_after[n]);
    assert(!hosted_sdio_read_counter32(NULL,0x44,0x0fff0000,&value));
    assert((value&0x0fff0000)==tx_after[n] && g_counter_retries==1);
  }
  setup(0x60,0x12345,1,0xabc12345);
  assert(!hosted_sdio_read_counter32(NULL,0x60,0xfffff,&value));
  assert((value&0xfffff)==0x12345 && !g_counter_retries);
  setup(0x60,0xff,0,0); churn=true; value=0xdeadbeef;
  assert(hosted_sdio_read_counter32(NULL,0x60,0xfffff,&value)==-EAGAIN);
  assert(value==0xdeadbeef && reads==28 && g_counter_exhausted==1);
  for(unsigned fail=1;fail<=7;fail++) {
    setup(0x60,0xff,0,0); fail_on=fail; value=0xdeadbeef;
    assert(hosted_sdio_read_counter32(NULL,0x60,0xfffff,&value)==-ETIMEDOUT);
    assert(value==0xdeadbeef);
  }
  setup(0x60,UINT32_MAX,0,0);
  assert(hosted_sdio_read_counter32(NULL,0x60,0xfffff,&value)==-ENODEV);
  setup(0x60,0xff,4,0x100); g_hosted.rx_bytes=0xf0;
  assert(!hosted_pending_rx_bytes(&value) && value==16);
  setup(0x44,0x00ff0000,2,0x01000000); g_hosted.tx_buffers=250;
  assert(!hosted_tx_buffers_free(&value) && value==6);
  puts("PASS actual counter reads: carries, wrap, unrelated bits, bounded churn, bus errors and RX/TX delta integration");
}
'''
with tempfile.TemporaryDirectory(prefix='c6-counter-snapshot-') as directory:
    out = Path(directory)
    (out / 'test.c').write_text(code)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(out / 'test.c'),
                    '-o', str(out / 'test')], check=True)
    subprocess.run([str(out / 'test')], check=True, timeout=10)
