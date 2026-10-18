#!/usr/bin/env python3
"""Test one-shot SD lock-screen JPEG loading without LVGL or hardware."""
import subprocess,tempfile,argparse
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--live-jpeg',type=Path);p.add_argument('--rgb565-out',type=Path);a=p.parse_args()
repo=Path(__file__).resolve().parents[1];desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop'
with tempfile.TemporaryDirectory(prefix='lock-photo-') as tmp:
 root=Path(tmp);photo=root/'photo.jpg'
 subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-f','lavfi','-i','testsrc2=size=1024x600:rate=1',
  '-frames:v','1','-q:v','4',str(photo)],check=True)
 subprocess.run(['cc','-D_GNU_SOURCE','-std=gnu11','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer',
  '-DGLASS_LOCK_PHOTO_PATH="'+str(photo)+'"','-DMUSIC_CACHE_ROOT="'+str(root)+'/cache"','-I'+str(desktop),str(repo/'app/espdl-quickapp/tests/lock_photo_host.c'),
  str(desktop/'qpk_mjpeg.c'),str(desktop/'qpk_tjpgd.c'),'-o',str(root/'test')],check=True)
 subprocess.run([str(root/'test'),str(photo)],check=True,timeout=20)

 if a.live_jpeg:
  assert a.rgb565_out
  subprocess.run([str(root/'test'),'--export',str(a.live_jpeg.resolve()),str(a.rgb565_out.resolve())],check=True,timeout=15)
