"""Vendor the Apache-2.0 host core, and apply the NuttX/P4 integration."""
from pathlib import Path
import shutil
import json

ws = Path(__file__).resolve().parent.parent
delivery = ws / '04-v3-20260913/camera-usb-fix'
overlay = delivery / 'overlay'
ref = ws / 'diagnostics/cherryusb-reference/source'
chip = overlay / 'nuttx/arch/risc-v/src/esp32p4'
vendor = chip / 'camera_usb/cherryusb'
for folder in ['common', 'core', 'class/hub', 'port/dwc2']:
    for p in (ref / folder).glob('*'):
        if p.is_file() and p.suffix in ['.h', '.c'] and p.name not in ['usbd_core.c', 'usb_dc_dwc2.c', 'usb_glue_esp.c']:
            dst = vendor / folder / p.name
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(p, dst)
shutil.copyfile(ref / 'LICENSE', vendor / 'LICENSE')
shutil.copyfile(ws / 'diagnostics/cherryusb-reference/manifest.json', vendor / 'UPSTREAM.json')
osal = vendor / 'usb_osal_nuttx.c'
shutil.copyfile(ref / 'osal/usb_osal_nuttx.c', osal)
s = osal.read_text()
s = s.replace('#include <nuttx/kmalloc.h>', '#include <nuttx/kmalloc.h>\n#include <nuttx/mutex.h>')
s = s.replace('nxsem_wait(__sem)', 'nxsem_wait_uninterruptible(__sem)')
s = s.replace('nxsem_tickwait(__sem, MSEC2TICK(timeout))', 'nxsem_tickwait_uninterruptible(__sem, MSEC2TICK(timeout))')
s = s.replace('O_RDWR | O_CREAT,', 'O_RDWR | O_CREAT,')
s = s.replace('msec2spec(&__timeout, timeout);', 'msec2spec(&__timeout, MSEC2TICK(timeout));\n            if (__timeout.tv_nsec >= 1000000000) { __timeout.tv_sec++; __timeout.tv_nsec -= 1000000000; }')
# One hub consumer; prevent ISR from blocking if the queue is full.
s = s.replace('file_mq_send(&mq_adpt->mq, (const char *)&addr, mq_adpt->msgsize, 0)', 'file_mq_ticksend(&mq_adpt->mq, (const char *)&addr, mq_adpt->msgsize, 0, 0)')
osal.write_text(s)
p = vendor / 'core/usbh_core.c'
s = p.read_text()
start = s.index('#ifdef __ARMCC_VERSION /* ARM C Compiler */', s.index('int usbh_initialize('))
end = s.index('    usbh_hub_initialize(bus);\n    return 0;', start)
s = s[:start] + '''    extern const struct usbh_class_info camera_usb_classes[2];
    usbh_class_info_table_begin = (struct usbh_class_info *)camera_usb_classes;
    usbh_class_info_table_end = usbh_class_info_table_begin + 2;
    return usbh_hub_initialize(bus);''' + s[end + len('    usbh_hub_initialize(bus);\n    return 0;'):]
s = s.replace('while (p[DESC_bLength]) {', '''while (desc_len < length) {
            if (length - desc_len < 2 || p[0] < 2 || p[0] > length - desc_len) return -USB_ERR_INVAL;''', 1)
s = s.replace('if (cur_ep >= CONFIG_USBHOST_MAX_ENDPOINTS) {', 'if (cur_iface >= CONFIG_USBHOST_MAX_INTERFACES || cur_alt_setting >= CONFIG_USBHOST_MAX_INTF_ALTSETTINGS || cur_ep >= CONFIG_USBHOST_MAX_ENDPOINTS) {')
p.write_text(s)
p = vendor / 'class/hub/usbh_hub.c'
s = p.read_text().replace('    usb_hc_init(bus);', '    ret = usb_hc_init(bus);\n    if (ret < 0) { USB_LOG_ERR("USB host init failed: %d\\n", ret); return; }')
p.write_text(s)
p = vendor / 'port/dwc2/usb_hc_dwc2.c'
s = p.read_text()
s = s.replace('static inline void dwc2_set_mode', 'static inline int dwc2_set_mode')
start = s.index('    while (1) {', s.index('static inline int dwc2_set_mode'))
end = s.index('\n}', start)
s = s[:start] + '''    for (int retry = 0; retry < 100; retry++) {
        if ((USB_OTG_GLB->GINTSTS & 1U) == USB_OTG_MODE_HOST) return 0;
        usb_osal_msleep(1);
    }
    return -USB_ERR_TIMEOUT;''' + s[end:]
s = s.replace('    dwc2_set_mode(bus, USB_OTG_MODE_HOST);', '    if (ret < 0) return ret;\n    ret = dwc2_set_mode(bus, USB_OTG_MODE_HOST);\n    if (ret < 0) return ret;')
# A single IN isochronous service interval, 1..3 HS transactions. The
# completion callback rearms immediately, keeping frame payload boundaries.
start = s.index('#if 0\nstatic void dwc2_iso_urb_init')
end = s.index('#endif', start) + len('#endif')
s = s[:start] + '''static void dwc2_iso_urb_init(struct usbh_bus *bus, uint8_t chidx, struct usbh_urb *urb)
{
    struct dwc2_chan *chan = &g_dwc2_hcd[bus->hcd.hcd_id].chan_pool[chidx];
    uint16_t mps = USB_GET_MAXPACKETSIZE(urb->ep->wMaxPacketSize);
    uint8_t mult = 1 + ((urb->ep->wMaxPacketSize >> 11) & 3);
    chan->num_packets = mult;
    chan->xferlen = mps * mult;
    dwc2_chan_init(bus, chidx, urb->hport->dev_addr, urb->ep->bEndpointAddress,
                   USB_ENDPOINT_TYPE_ISOCHRONOUS, mps, mult, urb->hport->speed);
    /* For HS IN, DPID advertises the maximum transaction count. */
    uint8_t pid = mult == 3 ? HC_PID_DATA2 : mult == 2 ? HC_PID_DATA1 : HC_PID_DATA0;
    dwc2_chan_transfer(bus, chidx, urb->ep->bEndpointAddress,
                       urb->transfer_buffer, chan->xferlen, mult, pid);
}''' + s[end:]
s = s.replace('    bus = urb->hport->bus;\n\n    if (!(USB_OTG_HPRT', '''    bus = urb->hport->bus;
    if (USB_GET_ENDPOINT_TYPE(urb->ep->bmAttributes) == USB_ENDPOINT_TYPE_ISOCHRONOUS) {
        unsigned mult = 1 + ((urb->ep->wMaxPacketSize >> 11) & 3);
        unsigned mps = USB_GET_MAXPACKETSIZE(urb->ep->wMaxPacketSize);
        if (!(urb->ep->bEndpointAddress & 0x80) || !mps || mult > 3 ||
            urb->transfer_buffer_length < mps * mult || urb->ep->bInterval != 1 ||
            urb->hport->speed != USB_SPEED_HIGH) return -USB_ERR_NOTSUPP;
    }

    if (!(USB_OTG_HPRT''')
s = s.replace('        case USB_ENDPOINT_TYPE_ISOCHRONOUS:\n            break;', '        case USB_ENDPOINT_TYPE_ISOCHRONOUS:\n            dwc2_iso_urb_init(bus, chidx, urb);\n            break;', 1)
s = s.replace('} else if (USB_GET_ENDPOINT_TYPE(urb->ep->bmAttributes) == USB_ENDPOINT_TYPE_ISOCHRONOUS) {\n            } else {', '''} else if (USB_GET_ENDPOINT_TYPE(urb->ep->bmAttributes) == USB_ENDPOINT_TYPE_ISOCHRONOUS) {
                usb_dcache_invalidate((uintptr_t)urb->transfer_buffer, USB_ALIGN_UP(urb->actual_length, CONFIG_USB_ALIGN_SIZE));
                urb->errorcode = 0;
                dwc2_urb_waitup(urb);
            } else {''')
s = s.replace('    usb_hc_low_level_init(bus);', '    extern int camera_usb_phy_ready(void);\n    usb_hc_low_level_init(bus);\n    if (!camera_usb_phy_ready()) return -USB_ERR_IO;')
p.write_text(s)
base = ws / 'diagnostics/camera-baseline'
for relative in ['nuttx/arch/risc-v/src/esp32p4/Make.defs',
                 'nuttx/arch/risc-v/src/esp32p4/Kconfig',
                 'nuttx/boards/risc-v/esp32p4/esp32p4-function-ev-board/src/esp32p4_bringup.c']:
    dst = overlay / relative
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(base / relative, dst)
p = chip / 'Make.defs'
p.write_text(p.read_text() + '''
ifeq ($(CONFIG_ESP32P4_CAMERA_USB),y)
CAMUSB = chip/camera_usb
CAMCHERRY = $(CAMUSB)/cherryusb
INCLUDES += -I$(ARCH_SRCDIR)/$(CAMUSB) -I$(ARCH_SRCDIR)/$(CAMCHERRY)/common
INCLUDES += -I$(ARCH_SRCDIR)/$(CAMCHERRY)/core -I$(ARCH_SRCDIR)/$(CAMCHERRY)/class/hub
INCLUDES += -I$(ARCH_SRCDIR)/$(CAMCHERRY)/port/dwc2
CHIP_CSRCS += $(CAMCHERRY)/core/usbh_core.c $(CAMCHERRY)/class/hub/usbh_hub.c
CHIP_CSRCS += $(CAMCHERRY)/port/dwc2/usb_hc_dwc2.c $(CAMCHERRY)/usb_osal_nuttx.c
CHIP_CSRCS += $(CAMUSB)/camera_usb_port.c $(CAMUSB)/camera_uvc.c $(CAMUSB)/camera_uvc_protocol.c
endif
''')
p = chip / 'Kconfig'
p.write_text(p.read_text() + '''
config ESP32P4_CAMERA_USB
	bool "USB 2.0 UVC camera host (CherryUSB DWC2)"
	default n
	help
		P4 HS host, UVC MJPEG capture, hotplug inventory and camera switching.
		Uses the dedicated OTG HS port; USB Serial/JTAG stays on its own PHY.
''')
p = overlay / 'nuttx/boards/risc-v/esp32p4/esp32p4-function-ev-board/src/esp32p4_bringup.c'
s = p.read_text()
pos = s.index('{', s.index('int esp_bringup(')) + 1
s = s[:pos] + '''
#ifdef CONFIG_ESP32P4_CAMERA_USB
  extern int camera_usb_initialize(void);
  int usb_camera_ret = camera_usb_initialize();
  if (usb_camera_ret < 0)
    syslog(LOG_ERR, "USB camera host unavailable: %d\\n", usb_camera_ret);
#endif
''' + s[pos:]
p.write_text(s)
p = delivery / 'resolved.config'
s = p.read_text()
if 'CONFIG_ESP32P4_CAMERA_USB=y' not in s: s += '\nCONFIG_ESP32P4_CAMERA_USB=y\n'
p.write_text(s)
target = overlay / 'nuttx/.config'
shutil.copyfile(p, target)
print('Host integration prepared')
