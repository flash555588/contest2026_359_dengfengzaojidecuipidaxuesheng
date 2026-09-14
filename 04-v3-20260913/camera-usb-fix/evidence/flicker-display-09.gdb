set pagination off
set confirm off
set remotetimeout 20
target remote 127.0.0.1:3333
python
import time
for sample in range(3):
    gdb.execute('monitor halt')
    for name in ['g_esp_mipi_dsi_frames', 'g_esp_mipi_dsi_underruns', 'g_camera.frames', 'g_camera.fb_page', 'g_esp_mipi_dsi.fb', 'g_esp_mipi_dsi.pending_fb', 'g_lcd_plane.yoffset']:
        print('%s=%s' % (name, gdb.parse_and_eval(name)))
    gdb.execute('monitor resume')
    if sample < 2: time.sleep(4)
end
detach
quit
