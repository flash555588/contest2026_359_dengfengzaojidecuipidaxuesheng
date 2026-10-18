"""Check the production completion path with both NuttX callback ABIs."""
from pathlib import Path
import subprocess,tempfile
from render_all_ui import function
repo=Path(__file__).resolve().parents[1]
source=repo/'app/espdl-quickapp/overlay/nuttx/drivers/audio/es8311.c'
body=function(source.read_text(),'es8311_returnbuffers')
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#define FAR
#define AUDIO_APB_FINAL 1
#define AUDIO_CALLBACK_IOERR 2
#define AUDIO_CALLBACK_DEQUEUE 3
#define DEBUGASSERT assert
#define audinfo(...) ((void)0)
typedef int irqstate_t;
struct ap_buffer_s {unsigned flags;int refs;};
struct queue {struct ap_buffer_s *item;};
static int depth,events[4],count;
static int enter_critical_section(void){assert(!depth);depth=1;return 0;}
static void leave_critical_section(int f){(void)f;assert(depth);depth=0;}
static void *dq_peek(struct queue *q){return q->item;}
static void *dq_remfirst(struct queue *q){void *p=q->item;q->item=NULL;return p;}
static bool dq_empty(struct queue *q){return !q->item;}
static void apb_free(struct ap_buffer_s *p){assert(p->refs==2);p->refs--;}
#ifdef CONFIG_AUDIO_MULTI_SESSION
typedef void (*callback_t)(void *,int,struct ap_buffer_s *,int,void *);
#else
typedef void (*callback_t)(void *,int,struct ap_buffer_s *,int);
#endif
struct es8311_dev_s {struct queue doneq,pendq;int result,inflight;bool terminating;
 struct {callback_t upper;void *priv;} dev;};
#ifdef CONFIG_AUDIO_MULTI_SESSION
static void upper(void *u,int reason,struct ap_buffer_s *p,int status,void *session)
#else
static void upper(void *u,int reason,struct ap_buffer_s *p,int status)
#endif
{
#ifdef CONFIG_AUDIO_MULTI_SESSION
 assert(!session);
#endif
 assert(!depth && u==p && count<4);events[count++]=reason;
 if(reason==AUDIO_CALLBACK_IOERR)assert(status<0 && p->refs==2);
 else assert(reason==AUDIO_CALLBACK_DEQUEUE && p->refs==1);
}
'''
suffix=r'''
int main(void){
 for(int failed=0;failed<2;failed++)for(int final=0;final<2;final++){
  struct ap_buffer_s apb={.refs=2,.flags=final?AUDIO_APB_FINAL:0};
  struct es8311_dev_s p={.doneq={&apb},.result=failed?-12:0,.dev={upper,&apb}};
  count=0;es8311_returnbuffers(&p);
  assert(!depth && !p.doneq.item && p.terminating==(bool)final && apb.refs==1);
  if(failed)assert(count==2 && events[0]==AUDIO_CALLBACK_IOERR && events[1]==AUDIO_CALLBACK_DEQUEUE);
  else assert(count==1 && events[0]==AUDIO_CALLBACK_DEQUEUE);
 }
 puts("ES8311_RETURN_PASS: error before dequeue; success has no error; refs and critical sections; final buffer");
}
'''
with tempfile.TemporaryDirectory(prefix='es8311-return-') as tmp:
 p=Path(tmp)/'test.c';p.write_text(prefix+body+suffix)
 for flags in [[],['-DCONFIG_AUDIO_MULTI_SESSION']]:
  exe=p.with_suffix('')
  subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',*flags,str(p),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True,timeout=15)
