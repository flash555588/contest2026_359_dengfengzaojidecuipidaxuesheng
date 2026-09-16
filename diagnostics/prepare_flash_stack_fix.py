"""Install P4 flash dispatch for callers with PSRAM stacks; keep HAL protocol unchanged."""
from pathlib import Path
ws=Path(__file__).resolve().parent.parent
build=Path('/tmp/v3-desktop-espdl-20260915/nuttx')
overlay=ws/'04-v3-20260913/espdl-quickapp/overlay/nuttx'
relative=Path('arch/risc-v/src/common/espressif/esp_spiflash.c')
target=overlay/relative
assert not target.exists(), 'Do not overwrite a reviewed overlay'
source=(build/relative).read_text()
backup=ws/'diagnostics/flash-stack-baseline'
backup.mkdir(exist_ok=True)
(backup/'esp_spiflash.c').write_text(source)
source=source.replace('#include <nuttx/mutex.h>', '#include <nuttx/mutex.h>\n#include <nuttx/semaphore.h>\n#include <nuttx/kthread.h>\n#include "esp_memory_utils.h"')
for name in ('read','write','erase'):
    source=source.replace('int esp_spiflash_'+name+'(', 'static int spiflash_direct_'+name+'(')
source += r'''

/* P4 suspends the shared external-memory cache during SPI1 operations.
 * An ordinary pthread may own a PSRAM stack. Running the HAL on that stack
 * can fault while cache is off and strand the other CPU in its flash IPC.
 * Use a privileged NuttX thread (internal kernel heap stack) for those callers,
 * the same separation used by ESP32's PSRAM-stack flash support. */
static mutex_t g_flash_dispatch_lock = NXMUTEX_INITIALIZER;
static sem_t g_flash_work = SEM_INITIALIZER(0);
static sem_t g_flash_done = SEM_INITIALIZER(0);
static int g_flash_worker_pid;
static struct {
  unsigned operation;
  uint32_t address, length;
  void *buffer;
  int result;
} g_flash_job;
volatile uint32_t g_flash_external_stack_ops;
volatile uintptr_t g_flash_caller_sp, g_flash_worker_sp;

static int spiflash_stack_worker(int argc, char **argv)
{
  (void)argc; (void)argv;
  int marker;
  g_flash_worker_sp = (uintptr_t)&marker;
  for (;;) {
    int ret = nxsem_wait_uninterruptible(&g_flash_work);
    if (ret < 0) continue;
    if (!esp_ptr_in_dram(&marker)) g_flash_job.result = -EFAULT;
    else if (g_flash_job.operation == 0)
      g_flash_job.result = spiflash_direct_read(g_flash_job.address, g_flash_job.buffer, g_flash_job.length);
    else if (g_flash_job.operation == 1)
      g_flash_job.result = spiflash_direct_write(g_flash_job.address, g_flash_job.buffer, g_flash_job.length);
    else g_flash_job.result = spiflash_direct_erase(g_flash_job.address, g_flash_job.length);
    nxsem_post(&g_flash_done);
  }
  return 0;
}

static int spiflash_dispatch(unsigned op, uint32_t address, void *buffer, uint32_t length)
{
  int ret = nxmutex_lock(&g_flash_dispatch_lock);
  if (ret < 0) return ret;
  if (!g_flash_worker_pid) {
    int pid = kthread_create("flash-stack", 100, 4096, spiflash_stack_worker, NULL);
    if (pid < 0) { nxmutex_unlock(&g_flash_dispatch_lock); return pid; }
    g_flash_worker_pid = pid;
  }
  g_flash_caller_sp = (uintptr_t)&ret; g_flash_external_stack_ops++;
  g_flash_job.operation = op; g_flash_job.address = address;
  g_flash_job.buffer = buffer; g_flash_job.length = length;
  nxsem_post(&g_flash_work);
  ret = nxsem_wait_uninterruptible(&g_flash_done);
  if (ret >= 0) ret = g_flash_job.result;
  nxmutex_unlock(&g_flash_dispatch_lock);
  return ret;
}

int esp_spiflash_read(uint32_t address, void *buffer, uint32_t length)
{
  int marker;
  return esp_ptr_in_dram(&marker) ? spiflash_direct_read(address, buffer, length) :
    spiflash_dispatch(0, address, buffer, length);
}
int esp_spiflash_write(uint32_t address, const void *buffer, uint32_t length)
{
  int marker;
  return esp_ptr_in_dram(&marker) ? spiflash_direct_write(address, buffer, length) :
    spiflash_dispatch(1, address, (void *)buffer, length);
}
int esp_spiflash_erase(uint32_t address, uint32_t length)
{
  int marker;
  return esp_ptr_in_dram(&marker) ? spiflash_direct_erase(address, length) :
    spiflash_dispatch(2, address, NULL, length);
}
'''
target.parent.mkdir(parents=True,exist_ok=True); target.write_text(source)
# Capture the first architectural exception before a cache-off exception can
# recursively fault inside the flash-resident panic reporter. Metadata only.
relative=Path('arch/risc-v/src/common/riscv_doirq.c')
target=overlay/relative; assert not target.exists()
source=(build/relative).read_text(); (backup/'riscv_doirq.c').write_text(source)
anchor='uintreg_t *riscv_doirq(int irq, uintreg_t *regs)\n{'
replacement='''volatile uintreg_t g_first_fault[2][8];

uintreg_t *riscv_doirq(int irq, uintreg_t *regs)
{
  if (irq < RISCV_IRQ_ECALLU) {
    uintreg_t cpu, cause, value;
    __asm__ volatile("csrr %0, mhartid" : "=r"(cpu));
    __asm__ volatile("csrr %0, mcause" : "=r"(cause));
    __asm__ volatile("csrr %0, mtval" : "=r"(value));
    if (cpu < 2 && !g_first_fault[cpu][0]) {
      g_first_fault[cpu][0] = 1;
      g_first_fault[cpu][1] = irq;
      g_first_fault[cpu][2] = regs[REG_EPC];
      g_first_fault[cpu][3] = regs[REG_SP];
      g_first_fault[cpu][4] = regs[REG_RA];
      g_first_fault[cpu][5] = cause;
      g_first_fault[cpu][6] = value;
    }
  }'''
assert anchor in source; target.write_text(source.replace(anchor,replacement))
print('Prepared flash stack dispatch and first-fault metadata')
