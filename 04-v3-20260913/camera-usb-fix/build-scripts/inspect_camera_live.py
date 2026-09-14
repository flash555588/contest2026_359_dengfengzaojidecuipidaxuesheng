"""Read camera state over JTAG without invoking target functions."""
from pathlib import Path
import argparse
import subprocess
import time
import socket
ws = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--display', action='store_true')
args = parser.parse_args()
ocd = Path('D:/.espressif/tools/openocd-esp32/v0.12.0-esp32-20250707/openocd-esp32')
gdb = Path('D:/.espressif/tools/riscv32-esp-elf-gdb/16.3_20250913/riscv32-esp-elf-gdb/bin/riscv32-esp-elf-gdb.exe')
commands = args.output.with_suffix('.gdb')
commands.write_text('''set pagination off
set confirm off
set remotetimeout 20
set print elements 80
target remote 127.0.0.1:3333
monitor halt
p g_phy_ready
p/x *(unsigned int *)0x5000000c
p/x *(unsigned int *)0x50000010
p/x *(unsigned int *)0x50000014
p/x *(unsigned int *)0x50000008
p/x *(unsigned int *)0x50000048
p/x *(unsigned int *)0x50000050
p/x *(unsigned int *)0x50000400
p/x *(unsigned int *)0x50000404
p/x *(unsigned int *)0x50000408
p/x *(unsigned int *)0x50000440
p/x *(unsigned int *)0x50000500
p/x *(unsigned int *)0x50000508
p/x *(unsigned int *)0x50000510
p/x *(unsigned int *)0x50000514
p g_dwc2_hcd[0].user_params
p g_devices[0].info
p/x g_devices[0].control
p/x g_devices[0].desc.caps.uvc_version
p g_devices[0].selected
p g_devices[0].stats
p g_camera
p g_camera_last_error
info threads
thread apply all bt
p g_running_tasks[0]->xcp
p g_running_tasks[1]->xcp
p/x *g_running_tasks[0]->xcp.regs@34
p/x *g_running_tasks[1]->xcp.regs@34
p/x g_last_regs
if g_last_regs[1][2] >= 0x4ff40000 && g_last_regs[1][2] < 0x4ffffff0 - 640
x/160wx g_last_regs[1][2]
end
monitor targets esp32p4.hp.cpu1
monitor reg mepc force
monitor reg mtval force
monitor reg sp force
monitor reg ra force
monitor targets esp32p4.hp.cpu0
monitor resume
detach
quit
''')
if args.display:
    commands.write_text('''set pagination off
set confirm off
set remotetimeout 20
target remote 127.0.0.1:3333
python
import time
for sample in range(3):
    gdb.execute('monitor halt')
    for name in ['g_esp_mipi_dsi_frames', 'g_esp_mipi_dsi_underruns', 'g_camera.frames', 'g_camera.fb_page', 'g_esp_mipi_dsi.fb', 'g_esp_mipi_dsi.pending_fb', 'g_lcd_plane.yoffset']:
        print('%s=%s' % (name, gdb.parse_and_eval(name)))
    gdb.execute('monitor resume')
    if sample < 2: time.sleep(4)
end
detach
quit
''')
with args.output.with_suffix('.openocd.log').open('xb') as log:
    server = subprocess.Popen([str(ocd / 'bin/openocd.exe'), '-s', str(ocd / 'share/openocd/scripts'), '-f', 'board/esp32p4-builtin.cfg', '-c',
        'adapter speed 2000; bindto 127.0.0.1; gdb_memory_map disable; gdb_flash_program disable; gdb port 3333; tcl port 6666; telnet port disabled; esp32p4.hp.cpu0 configure -event gdb-attach {halt}; esp32p4.hp.cpu1 configure -event gdb-attach {halt}; init'],
        stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        time.sleep(1)
        result = subprocess.run([str(gdb), '-q', '-batch', str(ws / '04-v3-20260913/camera-usb-fix/nuttx.elf'), '-x', str(commands)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=50)
        args.output.write_bytes(result.stdout)
        print(result.stdout.decode('utf-8', errors='replace'))
    finally:
        try:
            with socket.create_connection(('127.0.0.1', 6666), timeout=2) as control:
                control.sendall(b'resume\x1ashutdown\x1a')
            server.wait(timeout=5)
        except (OSError, subprocess.TimeoutExpired):
            server.terminate(); server.wait(timeout=5)
