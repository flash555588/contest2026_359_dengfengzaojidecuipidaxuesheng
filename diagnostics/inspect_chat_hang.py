"""Read stack/lock metadata from a stalled board, without reset or LVGL calls."""
from pathlib import Path
import subprocess, time, socket
ws=Path(__file__).resolve().parent.parent
delivery=ws/'04-v3-20260913/espdl-quickapp'
out=delivery/'evidence/chat-hang-1'
out.mkdir(exist_ok=True)
ocd=Path('D:/.espressif/tools/openocd-esp32/v0.12.0-esp32-20250707/openocd-esp32')
gdb=Path('D:/.espressif/tools/riscv32-esp-elf-gdb/16.3_20250913/riscv32-esp-elf-gdb/bin/riscv32-esp-elf-gdb.exe')
script=out/'inspect.gdb'
script.write_text('''set pagination off
set confirm off
set remotetimeout 10
target remote 127.0.0.1:3333
monitor halt
thread apply all bt 12
python
for expr in ["g_running_tasks[0]->name", "g_running_tasks[1]->name", "'glass_qpk_builder.c'::info.busy", "'glass_qpk_builder.c'::info.revision", "'glass_qpk_builder.c'::info.saved_revision", "'glass_qpk_builder.c'::state_lock", "'glass_qpk_builder.c'::io_lock", "'glass_chat_service.c'::chat_lock", "g_chat_ui.refresh_max_ms", "g_chat_ui.rebuilds", "g_chat_ui.live_updates"]:
    try: print(expr, gdb.parse_and_eval(expr))
    except gdb.error as e: print(e)
for i in range(int(gdb.parse_and_eval('g_npidhash'))):
    t = gdb.parse_and_eval('g_pidhash[%d]' % i)
    if int(t):
        print('TASK', t['pid'], t['name'], 'state', t['task_state'], 'stack', t['stack_base_ptr'], 'size', t['adj_stack_size'])
        try:
            regs=t['xcp']['regs']
            if int(regs):
                gdb.execute('info symbol 0x%x' % int(regs[0]))
                gdb.execute('info symbol 0x%x' % int(regs[1]))
        except gdb.error as e: print(e)
end
monitor resume
detach
quit
''',encoding='utf-8')
with (out/'openocd.log').open('w') as log:
    server=subprocess.Popen([str(ocd/'bin/openocd.exe'),'-s',str(ocd/'share/openocd/scripts'),
        '-f','board/esp32p4-builtin.cfg','-c','adapter speed 6000; bindto 127.0.0.1; gdb_memory_map disable; gdb_flash_program disable; gdb port 3333; tcl port 6666; telnet port disabled; init'],stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        time.sleep(1)
        result=subprocess.run([str(gdb),'-q','-batch',str(delivery/'nuttx.elf'),'-x',str(script)],capture_output=True,text=True,timeout=45)
        (out/'stacks.log').write_text(result.stdout+'\n'+result.stderr,encoding='utf-8')
        print(result.stdout[-16000:]); print(result.stderr[-1000:])
    finally:
        try:
            with socket.create_connection(('127.0.0.1',6666),timeout=2) as connection:
                connection.sendall(b'resume\x1a'); connection.recv(4096)
                connection.sendall(b'shutdown\x1a')
        except OSError: pass
        try: server.wait(timeout=4)
        except subprocess.TimeoutExpired: server.terminate()
