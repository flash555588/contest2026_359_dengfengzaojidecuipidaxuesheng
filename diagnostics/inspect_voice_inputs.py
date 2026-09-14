"""Copy the current isolated build's audio/chat interfaces for review."""
from pathlib import Path
import shutil

root = Path('/tmp/v3-desktop-camera-usb-20260914')
out = Path(__file__).resolve().parent / 'voice-reference'
files = [
    'apps/system/espclaw/Makefile', 'apps/system/espclaw/main.c',
    'apps/system/espclaw/sources.mk', 'apps/system/espclaw/port/tls_mbedtls.c',
    'apps/system/espclaw/port/connect.c',
    'nuttx/audio/audio.c', 'apps/include/netutils/webclient.h',
    'nuttx/arch/risc-v/src/common/espressif/esp_i2s.c',
    'nuttx/include/nuttx/audio/i2s.h',
    'apps/netutils/webclient/webclient.c',
    'apps/system/espclaw/port/espclaw_main.c',
    'apps/system/espclaw/port/http_webclient.c',
    'apps/system/espclaw/include/claw_webclient.h',
    'apps/system/espclaw/include/claw_tls.h',
    'apps/system/espclaw/core/llm/claw_llm_http_transport.c',
    'apps/system/espclaw/core/llm/claw_llm_http_transport.h',
    'apps/system/espclaw/core/llm/media/claw_media_pipeline.c',
    'apps/system/espclaw/include/claw_core.h',
    'nuttx/include/nuttx/audio/audio.h',
    'nuttx/drivers/audio/audio.c', 'nuttx/drivers/audio/es8311.c',
    'apps/system/nxrecorder/nxrecorder.c',
    'apps/system/nxrecorder/nxrecorder_main.c',
    'apps/include/system/nxrecorder.h',
    'apps/system/desktop/glass_ui.inc',
    'apps/system/desktop/desktop_main.c',
    'apps/system/desktop/Makefile',
]
for item in files:
    source = root / item
    if not source.is_file():
        print('missing:', item)
        continue
    target = out / item
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
print('Copied', len(files), 'requested interfaces to', out)
