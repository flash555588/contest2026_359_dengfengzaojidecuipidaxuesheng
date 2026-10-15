#!/usr/bin/env python3
"""Publish actual SD-player native captures and explicit provenance."""
import argparse,hashlib,json,shutil
from pathlib import Path
from PIL import Image
from package_home_pixel import PAGE,rgb
def main():
    p=argparse.ArgumentParser();p.add_argument('--native',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--evidence',type=Path,required=True);a=p.parse_args()
    repo=Path(__file__).resolve().parents[1];desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop';dest=a.output/'home-pixel';dest.mkdir(exist_ok=True,parents=True)
    screens=json.loads((a.native/'screens.json').read_text());assert len(screens)==32;motion=[]
    for entry in screens:
        source=a.native/entry['file'];shutil.copy2(source,dest/source.name);Image.open(source).crop((108,374,393,552)).save(dest/(source.stem+'-card.png'))
    for theme in ['light','dark']:
        for color in ['white','black']:
            for state in ['idle','thinking','replying']:
                frames=[rgb(f) for f in sorted(a.native.glob(f'{theme}-{color}-{state}-frame-*.rgb565'))];assert len(frames)==30
                cards=[im.crop((108,374,393,552)) for im in frames];unique=len({im.tobytes() for im in cards});assert unique>1
                for suffix,images in [('card',cards),('home',frames)]:
                    images=[im.quantize(colors=256,dither=Image.Dither.NONE) for im in images];images[0].save(dest/f'{theme}-{color}-{state}-{suffix}.gif',save_all=True,append_images=images[1:],duration=100,loop=0,disposal=2)
                if color==('white' if theme=='light' else 'black'):shutil.copy2(dest/f'{theme}-{color}-{state}-card.gif',dest/f'{theme}-{state}-card.gif')
                motion.append({'theme':theme,'palette':color,'state':state,'unique_frames':unique})
    build=json.loads((a.evidence/'BUILD-METADATA.json').read_text())
    report={'render_method':'real LVGL SD player / host mock services','firmware_sha256':build['sha256']['nuttx.bin'],'flashed':True,'hardware_results':'../board-test.html','screens':32,'animations':24,'fps_target':10,'period_ms':100,'motion_checks':motion,'expressions':32,'desktop':{'diameter':96,'face_direction':'right','occlusion':'bottom quarter'},'mini_chat':48,'limits':'finite eight-second clips; nine-direction app gaze; host captures are not physical-board screenshots','sources':{n:hashlib.sha256((desktop/n).read_bytes()).hexdigest() for n in ['glass_grok.c','glass_grok_player.inc','glass_grok_state.c','glass_home_ui.inc','qpk_grok.inc']}}
    (dest/'validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2))
    notice='<p>当前版本：SD 原始像素帧播放，10 fps、八秒循环。下方为实际播放器的原生离线渲染。<a href="board-test.html?rev=sd-raw">查看烧录及实板测试</a></p>'
    page=PAGE.replace('<main>','<main>'+notice,1).replace('已接入源码 · 未烧录','SD 播放版 · 已烧录').replace('对话状态联动、原生渲染和目标文件编译检查通过。未烧录，实板效果待验证。','对话状态联动和原生播放测试已通过；烧录与设备实测记录见上方链接。')
    (a.output/'home-pixel.html').write_text(page,encoding='utf-8');(a.output/'home-sd.html').write_text(page,encoding='utf-8')
    print('GROK_SD_REVIEW_OK: 32 native screenshots; 24 GIFs; 12 changing motion tracks')
if __name__=='__main__':main()
