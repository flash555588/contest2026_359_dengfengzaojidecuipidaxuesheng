/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <nuttx/audio/es8311_barge.h>
#include <nuttx/audio/i2s_observer.h>
#include "glass_custom_wake.h"
#define VOICE_RATE 16000
#define VOICE_BYTES 960
#define VOICE_FRAMES 240
static atomic_bool speech_cancel;
static pthread_mutex_t speech_lock=PTHREAD_MUTEX_INITIALIZER;
static struct {unsigned wake_evaluations;float wake_score;} speech;
static atomic_uint events,submitted,callbacks,allocations,models;
static bool fail_model,fail_apb,fail_submit,hang_rx,wrong_rate;
static unsigned wake_after;
static struct i2s_dev_s i2s;
static pthread_t dma_thread;
static bool thread_exists;
static atomic_bool retire;
static struct ap_buffer_s *dma_buffer;
static i2s_callback_t dma_callback;
static void *dma_arg;
struct glass_custom_wake {unsigned feeds;};
static bool speech_cancelled(void *unused){(void)unused;return atomic_load(&speech_cancel);}
static unsigned downmix(int16_t *mono,const int16_t *stereo,unsigned n)
{for(unsigned i=0;i<n;i++)mono[i]=stereo[i*2];return 0;}
static void glass_speech_capture_woke(void){atomic_fetch_add(&events,1);}
int glass_custom_wake_create(const char *root,struct glass_custom_wake **out)
{(void)root;*out=NULL;if(fail_model)return -ENOENT;*out=calloc(1,sizeof(**out));assert(*out);atomic_fetch_add(&models,1);return 0;}
void glass_custom_wake_destroy(struct glass_custom_wake *wake)
{if(wake){free(wake);atomic_fetch_sub(&models,1);}}
int glass_custom_wake_feed(struct glass_custom_wake *wake,const int16_t *pcm,size_t n)
{assert(pcm&&n==VOICE_FRAMES);return ++wake->feeds==wake_after?1:0;}
unsigned glass_custom_wake_evaluations(const struct glass_custom_wake *wake){return wake->feeds;}
float glass_custom_wake_score(const struct glass_custom_wake *wake){(void)wake;return .9f;}
int apb_alloc(struct audio_buf_desc_s *desc)
{if(fail_apb)return -ENOMEM;*desc->u.pbuffer=calloc(1,sizeof(struct ap_buffer_s));assert(*desc->u.pbuffer);
 (*desc->u.pbuffer)->nmaxbytes=desc->numbytes;atomic_init(&(*desc->u.pbuffer)->refs,1);atomic_fetch_add(&allocations,1);return sizeof(*desc);}
void apb_free(struct ap_buffer_s *buffer)
{assert(atomic_load(&buffer->refs)>0);if(atomic_fetch_sub(&buffer->refs,1)==1){free(buffer);atomic_fetch_sub(&allocations,1);}}
static void *dma(void *arg)
{(void)arg;usleep(1000);while(hang_rx&&!atomic_load(&retire))usleep(1000);
 int ret=atomic_load(&retire)?-ECANCELED:0;dma_buffer->nbytes=ret?0:VOICE_BYTES;
 dma_callback(&i2s,dma_buffer,dma_arg,ret);apb_free(dma_buffer);atomic_fetch_add(&callbacks,1);return NULL;}
int fake_receive(struct i2s_dev_s *dev,struct ap_buffer_s *buffer,i2s_callback_t callback,void *arg,unsigned timeout)
{assert(dev==&i2s&&!timeout);if(thread_exists){pthread_join(dma_thread,NULL);thread_exists=false;}
 if(fail_submit)return -ENOMEM;
 dma_buffer=buffer;dma_callback=callback;dma_arg=arg;atomic_store(&retire,false);atomic_fetch_add(&buffer->refs,1);
 assert(!pthread_create(&dma_thread,NULL,dma,NULL));thread_exists=true;atomic_fetch_add(&submitted,1);return 0;}
int fake_i2s_ioctl(struct i2s_dev_s *dev,int cmd,unsigned long arg)
{assert(dev==&i2s&&!arg&&(cmd==I2SIOC_RXSTOP||cmd==AUDIOIOC_STOP));atomic_store(&retire,true);
 if(thread_exists){pthread_join(dma_thread,NULL);thread_exists=false;}return 0;}
static int fake_ioctl(int fd,int cmd,uintptr_t arg)
{assert(fd==17&&cmd==ES8311IOC_BARGEIN);struct es8311_bargein *tap=(void *)arg;tap->i2s=&i2s;tap->rate=wrong_rate?22050:16000;return 0;}
#define ioctl fake_ioctl
#include "glass_speech_barge.inc"
static void reset(void)
{assert(!thread_exists&&!atomic_load(&allocations)&&!atomic_load(&models));
 atomic_store(&speech_cancel,false);atomic_store(&events,0);atomic_store(&submitted,0);atomic_store(&callbacks,0);
 fail_model=fail_apb=fail_submit=hang_rx=wrong_rate=false;wake_after=0;}
int main(void)
{
 reset();wake_after=3;struct speech_barge *barge=speech_barge_start(17);assert(barge);
 for(unsigned i=0;i<500&&!atomic_load(&events);i++)usleep(1000);
 assert(atomic_load(&events)==1&&atomic_load(&speech_cancel));speech_barge_stop(barge);
 assert(atomic_load(&submitted)==atomic_load(&callbacks));
 reset();barge=speech_barge_start(17);assert(barge);while(atomic_load(&submitted)<2)usleep(1000);
 speech_barge_stop(barge);assert(!atomic_load(&events)&&atomic_load(&submitted)==atomic_load(&callbacks));
 reset();hang_rx=true;barge=speech_barge_start(17);assert(barge);usleep(350000);speech_barge_stop(barge);
 assert(atomic_load(&submitted)==1&&atomic_load(&callbacks)==1&&!atomic_load(&events));
 for(unsigned failure=0;failure<3;failure++){
   reset();fail_model=failure==0;fail_apb=failure==1;fail_submit=failure==2;
   barge=speech_barge_start(17);assert(barge);usleep(30000);speech_barge_stop(barge);
   assert(!atomic_load(&events));
 }
 reset();wrong_rate=true;assert(!speech_barge_start(17));reset();
 puts("SPEECH_BARGE_OK: actual observer loop; microphone event cancels TTS; callback retirement before free; stop, timeout, missing model, OOM, unsupported rate; hardware/AEC not tested");
 return 0;
}
