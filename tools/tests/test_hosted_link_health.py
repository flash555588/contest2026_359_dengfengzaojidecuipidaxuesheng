"""Fault-inject actual Hosted RX/TX/init code; no device access."""
from pathlib import Path
import re
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[2]
source = repo / 'app/espdl-quickapp/overlay/apps/system/c6probe/esp_hosted.c'
_test_output = tempfile.TemporaryDirectory(prefix='c6-link-health-')
output = Path(_test_output.name)
text = source.read_text()


def function(name):
    start = re.search(r'^(?:static )?int ' + name + r'\(', text, re.M).start()
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>
#define FAR
#define OK 0
#define LOG_ERR 3
#define syslog(level, ...) ((void)(level), (void)fprintf(stderr, __VA_ARGS__))
#define C6_RX_BATCH_SIZE 65536
#define C6_RESET_GPIO 54
#define C6_REG_HOST_TO_SLAVE 0x8c
#define C6_HOST_TO_SLAVE_OPEN 1
#define C6_REG_INT_ENA 0xdc
#define C6_INT_NEW_PACKET (1u << 23)
#define ESP_HOSTED_HEADER_SIZE 12
#define ESP_HOSTED_MAX_FRAME 16384
#define ESP_HOSTED_IF_MAX 8
#define ESP_HOSTED_IF_HCI 4
#define ESP_HOSTED_RX_BUFFER_SIZE 1536
#define C6_RX_BYTE_MODULO 0x100000u
#define C6_TX_BUFFER_MODULO 0x1000u
#define C6_TX_RETRIES 10
#define C6_TX_RETRY_DELAY_MS 50
#define C6_SDIO_BLOCK_SIZE 512
#define C6_CMD53_END_ADDR 0x1f800u
struct esp_hosted_header_s {
  uint8_t if_type:4, if_num:4;
  uint8_t flags;
  uint16_t len, offset, checksum, seq_num;
  uint8_t reserved, pkt_type;
} __attribute__((packed));
struct sdio_dev_s { int unused; };
static struct sdio_dev_s card;
static struct {
  bool up;
  bool faulted;
  pthread_mutex_t lock;
  struct sdio_dev_s *sdio;
  uint32_t rx_bytes, tx_buffers;
  uint16_t seq_num[ESP_HOSTED_IF_MAX];
} g_hosted = {.lock = PTHREAD_MUTEX_INITIALIZER};
static pthread_mutex_t g_rx_dispatch_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t *g_frame_buf;
static uint8_t g_dma_block[2048];
static unsigned g_rx_block_limit = 4;
static uint32_t g_rx_peak, g_rx_large_batches;
static uint32_t g_hci_tx_packets, g_hci_tx_errors, g_tx_credit_waits;
static int g_link_error = -ENOTCONN;
static int injected_read_error, injected_boot_error;
static unsigned resets, enumerations, reads, dispatches;
static unsigned pending, fifo_reads, fail_read_at, writes, fail_write_at;
static unsigned setup_fail_at;
static unsigned free_tokens=10, fault_during_wait;
static int token_error;
static int nxmutex_lock(pthread_mutex_t *m) { return -pthread_mutex_lock(m); }
static int nxmutex_unlock(pthread_mutex_t *m) { return -pthread_mutex_unlock(m); }
static int hosted_reset_c6(void) { resets++; return injected_boot_error; }
struct sdio_dev_s *sdio_initialize(int slot) { assert(slot == 1); return &card; }
static int hosted_enumerate(struct sdio_dev_s *s, bool v) {
  (void)s; (void)v; enumerations++; return 0;
}
static int hosted_sdio_write_reg8(struct sdio_dev_s *s, uint32_t a, uint8_t v) {
  (void)s; (void)a; (void)v; return 0;
}
static int hosted_sdio_read_reg32(struct sdio_dev_s *s, uint32_t a, uint32_t *v) {
  (void)s; (void)a; *v=0; return 0;
}
static int hosted_link_fault(int error);
static int usleep(unsigned usec) {
  (void)usec;
  if(fault_during_wait) {
    assert(!pthread_mutex_trylock(&g_hosted.lock));
    fault_during_wait=0;
    hosted_link_fault(-EIO);
    pthread_mutex_unlock(&g_hosted.lock);
  }
  return 0;
}
static int hosted_pending_rx_bytes(uint32_t *length) {
  reads++; *length = pending; return injected_read_error;
}
static int hosted_do_read_block(struct sdio_dev_s *s, uint32_t a, unsigned n,
                                bool *submitted) {
  (void)s; (void)a; (void)n;
  fifo_reads++;
  *submitted=fifo_reads!=setup_fail_at;
  if(!*submitted) return -ENOMEM;
  return fifo_reads==fail_read_at ? -EIO : 0;
}
static int hosted_do_read_tail(struct sdio_dev_s *s, uint32_t a, unsigned n) {
  bool submitted;
  return hosted_do_read_block(s,a,n,&submitted);
}
static int hosted_tx_buffers_free(uint32_t *count) {
  *count=free_tokens; return token_error;
}
static int hosted_do_write_block(struct sdio_dev_s *s, uint32_t a, unsigned n) {
  (void)s; (void)a; (void)n; writes++;
  return writes==fail_write_at ? -ETIMEDOUT : 0;
}
static void hosted_dispatch_stream(uint8_t *buffer, uint32_t length) {
  (void)buffer; assert(length == pending); dispatches++;
}
'''
harness += '\n' + function('esp_hosted_link_error')
harness += '\n' + function('hosted_link_fault')
harness += '\n' + function('hosted_read_frame')
harness += '\n' + function('esp_hosted_initialize')
harness += '\n' + function('esp_hosted_poll')
harness += '\n' + function('esp_hosted_send')
harness += r'''
static void simulated_fresh_process(void) {
  /* Test fixture reset only. Production never clears faulted for a live
   * shared controller. Physical restart requires all owners to drain.
   */
  g_hosted.up=false; g_hosted.faulted=false;
  g_hosted.rx_bytes=g_hosted.tx_buffers=0;
  injected_read_error=injected_boot_error=0;
  pending=fifo_reads=fail_read_at=writes=fail_write_at=0;
  setup_fail_at=0;
  free_tokens=10; fault_during_wait=0; token_error=0;
  g_link_error=-ENOTCONN;
  assert(esp_hosted_initialize(false)==0);
}
int main(void) {
  uint8_t payload[1200]={0};
  assert(esp_hosted_link_error()==-ENOTCONN);
  assert(esp_hosted_initialize(false) == 0);
  assert(g_hosted.up && resets == 1 && enumerations == 1);
  pending=16;
  assert(esp_hosted_poll() == 1 && dispatches == 1);
  assert(!pthread_mutex_lock(&g_rx_dispatch_lock));
  assert(esp_hosted_poll() == 0);
  assert(!pthread_mutex_unlock(&g_rx_dispatch_lock));
  unsigned before_resets = resets, before_enumerations = enumerations;
  const int faults[] = {-ETIMEDOUT, -EIO};
  for (unsigned fault=0; fault<2; fault++) {
    injected_read_error = faults[fault];
    for (unsigned attempt=0; attempt<10; attempt++) {
      assert(esp_hosted_poll() == faults[fault]);
      assert(g_hosted.up);
      assert(esp_hosted_link_error() == faults[fault]);
      assert(esp_hosted_initialize(false) == faults[fault]);
      assert(resets == before_resets && enumerations == before_enumerations);
    }
  }
  puts("PASS: 20 register-read failures return real health errors; no hidden reset");
  injected_read_error = 0;
  assert(esp_hosted_poll() == 1 && dispatches == 2);
  assert(esp_hosted_link_error()==0 && esp_hosted_initialize(false)==0);
  puts("PASS: transient register errors recover on successful RX observation");
  simulated_fresh_process(); pending=4097; setup_fail_at=1;
  assert(esp_hosted_poll()==-ENOMEM && !g_hosted.faulted);
  assert(g_hosted.rx_bytes==0 && esp_hosted_initialize(false)==-ENOMEM);
  setup_fail_at=0;
  assert(esp_hosted_poll()==1 && esp_hosted_link_error()==0);
  simulated_fresh_process(); pending=4097; setup_fail_at=2;
  assert(esp_hosted_poll()==-ENETRESET && g_hosted.faulted);
  puts("PASS: first-block DMA setup failure recovers; later setup failure after FIFO consumption requires reset");
  /* Fail the first block, a later block, and the short tail. None can be
   * replayed safely because the hardware may have consumed the transfer.
   */
  for(unsigned fail=1; fail<=3; fail++) {
    simulated_fresh_process();
    pending=4097; fail_read_at=fail;
    uint32_t rx_before=g_hosted.rx_bytes;
    unsigned dispatched_before=dispatches;
    assert(esp_hosted_poll()==-ENETRESET);
    assert(g_hosted.faulted && esp_hosted_link_error()==-ENETRESET);
    assert(g_hosted.rx_bytes==rx_before && dispatches==dispatched_before);
    unsigned fifo_before=fifo_reads, reset_before=resets;
    assert(esp_hosted_poll()==-ENETRESET && fifo_reads==fifo_before);
    assert(esp_hosted_send(1,0,payload,sizeof(payload))==-ENETRESET);
    assert(!writes);
    assert(esp_hosted_initialize(false)==-ENETRESET && resets==reset_before);
  }
  simulated_fresh_process(); pending=C6_RX_BATCH_SIZE+1;
  assert(esp_hosted_poll()==-ENETRESET && !fifo_reads);
  puts("PASS: uncertain RX block/tail and impossible counter failures latch without replay");
  for(unsigned fail=1; fail<=3; fail++) {
    simulated_fresh_process(); fail_write_at=fail;
    assert(esp_hosted_send(1,0,payload,sizeof(payload))==-ENETRESET);
    assert(g_hosted.faulted && !g_hosted.tx_buffers);
    unsigned before=writes;
    assert(esp_hosted_send(1,0,payload,sizeof(payload))==-ENETRESET);
    assert(writes==before);
  }
  simulated_fresh_process(); token_error=-EIO;
  assert(esp_hosted_send(1,0,payload,sizeof(payload))==-EIO);
  assert(!g_hosted.faulted && !writes);
  assert(esp_hosted_link_error()==-EIO);
  assert(esp_hosted_initialize(false)==-EIO);
  token_error=0;
  assert(esp_hosted_send(1,0,payload,sizeof(payload))==0 && writes==3);
  simulated_fresh_process(); free_tokens=0;
  assert(esp_hosted_send(1,0,payload,sizeof(payload))==-EAGAIN);
  assert(!g_hosted.faulted && !writes);
  fault_during_wait=1;
  assert(esp_hosted_send(1,0,payload,sizeof(payload))==-ENETRESET);
  assert(!writes);
  puts("PASS: TX faults are not replayed; backpressure and register errors stay retryable; wait race rejected");
  g_hosted.up = false;
  injected_boot_error = -ETIMEDOUT;
  assert(esp_hosted_initialize(false) == -ETIMEDOUT && !g_hosted.up);
  puts("PASS: actual initialization failure propagates the error and leaves up=false");
  free(g_frame_buf);
  return 0;
}
'''
(output / 'recovery_harness.c').write_text(harness)
subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
                '-fsanitize=address,undefined', '-pthread',
                str(output / 'recovery_harness.c'), '-o', str(output / 'recovery_harness')], check=True)
result = subprocess.run([str(output / 'recovery_harness')], text=True, capture_output=True, check=True)
print(result.stdout, end='')
