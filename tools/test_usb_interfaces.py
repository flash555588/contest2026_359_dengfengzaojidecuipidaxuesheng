#!/usr/bin/env python3
"""ASan/UBSan checks of the production MSC, HID, UAC and role/CDC code."""
import argparse, os, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1];out=a.output.resolve()
driver=root/'app/espdl-quickapp/overlay/nuttx/arch/risc-v/src/esp32p4/camera_usb';cherry=driver/'cherryusb'
stubs={
'nuttx/spinlock.h':'#pragma once\n#include <nuttx/irq.h>\n',
'nuttx/config.h':'#pragma once\n#define CONFIG_FS_WRITABLE 1\n#define CONFIG_ESPRESSIF_MIPI_DSI 1\n',
'nuttx/kmalloc.h':'#pragma once\n#include <stdlib.h>\n#define kumm_free free\nstatic inline void *kumm_memalign(size_t a,size_t s){void *p=NULL;return posix_memalign(&p,a,s)?NULL:p;}\n',
'nuttx/mutex.h':'#pragma once\n#include <pthread.h>\ntypedef pthread_mutex_t mutex_t;\n#define NXMUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER\n#define nxmutex_init(p) pthread_mutex_init(p,NULL)\n#define nxmutex_lock pthread_mutex_lock\n#define nxmutex_unlock pthread_mutex_unlock\n',
'nuttx/irq.h':'#pragma once\ntypedef unsigned irqstate_t;\nstatic inline unsigned enter_critical_section(void){return 0;}\nstatic inline void leave_critical_section(unsigned s){(void)s;}\n',
'nuttx/fs/fs.h':'#pragma once\n#include <sys/types.h>\n#include <stdbool.h>\nstruct inode {void *i_private;};struct file {void *f_priv;};\nstruct geometry {bool geo_available,geo_mediachanged,geo_writeenabled;size_t geo_nsectors;unsigned geo_sectorsize;};\nstruct block_operations {int(*open)(struct inode*);int(*close)(struct inode*);ssize_t(*read)(struct inode*,unsigned char*,blkcnt_t,unsigned);ssize_t(*write)(struct inode*,const unsigned char*,blkcnt_t,unsigned);int(*geometry)(struct inode*,struct geometry*);};\nstruct file_operations {int(*close)(struct file*);int(*ioctl)(struct file*,int,unsigned long);};\nint register_driver(const char*,const struct file_operations*,mode_t,void*);int register_blockdriver(const char*,const struct block_operations*,mode_t,void*);int unregister_blockdriver(const char*);\n'
}
stubs['nuttx/kmalloc.h']=stubs['nuttx/kmalloc.h'].replace('void *p=NULL;', '''
#ifdef TEST_DEVICE
extern _Bool usb_test_no_memory;if(usb_test_no_memory)return NULL;
#endif
void *p=NULL;''')
stubs['nuttx/kmalloc.h']+='\n#define kmm_memalign kumm_memalign\n#define kmm_free free\n'
for name,body in stubs.items():f=out/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(body)
includes=[out,root/'app/espdl-quickapp/overlay/nuttx/include',driver,cherry/'common',cherry/'core',cherry/'class/hub',cherry/'class/msc',cherry/'class/hid',cherry/'class/cdc']
flags=['-std=gnu11','-D_GNU_SOURCE','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie','-ffunction-sections','-fdata-sections','-pthread',*['-I'+str(i) for i in includes]]
for case in ['MSC','HID','AUDIO','STATE','DEVICE']:
    exe=out/case.lower();extra=[str(driver/'board_usb_audio_protocol.c')] if case=='AUDIO' else []
    subprocess.run(['gcc',*flags,'-DTEST_'+case,str(root/'app/espdl-quickapp/tests/usb_interfaces_host.c'),*extra,'-Wl,--gc-sections','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1','UBSAN_OPTIONS':'halt_on_error=1'})
