/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef TEST_MSC
#include "cherryusb/class/msc/usbh_msc.c"
int usbh_kill_urb(struct usbh_urb *u){(void)u;return 0;}
void usb_osal_thread_schedule_other(void){}
int usbh_control_transfer(struct usbh_hubport *p,struct usb_setup_packet *s,uint8_t *d){(void)p;(void)s;(void)d;return 0;}
static unsigned phase,fault;static uint32_t tag;
int usbh_submit_urb(struct usbh_urb *u)
{
  phase++;u->actual_length=u->transfer_buffer_length;
  if(phase==1){tag=((struct CBW *)u->transfer_buffer)->dTag;if(fault==1)u->actual_length--;}
  if(phase==2&&fault==2)u->actual_length--;
  if(phase==3){struct CSW *c=(void *)u->transfer_buffer;c->dSignature=MSC_CSW_Signature;c->dTag=tag;c->bStatus=0;
    if(fault==3)u->actual_length--;if(fault==4)c->dTag++;if(fault==5)c->dDataResidue=1;if(fault==6)c->bStatus=1;}
  return 0;
}
int main(void)
{
  struct usbh_hubport port={0};struct usb_endpoint_descriptor in={.bEndpointAddress=0x81},out={.bEndpointAddress=2};
  struct usbh_msc m={.hport=&port,.bulkin=&in,.bulkout=&out};uint8_t shared[64],data[512];
  for(fault=0;fault<7;fault++){
    memset(shared,0,sizeof(shared));struct CBW *c=(void *)shared;c->dTag=42;c->dDataLength=sizeof(data);c->bCBLength=10;c->CB[0]=SCSI_CMD_READ10;
    phase=0;int ret=usbh_bulk_cbw_csw_xfer(&m,c,(void *)shared,data,100);
    assert(fault?ret<0:ret==0);
  }
  puts("USB_MSC_OK: shared CBW/CSW identity, short CBW/data/CSW, residue and command failure");return 0;
}
#elif defined(TEST_HID)
#include "cherryusb/class/hid/usbh_hid.c"
int usbh_kill_urb(struct usbh_urb *u){(void)u;return 0;}
void usb_osal_thread_schedule_other(void){}
int usbh_control_transfer(struct usbh_hubport *p,struct usb_setup_packet *s,uint8_t *data)
{(void)p;(void)data;assert(s->wIndex==2);return s->wLength;}
int main(void)
{
  uint8_t config[]={9,2,27,0,1,1,0,0x80,50,9,4,2,0,1,3,1,2,0,9,0x21,0x11,1,0,1,0x22,50,0};
  struct usbh_hubport *p=calloc(1,sizeof(*p));assert(p);struct usb_setup_packet setup={0};p->setup=&setup;p->raw_config_desc=config;p->speed=USB_SPEED_HIGH;
  struct usbh_interface_altsetting *a=&p->config.intf[2].altsetting[0];a->intf_desc.bNumEndpoints=1;
  a->ep[0].ep_desc=(struct usb_endpoint_descriptor){.bEndpointAddress=0x81,.bmAttributes=3,.wMaxPacketSize=8,.bInterval=1};
  for(unsigned i=0;i<500;i++){config[18]=i%3;assert(usbh_hid_connect(p,2)<0);assert(g_devinuse==0);}config[18]=9;
  assert(!usbh_hid_connect(p,2));struct usbh_hid *hid=p->config.intf[2].priv;assert(hid&&hid->protocol==2&&hid->report_size==50);
  uint8_t b=1;assert(usbh_hid_set_report(hid,2,0,&b,1)==1);assert(!usbh_hid_set_protocol(hid,0));
  assert(!usbh_hid_disconnect(p,2));assert(!g_devinuse);free(p);
  puts("USB_HID_OK: malformed descriptor cleanup, bounded endpoint, nonzero interface controls");return 0;
}
#elif defined(TEST_AUDIO)
#include "board_usb_audio_protocol.h"
int main(void)
{
  uint8_t c[]={9,2,0,0,2,1,0,0x80,50,9,4,0,0,0,1,1,0,0,
    9,0x24,1,0,1,9,0,1,1,9,4,1,1,1,1,2,0,0,7,0x24,1,1,0,1,0,
    11,0x24,2,1,2,2,16,1,0x80,0xbb,0,9,5,0x81,5,192,0,1,0,0,7,0x25,1,1,0,0,0};
  c[2]=sizeof(c);struct board_usb_info out;assert(!board_usb_audio_parse(c,sizeof(c),0,&out));
  assert(out.formats==1&&out.format[0].rate==48000&&out.format[0].channels==2&&out.format[0].rate_control);
  for(unsigned i=0;i<sizeof(c);i++){assert(board_usb_audio_parse(c,i,0,&out)<0);}
  uint32_t rng=13;uint8_t fuzz[256];
  for(unsigned n=0;n<100000;n++){unsigned length=n%sizeof(fuzz);
    for(unsigned i=0;i<length;i++){rng=rng*1664525+1013904223;fuzz[i]=rng>>24;}
    if(length>=9){fuzz[0]=9;fuzz[1]=2;fuzz[2]=length;fuzz[3]=0;}
    (void)board_usb_audio_parse(fuzz,length,0,&out);
  }
  puts("USB_UAC_OK: PCM16 descriptor, truncated input, 100000 malformed descriptor walks");return 0;
}
#elif defined(TEST_DEVICE)
#include "cherryusb/core/usbd_core.c"
#include "cherryusb/class/cdc/usbd_cdc_acm.c"
bool usb_test_no_memory;
int usb_dc_init(uint8_t b){(void)b;return 0;}
int usb_dc_deinit(uint8_t b){(void)b;return 0;}
int usbd_set_address(uint8_t b,uint8_t a){(void)b;(void)a;return 0;}
int usbd_set_remote_wakeup(uint8_t b){(void)b;return 0;}
uint8_t usbd_get_port_speed(uint8_t b){(void)b;return USB_SPEED_HIGH;}
int usbd_ep_open(uint8_t b,const struct usb_endpoint_descriptor *ep){(void)b;(void)ep;return 0;}
int usbd_ep_close(uint8_t b,uint8_t e){(void)b;(void)e;return 0;}
int usbd_ep_set_stall(uint8_t b,uint8_t e){(void)b;(void)e;return 0;}
int usbd_ep_clear_stall(uint8_t b,uint8_t e){(void)b;(void)e;return 0;}
int usbd_ep_is_stalled(uint8_t b,uint8_t e,uint8_t *stalled){(void)b;(void)e;*stalled=0;return 0;}
int usbd_ep_start_write(uint8_t b,uint8_t e,const uint8_t *p,uint32_t n){(void)b;(void)e;(void)p;(void)n;return 0;}
int usbd_ep_start_read(uint8_t b,uint8_t e,uint8_t *p,uint32_t n){(void)b;(void)e;(void)p;(void)n;return 0;}
static void device_event(uint8_t b,uint8_t e){(void)b;(void)e;}
int main(void)
{
  struct usb_descriptor descriptors={0};struct usbd_interface i={0};
  usb_test_no_memory=true;usbd_desc_register(0,&descriptors);usbd_add_interface(0,&i);
  assert(usbd_initialize(0,0,device_event)==-USB_ERR_NOMEM);assert(!usbd_deinitialize(0));
  usb_test_no_memory=false;usbd_desc_register(0,&descriptors);assert(g_usbd_core&&(uintptr_t)g_usbd_core%64==0);
  struct usbd_core_priv *allocation=g_usbd_core;
  for(unsigned round=0;round<100;round++){usbd_desc_register(0,&descriptors);usbd_add_interface(0,&i);assert(i.intf_num==0&&g_usbd_core==allocation);assert(!usbd_initialize(0,0,device_event));assert(!usbd_deinitialize(0));}
  uint8_t line[]={0,0xc2,1,0,0,0,8},*data=line;uint32_t len=7;
  struct usb_setup_packet setup={.bmRequestType=0x21,.bRequest=CDC_REQUEST_SET_LINE_CODING,.wLength=7};
  assert(!cdc_acm_class_interface_request_handler(0,&setup,&data,&len));
  setup.wLength=1;assert(cdc_acm_class_interface_request_handler(0,&setup,&data,&len)<0);setup.wLength=7;
  line[5]=255;assert(cdc_acm_class_interface_request_handler(0,&setup,&data,&len)<0);line[5]=0;
  setup.wIndex=1;assert(cdc_acm_class_interface_request_handler(0,&setup,&data,&len)<0);
  puts("USB_DEVICE_OK: allocation failure, aligned lifetime pool, repeated role initialization, malformed CDC class requests");return 0;
}
#elif defined(TEST_STATE)
#include "board_usb.c"
#include "board_usb_mode.c"
#include "board_usb_cdc.c"
static bool fail_init;static unsigned host_starts,host_stops;static void (*event_cb)(uint8_t,uint8_t);
int register_driver(const char *p,const struct file_operations *f,mode_t m,void *v){(void)p;(void)f;(void)m;(void)v;return 0;}
int register_blockdriver(const char *p,const struct block_operations *f,mode_t m,void *v){(void)p;(void)f;(void)m;(void)v;return 0;}
int unregister_blockdriver(const char *p){(void)p;return 0;}
int usbh_initialize(uint8_t b,uintptr_t r,usbh_event_handler_t cb){(void)b;(void)r;(void)cb;host_starts++;return fail_init?-1:0;}
int usbh_deinitialize(uint8_t b){(void)b;host_stops++;return 0;}
bool camera_usb_busy(void){return false;}
int usbh_kill_urb(struct usbh_urb *u){u->hcpriv=NULL;return 0;}
int usbh_submit_urb(struct usbh_urb *u){u->hcpriv=u;return 0;}
int usbh_msc_scsi_read10(struct usbh_msc *m,uint32_t s,const uint8_t *p,uint32_t n){(void)m;(void)s;memset((void *)p,42,n*512);return 0;}
int usbh_msc_scsi_write10(struct usbh_msc *m,uint32_t s,const uint8_t *p,uint32_t n){(void)m;(void)s;(void)p;(void)n;return 0;}
int usbh_hid_set_report(struct usbh_hid *h,uint8_t t,uint8_t id,uint8_t *p,uint32_t n){(void)h;(void)t;(void)id;(void)p;(void)n;return 0;}
int board_usb_audio_start(struct board_usb_slot *s,unsigned mode,void *owner){if(mode>=s->info.formats)return -EINVAL;s->owner=owner;s->info.running=true;return 0;}
void board_usb_audio_stop(struct board_usb_slot *s){s->owner=NULL;s->info.running=false;}
void usbd_desc_register(uint8_t b,const struct usb_descriptor *d){(void)b;assert(d->other_speed_descriptor_callback(USB_SPEED_HIGH)[1]==7);}
struct usbd_interface *usbd_cdc_acm_init_intf(uint8_t b,struct usbd_interface *i){(void)b;return i;}
void usbd_add_interface(uint8_t b,struct usbd_interface *i){(void)b;(void)i;}
void usbd_add_endpoint(uint8_t b,struct usbd_endpoint *e){(void)b;(void)e;}
int usbd_initialize(uint8_t b,uintptr_t r,usbd_event_handler_t cb){(void)b;(void)r;event_cb=cb;return 0;}
int usbd_deinitialize(uint8_t b){if(event_cb)event_cb(b,USBD_EVENT_DEINIT);return 0;}
int board_usb_device_irq_ready(unsigned b){(void)b;return !fail_init;}
uint16_t usbd_get_ep_mps(uint8_t b,uint8_t e){(void)b;(void)e;return 512;}
int usbd_ep_start_read(uint8_t b,uint8_t e,uint8_t *p,uint32_t n){(void)b;(void)e;(void)p;(void)n;return 0;}
int usbd_ep_start_write(uint8_t b,uint8_t e,const uint8_t *p,uint32_t n){(void)b;(void)e;(void)p;(void)n;return 0;}
int main(void)
{
  struct usbh_hubport *p=calloc(1,sizeof(*p));assert(p);struct board_usb_slot *s=board_usb_attach(BOARD_USB_MSC,p,p);assert(s);
  struct inode inode={.i_private=s};assert(!block_open(&inode));struct board_usb_mode m={.role=BOARD_USB_OFF};assert(board_usb_set_mode(&m)==-EBUSY&&!host_stops);
  board_usb_detach(s);assert(block_open(&inode)==-ENODEV);struct board_usb_slot *other=board_usb_attach(BOARD_USB_HID,p,p);assert(other!=s);board_usb_detach(other);
  assert(!block_close(&inode));uint32_t gen=s->info.generation;s=board_usb_attach(BOARD_USB_AUDIO,p,p);assert(s->info.generation==gen+1);
  struct file owner={0},intruder={0};struct board_usb_transfer tr={.id=s->info.id,.generation=gen,.operation=BOARD_USB_INFO};
  assert(control_ioctl(&owner,BOARDUSBIOC_TRANSFER,(unsigned long)&tr)==-ESTALE);tr.generation=s->info.generation;s->info.formats=1;tr.operation=BOARD_USB_AUDIO_START;
  assert(!control_ioctl(&owner,BOARDUSBIOC_TRANSFER,(unsigned long)&tr));assert(control_ioctl(&intruder,BOARDUSBIOC_TRANSFER,(unsigned long)&tr)==-EBUSY);
  assert(board_usb_set_mode(&m)==-EBUSY);assert(!control_close(&owner));board_usb_detach(s);
  m.role=BOARD_USB_CDC;assert(!board_usb_set_mode(&m)&&board_usb_hs_role()==BOARD_USB_CDC&&host_stops==1);
  event_cb(0,USBD_EVENT_CONFIGURED);tr=(struct board_usb_transfer){.operation=BOARD_USB_CDC_WRITE,.length=3};assert(!board_usb_cdc_transfer(0,&tr)&&tr.actual==3);
  m.role=BOARD_USB_HOST;assert(board_usb_set_mode(&m)==-EBUSY);in_complete(0,CDC_IN,3);assert(!board_usb_set_mode(&m)&&host_starts==1);
  m.port=1;m.role=BOARD_USB_CDC;assert(board_usb_set_mode(&m)==-EBUSY&&board_usb_fs_role()==BOARD_USB_OFF);
  m.port=0;m.role=BOARD_USB_CDC;fail_init=true;assert(board_usb_set_mode(&m)==-EIO&&board_usb_hs_role()==BOARD_USB_OFF);fail_init=false;
  m.role=BOARD_USB_HOST;assert(!board_usb_set_mode(&m));free(p);
  puts("USB_STATE_OK: disk lifetime, stale generations, owner isolation, busy role changes, CDC transfers, FS/display guard and init failure");return 0;
}
#endif
