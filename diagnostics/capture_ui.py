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
parser.add_argument('--ble-state', action='store_true', help='Read the current BLE list from target memory')
parser.add_argument('--tcl', action='store_true', help='Read frame through bounded OpenOCD TCL commands, without GDB')
parser.add_argument('--keep-page', action='store_true', help='Keep the current page/theme for interaction captures')
args = parser.parse_args()
assert not (args.tcl and args.ble_state)
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
if args.tcl:
    nm = Path('D:/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20251107/riscv32-esp-elf/bin/riscv32-esp-elf-nm.exe')
    symbols = {}
    for line in subprocess.check_output([str(nm), str(delivery / 'nuttx.elf')], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[2] in ('g_ui_probe_frame', 'g_ui_probe_frame_bytes'):
            symbols[fields[2]] = int(fields[0], 16)

def tcl_frame(raw):
    with socket.create_connection(('127.0.0.1', 6666), timeout=3) as control:
        # 250 KiB frames take about 13 s over a 1 MHz debug link.
        control.settimeout(30)
        def command(text):
            control.sendall(text.encode() + b'\x1a')
            reply = bytearray()
            while not reply.endswith(b'\x1a'):
                chunk = control.recv(8192)
                if not chunk: raise RuntimeError('OpenOCD connection closed')
                reply.extend(chunk)
            return reply[:-1].decode(errors='replace').strip()
        command('targets esp32p4.hp.cpu0')
        command('halt 2000')
        try:
            address = int(command('read_memory 0x%x 32 1' % symbols['g_ui_probe_frame']), 0)
            size = int(command('read_memory 0x%x 32 1' % symbols['g_ui_probe_frame_bytes']), 0)
            assert 0x48000000 <= address < 0x50000000 and 0 < size <= 1024 * 600 * 4 and size % 4 == 0
            response = command('dump_image {%s} 0x%x 0x%x' % (raw, address, size))
            assert (workspace / raw).stat().st_size == size, response
        finally:
            command('resume')

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
        f'gdb port {"disabled" if args.tcl else "3333"}; tcl port 6666; telnet port disabled; '
        'esp32p4.hp.cpu0 configure -event gdb-attach {halt}; '
        'esp32p4.hp.cpu1 configure -event gdb-attach {halt}; init'],
        stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        time.sleep(1)
        assert 'nsh> ' in console('')
        if not args.keep_page:
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
            command = 'dialog' if page == 'dialog-menu' else 'files' if page == 'files-photo' else page
            camera_page = page in ['camera', 'camera-preview']
            espdl_live = page in ['espdl-follow', 'espdl-result']
            output = console('desktop camera' if camera_page else 'desktop ui ' + ('espdl' if espdl_live else command))
            assert ('requested' if camera_page else 'ready') in output, output
            if page == 'files-photo':
                # Open the folder the camera writes to and then the first image.
                time.sleep(1.5)
                listing = console('desktop ui audit')
                if '"text":"photos"' in listing:
                    output += listing + console('desktop ui click:photos')
                    time.sleep(1.5)
                    listing = console('desktop ui audit')
                images = re.findall(r'"text":"([^"]*\.jpg)"', listing)
                assert images, 'No JPEG row to open: ' + listing
                output += console('desktop ui click:' + images[0])
                time.sleep(2.5)
            if espdl_live:
                if page == 'espdl-result':
                    output += console('desktop ui espdl-classify')
                    output += console('desktop ui espdl-photo')
                else:
                    output += console('desktop ui espdl-start')
                time.sleep(5)
            if page == 'camera-preview':
                time.sleep(1)
                output += console('desktop ui camera-start')
                time.sleep(5)
            # Let asynchronous file/status tasks and entry animations settle.
            time.sleep(15 if page == 'wifi-scan' else 11 if page == 'ble-scan' else 4 if page in ['bluetooth', 'camera'] else 0.15 if page == 'toast' else 1.2)
            # The console UART can drop a byte while the desktop is busy, so
            # re-read a corrupted layout dump instead of failing the capture.
            for attempt in range(3):
                audit = console('desktop ui audit')
                stem.with_suffix('.log').write_text(output + '\n' + audit, encoding='utf-8')
                match = re.search(r'UI_LAYOUT_BEGIN\n(.*?)\nUI_LAYOUT_END', audit, re.S)
                if not match:
                    continue
                try:
                    objects = json.loads(match.group(1))
                except json.JSONDecodeError:
                    print('Retrying layout dump for', page, flush=True)
                    continue
                break
            else:
                raise RuntimeError('Layout dump stayed corrupt: ' + page)
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
'''.replace('RAWFILE', raw).replace('monitor halt\npython',
    'monitor halt\n' + ('set print elements 0\np g_ble_view\n' if args.ble_state else '') + 'python'), encoding='utf-8')
            if args.tcl:
                tcl_frame(raw)
            else:
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
            if camera_page:
                assert 'requested' in console('desktop camera-stop')
            if espdl_live:
                assert 'ready' in console('desktop ui espdl-stop')
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
