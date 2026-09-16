"""One-time separation of USB acquisition from the ESP-DL worker."""
from pathlib import Path
p=Path(__file__).resolve().parent.parent/'04-v3-20260913/espdl-quickapp/overlay/apps/system/desktop'
s=(p/'qpk_espdl_service.c').read_text()
begin=s.index('static int capture(')
end=s.index('static void *worker(',begin)
capture=s[begin:end]
capture=capture.replace('static int capture(const struct qpk_dl_request *request,uint16_t *pixels)',
'''struct capture_context { int fd; struct uvc_camera_start mode; uint8_t *input; void *decoder; };
void qpk_dl_capture_close(void *context)
{
  struct capture_context *c=context;
  if (!c) return;
  ioctl(c->fd,UVCIOC_STOP,0);close(c->fd);
  qpk_mjpeg_destroy(c->decoder);free(c->input);free(c);
}
int qpk_dl_capture_open(const struct qpk_dl_request *request,void **context)''')
capture=capture.replace('  char path[24];','  *context=NULL;\n  char path[24];',1)
cut=capture.index('  uint64_t deadline=')
finish=capture.index('done:\n',cut)
loop=capture[cut:finish]
capture=capture[:cut]+'''  struct capture_context *c=malloc(sizeof(*c));
  if (!c) {ret=-ENOMEM;goto done;}
  *c=(struct capture_context){fd,mode,input,decoder};
  *context=c;free(caps);return 0;
'''+capture[finish:]
capture+='''int qpk_dl_capture_next(void *context,uint16_t *pixels)
{
  struct capture_context *c=context;
  int fd=c->fd,ret;
  struct uvc_camera_start mode=c->mode;
  uint8_t *input=c->input;void *decoder=c->decoder;
'''+loop+'''  return ret;
}
'''
headers=s[:s.index('static pthread_mutex_t')]
clock=s[s.index('static uint64_t now_ms'):s.index('bool qpk_dl_should_cancel')]
(p/'qpk_espdl_capture.c').write_text(headers+clock+capture)
