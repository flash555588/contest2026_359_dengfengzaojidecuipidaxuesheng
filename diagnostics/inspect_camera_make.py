from pathlib import Path
import subprocess
root = Path('/tmp/v3-desktop-camera-usb-20260914/nuttx')
r = subprocess.run(['make', '-C', str(root / 'arch/risc-v/src'), 'TOPDIR=' + str(root),
                    '--eval=camvars:;$(info CAM_VALUE=$(CONFIG_ESP32P4_CAMERA_USB) SRC=$(filter %uvc.c %usbh_core.c,$(CHIP_CSRCS)) TOP=$(TOPDIR) APP=$(APPDIR)) @true',
                    'camvars'], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
print(r.stdout[-3500:])
print((root / 'tools/Make.defs').read_text()[:2400])
