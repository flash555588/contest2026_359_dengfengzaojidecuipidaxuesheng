"""Verify the camera shutter feedback and the saved photo on the board.

Captures the displayed framebuffer twice: right after the shutter is pressed
and after the result notice expires. The shutter centre must change from the
saved colour back to the idle accent, and the page must report the file.
"""
from pathlib import Path
import argparse
import json
import socket
import struct
import subprocess
import sys
import time

import serial
from PIL import Image

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--delivery', type=Path, required=True)
parser.add_argument('--notice-seconds', type=float, default=3.4)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
workspace = Path(__file__).resolve().parent.parent
ocd = Path('D:/.espressif/tools/openocd-esp32/v0.12.0-esp32-20250707/openocd-esp32')
gdb = Path('D:/.espressif/tools/riscv32-esp-elf-gdb/16.3_20250913/riscv32-esp-elf-gdb/bin/riscv32-esp-elf-gdb.exe')


def console(command, timeout=12):
    assert len(command.encode()) <= 78
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=3)
    port.dtr = False
    port.rts = False
    port.port = 'COM23'
    data = bytearray()
    received_at = time.monotonic()
    with port:
        encoded = command.encode() + b'\r'
        for start in range(0, len(encoded), 8):
            port.write(encoded[start:start + 8])
            time.sleep(0.03)
            data.extend(port.read(port.in_waiting))
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            chunk = port.read(max(1, port.in_waiting))
            data.extend(chunk)
            if chunk:
                received_at = time.monotonic()
            if b'nsh> ' in data and (data.endswith(b'nsh> ') or data.endswith(b'\x1b[K') or
                                     time.monotonic() - received_at > 0.25):
                return data.decode('utf-8', errors='replace').replace('\r', '')
    raise RuntimeError('Console timeout: ' + command)


def grab(name):
    stem = args.output / name
    assert 'frame result=0' in console('desktop ui frame'), name
    raw = stem.with_suffix('.rle565').resolve().relative_to(workspace).as_posix()
    script = stem.with_suffix('.gdb')
    script.write_text('''set pagination off
set confirm off
set remotetimeout 20
target remote 127.0.0.1:3333
monitor halt
python
address = int(gdb.parse_and_eval('g_ui_probe_frame'))
size = int(gdb.parse_and_eval('g_ui_probe_frame_bytes'))
assert address and 0 < size <= 1024 * 600 * 4 and size % 4 == 0
gdb.execute('monitor dump_image "{raw}" 0x%x 0x%x' % (address, size))
end
monitor resume
detach
quit
'''.replace('{raw}', raw), encoding='utf-8')
    result = subprocess.run([str(gdb), '-q', '-batch', str(args.delivery / 'nuttx.elf'), '-x', str(script)],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    stem.with_suffix('.gdb.log').write_bytes(result.stdout)
    result.check_returncode()
    encoded = stem.with_suffix('.rle565').read_bytes()
    data = b''.join(struct.pack('<H', value) * count for count, value in struct.iter_unpack('<HH', encoded))
    assert len(data) == 1024 * 600 * 2, name
    pixels = list(struct.unpack('<%dH' % (1024 * 600), data))
    Image.frombytes('RGB', (1024, 600), data, 'raw', 'BGR;16').save(stem.with_suffix('.png'))
    assert 'frame released' in console('desktop ui frame-free')
    return pixels


report = {}
with (args.output / 'openocd.log').open('xb') as log:
    server = subprocess.Popen([str(ocd / 'bin/openocd.exe'), '-s', str(ocd / 'share/openocd/scripts'),
        '-f', 'board/esp32p4-builtin.cfg', '-c',
        'adapter speed 6000; bindto 127.0.0.1; gdb_memory_map disable; gdb_flash_program disable; '
        'gdb port 3333; tcl port 6666; telnet port disabled; '
        'esp32p4.hp.cpu0 configure -event gdb-attach {halt}; '
        'esp32p4.hp.cpu1 configure -event gdb-attach {halt}; init'],
        stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        time.sleep(1)
        assert 'nsh> ' in console('')
        assert 'ready' in console('desktop ui light')
        output = console('desktop camera')
        assert 'requested' in output, output
        time.sleep(2)
        output += console('desktop ui camera-start')
        assert 'clicked=1' in output, output
        time.sleep(5)
        output += console('desktop ui camera-photo')
        time.sleep(0.7)
        saved = grab('camera-photo-saved')
        time.sleep(args.notice_seconds)
        idle = grab('camera-photo-idle')
        output += console('desktop camera')
        output += console('desktop ui camera-stop')
        (args.output / 'console.log').write_text(output, encoding='utf-8')
    finally:
        try:
            with socket.create_connection(('127.0.0.1', 6666), timeout=2) as control:
                control.sendall(b'shutdown\x1a')
            server.wait(timeout=5)
        except (OSError, subprocess.TimeoutExpired):
            server.terminate()
            server.wait(timeout=10)

shutter = 550 * 1024 + 512
report['shutter_saved'] = saved[shutter]
report['shutter_idle'] = idle[shutter]
report['corner_page_pixel'] = [saved[0], idle[0]]
report['saved_is_green'] = saved[shutter] >> 5 & 0x3f > saved[shutter] >> 11 & 0x1f
report['idle_differs_from_saved'] = saved[shutter] != idle[shutter]
report['photo_line'] = [line for line in output.splitlines() if 'photo' in line]
(args.output / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report, indent=2))
assert report['saved_is_green'] and report['idle_differs_from_saved']
