/* SPDX-License-Identifier: Apache-2.0 */
/* One microphone observer owned by the currently reserved playback session.
 * No second audio reservation, codec reset or sample-rate change is allowed. */
#include <nuttx/audio/es8311_barge.h>
#include <nuttx/audio/i2s_observer.h>
#include <semaphore.h>
struct speech_barge {
  struct i2s_dev_s *i2s;
  pthread_t thread;
  pthread_mutex_t lock;
  sem_t completed;
  atomic_bool stop;
  int result;
};
static void speech_barge_received(struct i2s_dev_s *i2s,struct ap_buffer_s *apb,
                                 void *arg,int result)
{
  (void)i2s;(void)apb;
  struct speech_barge *barge=arg;
  barge->result=result;
  sem_post(&barge->completed);
}
static int speech_barge_rxstop(struct speech_barge *barge)
{
  int ret=I2S_IOCTL(barge->i2s,I2SIOC_RXSTOP,0);
  /* A broken RX must not leave a callback pointing into freed task state.
   * On a lower-half fault, abort the owned playback too. */
  if(ret<0)ret=I2S_IOCTL(barge->i2s,AUDIOIOC_STOP,0);
  return ret;
}
static void *speech_barge_worker(void *arg)
{
#if defined(__riscv) && defined(CONFIG_ARCH_CHIP_ESP32P4)
  __asm__ volatile("csrwi 0x7f2, 1\ncsrwi 0x7f1, 1" ::: "memory");
#endif
  struct speech_barge *barge=arg;
  struct glass_custom_wake *wake=NULL;
  struct ap_buffer_s *buffer=NULL;
  struct audio_buf_desc_s desc={.numbytes=VOICE_BYTES,.u.pbuffer=&buffer};
  int ret=glass_custom_wake_create("/sdcard/speech",&wake);
  if(!ret){ret=apb_alloc(&desc);if(ret>=0)ret=buffer?0:-ENOMEM;}
  unsigned frames=0;
  while(!ret&&!atomic_load(&barge->stop)&&!speech_cancelled(NULL)){
    pthread_mutex_lock(&barge->lock);
    if(atomic_load(&barge->stop)){pthread_mutex_unlock(&barge->lock);break;}
    buffer->nbytes=buffer->nmaxbytes;buffer->curbyte=0;buffer->flags=0;
    /* This P4 lower half currently stores but does not implement the generic
     * I2S timeout. Retire RX explicitly on a bounded semaphore timeout. */
    ret=I2S_RECEIVE(barge->i2s,buffer,speech_barge_received,barge,0);
    pthread_mutex_unlock(&barge->lock);
    if(ret<0)break;
    if(!frames)printf("[speech] barge listening rate=%u\n",VOICE_RATE);
    struct timespec end;clock_gettime(CLOCK_REALTIME,&end);
    end.tv_nsec+=250000000;
    if(end.tv_nsec>=1000000000){end.tv_nsec-=1000000000;end.tv_sec++;}
    int waited;
    do{waited=sem_timedwait(&barge->completed,&end);}while(waited<0&&errno==EINTR);
    if(waited<0){
      ret=-errno;
      pthread_mutex_lock(&barge->lock);speech_barge_rxstop(barge);pthread_mutex_unlock(&barge->lock);
      /* STOP synchronizes the actual callback; timeout alone never transfers
       * DMA ownership back to this task. */
      while(sem_wait(&barge->completed)<0&&errno==EINTR){}
      break;
    }
    ret=barge->result;
    if(ret<0||atomic_load(&barge->stop)||speech_cancelled(NULL))break;
    if(!buffer->nbytes||buffer->nbytes>VOICE_BYTES||buffer->nbytes%4){ret=-EIO;break;}
    int16_t mono[VOICE_FRAMES];unsigned n=buffer->nbytes/4;
    downmix(mono,(const int16_t *)buffer->samp,n);
    frames+=n;int event=glass_custom_wake_feed(wake,mono,n);
    if(event<0){ret=event;break;}
    pthread_mutex_lock(&speech_lock);
    speech.wake_evaluations=glass_custom_wake_evaluations(wake);
    speech.wake_score=glass_custom_wake_score(wake);
    pthread_mutex_unlock(&speech_lock);
    if(event>0){
      atomic_store(&speech_cancel,true);
      glass_speech_capture_woke();
      printf("[speech] barge wake frames=%u\n",frames);
      break;
    }
  }
  if(buffer)apb_free(buffer);
  glass_custom_wake_destroy(wake);
  printf("[speech] barge released frames=%u result=%d\n",frames,ret);
  return NULL;
}
static struct speech_barge *speech_barge_start(int fd)
{
  struct es8311_bargein tap={0};
  if(ioctl(fd,ES8311IOC_BARGEIN,(uintptr_t)&tap)<0||!tap.i2s||tap.rate!=VOICE_RATE){
    printf("[speech] barge unavailable codec/rate\n");return NULL;
  }
  struct speech_barge *barge=calloc(1,sizeof(*barge));if(!barge)return NULL;
  barge->i2s=tap.i2s;atomic_init(&barge->stop,false);
  int ret=pthread_mutex_init(&barge->lock,NULL);
  if(ret){free(barge);return NULL;}
  if(sem_init(&barge->completed,0,0)){pthread_mutex_destroy(&barge->lock);free(barge);return NULL;}
  pthread_attr_t attr;ret=pthread_attr_init(&attr);
  if(!ret){
    ret=pthread_attr_setstacksize(&attr,96*1024);
    if(!ret)ret=pthread_create(&barge->thread,&attr,speech_barge_worker,barge);
    pthread_attr_destroy(&attr);
  }
  if(ret){sem_destroy(&barge->completed);pthread_mutex_destroy(&barge->lock);free(barge);return NULL;}
  return barge;
}
static void speech_barge_stop(struct speech_barge *barge)
{
  if(!barge)return;
  pthread_mutex_lock(&barge->lock);
  atomic_store(&barge->stop,true);
  speech_barge_rxstop(barge);
  pthread_mutex_unlock(&barge->lock);
  pthread_join(barge->thread,NULL);
  sem_destroy(&barge->completed);pthread_mutex_destroy(&barge->lock);free(barge);
}
