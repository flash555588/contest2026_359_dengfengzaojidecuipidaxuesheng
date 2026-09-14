set pagination off
set confirm off
set remotetimeout 20
target remote 127.0.0.1:3333
monitor halt
python
address = int(gdb.parse_and_eval('g_esp_mipi_dsi.fb'))
size = int(gdb.parse_and_eval('g_esp_mipi_dsi.fb_size'))
assert size == 1024 * 600 * 2
gdb.execute('monitor dump_image "04-v3-20260913/ui-layout-fix/evidence/screenshots-round2/light-dropdown.rgb565" 0x%x 0x%x' % (address, size))
end
monitor resume
detach
quit
