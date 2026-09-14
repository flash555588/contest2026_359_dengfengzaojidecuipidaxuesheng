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
p g_devices[0].port->config.intf[1]
monitor resume
detach
quit
