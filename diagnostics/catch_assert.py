"""Catch the next up_assert() on the board and print its call stack."""
from pathlib import Path
import argparse
import socket
import subprocess
import sys
import time

import serial
from esptool.reset import HardReset

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--elf', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--wait', type=float, default=75)
parser.add_argument('--no-reset', action='store_true',
                    help='Attach without rebooting, so the serial port stays free')
args = parser.parse_args()
root = Path(__file__).resolve().parent
ocd = Path('D:/.espressif/tools/openocd-esp32/v0.12.0-esp32-20250707/openocd-esp32')
gdb = Path('D:/.espressif/tools/riscv32-esp-elf-gdb/16.3_20250913/riscv32-esp-elf-gdb/bin/riscv32-esp-elf-gdb.exe')

if not args.no_reset:
    # Reboot first, then attach: the assert fires once the radio associates.
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = False
    port.rts = False
    port.port = 'COM23'
    with port:
        HardReset(port, uses_usb=True)()

script = root / 'catch-assert.gdb'
script.write_text('''set pagination off
set confirm off
set remotetimeout 60
target remote 127.0.0.1:3333
break up_assert
break __assert
continue
printf "\\n==== stopped after assert ====\\n"
bt
info registers
printf "==== tasks ====\\n"
thread apply all bt
detach
quit
''', encoding='utf-8')

with (args.output.parent / (args.output.stem + '.openocd.log')).open('wb') as log:
    server = subprocess.Popen([str(ocd / 'bin/openocd.exe'), '-s', str(ocd / 'share/openocd/scripts'),
        '-f', 'board/esp32p4-builtin.cfg', '-c',
        'adapter speed 4000; bindto 127.0.0.1; gdb_memory_map disable; gdb_flash_program disable; '
        'gdb port 3333; tcl port 6666; telnet port disabled; '
        'esp32p4.hp.cpu0 configure -event gdb-attach {halt}; '
        'esp32p4.hp.cpu1 configure -event gdb-attach {halt}; init'],
        stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        # Wait until openocd accepts TCL connections instead of guessing.
        for _ in range(40):
            try:
                with socket.create_connection(('127.0.0.1', 6666), timeout=0.5):
                    break
            except OSError:
                time.sleep(0.25)
        else:
            raise RuntimeError('openocd did not start: ' +
                               (args.output.parent / (args.output.stem + '.openocd.log')).read_text(
                                   encoding='utf-8', errors='replace'))
        result = subprocess.run([str(gdb), '-q', '-batch', str(args.elf), '-x', str(script)],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=args.wait)
        text = result.stdout.decode('utf-8', errors='replace')
    except subprocess.TimeoutExpired as error:
        text = (error.stdout or b'').decode('utf-8', errors='replace') + '\n[no assert within the window]\n'
    finally:
        try:
            with socket.create_connection(('127.0.0.1', 6666), timeout=2) as control:
                control.sendall(b'resume\x1ashutdown\x1a')
            server.wait(timeout=5)
        except (OSError, subprocess.TimeoutExpired):
            server.terminate()
            server.wait(timeout=10)

args.output.write_text(text, encoding='utf-8')
print(text)
