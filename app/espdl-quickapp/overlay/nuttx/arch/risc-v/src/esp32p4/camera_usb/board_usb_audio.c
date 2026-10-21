/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include "board_usb_internal.h"
#include "board_usb_audio_protocol.h"
#include <errno.h>
#include <string.h>
#include <stdio.h>

static void packet(struct board_usb_slot *s)
{
  if(s->endpoint->bEndpointAddress&0x80){s->urb.transfer_buffer_length=USB_GET_MAXPACKETSIZE(s->endpoint->wMaxPacketSize);return;}
  /* Fractional sample rates (44.1 kHz) retain phase across USB intervals. */
  uint32_t interval=(1u<<(s->endpoint->bInterval-1))*(s->port->speed==USB_SPEED_HIGH?125:1000);
  s->phase+=s->info.format[s->audio_interface].rate*interval;
  unsigned samples=s->phase/1000000;s->phase%=1000000;
  unsigned bytes=samples*s->info.format[s->audio_interface].channels*2;
  if(bytes>BOARD_USB_BYTES)bytes=BOARD_USB_BYTES;
  unsigned written=0;
  while(written<bytes&&s->queued){struct board_usb_packet *p=s->packets+s->head;
    unsigned amount=p->length<bytes-written?p->length:bytes-written;
    memcpy(s->dma+written,p->data,amount);written+=amount;
    p->length-=amount;memmove(p->data,p->data+amount,p->length);
    if(!p->length){s->head=(s->head+1)%8;s->queued--;}}
  if(written<bytes){memset(s->dma+written,0,bytes-written);s->info.dropped++;}
  s->urb.transfer_buffer_length=bytes;
}
static void received(void *arg,int bytes)
{
  struct board_usb_slot *s=arg;irqstate_t flags=enter_critical_section();
  if(s->info.running&&s->info.connected){
    if(bytes<0){s->info.error=-EIO;s->info.running=false;}
    else if(s->endpoint->bEndpointAddress&0x80)board_usb_enqueue(s,s->dma,bytes);
    else{s->info.bytes+=bytes;s->info.packets++;}
    if(s->info.running){packet(s);if(usbh_submit_urb(&s->urb)<0){s->info.error=-EIO;s->info.running=false;}}
  }leave_critical_section(flags);
}
static int audio_control(struct board_usb_slot *s,unsigned request,unsigned value,unsigned index,unsigned bytes)
{
  struct usb_setup_packet *p=s->port->setup;p->bmRequestType=request==11?1:0x22;
  p->bRequest=request;p->wValue=value;p->wIndex=index;p->wLength=bytes;
  return usbh_control_transfer(s->port,p,bytes?s->control:NULL)<0?-EIO:0;
}
void board_usb_audio_stop(struct board_usb_slot *s)
{
  irqstate_t flags=enter_critical_section();s->info.running=false;
  if(s->urb.hcpriv)(void)usbh_kill_urb(&s->urb);leave_critical_section(flags);
  if(s->owner&&s->port&&s->info.connected)(void)audio_control(s,11,0,s->info.format[s->audio_interface].interface,0);
  s->owner=NULL;s->queued=0;s->endpoint=NULL;
}
int board_usb_audio_start(struct board_usb_slot *s,unsigned mode,void *owner)
{
  if(s->info.running||s->owner)return -EBUSY;if(mode>=s->info.formats)return -EINVAL;
  const struct board_usb_format *f=s->info.format+mode;
  if(f->interface>=CONFIG_USBHOST_MAX_INTERFACES||f->alternate>=CONFIG_USBHOST_MAX_INTF_ALTSETTINGS)return -EINVAL;
  struct usbh_interface_altsetting *a=&s->port->config.intf[f->interface].altsetting[f->alternate];
  struct usb_endpoint_descriptor *ep=NULL;
  for(unsigned i=0;i<a->intf_desc.bNumEndpoints&&i<CONFIG_USBHOST_MAX_ENDPOINTS;i++)if(a->ep[i].ep_desc.bEndpointAddress==f->endpoint)ep=&a->ep[i].ep_desc;
  if(!ep)return -ENOTSUP;
  if(!ep->bInterval||ep->bInterval>16)return -ENOTSUP;
  uint32_t interval=(1u<<(ep->bInterval-1))*(s->port->speed==USB_SPEED_HIGH?125:1000);
  if(interval>10000||((uint64_t)f->rate*interval+999999)/1000000*f->channels*2>f->packet_bytes)return -ENOTSUP;
  if(!f->rate_control)for(unsigned i=0;i<s->info.formats;i++){
    const struct board_usb_format *other=s->info.format+i;
    if(other->interface==f->interface&&other->alternate==f->alternate&&other->rate!=f->rate)return -ENOTSUP;
  }
  int ret=audio_control(s,11,f->alternate,f->interface,0);if(ret)return ret;
  if(f->rate_control){s->control[0]=f->rate;s->control[1]=f->rate>>8;s->control[2]=f->rate>>16;
    ret=audio_control(s,1,0x100,f->endpoint,3);if(ret){(void)audio_control(s,11,0,f->interface,0);return ret;}}
  s->endpoint=ep;s->audio_interface=mode;s->owner=owner;s->head=s->queued=s->phase=0;s->info.error=0;
  memset(&s->urb,0,sizeof(s->urb));s->urb.hport=s->port;s->urb.ep=ep;s->urb.transfer_buffer=s->dma;
  s->urb.interval=interval;s->urb.complete=received;s->urb.arg=s;
  irqstate_t flags=enter_critical_section();s->info.running=true;packet(s);ret=usbh_submit_urb(&s->urb);
  if(ret<0)s->info.running=false;leave_critical_section(flags);
  if(ret<0){board_usb_audio_stop(s);return -EIO;}return 0;
}
static int connect(struct usbh_hubport *port,uint8_t intf)
{
  struct board_usb_slot *s=board_usb_attach(BOARD_USB_AUDIO,port,NULL);if(!s)return -ENOMEM;
  unsigned bytes=port->raw_config_desc[2]|(unsigned)port->raw_config_desc[3]<<8;
  int ret=board_usb_audio_parse(port->raw_config_desc,bytes,intf,&s->info);
  if(ret){board_usb_detach(s);return ret;}
  snprintf(s->info.path,sizeof(s->info.path),"audio:%u",(unsigned)s->info.id);
  port->config.intf[intf].priv=s;return 0;
}
static int disconnect(struct usbh_hubport *port,uint8_t intf)
{struct board_usb_slot *s=port->config.intf[intf].priv;board_usb_detach(s);port->config.intf[intf].priv=NULL;return 0;}
static int ignore(struct usbh_hubport *port,uint8_t intf){(void)port;(void)intf;return 0;}
const struct usbh_class_driver board_audio_control_driver={.driver_name="uac1",.connect=connect,.disconnect=disconnect};
const struct usbh_class_driver board_audio_stream_driver={.driver_name="uac1-stream",.connect=ignore,.disconnect=ignore};
