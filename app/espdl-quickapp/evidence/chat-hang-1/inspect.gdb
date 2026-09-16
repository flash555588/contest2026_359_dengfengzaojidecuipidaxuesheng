set pagination off
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
