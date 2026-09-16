/* SPDX-License-Identifier: Apache-2.0
 * UVC MJPEG transport. Only the IRQ assembles payloads. read() claims a
 * complete buffer, copies outside the IRQ lock, then releases it. A device
 * slot is never reused while an old file descriptor still references it.
 */
#include <nuttx/config.h>
#include <nuttx/fs/fs.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/video/uvc_camera.h>
#include <poll.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include "usbh_core.h"
#include "camera_uvc_protocol.h"

#define UVC_BUFFERS 3
#define UVC_TRANSFER_MAX 65536
struct camera_uvc
{
  mutex_t lock;
  struct usbh_hubport *port;
  struct uvc_camera_device info;
  struct uvc_descriptor_info desc;
  struct uvc_camera_start selected;
  struct uvc_camera_stats stats;
  struct usbh_urb urb;
  struct usb_endpoint_descriptor endpoint;
  uint8_t control[64] __attribute__((aligned(64)));
  uint8_t *transfer;
  uint8_t *frames[UVC_BUFFERS];
  size_t lengths[UVC_BUFFERS];
  struct uvc_payload_state payload;
  struct pollfd *pollers[2];
  int assembling, ready, reading;
  unsigned refs;
  bool running, claimed;
};
struct camera_uvc_file { struct camera_uvc *dev; uint32_t generation; bool owner; };
static struct camera_uvc g_devices[UVC_CAMERA_MAX_DEVICES] __attribute__((aligned(64)));
static struct camera_uvc *g_stream;
static uint32_t g_generation;

static uint16_t get16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t get32(const uint8_t *p) { return get16(p) | (uint32_t)get16(p + 2) << 16; }
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = v >> (8 * i); }

static void uvc_notify(struct camera_uvc *d, pollevent_t events)
{ poll_notify(d->pollers, 2, events); }

static void uvc_complete(void *arg, int bytes)
{
  struct camera_uvc *d = arg;
  irqstate_t flags = enter_critical_section();
  if (!d->running) { leave_critical_section(flags); return; }
  d->stats.packets++;
  if (bytes < 0)
    { d->stats.errors++; d->payload.damaged = true; }
  else if (bytes > 0)
    {
      int size = camera_uvc_payload(&d->payload, d->transfer, bytes,
                                    d->frames[d->assembling], d->selected.max_frame_size);
      if (size < 0) d->stats.dropped++;
      if (size > 0)
        {
          if (d->ready >= 0) d->stats.dropped++;
          d->ready = d->assembling;
          d->lengths[d->ready] = size;
          d->stats.frames++;
          for (int i = 0; i < UVC_BUFFERS; i++)
            if (i != d->ready && i != d->reading) { d->assembling = i; break; }
          uvc_notify(d, POLLIN);
        }
    }
  if (!d->info.connected || !d->port->connected)
    { d->running = false; d->stats.error = -ENODEV; uvc_notify(d, POLLHUP); leave_critical_section(flags); return; }
  d->urb.transfer_buffer_length = d->selected.max_payload_size;
  if (usbh_submit_urb(&d->urb) < 0)
    { d->running = false; d->stats.error = -EIO; uvc_notify(d, POLLERR); }
  leave_critical_section(flags);
}

static int uvc_control(struct camera_uvc *d, uint8_t type, uint8_t request,
                       uint16_t value, uint16_t index, unsigned size)
{
  struct usb_setup_packet *s = d->port->setup;
  s->bmRequestType = type; s->bRequest = request;
  s->wValue = value; s->wIndex = index; s->wLength = size;
  int n = usbh_control_transfer(d->port, s, size ? d->control : NULL);
  if (n < 0 || ((type & 0x80) && n != (int)size))
    printf("[uvc] control type=%02x req=%02x value=%04x size=%u result=%d\n",
           type, request, value, size, n);
  return n < 0 ? -EIO : (type & 0x80) && n != (int)size ? -EPROTO : 0;
}

/* Called under dev->lock. Kill DMA before freeing its buffers, including
 * on unplug. close/STOP remain successful after the physical device leaves. */
static void uvc_stop(struct camera_uvc *d)
{
  irqstate_t flags = enter_critical_section();
  d->running = false;
  if (d->urb.hcpriv) usbh_kill_urb(&d->urb);
  d->ready = -1;
  if (g_stream == d) g_stream = NULL;
  uvc_notify(d, POLLHUP);
  leave_critical_section(flags);
  if (d->claimed && d->port && d->info.connected && d->port->connected)
    uvc_control(d, 1, 11, 0, d->desc.stream_interface, 0);
  d->claimed = false;
  for (int i = 0; i < UVC_BUFFERS; i++) { kumm_free(d->frames[i]); d->frames[i] = NULL; }
  kmm_free(d->transfer); d->transfer = NULL;
  memset(&d->urb, 0, sizeof(d->urb));
}

static int uvc_start(struct camera_uvc *d, struct uvc_camera_start *req)
{
  if (!d->info.connected || !d->port) return -ENODEV;
  if (req->generation != d->info.generation) return -ESTALE;
  if (req->mode >= d->desc.caps.count) return -EINVAL;
  const struct uvc_camera_mode *m = &d->desc.caps.modes[req->mode];
  if (!camera_uvc_interval_valid(m, req->interval)) return -EINVAL;
  irqstate_t flags = enter_critical_section();
  bool busy = g_stream != NULL;
  if (!busy) g_stream = d;
  leave_critical_section(flags);
  if (busy) return -EBUSY;
  int ret = -EIO;
  uint16_t length = d->desc.caps.uvc_version >= 0x150 ? 48 :
                    d->desc.caps.uvc_version >= 0x110 ? 34 : 26;
  d->claimed = true;
  /* GET_LEN is optional in UVC 1.0. Use its declared version on STALL. */
  if (d->desc.caps.uvc_version >= 0x110 &&
      uvc_control(d, 0xa1, 0x85, 0x100, d->desc.stream_interface, 2) == 0)
    {
      uint16_t reported = get16(d->control);
      if (reported == 26 || reported == 34 || reported == 48) length = reported;
      else printf("[uvc] ignoring invalid GET_LEN %u; use UVC version length %u\n", reported, length);
    }
  memset(d->control, 0, sizeof(d->control));
  d->control[0] = 1; d->control[2] = m->format_index; d->control[3] = m->frame_index;
  put32(d->control + 4, req->interval);
  if (uvc_control(d, 0x21, 1, 0x100, d->desc.stream_interface, length) < 0 ||
      uvc_control(d, 0xa1, 0x81, 0x100, d->desc.stream_interface, length) < 0) goto fail;
  if (d->control[2] != m->format_index || d->control[3] != m->frame_index ||
      !camera_uvc_interval_valid(m, get32(d->control + 4)))
    {
      printf("[uvc] Probe mismatch format=%u/%u frame=%u/%u interval=%lu/%lu\n",
        d->control[2], m->format_index, d->control[3], m->frame_index,
        (unsigned long)get32(d->control + 4), (unsigned long)req->interval);
      ret = -EPROTO; goto fail;
    }
  d->selected = *req;
  d->selected.interval = get32(d->control + 4);
  d->selected.width = m->width; d->selected.height = m->height;
  d->selected.max_frame_size = get32(d->control + 18);
  d->selected.max_payload_size = get32(d->control + 22);
  if (!d->selected.max_frame_size) d->selected.max_frame_size = m->max_frame_size;
  if (!d->selected.max_frame_size || d->selected.max_frame_size > UVC_CAMERA_MAX_FRAME ||
      d->selected.max_payload_size < 2 || d->selected.max_payload_size > UVC_TRANSFER_MAX)
    { ret = -EOVERFLOW; goto fail; }
  unsigned best_capacity = UINT32_MAX;
  int best_alt = -1;
  struct usbh_interface *intf = &d->port->config.intf[d->desc.stream_interface];
  for (unsigned a = 0; a < intf->altsetting_num; a++)
    {
      struct usbh_interface_altsetting *alt = &intf->altsetting[a];
      for (unsigned e = 0; e < alt->intf_desc.bNumEndpoints; e++)
        {
          struct usb_endpoint_descriptor *ep = &alt->ep[e].ep_desc;
          if (ep->bEndpointAddress != d->desc.endpoint) continue;
          unsigned type = ep->bmAttributes & 3;
          unsigned mult = 1 + ((ep->wMaxPacketSize >> 11) & 3);
          unsigned capacity = (ep->wMaxPacketSize & 2047) * mult;
          if (type == 2) capacity = d->selected.max_payload_size;
          if (type != 1 && type != 2) continue;
          if (type == 1 && (d->port->speed != USB_SPEED_HIGH || ep->bInterval != 1 || mult > 3)) continue;
          if (capacity >= d->selected.max_payload_size && capacity < best_capacity)
            { best_alt = a; best_capacity = capacity; d->endpoint = *ep; }
        }
    }
  if (best_alt < 0) { ret = -ENOTSUP; goto fail; }
  if (uvc_control(d, 0x21, 1, 0x200, d->desc.stream_interface, length) < 0) goto fail;
  if ((d->endpoint.bmAttributes & 3) == 1) d->selected.max_payload_size = best_capacity;
  else
    {
      unsigned mps = d->endpoint.wMaxPacketSize & 2047;
      if (!mps || d->selected.max_payload_size % mps) { ret = -ENOTSUP; goto fail; }
    }
  d->transfer = kmm_memalign(64, (d->selected.max_payload_size + 63) & ~63);
  for (int i = 0; i < UVC_BUFFERS; i++)
    d->frames[i] = kumm_memalign(64, d->selected.max_frame_size);
  if (!d->transfer || !d->frames[0] || !d->frames[1] || !d->frames[2])
    { ret = -ENOMEM; goto fail; }
  if (uvc_control(d, 1, 11, best_alt, d->desc.stream_interface, 0) < 0) goto fail;
  memset(&d->urb, 0, sizeof(d->urb));
  memset(&d->stats, 0, sizeof(d->stats));
  memset(&d->payload, 0, sizeof(d->payload)); d->payload.fid = -1;
  d->ready = d->reading = -1; d->assembling = 0;
  d->urb.hport = d->port; d->urb.ep = &d->endpoint;
  d->urb.transfer_buffer = d->transfer;
  d->urb.transfer_buffer_length = d->selected.max_payload_size;
  d->urb.complete = uvc_complete; d->urb.arg = d;
  flags = enter_critical_section();
  d->running = true;
  ret = usbh_submit_urb(&d->urb);
  leave_critical_section(flags);
  if (ret < 0) { ret = -EIO; goto fail; }
  *req = d->selected;
  printf("[uvc] %s MJPEG %ux%u interval=%lu payload=%lu alt=%d\n", d->info.name,
         req->width, req->height, (unsigned long)req->interval,
         (unsigned long)req->max_payload_size, best_alt);
  return 0;
fail:
  printf("[uvc] stream start failed: %d\n", ret);
  uvc_stop(d);
  return ret;
}

static int uvc_open(struct file *file)
{
  struct camera_uvc *d = file->f_inode->i_private;
  struct camera_uvc_file *f = kmm_zalloc(sizeof(*f));
  if (!f) return -ENOMEM;
  nxmutex_lock(&d->lock);
  if (!d->info.connected) { nxmutex_unlock(&d->lock); kmm_free(f); return -ENODEV; }
  f->dev = d; f->generation = d->info.generation; d->refs++;
  file->f_priv = f;
  nxmutex_unlock(&d->lock);
  return 0;
}
static int uvc_close(struct file *file)
{
  struct camera_uvc_file *f = file->f_priv;
  struct camera_uvc *d = f->dev;
  nxmutex_lock(&d->lock);
  if (f->owner) uvc_stop(d);
  d->refs--;
  nxmutex_unlock(&d->lock);
  kmm_free(f);
  return 0;
}
static ssize_t uvc_read(struct file *file, char *buffer, size_t length)
{
  struct camera_uvc_file *f = file->f_priv;
  struct camera_uvc *d = f->dev;
  nxmutex_lock(&d->lock);
  ssize_t ret = -EAGAIN;
  irqstate_t flags = enter_critical_section();
  int frame = d->ready;
  if (!f->owner) ret = -EPERM;
  else if (!d->info.connected) ret = -ENODEV;
  else if (!d->running) ret = d->stats.error ? d->stats.error : -EPIPE;
  else if (frame >= 0)
    {
      ret = d->lengths[frame];
      if (length < (size_t)ret) ret = -EMSGSIZE;
      else { d->ready = -1; d->reading = frame; }
    }
  leave_critical_section(flags);
  if (ret > 0)
    {
      memcpy(buffer, d->frames[frame], ret);
      flags = enter_critical_section(); d->reading = -1; leave_critical_section(flags);
    }
  nxmutex_unlock(&d->lock);
  return ret;
}
static int uvc_ioctl(struct file *file, int cmd, unsigned long arg)
{
  struct camera_uvc_file *f = file->f_priv;
  struct camera_uvc *d = f->dev;
  int ret = 0;
  if (!arg && cmd != UVCIOC_STOP) return -EINVAL;
  nxmutex_lock(&d->lock);
  if (cmd == UVCIOC_STOP)
    { if (f->owner) { uvc_stop(d); f->owner = false; } }
  else if (!d->info.connected) ret = -ENODEV;
  else if (f->generation != d->info.generation) ret = -ESTALE;
  else if (cmd == UVCIOC_CAPS) memcpy((void *)arg, &d->desc.caps, sizeof(d->desc.caps));
  else if (cmd == UVCIOC_START)
    { ret = uvc_start(d, (void *)arg); if (!ret) f->owner = true; }
  else if (cmd == UVCIOC_STATS)
    { irqstate_t flags = enter_critical_section(); memcpy((void *)arg, &d->stats, sizeof(d->stats)); leave_critical_section(flags); }
  else ret = -ENOTTY;
  nxmutex_unlock(&d->lock);
  return ret;
}
static int uvc_poll(struct file *file, struct pollfd *fds, bool setup)
{
  struct camera_uvc *d = ((struct camera_uvc_file *)file->f_priv)->dev;
  int ret = 0;
  irqstate_t flags = enter_critical_section();
  if (setup)
    {
      unsigned i;
      for (i = 0; i < 2; i++) if (!d->pollers[i]) break;
      if (i == 2) ret = -EBUSY;
      else
        {
          d->pollers[i] = fds; fds->priv = &d->pollers[i];
          if (!d->info.connected || !d->running) poll_notify(&fds, 1, POLLHUP);
          else if (d->ready >= 0) poll_notify(&fds, 1, POLLIN);
        }
    }
  else if (fds->priv) { *(struct pollfd **)fds->priv = NULL; fds->priv = NULL; }
  leave_critical_section(flags);
  return ret;
}
static int uvc_list_ioctl(struct file *file, int cmd, unsigned long arg)
{
  if (cmd != UVCIOC_LIST) return -ENOTTY;
  if (!arg) return -EINVAL;
  struct uvc_camera_list *list = (void *)arg;
  memset(list, 0, sizeof(*list));
  irqstate_t flags = enter_critical_section();
  for (int i = 0; i < UVC_CAMERA_MAX_DEVICES; i++)
    if (g_devices[i].info.connected) list->devices[list->count++] = g_devices[i].info;
  leave_critical_section(flags);
  return 0;
}
static const struct file_operations g_uvc_ops =
{ .open = uvc_open, .close = uvc_close, .read = uvc_read, .ioctl = uvc_ioctl, .poll = uvc_poll };
static const struct file_operations g_uvc_list_ops = { .ioctl = uvc_list_ioctl };

static void uvc_name(struct camera_uvc *d)
{
  snprintf(d->info.name, sizeof(d->info.name), "USB Camera %04X:%04X", d->info.vid, d->info.pid);
  uint8_t index = d->port->device_desc.iProduct;
  if (!index) return;
  /* Query string language, then UTF-16 product; failure preserves fallback. */
  if (uvc_control(d, 0x80, 6, 0x300, 0, 4) < 0 || d->control[0] < 4) return;
  uint16_t lang = get16(d->control + 2);
  if (uvc_control(d, 0x80, 6, 0x300 | index, lang, 2) < 0) return;
  unsigned len = d->control[0];
  if (len < 4 || len > sizeof(d->control) || (len & 1)) return;
  if (uvc_control(d, 0x80, 6, 0x300 | index, lang, len) < 0 || d->control[1] != 3) return;
  char name[64] = {0}; unsigned used = 0;
  for (unsigned i = 2; i < len; i += 2)
    {
      uint32_t c = get16(d->control + i);
      if (c < 32 || (c >= 0xd800 && c <= 0xdfff)) continue;
      unsigned count = c < 0x80 ? 1 : c < 0x800 ? 2 : 3;
      if (used + count >= sizeof(name)) break;
      if (count == 1) name[used++] = c;
      else if (count == 2) { name[used++] = 0xc0 | (c >> 6); name[used++] = 0x80 | (c & 63); }
      else { name[used++] = 0xe0 | (c >> 12); name[used++] = 0x80 | ((c >> 6) & 63); name[used++] = 0x80 | (c & 63); }
    }
  if (used) memcpy(d->info.name, name, sizeof(name));
}
static int uvc_connect(struct usbh_hubport *port, uint8_t intf)
{
  struct camera_uvc *d = NULL;
  for (unsigned i = 0; i < UVC_CAMERA_MAX_DEVICES; i++)
    {
      nxmutex_lock(&g_devices[i].lock);
      if (!g_devices[i].info.connected && !g_devices[i].refs)
        { d = &g_devices[i]; break; }
      nxmutex_unlock(&g_devices[i].lock);
    }
  if (!d) return -USB_ERR_NOMEM;
  int ret = camera_uvc_parse(port->raw_config_desc, port->config.config_desc.wTotalLength, intf, &d->desc);
  if (ret < 0) { nxmutex_unlock(&d->lock); return -USB_ERR_INVAL; }
  d->port = port;
  d->info.generation = ++g_generation;
  d->info.vid = port->device_desc.idVendor; d->info.pid = port->device_desc.idProduct;
  d->info.supported = d->desc.caps.count > 0;
  d->desc.caps.generation = d->info.generation;
  uvc_name(d);
  port->config.intf[intf].priv = d;
  snprintf(port->config.intf[intf].devname, CONFIG_USBHOST_DEV_NAMELEN, "/dev/uvc%u", d->info.id);
  irqstate_t flags = enter_critical_section();
  d->info.connected = true;
  leave_critical_section(flags);
  printf("[uvc] attached %u: %s, %u MJPEG resolutions\n", d->info.id, d->info.name, d->desc.caps.count);
  nxmutex_unlock(&d->lock);
  return 0;
}
static int uvc_disconnect(struct usbh_hubport *port, uint8_t intf)
{
  struct camera_uvc *d = port->config.intf[intf].priv;
  if (!d) return 0;
  nxmutex_lock(&d->lock);
  irqstate_t flags = enter_critical_section();
  d->info.connected = false;
  leave_critical_section(flags);
  uvc_stop(d); d->port = NULL;
  port->config.intf[intf].priv = NULL;
  printf("[uvc] detached %u\n", d->info.id);
  nxmutex_unlock(&d->lock);
  return 0;
}
static const struct usbh_class_driver g_camera_driver =
{ .driver_name = "uvc", .connect = uvc_connect, .disconnect = uvc_disconnect };
extern const struct usbh_class_driver hub_class_driver;
const struct usbh_class_info camera_usb_classes[2] =
{
  { .match_flags = USB_CLASS_MATCH_INTF_CLASS, .bInterfaceClass = 9, .class_driver = &hub_class_driver },
  { .match_flags = USB_CLASS_MATCH_INTF_CLASS | USB_CLASS_MATCH_INTF_SUBCLASS,
    .bInterfaceClass = 14, .bInterfaceSubClass = 2, .class_driver = &g_camera_driver }
};
int camera_usb_initialize(void)
{
  int ret = register_driver("/dev/uvcctl", &g_uvc_list_ops, 0666, NULL);
  if (ret < 0) return ret;
  for (unsigned i = 0; i < UVC_CAMERA_MAX_DEVICES; i++)
    {
      struct camera_uvc *d = &g_devices[i];
      nxmutex_init(&d->lock); d->info.id = i; d->ready = d->reading = -1;
      char path[24]; snprintf(path, sizeof(path), "/dev/uvc%u", i);
      ret = register_driver(path, &g_uvc_ops, 0666, d);
      if (ret < 0) return ret;
    }
  return usbh_initialize(0, 0x50000000, NULL);
}
