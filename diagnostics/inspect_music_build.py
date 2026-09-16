from pathlib import Path
import shutil

ws = Path(__file__).resolve().parent.parent
root = Path('/tmp/v3-desktop-espdl-20260915')
out = ws/'diagnostics/music-reference'
for rel in ('nuttx/include/nuttx/audio/audio.h', 'nuttx/drivers/audio/es8311.c',
            'nuttx/boards/risc-v/esp32p4/common/src/esp32p4_es8311.c',
            'apps/system/nxplayer/nxplayer.c', 'apps/system/desktop/glass_wifi.inc',
            'apps/system/desktop/glass_files.inc', 'apps/graphics/lvgl/lvgl/src/others/ime/lv_ime_pinyin.c'):
    p = root/rel
    if p.exists():
        target = out/p.name
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(p,target)
        print(rel)
