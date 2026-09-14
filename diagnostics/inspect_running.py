"""Read live firmware state via local USB JTAG, then resume both cores."""
from pathlib import Path
import argparse
import subprocess
import time
import sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--framebuffer', action='store_true')
parser.add_argument('--output', type=Path, default=root / 'running-state.txt')
parser.add_argument('--task-pid', type=int)
parser.add_argument('--ble', action='store_true')
parser.add_argument('--elf', type=Path,
                    default=root.parent / '04-v3-20260913/black-screen-fix/nuttx.elf')
args = parser.parse_args()
capture_frame = args.framebuffer
openocd_root = Path('D:/.espressif/tools/openocd-esp32/v0.12.0-esp32-20250707/openocd-esp32')
gdb = 'D:/.espressif/tools/riscv32-esp-elf-gdb/16.3_20250913/riscv32-esp-elf-gdb/bin/riscv32-esp-elf-gdb.exe'
commands = root / 'inspect-running.gdb'
command_text = '''set pagination off
set confirm off
set remotetimeout 20
target remote 127.0.0.1:3333
monitor halt
p g_lcd_ready
p g_lcd_plane
p g_esp_mipi_dsi
p g_nx_initstate
p g_usbserial_priv
p g_kmmheap
python
for i in range(int(gdb.parse_and_eval('g_npidhash'))):
    task = gdb.parse_and_eval('g_pidhash[%d]' % i)
    if int(task):
        print('TASK', task['pid'], task['name'], 'state', task['task_state'])
end
p g_npidhash
p g_running_tasks[0]->name
p g_running_tasks[1]->name
python
if CAPTURE_FRAME and int(gdb.parse_and_eval('g_lcd_ready')):
    address = int(gdb.parse_and_eval('g_esp_mipi_dsi.fb'))
    size = int(gdb.parse_and_eval('g_esp_mipi_dsi.fb_size'))
    gdb.execute('monitor dump_image diagnostics/desktop-framebuffer.bin 0x%x 0x%x' % (address, size))
end
monitor resume
detach
quit
'''
if args.task_pid is not None:
    task_check = '''python
task = next(gdb.parse_and_eval('g_pidhash[%d]' % i) for i in range(int(gdb.parse_and_eval('g_npidhash'))) if int(gdb.parse_and_eval('g_pidhash[%d]' % i)) and int(gdb.parse_and_eval('g_pidhash[%d]->pid' % i)) == TASK_PID)
print('WAITOBJ', task['waitobj'])
print('SAVED REGS', task['xcp']['regs'])
regs = task['xcp']['regs']
gdb.execute('x/34wx 0x%x' % int(regs))
gdb.execute('info symbol 0x%x' % int(regs[0]))
gdb.execute('info symbol 0x%x' % int(regs[1]))
gdb.execute('x/64wx 0x%x' % int(regs[2]))
end
'''.replace('TASK_PID', str(args.task_pid))
    command_text = command_text.replace('monitor resume', task_check + 'monitor resume')
if args.ble:
    command_text = command_text.replace('monitor resume', '''python
for expression in ['g_netlock', '*(struct bluetooth_conn_s *)g_active_bluetooth_connections.head', 'ble_hci_sock_state', "'glass_ble.c'::g_state", "'glass_ble.c'::g_command", 'ble_hs_timer', 'ble_gap_master']:
    try:
        print(expression, gdb.parse_and_eval(expression))
    except gdb.error as error:
        print('UNAVAILABLE', expression, error)
end
monitor resume''')
commands.write_text(command_text.replace('CAPTURE_FRAME', str(capture_frame)), encoding='utf-8')
with args.output.with_suffix('.openocd.log').open('w') as log:
    server = subprocess.Popen([str(openocd_root / 'bin/openocd.exe'), '-s',
        str(openocd_root / 'share/openocd/scripts'), '-f', 'board/esp32p4-builtin.cfg',
        '-c', 'adapter speed 6000; bindto 127.0.0.1; gdb_memory_map disable; gdb_flash_program disable; gdb port 3333; tcl port disabled; telnet port disabled; esp32p4.hp.cpu0 configure -event gdb-attach {halt}; esp32p4.hp.cpu1 configure -event gdb-attach {halt}; init'],
        stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        time.sleep(1)
        with args.output.open('wb') as output:
            result = subprocess.run([gdb, '-q', '-batch',
                str(args.elf), '-x', str(commands)],
                stdout=output, stderr=subprocess.STDOUT, timeout=120 if capture_frame else 60)
        result.stdout = args.output.read_bytes()
        print(result.stdout.decode('utf-8', errors='replace')[:18000])
    finally:
        server.terminate()
        server.wait(timeout=10)
raise SystemExit(result.returncode)
