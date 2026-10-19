/* SPDX-License-Identifier: Apache-2.0 */
/* Shared 16 kHz stereo capture and mono downmix. Worker thread only. */
#ifndef VOICE_CAPTURE_CANCELLED
#define VOICE_CAPTURE_CANCELLED() false
#endif
#ifndef VOICE_CAPTURE_LOCK
#define VOICE_CAPTURE_LOCK() music_pcm_session_lock()
#endif
struct voice_capture {
  int fd;
  mqd_t mq;
  char mqname[48];
  struct ap_buffer_s *buffers[VOICE_BUFFERS];
  unsigned count;
  bool reserved;
  bool registered;
  bool started;
  bool session_locked;
  unsigned silent_waits;
  unsigned settle;
};
static int capture_open(struct voice_capture *capture)
{
  memset(capture,0,sizeof(*capture));
  capture->fd=-1;
  capture->mq=(mqd_t)-1;
  capture->settle=VOICE_SETTLE_FRAMES;
  if(VOICE_CAPTURE_CANCELLED())return -ECANCELED;
  int ret=0;
#ifdef VOICE_CAPTURE_BACKGROUND
  if(!VOICE_CAPTURE_BACKGROUND())
#endif
    ret=glass_music_stop_wait(5000);
  if(ret) return ret;
  if(VOICE_CAPTURE_CANCELLED())return -ECANCELED;
  #ifdef SPEECH_CAPTURE_TRY
  ret=music_pcm_session_trylock();
#else
  ret=VOICE_CAPTURE_LOCK();
#endif
  if(ret) return ret;
  capture->session_locked=true;
  if(VOICE_CAPTURE_CANCELLED()){ret=-ECANCELED;goto fail;}
  capture->fd=open("/dev/audio/pcm_in0",O_RDWR);
  if(capture->fd<0) { ret=-errno; goto fail; }
  if(ioctl(capture->fd,AUDIOIOC_RESERVE,0)<0) { ret=-errno; goto fail; }
  capture->reserved=true;
  struct audio_caps_desc_s caps={0};
  caps.caps.ac_len=sizeof(caps.caps);
  caps.caps.ac_type=AUDIO_TYPE_INPUT;
  caps.caps.ac_subtype=AUDIO_FMT_PCM;
  caps.caps.ac_channels=2;
  caps.caps.ac_controls.hw[0]=VOICE_RATE;
  caps.caps.ac_controls.b[2]=16;
  if(ioctl(capture->fd,AUDIOIOC_CONFIGURE,(uintptr_t)&caps)<0) {
    ret=-errno; goto fail;
  }
  struct mq_attr attr={.mq_maxmsg=10,.mq_msgsize=sizeof(struct audio_msg_s)};
  snprintf(capture->mqname,sizeof(capture->mqname),"/ime-voice-%lx",
           (unsigned long)pthread_self());
  mq_unlink(capture->mqname);
  capture->mq=mq_open(capture->mqname,O_RDWR|O_CREAT|O_EXCL,0600,&attr);
  if(capture->mq==(mqd_t)-1) { ret=-errno; goto fail; }
  if(ioctl(capture->fd,AUDIOIOC_REGISTERMQ,(uintptr_t)capture->mq)<0) {
    ret=-errno; goto fail;
  }
  capture->registered=true;
  struct ap_buffer_info_s info={0};
  if(ioctl(capture->fd,AUDIOIOC_GETBUFFERINFO,(uintptr_t)&info)<0) {
    ret=-errno; goto fail;
  }
  capture->count=info.nbuffers<VOICE_BUFFERS?info.nbuffers:VOICE_BUFFERS;
  if(capture->count<2) { ret=-ENOBUFS; goto fail; }
  for(unsigned i=0;i<capture->count;i++) {
    struct audio_buf_desc_s desc={.numbytes=VOICE_BYTES,
                                  .u.pbuffer=&capture->buffers[i]};
    int size=ioctl(capture->fd,AUDIOIOC_ALLOCBUFFER,(uintptr_t)&desc);
    if(size!=sizeof(desc)||!capture->buffers[i]) {
      ret=size<0?-errno:-ENOMEM; goto fail;
    }
  }
  if(VOICE_CAPTURE_CANCELLED()){ret=-ECANCELED;goto fail;}
  if(ioctl(capture->fd,AUDIOIOC_START,0)<0) { ret=-errno; goto fail; }
  capture->started=true;
  for(unsigned i=0;i<capture->count;i++) {
    struct ap_buffer_s *buffer=capture->buffers[i];
    buffer->nbytes=buffer->nmaxbytes; buffer->curbyte=0; buffer->flags=0;
    struct audio_buf_desc_s desc={.numbytes=buffer->nbytes,.u.buffer=buffer};
    if(ioctl(capture->fd,AUDIOIOC_ENQUEUEBUFFER,(uintptr_t)&desc)<0) {
      ret=-errno; goto fail;
    }
  }
  return 0;
fail:
  return ret;
}

static void capture_close(struct voice_capture *capture)
{
  if(capture->fd>=0&&capture->started) ioctl(capture->fd,AUDIOIOC_STOP,0);
  if(capture->fd>=0) for(unsigned i=0;i<VOICE_BUFFERS;i++)
    if(capture->buffers[i]) {
      struct audio_buf_desc_s desc={.u.buffer=capture->buffers[i]};
      ioctl(capture->fd,AUDIOIOC_FREEBUFFER,(uintptr_t)&desc);
    }
  if(capture->fd>=0&&capture->registered)
    ioctl(capture->fd,AUDIOIOC_UNREGISTERMQ,(uintptr_t)capture->mq);
  if(capture->fd>=0&&capture->reserved) ioctl(capture->fd,AUDIOIOC_RELEASE,0);
  if(capture->fd>=0) close(capture->fd);
  if(capture->mq!=(mqd_t)-1) {
    mq_close(capture->mq);
    mq_unlink(capture->mqname);
  }
  if(capture->session_locked) music_pcm_session_unlock();
  capture->fd=-1;
}

static unsigned magnitude16(int value)
{ return value<0?(unsigned)(-value):(unsigned)value; }

static unsigned downmix(int16_t *mono,const int16_t *stereo,unsigned frames)
{
  uint64_t left=0,right=0;
  for(unsigned i=0;i<frames;i++) {
    left+=magnitude16(stereo[i*2]);
    right+=magnitude16(stereo[i*2+1]);
  }
  unsigned channel=left>right*2+256?1:right>left*2+256?2:0;
  unsigned peak=0;
  for(unsigned i=0;i<frames;i++) {
    int value=channel==1?stereo[i*2]:channel==2?stereo[i*2+1]:
      ((int)stereo[i*2]+(int)stereo[i*2+1])/2;
    mono[i]=(int16_t)value;
    unsigned level=magnitude16(value);
    if(level>peak) peak=level;
  }
  return peak;
}

/* Returns mono frames, zero for a timed wait, or a negative errno. */
static int capture_next(struct voice_capture *capture,int16_t mono[VOICE_FRAMES],
                        unsigned *level)
{
  struct timespec until;
  clock_gettime(CLOCK_REALTIME,&until);
  until.tv_nsec+=100000000;
  if(until.tv_nsec>=1000000000) { until.tv_sec++; until.tv_nsec-=1000000000; }
  struct audio_msg_s message;
  ssize_t n;
  do { n=mq_timedreceive(capture->mq,(char *)&message,sizeof(message),NULL,&until); }
  while(n<0&&errno==EINTR);
  if(n<0&&errno==ETIMEDOUT)
    return ++capture->silent_waits<30?0:-ETIMEDOUT;
  if(n!=sizeof(message)) return n<0?-errno:-EIO;
  if(message.msg_id==AUDIO_MSG_IOERR||message.msg_id==AUDIO_MSG_COMPLETE)
    return -EIO;
  if(message.msg_id!=AUDIO_MSG_DEQUEUE) return 0;
  struct ap_buffer_s *buffer=message.u.ptr;
  bool known=false;
  for(unsigned i=0;i<capture->count;i++) if(buffer==capture->buffers[i]) known=true;
  if(!known||!buffer->nbytes||buffer->nbytes>VOICE_BYTES||buffer->nbytes%4)
    return -EIO;
  capture->silent_waits=0;
  unsigned frames=buffer->nbytes/4;
  *level=downmix(mono,(const int16_t *)buffer->samp,frames);
  buffer->curbyte=0; buffer->flags=0; buffer->nbytes=buffer->nmaxbytes;
  struct audio_buf_desc_s next={.numbytes=buffer->nbytes,.u.buffer=buffer};
  if(ioctl(capture->fd,AUDIOIOC_ENQUEUEBUFFER,(uintptr_t)&next)<0) return -errno;
  if(capture->settle) {
    unsigned skip=capture->settle<frames?capture->settle:frames;
    capture->settle-=skip;
    memmove(mono,mono+skip,(frames-skip)*sizeof(*mono));
    frames-=skip;
  }
  return (int)frames;
}

