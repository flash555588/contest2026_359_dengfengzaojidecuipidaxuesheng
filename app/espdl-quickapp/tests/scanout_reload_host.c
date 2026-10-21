/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "actual-registers.h"

#define FAR
#define IRAM_ATTR
#define CONFIG_ESP32P4_DSI_FLASH_RELOAD 1
#define ESP_MIPI_DSI_DMA_CHAN 0
#define DW_GDMA_LL_MASTER_PORT_MEMORY 1
#define DW_GDMA_LL_MASTER_PORT_MIPI_CSI 0
#define DW_GDMA_LL_MASTER_PORT_MIPI_DSI 0
#define DW_GDMA_LL_LINK_LIST_ALIGNMENT 64
#define MIPI_DSI_BRG_MEM_BASE 0x50000000
#define MIPI_CSI_BRG_MEM_BASE 0x50010000
#define MIPI_DSI_BRG_LL_EVENT_UNDERRUN 1
#define DW_GDMA_LL_CHANNEL_EVENT_BLOCK_TFR_DONE (1u<<0)
#define DW_GDMA_LL_CHANNEL_EVENT_DMA_TFR_DONE (1u<<1)
#define DW_GDMA_LL_CHANNEL_EVENT_SRC_TRANSCOMP (1u<<3)
#define DW_GDMA_LL_CHANNEL_EVENT_DST_TRANSCOMP (1u<<4)
#define UNUSED(x) (void)(x)
#define OK 0
#define MSEC2TICK(x) (x)
typedef enum {DW_GDMA_BLOCK_TRANSFER_CONTIGUOUS,DW_GDMA_BLOCK_TRANSFER_RELOAD,
 DW_GDMA_BLOCK_TRANSFER_SHADOW,DW_GDMA_BLOCK_TRANSFER_LIST} dw_gdma_block_transfer_type_t;
typedef struct {dmac_chn_sar0_reg_t sar_lo;dmac_chn_block_ts0_reg_t block_ts_lo;
 dmac_chn_ctl0_reg_t ctrl_lo;dmac_chn_ctl1_reg_t ctrl_hi;} dw_gdma_link_list_item_t;
typedef pthread_mutex_t mutex_t;
typedef pthread_mutex_t spinlock_t;
#define NXMUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
typedef struct {unsigned count;} test_sem_t;
typedef int irqstate_t;
static int nxmutex_lock(mutex_t *p){return -pthread_mutex_lock(p);}
static int nxmutex_unlock(mutex_t *p){return -pthread_mutex_unlock(p);}
static irqstate_t spin_lock_irqsave(spinlock_t *p){assert(!pthread_mutex_lock(p));return 0;}
static void spin_unlock_irqrestore(spinlock_t *p,irqstate_t flags){(void)flags;assert(!pthread_mutex_unlock(p));}
static void nxsem_reset(test_sem_t *p,unsigned n){p->count=n;}
static void nxsem_post(test_sem_t *p){p->count++;}
static int nxsem_tickwait_uninterruptible(test_sem_t *,unsigned);
static uint64_t fake_us;
static uint64_t esp_camera_timing_now(void){return fake_us;}
static void esp_camera_display_frame(void){}
static unsigned mipi_dsi_brg_ll_get_interrupt_status(void *p){(void)p;return 0;}
static void mipi_dsi_brg_ll_clear_interrupt_status(void *p,unsigned s){(void)p;(void)s;}
struct esp_mipi_dsi_priv_s {
 struct {dw_gdma_dev_t *dev;} dw_hal;
 struct {void *bridge;} hal;
 spinlock_t dmalock;
 dw_gdma_link_list_item_t *lli,*lli_nc;
 void *fb,*pending_fb;
 bool dma_enabled,fb_bound,reload_error;
 unsigned reload_phase;
 uint64_t reload_started;
 uint32_t reload_frame_us,reload_status_mask;
 test_sem_t reload_done,fb_done;
};
static dw_gdma_dev_t device;
static dw_gdma_link_list_item_t descriptor;
static struct esp_mipi_dsi_priv_s g_esp_mipi_dsi={.dmalock=PTHREAD_MUTEX_INITIALIZER};
static uint32_t g_esp_mipi_dsi_frames,g_esp_mipi_dsi_underruns;
static void esp_mipi_dsi_dma_restart(struct esp_mipi_dsi_priv_s *priv);
static int esp_mipi_dsi_dma_isr(int,void *,void *);
#include "actual-ll.inc"
#include "esp_mipi_dsi_reload.inc"
#include "actual-restart.inc"
#include "actual-isr.inc"
#include "actual-pan.inc"

static bool timeout_entry,timeout_exit,accept_on_timeout,progress=true,inject_irq,short_sleep;
static void frame_irq(void)
{
 device.chen0.val=0;
 device.ch[0].int_st0.val=DW_GDMA_LL_CHANNEL_EVENT_DMA_TFR_DONE;
 assert(!esp_mipi_dsi_dma_isr(1,NULL,&g_esp_mipi_dsi));
}
static int nxsem_tickwait_uninterruptible(test_sem_t *sem,unsigned ticks)
{
 (void)ticks;
 if(g_esp_mipi_dsi.reload_phase==DSI_RELOAD_REQUESTED){
  if(timeout_entry)return -ETIMEDOUT;
  frame_irq();if(accept_on_timeout)return -ETIMEDOUT;
 }else if(g_esp_mipi_dsi.reload_phase==DSI_RELOAD_ENDING){
  if(timeout_exit)return -ETIMEDOUT;
  frame_irq();
 }else if(g_esp_mipi_dsi.pending_fb)frame_irq();
 if(!sem->count)return -ETIMEDOUT;
 sem->count--;return 0;
}
int nxsig_usleep(unsigned us)
{
 unsigned elapsed=short_sleep ? us/2+1 : us;
 fake_us+=elapsed;
 if(progress && g_esp_mipi_dsi.reload_phase==DSI_RELOAD_ACTIVE){
  device.ch[0].sar0.val+=64;
 }
 if(inject_irq){inject_irq=false;frame_irq();}
 return short_sleep ? -EINTR : 0;
}
static void reset(void)
{
 assert(!pthread_mutex_trylock(&g_dsi_flash_lock));pthread_mutex_unlock(&g_dsi_flash_lock);
 memset(&device,0,sizeof(device));memset(&descriptor,0,sizeof(descriptor));
 g_esp_mipi_dsi.dw_hal.dev=&device;g_esp_mipi_dsi.lli=g_esp_mipi_dsi.lli_nc=&descriptor;
 g_esp_mipi_dsi.fb=(void *)0x48000000;g_esp_mipi_dsi.pending_fb=NULL;
 g_esp_mipi_dsi.dma_enabled=g_esp_mipi_dsi.fb_bound=true;
 g_esp_mipi_dsi.reload_error=false;g_esp_mipi_dsi.reload_phase=DSI_RELOAD_IDLE;
 g_esp_mipi_dsi.reload_frame_us=16667;g_esp_mipi_dsi.reload_started=0;
 g_esp_mipi_dsi.reload_done.count=g_esp_mipi_dsi.fb_done.count=0;
 device.ch[0].int_st_ena0.val=UINT32_MAX;
 descriptor.ctrl_hi.ioc_blktfr=1;
 g_dsi_reload_qualified=false;g_esp_mipi_dsi_frames=0;fake_us=0;
 timeout_entry=timeout_exit=accept_on_timeout=inject_irq=short_sleep=false;progress=true;
}
static atomic_bool pan_entered;
static void *pan_thread(void *arg)
{
 (void)arg;atomic_store(&pan_entered,true);
 return (void *)(intptr_t)esp_mipi_dsi_swap_framebuffer((void *)0x48100000,100);
}
int main(void)
{
 reset();assert(esp_mipi_dsi_flash_begin()==0);
 assert(!esp_mipi_dsi_flash_probe()&&g_dsi_reload_qualified);
 assert(g_esp_mipi_dsi_frames==2); /* Entry/final IRQ only, no assisted frames. */
 assert(device.ch[0].cfg0.src_multblk_type==DW_GDMA_BLOCK_TRANSFER_LIST);
 assert(device.ch[0].int_st_ena0.val==UINT32_MAX);
 assert(esp_mipi_dsi_flash_begin()==1);
 assert(!(device.ch[0].int_st_ena0.val&DSI_RELOAD_NO_STALL));
 assert(!device.ch[0].ctl1.ioc_blktfr);
 pthread_t thread;atomic_store(&pan_entered,false);
 assert(!pthread_create(&thread,NULL,pan_thread,NULL));
 while(!atomic_load(&pan_entered))usleep(100);
 usleep(1000);assert(!g_esp_mipi_dsi.pending_fb);
 uint64_t began=g_esp_mipi_dsi.reload_started;short_sleep=true;
 assert(!esp_mipi_dsi_flash_end());assert(fake_us>=began+70001);
 void *result;assert(!pthread_join(thread,&result));assert(!result);
 assert(g_esp_mipi_dsi.fb==(void *)0x48100000);
 reset();progress=false;assert(esp_mipi_dsi_flash_probe()==-EIO&&!g_dsi_reload_qualified);
 reset();g_dsi_reload_qualified=true;progress=false;
 assert(esp_mipi_dsi_flash_probe()==-EIO&&!g_dsi_reload_qualified);
 reset();inject_irq=true;assert(esp_mipi_dsi_flash_probe()==-EIO&&!g_dsi_reload_qualified);
 reset();g_dsi_reload_qualified=true;timeout_entry=true;
 assert(esp_mipi_dsi_flash_begin()==-ETIMEDOUT);
 assert(g_esp_mipi_dsi.reload_phase==DSI_RELOAD_IDLE);
 timeout_entry=false;frame_irq();assert(g_esp_mipi_dsi.reload_phase==DSI_RELOAD_IDLE);
 reset();g_dsi_reload_qualified=true;accept_on_timeout=true;
 assert(esp_mipi_dsi_flash_begin()==1);accept_on_timeout=false;
 assert(!esp_mipi_dsi_flash_end());
 reset();g_dsi_reload_qualified=true;assert(esp_mipi_dsi_flash_begin()==1);
 timeout_exit=true;assert(esp_mipi_dsi_flash_end()==-ETIMEDOUT);
 assert(g_esp_mipi_dsi.reload_phase==DSI_RELOAD_ENDING);
 assert(esp_mipi_dsi_flash_begin()==-EBUSY);
 puts("SCANOUT_RELOAD_HOST_OK: production control/ISR/LL/P4 layout; qualification without ISR assistance, stalled/assisted probe rejection, interrupted waits, pending pan admission, late ready and exit timeout");
 return 0;
}
