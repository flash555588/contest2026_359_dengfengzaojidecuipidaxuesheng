/* Host test of the production Flash guard and extracted actual DSI ISR. */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <nuttx/spinlock.h>
#include <nuttx/video/display_flash.h>
#include "hal/dw_gdma_ll.h"
#define FAR
#define IRAM_ATTR
#define OK 0
#define UNUSED(x) (void)(x)
#define ESP_MIPI_DSI_DMA_CHAN 0
#define DW_GDMA_LL_CHANNEL_EVENT_DMA_TFR_DONE 1
#define MIPI_DSI_BRG_LL_EVENT_UNDERRUN 2
static unsigned progress_calls, shared_calls, frames, guard_frames, restarts;
static unsigned sleep_calls;
static uint64_t virtual_us;
static bool progress_irq;
static unsigned g_esp_mipi_dsi_underruns, g_esp_mipi_dsi_frames;
static dw_gdma_dev_t hw;
struct esp_mipi_dsi_priv_s {
  struct {dw_gdma_dev_t *dev;} dw_hal;
  struct {uint32_t *bridge;} hal;
  spinlock_t dmalock;
  bool dma_enabled;
  void *pending_fb, *fb, *lli_nc;
  unsigned fb_done;
};
static struct esp_mipi_dsi_priv_s g_esp_mipi_dsi={.dmalock=PTHREAD_MUTEX_INITIALIZER};
void up_udelay(unsigned us) {usleep(us);}
static void esp_camera_display_frame(void) {frames++;}
static void esp_camera_display_guard_frame(void) {guard_frames++;}
static uint32_t dw_gdma_ll_channel_get_intr_status(dw_gdma_dev_t *dev, unsigned ch) {return dev->ch[ch].status;}
static void dw_gdma_ll_channel_clear_intr(dw_gdma_dev_t *dev, unsigned ch, uint32_t status) {dev->ch[ch].status&=~status;}
static uint32_t mipi_dsi_brg_ll_get_interrupt_status(uint32_t *bridge) {return *bridge;}
static void mipi_dsi_brg_ll_clear_interrupt_status(uint32_t *bridge,uint32_t status) {*bridge&=~status;}
static void dw_gdma_ll_lli_set_src_addr(void *lli,uint32_t addr) {(void)lli;(void)addr;}
static void dw_gdma_ll_lli_set_src_master_port(void *lli,intptr_t addr) {(void)lli;(void)addr;}
static void esp_mipi_dsi_dma_restart(struct esp_mipi_dsi_priv_s *priv) {(void)priv;restarts++;}
static void nxsem_post(unsigned *sem) {(*sem)++;}
static void dw_gdma_shared_isr(void) {shared_calls++;hw.ch[1].status=0;}
#include "actual_isr.inc"
static uint64_t esp_camera_timing_now(void) {progress_calls++;return virtual_us;}
static void nxsig_usleep(unsigned us)
{
  virtual_us+=us;sleep_calls++;errno=EINTR;
  if(progress_irq) {
    hw.ch[0].status=1;
    esp_mipi_dsi_dma_isr(0,NULL,&g_esp_mipi_dsi);
  }
}
#include "actual_progress.inc"

static atomic_bool started, begin_done, finish;
static void *flash_thread(void *arg)
{
  (void)arg;
  atomic_store(&started,true);
  esp_display_flash_begin();
  atomic_store(&begin_done,true);
  while(!atomic_load(&finish))usleep(50);
  esp_display_flash_end();
  return NULL;
}
static void *configure_thread(void *arg)
{
  unsigned ch=(uintptr_t)arg;
  for(unsigned i=0;i<10000;i++) {
    esp_display_flash_intr_update(&hw,ch,0x80,true);
    esp_display_flash_intr_update(&hw,ch,0x80,false);
  }
  esp_display_flash_intr_update(&hw,ch,0x40,true);
  return NULL;
}
int main(void)
{
  /* Flash can precede controller clock/init. No MMIO pointer exists yet. */
  esp_display_flash_begin();
  assert(!esp_display_flash_isr_enter());
  esp_display_flash_end();
  hw.ch[0].int_sig_ena0.val=1;
  hw.ch[1].int_sig_ena0.val=3;
  hw.ch[2].int_sig_ena0.val=4;
  hw.ch[3].int_sig_ena0.val=8;
  hw.ch[1].status=0x100;
  esp_display_flash_begin();
  esp_display_flash_intr_update(&hw,0,1,true); /* First LL access after reset. */
  assert(hw.ch[0].int_sig_ena0.val==1);
  for(unsigned ch=1;ch<4;ch++)assert(hw.ch[ch].int_sig_ena0.val==0);
  esp_display_flash_begin();
  esp_display_flash_intr_update(&hw,1,1,false);
  esp_display_flash_intr_update(&hw,2,0x10,true);
  esp_display_flash_end();
  assert(hw.ch[1].int_sig_ena0.val==0);
  unsigned prior=progress_calls;
  errno=EIO;esp_display_flash_end();
  assert(errno==EIO&&progress_calls==prior+1);
  assert(hw.ch[1].int_sig_ena0.val==2&&hw.ch[2].int_sig_ena0.val==0x14);
  assert(hw.ch[1].status==0x100); /* Masking never clears a pending IRQ. */

  /* Atomic admission: begin gates new readers and waits for the old one
   * without holding a spinlock or preventing that ISR from releasing. */
  bool admitted=esp_display_flash_isr_enter();assert(admitted);
  pthread_t thread;
  assert(!pthread_create(&thread,NULL,flash_thread,NULL));
  while(!atomic_load(&started))usleep(50);
  for(;;) {
    bool reader=esp_display_flash_isr_enter();
    esp_display_flash_isr_leave(reader);
    if(!reader)break;
    usleep(50);
  }
  assert(!atomic_load(&begin_done));
  esp_display_flash_isr_leave(admitted);
  while(!atomic_load(&begin_done))usleep(50);
  assert(hw.ch[0].int_sig_ena0.val==1);
  for(unsigned ch=1;ch<4;ch++)assert(hw.ch[ch].int_sig_ena0.val==0);
  atomic_store(&finish,true);assert(!pthread_join(thread,NULL));

  esp_display_flash_begin();
  pthread_t writers[3];
  for(uintptr_t ch=1;ch<4;ch++)assert(!pthread_create(&writers[ch-1],NULL,configure_thread,(void *)ch));
  for(unsigned ch=1;ch<4;ch++)assert(!pthread_join(writers[ch-1],NULL));
  for(unsigned ch=1;ch<4;ch++)assert(hw.ch[ch].int_sig_ena0.val==0);
  esp_display_flash_end();
  assert(hw.ch[1].int_sig_ena0.val==0x42&&hw.ch[2].int_sig_ena0.val==0x54&&hw.ch[3].int_sig_ena0.val==0x48);

  uint32_t bridge=MIPI_DSI_BRG_LL_EVENT_UNDERRUN;
  char old_page,new_page;
  struct esp_mipi_dsi_priv_s *priv=&g_esp_mipi_dsi;
  priv->dw_hal.dev=&hw;priv->hal.bridge=&bridge;priv->dma_enabled=true;
  priv->fb=&old_page;priv->pending_fb=&new_page;
  assert(!esp_mipi_dsi_dma_isr(0,NULL,NULL));
  esp_display_flash_begin();
  shared_calls=0;hw.ch[1].status=0x100;
  for(unsigned i=0;i<100;i++) {
    hw.ch[0].status=1;
    assert(!esp_mipi_dsi_dma_isr(0,NULL,priv));
    assert(priv->pending_fb==&new_page&&priv->fb==&old_page);
    assert(priv->fb_done==0&&shared_calls==0&&hw.ch[1].status==0x100);
  }
  assert(frames==100&&guard_frames==100&&restarts==100);
  assert(g_esp_mipi_dsi_underruns==1&&bridge==0);
  hw.ch[0].status=0;esp_mipi_dsi_dma_isr(0,NULL,priv); /* Early return under guard. */
  assert(shared_calls==0);
  prior=sleep_calls;errno=EIO;esp_display_flash_end();
  assert(sleep_calls==prior+50&&errno==EIO); /* Real progress has a 50ms bound. */
  hw.ch[0].status=1;esp_mipi_dsi_dma_isr(0,NULL,priv);
  assert(priv->fb==&new_page&&priv->pending_fb==NULL&&priv->fb_done==1);
  assert(shared_calls==1&&hw.ch[1].status==0&&restarts==101);
  hw.ch[0].status=0;esp_mipi_dsi_dma_isr(0,NULL,priv); /* Normal early return. */
  assert(shared_calls==2);
  esp_display_flash_begin();esp_display_flash_end(); /* No leaked ISR ticket. */
  /* Real progress with two overlapping Flash callers: the first end cannot
   * publish the pending page; the last end restores IRQ work and waits for it. */
  priv->pending_fb=&old_page;progress_irq=true;
  atomic_store(&started,false);atomic_store(&begin_done,false);atomic_store(&finish,false);
  esp_display_flash_begin();
  assert(!pthread_create(&thread,NULL,flash_thread,NULL));
  while(!atomic_load(&begin_done))usleep(50);
  prior=progress_calls;esp_display_flash_end();
  assert(progress_calls==prior&&priv->pending_fb==&old_page&&priv->fb==&new_page&&priv->fb_done==1);
  atomic_store(&finish,true);assert(!pthread_join(thread,NULL));
  assert(priv->pending_fb==NULL&&priv->fb==&old_page&&priv->fb_done==2);
  pthread_mutex_destroy(&priv->dmalock);
  puts("display/Flash guard host invariants passed");
  return 0;
}
