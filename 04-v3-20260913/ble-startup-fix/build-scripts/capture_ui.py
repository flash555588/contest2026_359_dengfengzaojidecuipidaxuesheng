"""Capture real board page layouts and RGB565 frames without calling LVGL in GDB."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import socket
import struct
import subprocess
import sys
import time

import serial
from PIL import Image

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--theme', choices=['light', 'dark'], required=True)
parser.add_argument('--pages', nargs='+', required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--delivery', type=Path)
parser.add_argument('--adapter-speed', type=int, default=6000)
args = parser.parse_args()
workspace = Path(__file__).resolve().parent.parent
delivery = args.delivery or workspace / '04-v3-20260913/ui-layout-fix'
args.output.mkdir(parents=True, exist_ok=True)
(args.output / 'capture-metadata.json').write_text(json.dumps({
    'firmware_sha256': hashlib.sha256((delivery / 'nuttx.bin').read_bytes()).hexdigest(),
    'theme': args.theme, 'requested_pages': args.pages,
    'method': 'desktop-thread RGB565 snapshot; read-only JTAG; lossless RLE',
    'input_values_in_layout_report': False}, indent=2), encoding='utf-8')
ocd = Path('D:/.espressif/tools/openocd-esp32/v0.12.0-esp32-20250707/openocd-esp32')
gdb = Path('D:/.espressif/tools/riscv32-esp-elf-gdb/16.3_20250913/riscv32-esp-elf-gdb/bin/riscv32-esp-elf-gdb.exe')

def console(command, timeout=12):
    assert len(command.encode()) <= 78
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = False
    port.rts = False
    port.port = 'COM23'
    data = bytearray()
    received_at = time.monotonic()
    with port:
        encoded = command.encode() + b'\r'
        for start in range(0, len(encoded), 8):
            port.write(encoded[start:start+8])
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
    raise RuntimeError('Console timeout: ' + command + '\n' + data.decode(errors='replace'))

with (args.output / (args.theme + '-openocd.log')).open('xb') as log:
    server = subprocess.Popen([str(ocd / 'bin/openocd.exe'), '-s', str(ocd / 'share/openocd/scripts'),
        '-f', 'board/esp32p4-builtin.cfg', '-c',
        f'adapter speed {args.adapter_speed}; bindto 127.0.0.1; gdb_memory_map disable; gdb_flash_program disable; '
        'gdb port 3333; tcl port 6666; telnet port disabled; '
        'esp32p4.hp.cpu0 configure -event gdb-attach {halt}; '
        'esp32p4.hp.cpu1 configure -event gdb-attach {halt}; init'],
        stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        time.sleep(1)
        assert 'nsh> ' in console('')
        assert 'ready' in console('desktop ui ' + args.theme)
        for page in args.pages:
            stem = args.output / (args.theme + '-' + page)
            assert not stem.with_suffix('.png').exists()
            if page == 'dialog':
                # The dialog belongs to an app; close the previous page's
                # dropdown before exercising the app's modal state.
                assert 'ready' in console('desktop ui hello')
            if page == 'dialog-menu':
                assert 'ready' in console('desktop ui dropdown')
            command = 'dialog' if page == 'dialog-menu' else page
            output = console('desktop camera' if page == 'camera' else 'desktop ui ' + command)
            assert ('requested' if page == 'camera' else 'ready') in output, output
            # Let asynchronous file/status tasks and entry animations settle.
            time.sleep(15 if page == 'wifi-scan' else 11 if page == 'ble-scan' else 4 if page in ['bluetooth', 'camera'] else 0.15 if page == 'toast' else 1.2)
            audit = console('desktop ui audit')
            stem.with_suffix('.log').write_text(output + '\n' + audit, encoding='utf-8')
            match = re.search(r'UI_LAYOUT_BEGIN\n(.*?)\nUI_LAYOUT_END', audit, re.S)
            assert match, audit
            objects = json.loads(match.group(1))
            assert objects, 'Page did not produce a visible object tree: ' + page
            stem.with_suffix('.json').write_text(json.dumps(objects, ensure_ascii=False, indent=2), encoding='utf-8')
            assert 'frame result=0' in console('desktop ui frame')
            raw = stem.with_suffix('.rle565').resolve().relative_to(workspace).as_posix()
            commands = stem.with_suffix('.gdb')
            commands.write_text('''set pagination off
set confirm off
set remotetimeout 20
target remote 127.0.0.1:3333
monitor halt
python
address = int(gdb.parse_and_eval('g_ui_probe_frame'))
size = int(gdb.parse_and_eval('g_ui_probe_frame_bytes'))
assert address and 0 < size <= 1024 * 600 * 4 and size % 4 == 0
gdb.execute('monitor dump_image "RAWFILE" 0x%x 0x%x' % (address, size))
end
monitor resume
detach
quit
'''.replace('RAWFILE', raw), encoding='utf-8')
            result = subprocess.run([str(gdb), '-q', '-batch', str(delivery / 'nuttx.elf'), '-x', str(commands)],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
            stem.with_suffix('.gdb.log').write_bytes(result.stdout)
            result.check_returncode()
            encoded = stem.with_suffix('.rle565').read_bytes()
            data = b''.join(struct.pack('<H', value) * count for count, value in struct.iter_unpack('<HH', encoded))
            assert len(data) == 1024 * 600 * 2
            image = Image.frombytes('RGB', (1024, 600), data, 'raw', 'BGR;16')
            image.save(stem.with_suffix('.png'))
            assert 'frame released' in console('desktop ui frame-free')
            if page == 'camera':
                assert 'requested' in console('desktop camera-stop')
            print('Captured', args.theme, page, len(objects), 'objects', flush=True)
        assert 'nsh> ' in console('')
    finally:
        # Let OpenOCD flush USB JTAG before the next capture session.
        try:
            with socket.create_connection(('127.0.0.1', 6666), timeout=2) as control:
                control.sendall(b'shutdown\x1a')
            server.wait(timeout=5)
        except (OSError, subprocess.TimeoutExpired):
            server.terminate()
            server.wait(timeout=10)
