"""Read the halted panic state through openocd's TCL port.

GDB batch mode waits for another breakpoint hit, which never comes once the
board is already spinning inside the assert handler. The TCL interface can read
the live registers and stack of the halted core instead.
"""
from pathlib import Path
import argparse
import bisect
import socket
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--elf', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--depth', type=int, default=64)
args = parser.parse_args()
root = Path(__file__).resolve().parent
ocd = Path('D:/.espressif/tools/openocd-esp32/v0.12.0-esp32-20250707/openocd-esp32')
nm = Path('D:/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20251107/riscv32-esp-elf/bin/'
          'riscv32-esp-elf-nm.exe')

text = []


def log(message):
    print(message, flush=True)
    text.append(str(message))
    args.output.write_text('\n'.join(text) + '\n', encoding='utf-8')


symbols = []
metadata_symbols = {}
with subprocess.Popen([str(nm), '-n', str(args.elf)], stdout=subprocess.PIPE, text=True) as process:
    for line in process.stdout:
        parts = line.split()
        if len(parts) == 3 and parts[2] in ('g_first_fault', 'g_flash_external_stack_ops', 'g_flash_caller_sp', 'g_flash_worker_sp'):
            metadata_symbols[parts[2]] = int(parts[0], 16)
        if len(parts) == 3 and parts[1].lower() in 'tw':
            symbols.append((int(parts[0], 16), parts[2]))


def describe(address):
    if not address:
        return '-'
    index = bisect.bisect_right(symbols, (address, '\xff')) - 1
    if index < 0:
        return '0x%08x ?' % address
    base, name = symbols[index]
    return '0x%08x %s+0x%x' % (address, name, address - base)


with (args.output.parent / (args.output.stem + '.openocd.log')).open('wb') as logfile:
    server = subprocess.Popen([str(ocd / 'bin/openocd.exe'), '-s', str(ocd / 'share/openocd/scripts'),
        '-f', 'board/esp32p4-builtin.cfg', '-c',
        'adapter speed 4000; bindto 127.0.0.1; gdb_memory_map disable; gdb_flash_program disable; '
        'gdb port disabled; tcl port 6671; telnet port disabled; init'],
        stdout=logfile, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        control = None
        for _ in range(60):
            if server.poll() is not None:
                raise RuntimeError('openocd exited; see its log')
            try:
                control = socket.create_connection(('127.0.0.1', 6671), timeout=0.5)
                control.settimeout(6)
                break
            except OSError:
                time.sleep(0.25)
        if control is None:
            raise RuntimeError('openocd did not start')

        def tcl(command):
            log('> ' + command)
            control.sendall(command.encode() + b'\x1a')
            data = b''
            while not data.endswith(b'\x1a'):
                try:
                    chunk = control.recv(8192)
                except socket.timeout:
                    log('[timeout on %s; stopping to avoid mixed replies]' % command)
                    raise
                if not chunk:
                    break
                data += chunk
            return data[:-1].decode('utf-8', errors='replace').strip()

        log(tcl('capture "version"'))
        log(tcl('halt 2000'))
        for name, address in metadata_symbols.items():
            log(name)
            log(tcl('mdw 0x%x %d' % (address, 16 if name == 'g_first_fault' else 1)))
        listing = tcl('targets')
        log(listing)
        names = [word for word in listing.split()
                 if word.startswith('esp32p4.') and '.hp.cpu' in word]
        for name in dict.fromkeys(names):
            log('\n==== %s ====' % name)
            log(tcl('targets ' + name) or '')
            registers = '\n'.join(tcl('reg %s force' % reg)
                                  for reg in ('sp', 'ra', 'dpc', 'mepc', 'mcause', 'mtval'))
            log(registers)
            values = {}
            for line in registers.splitlines():
                parts = line.split('0x')
                if len(parts) != 2:
                    continue
                tokens = parts[0].split()
                # "(2) sp (/32): 0x..." keeps the name as the second token.
                if len(tokens) < 2:
                    continue
                try:
                    key = tokens[1] if tokens[0].startswith('(') else tokens[0]
                    values[key] = int(parts[1].split()[0], 16)
                except (IndexError, ValueError):
                    continue
            for key in ('pc', 'ra', 'mepc', 'mcause', 'mtval', 'sp'):
                if key in values:
                    log('%-6s %s' % (key, describe(values[key])))
            if 'sp' in values:
                stack = tcl('mdw 0x%x %d' % (values['sp'], args.depth))
                log(stack)
                for line in stack.splitlines():
                    if ':' not in line:
                        continue
                    words = line.split(':', 1)[1].split()
                    for word in words:
                        try:
                            value = int(word, 16)
                        except ValueError:
                            continue
                        if 0x40000000 <= value < 0x60000000 or 0x00000000 < value < 0x00040000:
                            rendered = describe(value)
                            if '?' not in rendered:
                                log('  %s[%s] -> %s' % (line.split(':')[0].strip(), word, rendered))
        log(tcl('targets esp32p4.hp.cpu0'))
        # Addresses/offsets below are audited against this exact flashed ELF.
        # Refuse to interpret another build with this layout.
        import hashlib
        firmware = args.elf.with_suffix('.bin')
        firmware_hash = hashlib.sha256(firmware.read_bytes()).hexdigest()
        if firmware_hash in ('9b3fa2af6fe0175538221a901747782b66be2ec41ad145dd899c83ee2b96f4b6',
                             '718b6f42af835fca4459d8073cb422ec5267c4af3855eaef48ed9582d4426e78'):
            def words(address, count):
                result = tcl('read_memory 0x%x 32 %d' % (address, count))
                return [int(word, 0) for word in result.split()]
            running = words(0x4ff5049c, 2)
            count, table = words(0x4ff50484, 2)
            log('running=%s count=%d table=0x%x' % (running, count, table))
            if 0 < count <= 256:
                for task in words(table, count):
                    if not task:
                        continue
                    data = words(task, 54)
                    name = b''.join(v.to_bytes(4, 'little') for v in data[46:54]).split(b'\0')[0]
                    log('TCB=0x%x pid=%d name=%r stack=0x%x size=%d regs=0x%x saved=0x%x' %
                        (task, data[13], name, data[31], data[29], data[45], data[44]))
                    if task in running or b'ntp' in name.lower() or b'weather' in name.lower():
                        log(tcl('mdw 0x%x 60' % task))
                        for regs in set(data[44:46]):
                            if 0x48000000 <= regs < 0x50000000:
                                log(tcl('mdw 0x%x 40' % regs))
                        log(tcl('mdw 0x%x %d' % (data[31], min(data[29] // 4, 512))))
                        if data[29] > 2048:
                            log(tcl('mdw 0x%x 384' % (data[31] + data[29] - 1536)))
                            stack_words = words(data[31], data[29] // 4)
                            untouched = 0
                            for word in stack_words:
                                if word != 0xdeadbeef: break
                                untouched += 4
                            log('STACK WATERMARK size=%d used=%d' % (data[29], data[29] - untouched))
                        if b'ntp' in name.lower() and firmware_hash.startswith('9b3fa2'):
                            log(tcl('mdw 0x%x 40' % (data[31] - 160)))
                            log(tcl('mdw 0x483a5320 172'))
        log(tcl('resume'))
        control.close()
    finally:
        try:
            with socket.create_connection(('127.0.0.1', 6671), timeout=2) as shutdown:
                shutdown.sendall(b'shutdown\x1a')
            server.wait(timeout=5)
        except (OSError, subprocess.TimeoutExpired):
            server.terminate()
            server.wait(timeout=10)

args.output.write_text('\n'.join(text) + '\n', encoding='utf-8')
