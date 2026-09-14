set pagination off
set confirm off
set remotetimeout 20
set print elements 80
target remote 127.0.0.1:3333
monitor halt
p g_phy_ready
p/x *(unsigned int *)0x5000000c
p/x *(unsigned int *)0x50000010
p/x *(unsigned int *)0x50000014
p/x *(unsigned int *)0x50000020
p/x *(unsigned int *)0x50000048
p/x *(unsigned int *)0x50000050
p/x *(unsigned int *)0x50000400
p/x *(unsigned int *)0x50000404
p/x *(unsigned int *)0x50000408
p/x *(unsigned int *)0x50000440
p/x *(unsigned int *)0x50000500
p/x *(unsigned int *)0x50000510
p/x *(unsigned int *)0x50000518
p/x *(unsigned int *)0x5000051c
p g_dwc2_hcd[0].user_params
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
if g_last_regs[1][2] >= 0x4ff40000 && g_last_regs[1][2] < 0x4ffffff0 - 640
x/160wx g_last_regs[1][2]
end
monitor targets esp32p4.hp.cpu1
monitor reg mepc force
monitor reg mtval force
monitor reg sp force
monitor reg ra force
monitor targets esp32p4.hp.cpu0
monitor resume
detach
quit
