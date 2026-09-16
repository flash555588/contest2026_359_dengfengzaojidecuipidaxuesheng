"""Exercise the real LVGL keyboard/input pipeline under ASan and UBSan."""
from pathlib import Path
import concurrent.futures
import subprocess
ws=Path(__file__).resolve().parent.parent
src=ws/'04-v3-20260913/espdl-quickapp/overlay/apps/system/desktop'
lv=Path('/tmp/v3-desktop-espdl-20260915/apps/graphics/lvgl/lvgl')
out=Path('/tmp/music-ime-tests'); out.mkdir(exist_ok=True)
(out/'nuttx').mkdir(exist_ok=True); (out/'nuttx/config.h').write_text('')
flags=['-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer',
       '-DLV_CONF_SKIP','-DLV_FONT_MONTSERRAT_24=1','-DLV_MEM_SIZE=4194304',
       '-I'+str(out),'-I'+str(lv.parent),'-I'+str(src)]
sources=list((lv/'src').rglob('*.c'))+[src/'glass_ime.c',ws/'diagnostics/music-tests/test_ime.c',src/'glass_pinyin_resource.c',src/'glass_pinyin.cpp']+list((src/'music_vendor/googlepinyin/src').glob('*.cpp'))
def compile_source(item):
    i,path=item; obj=out/f'{i}.o'
    if not obj.exists() or obj.stat().st_mtime < path.stat().st_mtime:
        compiler=['g++','-std=c++11'] if path.suffix=='.cpp' else ['cc']
        subprocess.run([*compiler,*flags,'-c',str(path),'-o',str(obj)],check=True,stdout=subprocess.DEVNULL)
    return str(obj)
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
    objects=list(pool.map(compile_source,enumerate(sources)))
subprocess.run(['g++',*flags,*objects,'-lm','-lpthread','-o',str(out/'test')],check=True)
result=subprocess.run([str(out/'test')],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(ws/'04-v3-20260913/espdl-quickapp/evidence/music-ime-tests.log').write_text(result.stdout)
print(result.stdout); result.check_returncode()
