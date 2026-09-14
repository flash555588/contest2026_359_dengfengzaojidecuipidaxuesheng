set pagination off
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
if False and int(gdb.parse_and_eval('g_lcd_ready')):
    address = int(gdb.parse_and_eval('g_esp_mipi_dsi.fb'))
    size = int(gdb.parse_and_eval('g_esp_mipi_dsi.fb_size'))
    gdb.execute('monitor dump_image diagnostics/desktop-framebuffer.bin 0x%x 0x%x' % (address, size))
end
monitor resume
detach
quit
