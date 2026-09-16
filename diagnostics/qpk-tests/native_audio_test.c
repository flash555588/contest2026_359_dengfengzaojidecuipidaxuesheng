/* Fault injection for production functions extracted by the Python runner. */
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAR
#define OS_SPINLOCK 1
#define CONFIG_SPINLOCK 1
#define OS_PORT_NUM_PROCESSORS 2
#define CONFIG_ESPRESSIF_I2S_MAXINFLIGHT 4
#define I2S_DMADESC_NUM 2
#define I2S_DMA_BUFFER_MAX_SIZE 4092
#define OK 0
#define DEBUGASSERT assert
#define VERIFY(x) assert((x) == 0)
#define ALIGN_UP(x, a) (((x) + (a) - 1) / (a) * (a))
#define i2serr(...) ((void)0)
#define i2sinfo(...) ((void)0)
#define i2s_dump_buffer(...) ((void)0)

typedef unsigned irqstate_t;
typedef struct { unsigned owner, count; } rspinlock_t;
static bool os_ready;
static unsigned current_cpu, irq_enabled = 1, task_locks, queue_locks;
static uint8_t g_int_flags_count[OS_PORT_NUM_PROCESSORS];
static irqstate_t g_int_flags[OS_PORT_NUM_PROCESSORS];
#define OSINIT_OS_READY() os_ready
static unsigned this_cpu(void) { return current_cpu; }
static irqstate_t up_irq_save(void) { unsigned old = irq_enabled; irq_enabled = 0; return old; }
static void up_irq_restore(irqstate_t flags) { irq_enabled = flags; }
static void sched_lock(void) { task_locks++; }
static void sched_unlock(void) { assert(task_locks); task_locks--; }
static void rspin_lock(rspinlock_t *l) {
  assert(!l->count || l->owner == current_cpu + 1);
  l->owner = current_cpu + 1; l->count++;
}
static void rspin_unlock(rspinlock_t *l) {
  assert(l->count && l->owner == current_cpu + 1);
  if (!--l->count) l->owner = 0;
}
static void try_preempt(void) { if (!task_locks) current_cpu ^= 1; }
static irqstate_t spin_lock_irqsave_nopreempt(unsigned *l) {
  assert(!*l); *l = 1; queue_locks++; sched_lock(); return up_irq_save();
}
static void spin_unlock_irqrestore_nopreempt(unsigned *l, irqstate_t f) {
  assert(*l && queue_locks); *l = 0; queue_locks--; up_irq_restore(f); sched_unlock();
}

static int allocations, fail_after = -1, mutex_error, mutex_unlocks;
static void *kmm_memalign(size_t alignment, size_t size) {
  assert(!queue_locks);
  if (fail_after == 0) return NULL;
  if (fail_after > 0) fail_after--;
  void *p = malloc(size); if (p) allocations++; return p;
}
static void kmm_free(void *p) { assert(!queue_locks); if (p) { allocations--; free(p); } }
static int nxsem_wait_uninterruptible(int *s) { assert(*s > 0); --*s; return 0; }
static int nxsem_post(int *s) { assert(!queue_locks); ++*s; return 0; }
static int nxmutex_lock(int *m) { if (mutex_error) return mutex_error; assert(!*m); *m = 1; return 0; }
static void nxmutex_unlock(int *m) { assert(*m); *m = 0; mutex_unlocks++; }

typedef struct { unsigned data[4]; } lldesc_t;
struct i2s_dev_s { int unused; };
struct ap_buffer_s {
  uint32_t nbytes, curbyte, nmaxbytes;
  unsigned refs;
  unsigned char samp[32];
};
typedef void (*i2s_callback_t)(struct i2s_dev_s *, struct ap_buffer_s *, void *, int);
struct esp_buffer_s {
  struct esp_buffer_s *flink;
  lldesc_t *dma_link[I2S_DMADESC_NUM];
  i2s_callback_t callback;
  uint32_t timeout;
  void *arg;
  struct ap_buffer_s *apb;
  uint8_t *buf;
  uint32_t nbytes;
  int result;
};
struct config_s { bool tx_en, rx_en; };
struct esp_i2s_s {
  struct i2s_dev_s dev;
  const struct config_s *config;
  unsigned slock, data_width;
  int lock, bufsem;
  struct { struct { unsigned bytes, value; } carry; } tx;
  struct esp_buffer_s *bf_freelist;
  struct esp_buffer_s containers[CONFIG_ESPRESSIF_I2S_MAXINFLIGHT];
};
static void apb_reference(struct ap_buffer_s *p) { p->refs++; }
static void apb_free(struct ap_buffer_s *p) { assert(p->refs); p->refs--; }
static int setup_error = -EIO;
static int i2s_txdma_setup(struct esp_i2s_s *p, struct esp_buffer_s *b) {
  b->buf = kmm_memalign(4, b->nbytes);
  return b->buf ? setup_error : -ENOMEM;
}
static int i2s_rxdma_setup(struct esp_i2s_s *p, struct esp_buffer_s *b) {
  return i2s_txdma_setup(p, b);
}

#include "native_audio.inc"

static void check_restored(struct esp_i2s_s *p, struct ap_buffer_s *a) {
  assert(allocations == 0 && a->refs == 1 && !p->lock && !p->slock);
  assert(p->bufsem == CONFIG_ESPRESSIF_I2S_MAXINFLIGHT);
  assert(!queue_locks && !task_locks);
}

int main(void) {
  rspinlock_t first = {0}, second = {0};
  nuttx_enter_critical(&first);
  assert(!irq_enabled && !task_locks && !first.count);
  nuttx_exit_critical(&first);
  assert(irq_enabled && !g_int_flags_count[0]);

  os_ready = true;
  nuttx_enter_critical(&first);
  assert(task_locks == 1 && !irq_enabled);
  try_preempt(); assert(current_cpu == 0);
  nuttx_enter_critical(&first);
  nuttx_enter_critical(&second);
  try_preempt(); assert(current_cpu == 0 && task_locks == 3);
  nuttx_exit_critical(&second);
  nuttx_exit_critical(&first);
  assert(!irq_enabled && task_locks == 1 && first.count == 1);
  nuttx_exit_critical(&first);
  assert(irq_enabled && !task_locks && !first.count && !g_int_flags_count[0]);
  irq_enabled = 0;
  nuttx_enter_critical(&first); nuttx_exit_critical(&first);
  assert(!irq_enabled && !task_locks);
  irq_enabled = 1;

  const struct config_s config = {true, true};
  struct esp_i2s_s driver = {.config = &config, .data_width = 16};
  struct ap_buffer_s apb = {.nbytes = 16, .nmaxbytes = 16, .refs = 1};
  assert(i2s_buf_initialize(&driver) == 0);
  for (int failure = 0; failure < I2S_DMADESC_NUM; failure++) {
    fail_after = failure;
    assert(i2s_buf_allocate(&driver) == NULL);
    check_restored(&driver, &apb);
  }
  fail_after = -1;
  struct esp_buffer_s *buffer = i2s_buf_allocate(&driver);
  assert(buffer && allocations == I2S_DMADESC_NUM);
  assert(i2s_buf_free(&driver, buffer) == 0);
  check_restored(&driver, &apb);

  for (int receive = 0; receive < 2; receive++) {
    int (*submit)(struct i2s_dev_s *, struct ap_buffer_s *, i2s_callback_t, void *, uint32_t)
      = receive ? i2s_receive : i2s_send;
    mutex_error = -EINTR; mutex_unlocks = 0;
    assert(submit(&driver.dev, &apb, NULL, NULL, 1) == -EINTR);
    assert(!mutex_unlocks); check_restored(&driver, &apb);
    mutex_error = 0;
    fail_after = I2S_DMADESC_NUM;
    assert(submit(&driver.dev, &apb, NULL, NULL, 1) == -ENOMEM);
    check_restored(&driver, &apb);
    fail_after = -1;
    assert(submit(&driver.dev, &apb, NULL, NULL, 1) == -EIO);
    check_restored(&driver, &apb);
  }
  puts("PASS: HAL nesting/preemption, I2S allocation rollback, TX/RX rejection ownership");
  return 0;
}
