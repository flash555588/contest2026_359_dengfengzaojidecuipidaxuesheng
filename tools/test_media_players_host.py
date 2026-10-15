#!/usr/bin/env python3
"""Build actual patched decoders and run deterministic host media regressions."""
import argparse
import hashlib
from pathlib import Path
import subprocess

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--apps',type=Path,required=True,help='Apps tree after install_bilibili_live.py')
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--live-flv',type=Path,required=True,help='3s 800x450 H26425fps + mono AAC-LC48kHz')
    p.add_argument('--h264-mp4',type=Path)
    p.add_argument('--mjpeg-mp4',type=Path)
    a=p.parse_args();repo=Path(__file__).resolve().parent.parent
    src=repo/'app/bilibili_live';desktop=repo/'app/espdl-quickapp/overlay/apps/system/desktop'
    oh=a.apps.resolve()/'system/bilibili_live/vendor/openh264/codec'
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    includes=[src,src/'local_vendor',src/'local_vendor/helix',desktop,*[oh/d for d in ['api/wels','common/inc','decoder/core/inc','decoder/plus/inc']]]
    flags=['-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-DUSE_DEFAULT_STDLIB',*['-I'+str(d) for d in includes]]
    def run(argv,**kwargs):subprocess.run(list(map(str,argv)),check=True,**kwargs)
    def compile_file(source):
        obj=out/(source.stem+'-'+hashlib.sha256(str(source).encode()).hexdigest()[:10]+'.o')
        run(['g++' if source.suffix=='.cpp' else 'cc',*flags,'-c',source,'-o',obj]);return obj
    decoder=[compile_file(s) for d in ['common/src','decoder/core/src','decoder/plus/src'] for s in sorted((oh/d).glob('*.cpp'))]
    helix=[compile_file(s) for s in sorted((src/'local_vendor/helix').glob('*.c')) if not s.name.startswith('sbr')]
    wrapper=(src/'bili_decoder.cpp').read_text(encoding='utf-8')
    wrapper=wrapper.replace('struct decoder {','extern "C" void test_before_decode(void);\nstruct decoder {')
    marker='  decoder *d = static_cast<decoder *>(ctx);\n  /* Inspect every'
    assert wrapper.count(marker)==1
    wrapper=wrapper.replace(marker,'  test_before_decode();\n'+marker)
    injected=out/'decoder-delay.cpp';injected.write_text(wrapper,encoding='utf-8')
    common=[compile_file(src/'bili_sps.c')]
    live=[*decoder,*helix,*common,compile_file(injected),compile_file(src/'bili_flv.c'),compile_file(src/'bili_av.c'),compile_file(src/'tests/test_live_av.c')]
    run(['g++','-pthread','-fsanitize=address,undefined',*live,'-lm','-o',out/'live'])
    run([out/'live',a.live_flv.resolve()],timeout=40)
    if a.h264_mp4 or a.mjpeg_mp4:
        local=[*decoder,*helix,*common,compile_file(src/'bili_decoder.cpp'),compile_file(src/'local_video_media.c'),compile_file(desktop/'qpk_mjpeg.c'),compile_file(desktop/'qpk_tjpgd.c')]
        for name,fixture in [('test_local_video',a.h264_mp4),('test_mp4_mjpeg',a.mjpeg_mp4)]:
            if fixture:
                run(['g++','-pthread','-fsanitize=address,undefined',*local,compile_file(src/'tests'/(name+'.c')),'-lm','-o',out/name])
                run([out/name,fixture.resolve()],timeout=40)
    print('PASS requested host functional assertions; inspect sanitizer diagnostics separately (known Helix signed-shift UB); no device playback performed')

if __name__=='__main__':main()
