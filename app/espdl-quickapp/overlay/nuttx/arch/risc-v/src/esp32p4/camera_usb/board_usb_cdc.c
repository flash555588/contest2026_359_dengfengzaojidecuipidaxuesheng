/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <nuttx/kmalloc.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/mutex.h>
#include "board_usb_internal.h"
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include <errno.h>

#define CDC_IN 0x81
#define CDC_OUT 0x02
#define CDC_NOTIFY 0x83
#define CDC_CONFIG_SIZE (9+CDC_ACM_DESCRIPTOR_LEN)
struct cdc_port {
  bool initialized,configured,tx_busy,dtr;
  unsigned head,queued,dropped;
  struct board_usb_packet packets[8];
  uint8_t *rx,*tx;
  struct usbd_interface interface[2];
  struct usbd_endpoint in,out;
  struct cdc_line_coding line;
};
static struct cdc_port *ports[2];
static const uint8_t device[]={USB_DEVICE_DESCRIPTOR_INIT(USB_2_0,0xef,2,1,0x303a,0x4002,0x0100,1)};
static const uint8_t fs_config[]={USB_CONFIG_DESCRIPTOR_INIT(CDC_CONFIG_SIZE,2,1,USB_CONFIG_BUS_POWERED,100),CDC_ACM_DESCRIPTOR_INIT(0,CDC_NOTIFY,CDC_OUT,CDC_IN,64,2)};
static const uint8_t hs_config[]={USB_CONFIG_DESCRIPTOR_INIT(CDC_CONFIG_SIZE,2,1,USB_CONFIG_BUS_POWERED,100),CDC_ACM_DESCRIPTOR_INIT(0,CDC_NOTIFY,CDC_OUT,CDC_IN,512,2)};
static const uint8_t fs_other[]={9,7,WBVAL(CDC_CONFIG_SIZE),2,1,0,USB_CONFIG_BUS_POWERED,50,CDC_ACM_DESCRIPTOR_INIT(0,CDC_NOTIFY,CDC_OUT,CDC_IN,64,2)};
static const uint8_t hs_other[]={9,7,WBVAL(CDC_CONFIG_SIZE),2,1,0,USB_CONFIG_BUS_POWERED,50,CDC_ACM_DESCRIPTOR_INIT(0,CDC_NOTIFY,CDC_OUT,CDC_IN,512,2)};
static const uint8_t qualifier[]={10,6,0,2,0,0,0,64,1,0};
static const uint8_t *device_desc(uint8_t speed){(void)speed;return device;}
static const uint8_t *config_desc(uint8_t speed){return speed==USB_SPEED_HIGH?hs_config:fs_config;}
static const uint8_t *quality_desc(uint8_t speed){(void)speed;return qualifier;}
static const uint8_t *other_desc(uint8_t speed){return speed==USB_SPEED_HIGH?fs_other:hs_other;}
static const char *string_desc(uint8_t speed,uint8_t index)
{(void)speed;static const char *strings[]={"\x09\x04","Espressif / ESPClaw","ESPClaw data CDC","ESPClaw-P4","CDC data"};return index<5?strings[index]:NULL;}
static const struct usb_descriptor descriptors={.device_descriptor_callback=device_desc,.config_descriptor_callback=config_desc,
  .device_quality_descriptor_callback=quality_desc,.other_speed_descriptor_callback=other_desc,.string_descriptor_callback=string_desc};
static void out_complete(uint8_t bus,uint8_t endpoint,uint32_t bytes)
{
  struct cdc_port *p=ports[bus];if(!p||!p->configured)return;
  irqstate_t flags=enter_critical_section();
  if(bytes<=BOARD_USB_BYTES){if(p->queued==8){p->dropped++;}else{
    struct board_usb_packet *packet=p->packets+(p->head+p->queued)%8;packet->length=bytes;memcpy(packet->data,p->rx,bytes);p->queued++;}}
  (void)usbd_ep_start_read(bus,endpoint,p->rx,usbd_get_ep_mps(bus,endpoint));leave_critical_section(flags);
}
static void in_complete(uint8_t bus,uint8_t endpoint,uint32_t bytes)
{
  struct cdc_port *p=ports[bus];if(!p)return;
  if(bytes&&bytes%usbd_get_ep_mps(bus,endpoint)==0){if(!usbd_ep_start_write(bus,endpoint,NULL,0))return;}
  p->tx_busy=false;
}
static void event(uint8_t bus,uint8_t kind)
{
  struct cdc_port *p=ports[bus];if(!p)return;
  if(kind==USBD_EVENT_CONFIGURED){p->configured=true;p->head=p->queued=0;p->tx_busy=false;
    (void)usbd_ep_start_read(bus,CDC_OUT,p->rx,usbd_get_ep_mps(bus,CDC_OUT));}
  else if(kind==USBD_EVENT_RESET||kind==USBD_EVENT_DISCONNECTED||kind==USBD_EVENT_DEINIT){p->configured=false;p->tx_busy=p->dtr=false;p->queued=0;}
}
void usbd_cdc_acm_set_dtr(uint8_t bus,uint8_t interface,bool value)
{(void)interface;if(bus<2&&ports[bus])ports[bus]->dtr=value;}
void usbd_cdc_acm_set_line_coding(uint8_t bus,uint8_t interface,struct cdc_line_coding *line)
{(void)interface;if(bus<2&&ports[bus])ports[bus]->line=*line;}
void usbd_cdc_acm_get_line_coding(uint8_t bus,uint8_t interface,struct cdc_line_coding *line)
{(void)interface;if(bus<2&&ports[bus])*line=ports[bus]->line;else memset(line,0,sizeof(*line));}
bool board_usb_cdc_busy(unsigned bus)
{if(bus>=2||!ports[bus])return false;irqstate_t flags=enter_critical_section();bool busy=ports[bus]->tx_busy;leave_critical_section(flags);return busy;}
void board_usb_cdc_state(unsigned bus,bool *configured)
{*configured=bus<2&&ports[bus]&&ports[bus]->configured;}
int board_usb_cdc_mode(unsigned bus,bool enable)
{
  if(bus>=2)return -EINVAL;
  if(!ports[bus]){if(!enable)return 0;ports[bus]=kumm_memalign(CONFIG_USB_ALIGN_SIZE,sizeof(struct cdc_port));
    if(!ports[bus])return -ENOMEM;memset(ports[bus],0,sizeof(struct cdc_port));
    ports[bus]->rx=kmm_memalign(CONFIG_USB_ALIGN_SIZE,BOARD_USB_BYTES);
    ports[bus]->tx=kmm_memalign(CONFIG_USB_ALIGN_SIZE,BOARD_USB_BYTES);
    if(!ports[bus]->rx||!ports[bus]->tx){kmm_free(ports[bus]->rx);kmm_free(ports[bus]->tx);kumm_free(ports[bus]);ports[bus]=NULL;return -ENOMEM;}}
  struct cdc_port *p=ports[bus];
  if(enable){
    if(p->initialized)return 0;
    p->line=(struct cdc_line_coding){.dwDTERate=115200,.bDataBits=8};
    usbd_desc_register(bus,&descriptors);memset(p->interface,0,sizeof(p->interface));
    usbd_add_interface(bus,usbd_cdc_acm_init_intf(bus,p->interface));
    usbd_add_interface(bus,usbd_cdc_acm_init_intf(bus,p->interface+1));
    p->in.ep_addr=CDC_IN;p->in.ep_cb=in_complete;p->out.ep_addr=CDC_OUT;p->out.ep_cb=out_complete;
    usbd_add_endpoint(bus,&p->in);usbd_add_endpoint(bus,&p->out);
    int ret=usbd_initialize(bus,bus?0x50040000:0x50000000,event);
    extern int board_usb_device_irq_ready(unsigned bus);
    if(ret<0||!board_usb_device_irq_ready(bus)){(void)usbd_deinitialize(bus);return -EIO;}p->initialized=true;return 0;
  }
  if(!p->initialized)return 0;
  if(p->tx_busy)return -EBUSY;
  int ret=usbd_deinitialize(bus);if(ret<0)return -EIO;p->initialized=false;return 0;
}
int board_usb_cdc_transfer(unsigned bus,struct board_usb_transfer *r)
{
  if(bus>=2||!ports[bus])return -ENODEV;
  struct cdc_port *p=ports[bus];irqstate_t flags=enter_critical_section();int ret=0;
  extern bool board_usb_cdc_available(unsigned bus);
  if(!board_usb_cdc_available(bus))ret=-EBUSY;
  else if(!p->configured)ret=-ENOTCONN;
  else if(r->operation==BOARD_USB_CDC_READ){
    if(!p->queued)ret=-EAGAIN;else{struct board_usb_packet *packet=p->packets+p->head;
      if(r->length<packet->length)ret=-EMSGSIZE;
      else{memcpy(r->data,packet->data,packet->length);r->actual=packet->length;p->head=(p->head+1)%8;p->queued--;}}
  }else if(r->operation==BOARD_USB_CDC_WRITE){
    if(p->tx_busy)ret=-EAGAIN;else{p->tx_busy=true;memcpy(p->tx,r->data,r->length);
      if(usbd_ep_start_write(bus,CDC_IN,p->tx,r->length)<0){p->tx_busy=false;ret=-EIO;}else r->actual=r->length;}
  }else ret=-EINVAL;
  leave_critical_section(flags);return ret;
}
