set pagination off
set confirm off
set remotetimeout 20
set print elements 80
target remote 127.0.0.1:3333
monitor halt
p g_phy_ready
p g_devices[0].info
p/x g_devices[0].control
p/x g_devices[0].desc.caps.uvc_version
p g_devices[0].selected
p g_devices[0].stats
p g_camera
p g_camera_last_error
info threads
thread apply all bt
p g_running_tasks[0]->xcp
p g_running_tasks[1]->xcp
p/x *g_running_tasks[0]->xcp.regs@34
p/x *g_running_tasks[1]->xcp.regs@34
p/x g_last_regs
x/160wx 0x4ff4faec
monitor targets esp32p4.hp.cpu1
monitor reg mepc force
monitor reg mtval force
monitor reg sp force
monitor reg ra force
monitor targets esp32p4.hp.cpu0
monitor resume
detach
quit
