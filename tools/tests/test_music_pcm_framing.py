"""Exercise the production PCM framing functions with a fake audio queue."""
from pathlib import Path
import hashlib, re, subprocess, tempfile
repo=Path(__file__).resolve().parents[2]
source=repo/'app/espdl-quickapp/overlay/apps/system/desktop/glass_music_pcm.c'
text=source.read_text()
parts=[text[text.index('static int pcm_message'):text.index('int music_pcm_volume')],
       text[text.index('static int pcm_submit'):text.index('int music_pcm_pause')],
       text[text.index('int music_pcm_drain'):text.index('void music_pcm_close')]]
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#define MUSIC_PCM_BUFFERS 4
#define MUSIC_PCM_FRAMES 1152
#define AUDIO_MSG_DEQUEUE 1
#define AUDIO_MSG_IOERR 2
#define AUDIO_MSG_COMPLETE 3
#define AUDIOIOC_ENQUEUEBUFFER 1
#define AUDIOIOC_START 2
struct ap_buffer_s {unsigned nbytes,curbyte,flags;unsigned char *samp;};
struct audio_msg_s {int msg_id;union {void *ptr;} u;};
struct audio_buf_desc_s {unsigned numbytes;union {struct ap_buffer_s *buffer;} u;};
struct music_pcm {int fd,mq;struct ap_buffer_s *buffers[4];bool queued[4],started;
 unsigned rate,pending,played_samples,submitted_samples[4],staged_frames,staged_index;};
static struct ap_buffer_s *fifo[1024];static unsigned head,tail,used,starts;
static int message_id=AUDIO_MSG_DEQUEUE;
static int16_t captured[65536];
static int ioctl(int fd,unsigned request,...);
static ssize_t mq_timedreceive(int mq,char *out,size_t size,void *priority,const struct timespec *deadline);
static void board_music_amplifier(bool enable) {(void)enable;}
'''
suffix=r'''
#include <stdarg.h>
static int ioctl(int fd,unsigned request,...) {
 (void)fd;
 if(request==AUDIOIOC_START){starts++;return 0;}
 assert(request==AUDIOIOC_ENQUEUEBUFFER);
 va_list args;va_start(args,request);
 struct audio_buf_desc_s *d=(void *)va_arg(args,unsigned long);va_end(args);
 struct ap_buffer_s *b=d->u.buffer;
 assert(b->nbytes && b->nbytes<=MUSIC_PCM_FRAMES*4 && b->nbytes%64==0 && b->curbyte==0);
 assert(used+b->nbytes/2<=65536);
 memcpy(captured+used,b->samp,b->nbytes);used+=b->nbytes/2;
 fifo[tail++]=b;return 0;
}
static ssize_t mq_timedreceive(int mq,char *out,size_t size,void *priority,const struct timespec *deadline) {
 (void)mq;(void)priority;(void)deadline;assert(size==sizeof(struct audio_msg_s));assert(head<tail);
 struct audio_msg_s msg={.msg_id=message_id,.u.ptr=fifo[head++]};
 memcpy(out,&msg,sizeof(msg));return sizeof(msg);
}
int main(void) {
 unsigned totals[]={1,15,16,17,591,542,1151,1152,1153,20000};
 unsigned splits[]={1,15,16,17,542,591,1151,1152,4096};
 unsigned cases=0;
 for(unsigned t=0;t<sizeof(totals)/sizeof(totals[0]);t++)
 for(unsigned s=0;s<sizeof(splits)/sizeof(splits[0]);s++) {
  struct music_pcm pcm={.fd=1};struct ap_buffer_s buffers[4];
  uint32_t storage[4][MUSIC_PCM_FRAMES+2];int16_t input[20000];
  for(unsigned i=0;i<4;i++) {
   storage[i][0]=0xdeadbeef;storage[i][MUSIC_PCM_FRAMES+1]=0xcafebabe;
   buffers[i]=(struct ap_buffer_s){.samp=(unsigned char *)&storage[i][1]};pcm.buffers[i]=&buffers[i];
  }
  for(unsigned i=0;i<totals[t];i++)input[i]=(int16_t)(i*97+1);
  head=tail=used=starts=0;
  for(unsigned offset=0;offset<totals[t];) {
   unsigned n=totals[t]-offset;if(n>splits[s])n=splits[s];
   assert(!music_pcm_write(&pcm,input+offset,n,1));offset+=n;
  }
  assert(!music_pcm_drain(&pcm));assert(pcm.played_samples==totals[t]);
  assert(!pcm.pending && !pcm.staged_frames && starts==1);
  assert(used==((totals[t]+15)&~15u)*2);
  for(unsigned i=0;i<totals[t];i++)assert(captured[2*i]==input[i] && captured[2*i+1]==input[i]);
  for(unsigned i=totals[t]*2;i<used;i++)assert(captured[i]==0);
  for(unsigned i=0;i<4;i++)assert(storage[i][0]==0xdeadbeef && storage[i][MUSIC_PCM_FRAMES+1]==0xcafebabe);
  cases++;
 }
 struct ap_buffer_s rejected={.nbytes=0};
 struct music_pcm bad={.buffers={&rejected},.queued={true},.pending=1,.submitted_samples={576}};
 head=tail=0;fifo[tail++]=&rejected;message_id=AUDIO_MSG_IOERR;
 assert(pcm_message(&bad)==-EIO && !bad.played_samples);
 printf("PCM_FRAMING_PASS cases=%u; sample order, stereo slots, 64-byte DMA alignment, tail silence, guards, queue drain, rejected DMA not counted\n",cases);
}
'''
with tempfile.TemporaryDirectory(prefix='pcm-framing-') as directory:
    header=source.with_suffix('.h').read_text()
    frames=int(re.search(r'^#define MUSIC_PCM_FRAMES (\d+)$',header,re.M)[1])
    prefix=re.sub(r'#define MUSIC_PCM_FRAMES \d+',f'#define MUSIC_PCM_FRAMES {frames}',prefix)
    path=Path(directory)/'check.c';path.write_text(prefix+'\n'.join(parts)+suffix)
    subprocess.run(['gcc','-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined','-g',str(path),'-o',str(path.with_suffix(''))],check=True)
    result=subprocess.check_output([str(path.with_suffix(''))],text=True)
    print('production_source_sha256='+hashlib.sha256(source.read_bytes()).hexdigest())
    print(result,end='')
