"""Read audio driver metadata via JTAG, without reading samples or user files."""
from pathlib import Path
import argparse
import hashlib
import json
import socket
import subprocess
import time
from elftools.elf.elffile import ELFFile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
ws = Path(__file__).resolve().parent.parent
delivery = ws / '04-v3-20260913/espdl-quickapp'
ocd = Path('D:/.espressif/tools/openocd-esp32/v0.12.0-esp32-20250707/openocd-esp32')
elf_stream = (delivery/'nuttx.elf').open('rb')
elf = ELFFile(elf_stream)
symbols = {s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
types = {}
for cu in elf.get_dwarf_info().iter_CUs():
    name = cu.get_top_DIE().attributes.get('DW_AT_name')
    if not name or not name.value.decode(errors='replace').endswith(('esp_i2s.c', 'es8311.c', 'glass_ble.c')): continue
    for die in cu.iter_DIEs():
        name = die.attributes.get('DW_AT_name')
        if name and (die.tag == 'DW_TAG_typedef' or 'DW_AT_byte_size' in die.attributes):
            types[name.value.decode(errors='replace')] = die

def base(die):
    while die.tag in ('DW_TAG_typedef', 'DW_TAG_const_type', 'DW_TAG_volatile_type', 'DW_TAG_restrict_type', 'DW_TAG_atomic_type'):
        die = die.get_DIE_from_attribute('DW_AT_type')
    return die

def size(die):
    die = base(die)
    if die.tag == 'DW_TAG_pointer_type': return 4
    if 'DW_AT_byte_size' in die.attributes: return die.attributes['DW_AT_byte_size'].value
    if die.tag == 'DW_TAG_array_type': return count(die) * size(die.get_DIE_from_attribute('DW_AT_type'))
    raise ValueError(die.tag)

def count(die):
    for child in die.iter_children():
        if child.tag == 'DW_TAG_subrange_type':
            if 'DW_AT_count' in child.attributes: return child.attributes['DW_AT_count'].value
            if 'DW_AT_upper_bound' in child.attributes: return child.attributes['DW_AT_upper_bound'].value + 1
    return 0

def decode(die, data):
    die = base(die)
    if die.tag in ('DW_TAG_structure_type', 'DW_TAG_union_type'):
        result = {}
        for member in die.iter_children():
            if member.tag != 'DW_TAG_member': continue
            name = member.attributes.get('DW_AT_name')
            name = name.value.decode() if name else None
            offset = member.attributes.get('DW_AT_data_member_location')
            offset = offset.value if offset else 0
            if not isinstance(offset, int): continue
            typ = member.get_DIE_from_attribute('DW_AT_type')
            if 'DW_AT_bit_size' in member.attributes:
                bits = member.attributes['DW_AT_bit_size'].value
                if 'DW_AT_data_bit_offset' in member.attributes:
                    shift = member.attributes['DW_AT_data_bit_offset'].value
                    result[name] = (int.from_bytes(data, 'little') >> shift) & ((1 << bits) - 1)
                continue
            value = decode(typ, data[offset:offset+size(typ)])
            if name is None and isinstance(value, dict): result.update(value)
            elif name is not None: result[name] = value
        return result
    if die.tag == 'DW_TAG_array_type':
        n = count(die); typ = die.get_DIE_from_attribute('DW_AT_type'); width = size(typ)
        return [decode(typ, data[i*width:(i+1)*width]) for i in range(min(n, 16))]
    encoding = die.attributes.get('DW_AT_encoding')
    return int.from_bytes(data, 'little', signed=encoding is not None and encoding.value == 5)

report = {'firmware_sha256':hashlib.sha256((delivery/'nuttx.bin').read_bytes()).hexdigest()}
with args.output.with_suffix('.openocd.log').open('wb') as log:
    server = subprocess.Popen([str(ocd/'bin/openocd.exe'), '-s', str(ocd/'share/openocd/scripts'),
        '-f', 'board/esp32p4-builtin.cfg', '-c',
        'adapter speed 4000; bindto 127.0.0.1; gdb port disabled; '
        'tcl port 6679; telnet port disabled; init'],
        stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    control = None
    try:
        for _ in range(60):
            if server.poll() is not None: raise RuntimeError('OpenOCD failed; see log')
            try:
                control = socket.create_connection(('127.0.0.1', 6679), timeout=.25)
                control.settimeout(4); break
            except OSError: time.sleep(.1)
        if control is None: raise RuntimeError('No OpenOCD control port')

        def tcl(command):
            control.sendall(command.encode()+b'\x1a'); data=b''
            while not data.endswith(b'\x1a'):
                part=control.recv(65536)
                if not part: raise RuntimeError('OpenOCD disconnected')
                data+=part
            return data[:-1].decode(errors='replace')

        def read(address, length):
            if not address: return b'\0'*length
            words = tcl(f'read_memory {address:#x} 32 {(length+3)//4}').split()
            return b''.join(int(word, 0).to_bytes(4, 'little') for word in words)[:length]

        def obj(name, address): return decode(types[name], read(address, size(types[name])))

        tcl('halt 2000')
        report['cores'] = {}
        for core in range(2):
            tcl(f'targets esp32p4.hp.cpu{core}')
            report['cores'][str(core)] = {reg:tcl(f'reg {reg} force') for reg in ('dpc', 'ra', 'sp', 'mepc', 'mcause', 'mtval', 'a0', 'a1', 'fp', 's1')}
            stack = int(report['cores'][str(core)]['sp'].split()[-1], 16)
            report['cores'][str(core)]['stack_words'] = [int.from_bytes(read(stack+i*4, 4), 'little') for i in range(64)]
        if 'g_first_fault' in symbols:
            data = read(symbols['g_first_fault'], 64)
            report['first_fault'] = [int.from_bytes(data[i:i+4], 'little') for i in range(0,64,4)]
        report['locks'] = {}
        for name, length in [('spinlock', 4), ('g_int_flags_count', 2), ('g_int_flags', 8), ('g_running_tasks', 8)]:
            if name in symbols:
                data = read(symbols[name], length)
                width = 1 if name == 'g_int_flags_count' else 4
                report['locks'][name] = [int.from_bytes(data[i:i+width], 'little') for i in range(0,length,width)]
        if 'tcb_s' in types:
            report['tasks'] = []
            for address in report['locks'].get('g_running_tasks', []):
                task = obj('tcb_s', address)
                report['tasks'].append({k:v for k,v in task.items() if k in ('pid','name','cpu','irqcount','lockcount','task_state')})
        driver = obj('esp_i2s_s', symbols['esp_i2s0_priv']); report['i2s'] = driver
        report['audio_lock_owner'] = []
        count_pid = int.from_bytes(read(symbols['g_npidhash'], 4), 'little')
        pid_table = int.from_bytes(read(symbols['g_pidhash'], 4), 'little')
        for index in range(min(count_pid, 1024)):
            address = int.from_bytes(read(pid_table+index*4, 4), 'little')
            if not address: continue
            task = obj('tcb_s', address)
            if task['pid'] != driver['lock']['sem']['val']['mholder']: continue
            entry = {k:task.get(k) for k in ('pid', 'name', 'task_state', 'lockcount', 'xcp')}
            regs = task['xcp'].get('regs', 0)
            if regs:
                words = [int.from_bytes(read(regs+i*4, 4), 'little') for i in range(33)]
                entry['registers'] = words
                entry['stack_words'] = [int.from_bytes(read(words[2]+i*4, 4), 'little') for i in range(96)]
            report['audio_lock_owner'].append(entry)
        config = obj('esp_i2s_config_s', driver['config']); report['config'] = config
        context = obj('i2s_hal_context_t', config['ctx']); report['context'] = context
        report['registers'] = obj('i2s_dev_t', context['dev'])
        report['buffers'] = []
        codec = 0
        for direction in ('tx', 'rx'):
            for queue in ('act', 'pend', 'done'):
                address = driver[direction][queue]['head']
                for _ in range(4):
                    if not address: break
                    buffer = obj('esp_buffer_s', address)
                    codec = codec or buffer['arg']
                    report['buffers'].append({'direction':direction, 'queue':queue, 'address':address,
                        'buffer':buffer, 'descriptor':[int.from_bytes(read(buffer['dma_link'][0]+i*4, 4), 'little') for i in range(3)]})
                    address = buffer['flink']
        if codec: report['codec'] = obj('es8311_dev_s', codec)
        # Ownership and GAP flags only: omit discovered addresses and names.
        report['ble_control'] = {}
        for name, length in [('ble_owner', 4), ('g_command', 4), ('g_cancel_scan', 1), ('g_cancel_connect', 1)]:
            if name in symbols:
                report['ble_control'][name] = int.from_bytes(read(symbols[name], length), 'little')
        if 'glass_ble_state' in types and 'g_state' in symbols:
            state = obj('glass_ble_state', symbols['g_state'])
            report['ble_control']['state'] = {k:v for k,v in state.items()
                if k in ('started', 'ready', 'scanning', 'connecting', 'connected', 'error', 'count')}
        args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
        print(json.dumps({'firmware_sha256':report['firmware_sha256'], 'cores':report['cores'], 'locks':report['locks'], 'tasks':report.get('tasks'), 'first_fault':report.get('first_fault'), 'registers':report['registers'],
                          'buffers':report['buffers'], 'codec':report.get('codec')}, indent=2))
    finally:
        if control:
            try: tcl('resume'); tcl('shutdown')
            except (OSError, RuntimeError): pass
            control.close()
        try: server.wait(timeout=5)
        except subprocess.TimeoutExpired: server.terminate(); server.wait(timeout=5)
        elf_stream.close()
