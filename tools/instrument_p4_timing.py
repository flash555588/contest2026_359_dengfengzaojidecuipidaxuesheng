"""Idempotent narrow instrumentation of the explicitly prepared P4 SDK."""
import hashlib
from pathlib import Path

def apply(nuttx):
    folder=Path(nuttx)/'arch/risc-v/src/common/espressif'
    receipts={}
    for name in ['esp_mipi_dsi.c','esp_spiflash.c']:
        path=folder/name;source=path.read_text()
        before=hashlib.sha256(source.encode()).hexdigest()
        if '/* ESPCLAW CAMERA TIMING */' not in source:
            marker='#include <nuttx/config.h>'
            if source.count(marker)!=1:raise RuntimeError('Unexpected P4 include anchor: '+name)
            source=source.replace(marker,marker+'\n/* ESPCLAW CAMERA TIMING */\n#include <nuttx/video/camera_timing.h>',1)
            if name=='esp_mipi_dsi.c':
                anchor='      g_esp_mipi_dsi_frames++;'
                if source.count(anchor)!=1:raise RuntimeError('Unexpected DSI completion anchor')
                source=source.replace(anchor,anchor+'\n      esp_camera_display_frame();',1)
            else:
                for op in ['read','write','erase']:
                    anchor='static int spiflash_direct_'+op+'('
                    if source.count(anchor)!=1:raise RuntimeError('Unexpected Flash anchor '+op)
                    source=source.replace(anchor,'static int spiflash_direct_'+op+'_untimed(',1)
                marker='/* P4 suspends the shared external-memory cache'
                if source.count(marker)!=1:raise RuntimeError('Unexpected prepared Flash worker anchor')
                wrappers=''
                for op,kind in [('read','void *'),('write','const void *'),('erase',None)]:
                    arguments='uint32_t address, '+(kind+'buffer, ' if kind else '')+'uint32_t length'
                    call='address, '+('buffer, ' if kind else '')+'length'
                    wrappers+=f'''static int spiflash_direct_{op}({arguments})
{{
  uint64_t started=esp_camera_flash_begin(CAMERA_FLASH_{op.upper()});
  int ret=spiflash_direct_{op}_untimed({call});
  esp_camera_flash_end(CAMERA_FLASH_{op.upper()},length,started,ret);
  return ret;
}}

'''
                source=source.replace(marker,wrappers+marker,1)
            path.write_text(source)
        expected='esp_camera_display_frame();' if name=='esp_mipi_dsi.c' else 'esp_camera_flash_end(CAMERA_FLASH_ERASE'
        if source.count(expected)!=1:raise RuntimeError('Incomplete timing instrumentation: '+name)
        receipts[name]={'before_sha256':before,'instrumented_sha256':hashlib.sha256(source.encode()).hexdigest()}
    return receipts
