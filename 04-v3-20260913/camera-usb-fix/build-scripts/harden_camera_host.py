"""Apply bounded enumeration and asynchronous transport error handling."""
from pathlib import Path
ws = Path(__file__).resolve().parent.parent
vendor = ws / '04-v3-20260913/camera-usb-fix/overlay/nuttx/arch/risc-v/src/esp32p4/camera_usb/cherryusb'
p = vendor / 'core/usbh_core.c'
s = p.read_text()
s = s.replace('    if (desc->bLength != USB_SIZEOF_CONFIG_DESC)', '    if (desc->bNumInterfaces > CONFIG_USBHOST_MAX_INTERFACES) return -USB_ERR_INVAL;\n    if (desc->bLength != USB_SIZEOF_CONFIG_DESC)', 1)
s = s.replace('hport->config.config_desc.bNumInterfaces; i++', 'CONFIG_USBHOST_MAX_INTERFACES; i++')
s = s.replace('        USB_ASSERT_MSG(intf_desc->bInterfaceNumber == i, "Interface number mismatch, do not support non-standard device\\r\\n");', '        if (!intf_desc->bLength) continue;\n        if (intf_desc->bInterfaceNumber != i) { ret = -USB_ERR_INVAL; goto errout; }')
s = s.replace('cur_ep >= CONFIG_USBHOST_MAX_ENDPOINTS)', 'cur_ep >= CONFIG_USBHOST_MAX_ENDPOINTS || cur_ep >= cur_ep_num)')
p.write_text(s)
p = vendor / 'port/dwc2/usb_hc_dwc2.c'
s = p.read_text()
# A stale channel interrupt after disconnect must not dereference a freed URB.
s = s.replace('    urb = chan->urb;\n    //printf', '    urb = chan->urb;\n    if (!urb) { USB_OTG_HC(ch_num)->HCINT = chan_intstatus; return; }\n    //printf')
# Do not ignore reset/FIFO errors during boot.
s = s.replace('    ret = dwc2_flush_txfifo(bus, 0x10U);\n    ret = dwc2_flush_rxfifo(bus);', '    ret = dwc2_flush_txfifo(bus, 0x10U);\n    if (ret < 0) return ret;\n    ret = dwc2_flush_rxfifo(bus);\n    if (ret < 0) return ret;')
p.write_text(s)
print('Host enumeration and IRQ guards applied')
