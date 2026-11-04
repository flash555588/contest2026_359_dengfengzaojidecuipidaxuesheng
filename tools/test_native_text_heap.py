"""Host tests for the generic native module loader.

Executes the production executable-heap allocator (mocked MMU/cache) and the
production path/ABI/ownership policy in native_host.c (mocked ELF loader).
No physical MMU, PMP or real ELF relocation is claimed.
"""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output;out.mkdir(parents=True,exist_ok=True)
native=Path(__file__).resolve().parents[1]/'app/espdl-quickapp/overlay/apps/system/desktop/native'
stubs=out/'include';(stubs/'lvgl').mkdir(parents=True,exist_ok=True)
headers={
'malloc.h':'''#include <stddef.h>
struct mallinfo {size_t fordblks;};struct mallinfo mallinfo(void);
void *memalign(size_t,size_t);
''',
'esp_cache.h':'''#include <stddef.h>
#define ESP_OK 0
#define ESP_CACHE_MSYNC_FLAG_DIR_C2M 4
#define ESP_CACHE_MSYNC_FLAG_TYPE_DATA 16
#define ESP_CACHE_MSYNC_FLAG_INVALIDATE 1
int esp_cache_msync(void *,size_t,int);
''',
'esp_mmu_map.h':'''#include <stddef.h>
#include <stdint.h>
typedef uint32_t esp_paddr_t;typedef int mmu_target_t;
#define MMU_TARGET_PSRAM0 2
#define MMU_MEM_CAP_EXEC 1
#define MMU_MEM_CAP_READ 2
int esp_mmu_vaddr_to_paddr(void *,esp_paddr_t *,mmu_target_t *);
int esp_mmu_map(esp_paddr_t,size_t,int,int,int,void **);
int esp_mmu_unmap(void *);
''',
'lvgl/lvgl.h':'typedef struct lv_obj lv_obj_t;\n',
'quickjs.h':'typedef struct JSContext JSContext;typedef struct {int tag;} JSValue;\n'}
for name,text in headers.items():(stubs/name).write_text('#pragma once\n'+text)

heap=r'''
#include <assert.h>
#include <stdlib.h>
#include <malloc.h>
#include <esp_mmu_map.h>
#define SLOTS 8
static unsigned fault,allocated,released,maps,unmaps;
static void *live[64];static esp_paddr_t phys[64];static unsigned count,next_alias;
struct mallinfo mallinfo(void){return (struct mallinfo){0};}
void *memalign(size_t a,size_t n){if(fault==1)return NULL;void *p;assert(!posix_memalign(&p,a,n));
 live[count]=p;phys[count]=0x100000u*(count+1);count++;allocated++;return p;}
static int find(void *p){for(unsigned i=0;i<count;i++)if(live[i]==p)return i;return -1;}
static void release(void *p){if(!p)return;int i=find(p);assert(i>=0);live[i]=NULL;released++;free(p);}
int esp_mmu_vaddr_to_paddr(void *v,esp_paddr_t *p,mmu_target_t *t){int i=find(v);assert(i>=0);*p=phys[i];*t=2;return fault==2?-1:0;}
int esp_cache_msync(void *v,size_t n,int flags){(void)v;(void)n;assert(flags==21);return fault==3?-1:0;}
int esp_mmu_map(esp_paddr_t p,size_t n,int target,int caps,int flags,void **o){
 assert(p&&!(n&65535)&&target==2&&caps==3&&!flags);maps++;
 *o=(void *)(uintptr_t)(fault==5?0x48000000u:0x4a000000u+0x100000u*next_alias++);return fault==4?-1:0;}
static int unmap_fail;
int esp_mmu_unmap(void *p){(void)p;unmaps++;return fault==6||unmap_fail?-1:0;}
int esp32p4_internal_call(int (*fn)(void *),void *arg){return fn(arg);}
__asm__(".globl _native_exec_start\n.set _native_exec_start,0x4a000000\n.globl _native_exec_end\n.set _native_exec_end,0x4c000000\n");
#define free release
'''+'#include "'+(native/'native_text_heap.c').as_posix()+'"\n'+r'''
#undef free
int main(void){
 assert(!up_textheap_memalign(0,1)&&!up_textheap_memalign(3,1));
 assert(!up_textheap_memalign(131072,1)&&!up_textheap_memalign(16,0));
 /* Only the 32 MiB executable window bounds a module; no policy cap. */
 assert(!up_textheap_memalign(16,32u*1024u*1024u+1)&&!up_textheap_memalign(16,(size_t)-1));
 assert(!allocated&&!maps);
 for(fault=1;fault<=5;fault++){assert(!up_textheap_memalign(16,65537));assert(allocated==released);}
 fault=0;
 /* Several modules resident at once, each in its own mapping. */
 void *p[SLOTS];
 for(unsigned i=0;i<SLOTS;i++){p[i]=up_textheap_memalign(16,65537);assert(p[i]);
  assert(up_textheap_heapmember(p[i])&&up_textheap_heapmember((char *)p[i]+131071));
  assert(!up_textheap_heapmember((char *)p[i]+131072));}
 assert(!up_textheap_memalign(16,1)); /* slot table full */
 up_textheap_free((char *)p[3]+1);assert(up_textheap_heapmember(p[3]));
 up_textheap_free(p[3]);assert(!up_textheap_heapmember(p[3])&&up_textheap_heapmember(p[4]));
 void *q=up_textheap_memalign(16,1);assert(q&&up_textheap_heapmember(q)); /* freed slot reused */
 /* Unmap failure keeps the mapping and its backing. */
 fault=6;up_textheap_free(q);assert(up_textheap_heapmember(q));
 fault=0;up_textheap_free(q);assert(!up_textheap_heapmember(q));
 for(unsigned i=0;i<SLOTS;i++)if(i!=3)up_textheap_free(p[i]);
 fault=0;assert(allocated==released);
 /* Out-of-window map whose rollback unmap fails: backing is retained and
  * that slot never reopens; the other slots keep working. */
 fault=5;unmap_fail=1;assert(!up_textheap_memalign(16,1));assert(allocated==released+1);
 fault=0;unmap_fail=0;
 for(unsigned i=0;i<SLOTS-1;i++){p[i]=up_textheap_memalign(16,1);assert(p[i]);}
 assert(!up_textheap_memalign(16,1));
 for(unsigned i=0;i<SLOTS-1;i++)up_textheap_free(p[i]);
 assert(allocated==released+1);
 return 0;
}
'''
(out/'heap.c').write_text(heap)

host=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include "native_host.h"
#define ABI "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define OTHER "1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
static int opened,closed,close_result,quiesced,attached,detached;static bool quiesce_ok=true;
static bool attach(JSContext *c,JSValue s,lv_obj_t *r,const char *p){(void)c;(void)s;(void)r;assert(!strcmp(p,"com.example.app"));attached++;return true;}
static void detach(void){detached++;}
static bool quiesce(void){quiesced++;return quiesce_ok;}
static struct ov_native_module api={OV_NATIVE_MODULE_ABI,sizeof(struct ov_native_module),ABI,"com.example.app",attach,detach,quiesce};
static const struct ov_native_module *query(void){return &api;}
static void *open_m(const char *path,const char *name){assert(strstr(path,"/native/module.elf")&&!strcmp(name,"ovnative0"));opened++;return &opened;}
static ov_native_module_query_fn query_m(void *h){(void)h;return query;}
static int close_m(void *h){(void)h;closed++;return close_result;}
static const struct ov_native_loader loader={open_m,query_m,close_m};
static void put(const char *path,const char *text){FILE *f=fopen(path,"wb");assert(f);fputs(text,f);fclose(f);}
int main(void){
 char m[OV_NATIVE_PATH_MAX],a[OV_NATIVE_PATH_MAX];
 mkdir(ROOT "/sd",0755);mkdir(ROOT "/sd/app",0755);mkdir(ROOT "/sd/plain",0755);mkdir(ROOT "/sd/app/native",0755);
 put(ROOT "/sd/app/native/module.elf","ELF");put(ROOT "/sd/app/native/host.abi",ABI "\n");
 assert(ov_native_locate(ROOT "/sd/app/app.js",m,a)==OV_NATIVE_FOUND);
 assert(!strcmp(m,ROOT "/sd/app/native/module.elf")&&!strcmp(a,ROOT "/sd/app/native/host.abi"));
 assert(ov_native_locate(ROOT "/sd/plain/app.js",m,a)==OV_NATIVE_NONE);   /* plain Quick App */
 assert(ov_native_locate(ROOT "/data/missing/app.js",m,a)==OV_NATIVE_NONE);
 assert(ov_native_locate("builtin:/camera/app.js",m,a)==OV_NATIVE_NONE);
 assert(ov_native_locate(ROOT "/other/app/app.js",m,a)==OV_NATIVE_NONE);
 assert(ov_native_locate(ROOT "/sd/../sd/app/app.js",m,a)==OV_NATIVE_NONE);
 assert(ov_native_locate(ROOT "/sd/.hidden/app.js",m,a)==OV_NATIVE_NONE);
 assert(ov_native_locate(ROOT "/sd/app/sub/app.js",m,a)==OV_NATIVE_NONE);
 assert(ov_native_locate(ROOT "/sd/app/main.js",m,a)==OV_NATIVE_NONE);
 assert(ov_native_locate(NULL,m,a)==OV_NATIVE_NONE);
 struct ov_native_host h={0};
 assert(ov_native_locate(ROOT "/sd/app/app.js",m,a)==OV_NATIVE_FOUND);
 /* ABI file mismatch rejects before any code is opened. */
 assert(!ov_native_host_load(&h,&loader,OTHER,"com.example.app","ovnative0",m,a)&&!opened&&h.error[0]);
 put(ROOT "/sd/app/native/host.abi","short");
 assert(!ov_native_host_load(&h,&loader,ABI,"com.example.app","ovnative0",m,a)&&!opened);
 put(ROOT "/sd/app/native/host.abi",ABI);
 /* A module declaring another package is unloaded immediately. */
 assert(!ov_native_host_load(&h,&loader,ABI,"com.other","ovnative0",m,a)&&opened==1&&closed==1&&!h.api);
 api.bytes--;assert(!ov_native_host_load(&h,&loader,ABI,"com.example.app","ovnative0",m,a)&&closed==2);api.bytes++;
 assert(ov_native_host_load(&h,&loader,ABI,"com.example.app","ovnative0",m,a)&&h.api&&opened==3);
 JSValue v={0};
 assert(ov_native_host_unload(&h,&loader)&&!h.api&&closed==3); /* never attached; quiesce ok */
 assert(ov_native_host_load(&h,&loader,ABI,"com.example.app","ovnative0",m,a)&&opened==4);
 assert(ov_native_host_attach(&h,NULL,v,NULL,"com.example.app")&&h.attached&&attached==1);
 assert(!ov_native_host_attach(&h,NULL,v,NULL,"com.example.app"));
 int q=quiesced;assert(!ov_native_host_unload(&h,&loader)&&quiesced==q);  /* never while attached */
 ov_native_host_detach(&h);assert(!h.attached&&detached==1);
 quiesce_ok=false;assert(!ov_native_host_unload(&h,&loader)&&h.api);       /* module stays resident */
 quiesce_ok=true;close_result=-1;assert(!ov_native_host_unload(&h,&loader)&&h.api); /* rmmod failed */
 assert(ov_native_host_attach(&h,NULL,v,NULL,"com.example.app")&&attached==2);    /* reattach after quiesce */
 ov_native_host_detach(&h);
 close_result=0;assert(ov_native_host_unload(&h,&loader)&&!h.api&&!h.handle);
 api.quiesce=NULL;assert(ov_native_host_load(&h,&loader,ABI,"com.example.app","ovnative0",m,a));
 assert(!ov_native_host_unload(&h,&loader)&&h.api);                          /* no quiesce: resident */
 return 0;
}
'''
(out/'host.c').write_text(host)
root=out/'fs';subprocess.run(['rm','-rf',str(root)],check=True);root.mkdir()
cflags=['-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(stubs)]
subprocess.run(['gcc',*cflags,'-fno-pie','-no-pie',str(out/'heap.c'),'-o',str(out/'heap')],check=True)
subprocess.run([str(out/'heap')],check=True)
subprocess.run(['gcc',*cflags,'-Wno-unused-parameter','-I'+str(native),f'-DROOT="{root.as_posix()}"',
                f'-DQPK_VOLUME_SD_ROOT="{root.as_posix()}/sd"',f'-DQPK_VOLUME_INTERNAL_ROOT="{root.as_posix()}/data"',
                str(out/'host.c'),str(native/'native_host.c'),'-o',str(out/'host')],check=True)
subprocess.run([str(out/'host')],check=True)
(out/'RESULT.json').write_text(json.dumps({'passed':True,
 'scope':'production allocator and host policy with mocked MMU/cache/ELF; no physical MMU/PMP/relocation claim',
 'boot_physical_reservation_bytes':0,'system_policy_caps':'none; executable window only','resident_slots':8,
 'tested':['bounds','no headroom gate','allocation/translation/cache/map failure','alias range','multiple resident modules',
  'slot reuse','unmap failure retains storage','entry path policy','plain quick app passthrough','host ABI file',
  'struct ABI','package ownership','attach/detach','quiesce unload','quiesce refusal','rmmod failure','no quiesce resident']},indent=2))
print('NATIVE_LOADER_HOST_OK')
