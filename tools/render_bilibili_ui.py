#!/usr/bin/env python3
"""Render the actual LVGL page on a Linux host; no device/network is used."""
import argparse
import concurrent.futures
import os
import struct
import zlib
from pathlib import Path
import subprocess

def run(*args):
    env=os.environ.copy();env["UBSAN_OPTIONS"]="halt_on_error=1:print_stacktrace=1"
    env["ASAN_OPTIONS"]="detect_leaks=1"
    subprocess.run(list(map(str,args)),check=True,env=env)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--apps",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    a=p.parse_args();apps=a.apps.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    lvgl=apps/"graphics/lvgl/lvgl";src=apps/"system/bilibili_live"
    config=(lvgl/"lv_conf_template.h").read_text().replace("#if 0 /*Set it to \"1\" to enable content*/", "#if 1 /*Host rendering*/")
    config=config.replace("#define LV_MEM_SIZE (64 * 1024U)","#define LV_MEM_SIZE (8 * 1024 * 1024U)")
    config=config.replace("#define LV_USE_TINY_TTF 0","#define LV_USE_TINY_TTF 1")
    confpath=out/"lv_conf.h"
    unchanged=confpath.exists() and confpath.read_text()==config
    if not unchanged: confpath.write_text(config)
    flags=["-O1","-g","-fsanitize=address,undefined","-fno-omit-frame-pointer","-pthread",
           "-I"+str(src),"-I"+str(apps/"graphics/lvgl"),"-I"+str(out),"-DLV_CONF_INCLUDE_SIMPLE"]
    sources=sorted((lvgl/"src").rglob("*.c"))
    def compile(item):
        i,s=item;o=out/f"lvgl-{i}.o"
        if unchanged and o.exists() and o.stat().st_mtime > s.stat().st_mtime: return o
        run("gcc",*flags,"-c",s,"-o",o);return o
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        objects=list(pool.map(compile,enumerate(sources)))
    cjson=apps/"netutils/cjson/cJSON"
    run("gcc",*flags,"-I"+str(cjson),src/"tests/render_ui.c",src/"bili_api.c",src/"bili_flv.c",
        cjson/"cJSON.c",*objects,"-lm","-o",out/"render")
    run(out/"render",apps/"system/desktop/AlibabaPuHuiTi-3-55-Regular.otf",out/"idle.rgb565")
    raw=(out/"idle.rgb565").read_bytes();rgb=bytearray()
    for y in range(600):
        rgb.append(0)
        for x in range(1024):
            i=(y*1024+x)*2;v=raw[i]|raw[i+1]<<8
            rgb.extend((((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31))
    def chunk(kind,data):
        return struct.pack(">I",len(data))+kind+data+struct.pack(">I",zlib.crc32(kind+data)&0xffffffff)
    png=b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",1024,600,8,2,0,0,0))
    png+=chunk(b"IDAT",zlib.compress(bytes(rgb)))+chunk(b"IEND",b"")
    (out/"lvgl-idle-host.png").write_bytes(png)
    print(f"Actual LVGL host framebuffer: {out / 'idle.rgb565'} (not board evidence)")

if __name__=="__main__":main()
