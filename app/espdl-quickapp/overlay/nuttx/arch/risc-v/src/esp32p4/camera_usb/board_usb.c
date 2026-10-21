/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <nuttx/fs/fs.h>
#include <nuttx/kmalloc.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include "board_usb_internal.h"
#include "usbh_msc.h"
#include "usbh_hid.h"

static mutex_t registry=NXMUTEX_INITIALIZER;
static struct board_usb_slot *slots[BOARD_USB_SLOTS];
struct board_usb_slot *board_usb_attach(unsigned kind,struct usbh_hubport *port,void *driver)
{
  nxmutex_lock(&registry);struct board_usb_slot *s=NULL;
  for(unsigned i=0;i<BOARD_USB_SLOTS;i++){
    if(slots[i]&&(slots[i]->info.connected||slots[i]->refs))continue;
    if(!slots[i]){slots[i]=kumm_memalign(CONFIG_USB_ALIGN_SIZE,sizeof(*s));
      if(!slots[i])break;memset(slots[i],0,sizeof(*s));
      slots[i]->dma=kmm_memalign(CONFIG_USB_ALIGN_SIZE,BOARD_USB_BYTES);
      slots[i]->control=kmm_memalign(CONFIG_USB_ALIGN_SIZE,BOARD_USB_BYTES);
      if(!slots[i]->dma||!slots[i]->control){kmm_free(slots[i]->dma);kmm_free(slots[i]->control);kumm_free(slots[i]);slots[i]=NULL;break;}
      nxmutex_init(&slots[i]->lock);slots[i]->info.id=i;}
    s=slots[i];nxmutex_lock(&s->lock);uint32_t generation=s->info.generation+1;
    if(!generation){nxmutex_unlock(&s->lock);s=NULL;continue;}
    unsigned id=s->info.id;memset(&s->info,0,sizeof(s->info));s->info.id=id;s->info.generation=generation;
    s->info.kind=kind;s->info.vid=port->device_desc.idVendor;s->info.pid=port->device_desc.idProduct;
    s->port=port;s->driver=driver;s->owner=NULL;s->head=s->queued=s->phase=0;
    s->endpoint=NULL;s->info.connected=true;nxmutex_unlock(&s->lock);break;
  }
  nxmutex_unlock(&registry);return s;
}
void board_usb_enqueue(struct board_usb_slot *s,const uint8_t *data,unsigned length)
{
  if(!s||length>BOARD_USB_BYTES){if(s)s->info.dropped++;return;}
  if(s->queued==8){s->head=(s->head+1)%8;s->queued--;s->info.dropped++;}
  struct board_usb_packet *p=s->packets+(s->head+s->queued)%8;p->length=length;memcpy(p->data,data,length);
  s->queued++;s->info.packets++;s->info.bytes+=length;
}
void board_usb_detach(struct board_usb_slot *s)
{
  if(!s)return;nxmutex_lock(&s->lock);irqstate_t flags=enter_critical_section();
  s->info.connected=s->info.running=false;
  if(s->urb.hcpriv)(void)usbh_kill_urb(&s->urb);
  s->queued=0;s->owner=NULL;s->port=NULL;s->driver=NULL;leave_critical_section(flags);nxmutex_unlock(&s->lock);
}
static int block_open(struct inode *inode)
{
  struct board_usb_slot *s=inode->i_private;nxmutex_lock(&s->lock);
  int ret=!board_usb_host_available()?-EBUSY:s->info.connected?0:-ENODEV;if(!ret)s->refs++;nxmutex_unlock(&s->lock);return ret;
}
static int block_close(struct inode *inode)
{struct board_usb_slot *s=inode->i_private;nxmutex_lock(&s->lock);if(s->refs)s->refs--;nxmutex_unlock(&s->lock);return 0;}
static ssize_t block_io(struct inode *inode,unsigned char *buffer,blkcnt_t sector,unsigned count,bool write)
{
  struct board_usb_slot *s=inode->i_private;nxmutex_lock(&s->lock);int ret=0;
  if(!s->info.connected||!s->driver)ret=-ENODEV;
  else if(sector<0||(uint64_t)sector>s->info.blocks||count>s->info.blocks-(uint64_t)sector)ret=-EINVAL;
  unsigned char *bounce=NULL;
  if(!ret&&count){bounce=kumm_memalign(CONFIG_USB_ALIGN_SIZE,s->info.block_size);if(!bounce)ret=-ENOMEM;}
  for(unsigned i=0;!ret&&i<count;i++){
    if(write)memcpy(bounce,buffer+(size_t)i*s->info.block_size,s->info.block_size);
    int rc=write?usbh_msc_scsi_write10(s->driver,(uint32_t)sector+i,bounce,1):usbh_msc_scsi_read10(s->driver,(uint32_t)sector+i,bounce,1);
    if(rc<0)ret=-EIO;else if(!write)memcpy(buffer+(size_t)i*s->info.block_size,bounce,s->info.block_size);
  }
  kumm_free(bounce);nxmutex_unlock(&s->lock);return ret?ret:(ssize_t)count;
}
static ssize_t block_read(struct inode *inode,unsigned char *buffer,blkcnt_t sector,unsigned count)
{return block_io(inode,buffer,sector,count,false);}
#ifdef CONFIG_FS_WRITABLE
static ssize_t block_write(struct inode *inode,const unsigned char *buffer,blkcnt_t sector,unsigned count)
{return block_io(inode,(unsigned char *)buffer,sector,count,true);}
#endif
static int block_geometry(struct inode *inode,struct geometry *geo)
{
  struct board_usb_slot *s=inode->i_private;nxmutex_lock(&s->lock);memset(geo,0,sizeof(*geo));
  geo->geo_available=s->info.connected;geo->geo_mediachanged=false;
#ifdef CONFIG_FS_WRITABLE
  geo->geo_writeenabled=true;
#endif
  geo->geo_nsectors=s->info.blocks;geo->geo_sectorsize=s->info.block_size;
  nxmutex_unlock(&s->lock);return 0;
}
static const struct block_operations block_ops={.open=block_open,.close=block_close,.read=block_read,
#ifdef CONFIG_FS_WRITABLE
 .write=block_write,
#endif
 .geometry=block_geometry};
void usbh_msc_run(struct usbh_msc *msc)
{
  if(usbh_msc_scsi_init(msc)<0||!msc->blocknum||msc->blocksize<512||msc->blocksize>4096||(msc->blocksize&(msc->blocksize-1)))return;
  struct board_usb_slot *s=board_usb_attach(BOARD_USB_MSC,msc->hport,msc);if(!s)return;
  s->info.blocks=msc->blocknum;s->info.block_size=msc->blocksize;
  snprintf(s->info.path,sizeof(s->info.path),"/dev/usbblk%u",(unsigned)s->info.id);
  /* A removed but still-mounted disk keeps its slot. Never reuse its inode. */
  (void)unregister_blockdriver(s->info.path);
  int ret=register_blockdriver(s->info.path,&block_ops,0660,s);
  if(ret){board_usb_detach(s);return;}msc->user_data=s;
}
void usbh_msc_stop(struct usbh_msc *msc)
{
  struct board_usb_slot *s=msc->user_data;if(!s)return;
  board_usb_detach(s);(void)unregister_blockdriver(s->info.path);msc->user_data=NULL;
}
static void hid_received(void *arg,int length)
{
  struct board_usb_slot *s=arg;irqstate_t flags=enter_critical_section();
  if(s->info.connected&&s->info.running){
    if(length>0)board_usb_enqueue(s,s->dma,length);
    else if(length<0){s->info.error=-EIO;s->info.running=false;}
    if(s->info.running&&usbh_submit_urb(&s->urb)<0){s->info.error=-EIO;s->info.running=false;}
  }leave_critical_section(flags);
}
void usbh_hid_run(struct usbh_hid *hid)
{
  if(!hid->intin||USB_GET_MAXPACKETSIZE(hid->intin->wMaxPacketSize)>BOARD_USB_BYTES)return;
  struct board_usb_slot *s=board_usb_attach(BOARD_USB_HID,hid->hport,hid);if(!s)return;
  s->info.protocol=hid->protocol;s->endpoint=hid->intin;hid->user_data=s;
  snprintf(s->info.path,sizeof(s->info.path),"hid:%u",(unsigned)s->info.id);
  if(hid->hport->config.intf[hid->intf].altsetting[0].intf_desc.bInterfaceSubClass==1&&usbh_hid_set_protocol(hid,0)<0){board_usb_detach(s);return;}
  usbh_int_urb_fill(&s->urb,s->port,s->endpoint,s->dma,USB_GET_MAXPACKETSIZE(s->endpoint->wMaxPacketSize),0,hid_received,s);
  irqstate_t flags=enter_critical_section();s->info.running=true;
  if(usbh_submit_urb(&s->urb)<0){s->info.running=false;s->info.error=-EIO;}leave_critical_section(flags);
}
void usbh_hid_stop(struct usbh_hid *hid)
{board_usb_detach(hid->user_data);hid->user_data=NULL;}
static int dequeue(struct board_usb_slot *s,struct board_usb_transfer *r)
{
  irqstate_t flags=enter_critical_section();int ret=-EAGAIN;
  if(s->queued){struct board_usb_packet *p=s->packets+s->head;
    if(r->length<p->length)ret=-EMSGSIZE;
    else{memcpy(r->data,p->data,p->length);r->actual=p->length;s->head=(s->head+1)%8;s->queued--;ret=0;}}
  leave_critical_section(flags);return ret;
}
static int control_ioctl(struct file *file,int command,unsigned long arg)
{
  if(!arg)return -EINVAL;
  if(command==BOARDUSBIOC_LIST){struct board_usb_list *list=(void *)arg;memset(list,0,sizeof(*list));list->version=1;
    list->hs_role=board_usb_hs_role();list->fs_role=board_usb_fs_role();list->fs_error=board_usb_fs_error();
    nxmutex_lock(&registry);for(unsigned i=0;i<BOARD_USB_SLOTS;i++)if(slots[i]){
      struct board_usb_slot *s=slots[i];nxmutex_lock(&s->lock);irqstate_t flags=enter_critical_section();
      if(s->info.connected)list->items[list->count++]=s->info;leave_critical_section(flags);nxmutex_unlock(&s->lock);}
    nxmutex_unlock(&registry);return 0;}
  if(command==BOARDUSBIOC_MODE)return board_usb_set_mode((void *)arg);
  if(command!=BOARDUSBIOC_TRANSFER)return -ENOTTY;
  struct board_usb_transfer *r=(void *)arg;r->actual=0;if(r->length>BOARD_USB_BYTES)return -EINVAL;
  if(r->operation==BOARD_USB_CDC_READ||r->operation==BOARD_USB_CDC_WRITE)return board_usb_cdc_transfer(r->id,r);
  if(r->id>=BOARD_USB_SLOTS)return -EINVAL;
  nxmutex_lock(&registry);struct board_usb_slot *s=slots[r->id];if(s)nxmutex_lock(&s->lock);nxmutex_unlock(&registry);
  if(!s)return -ENODEV;int ret=0;
  if(!s->info.connected)ret=-ENODEV;else if(s->info.generation!=r->generation)ret=-ESTALE;
  else if(r->operation==BOARD_USB_INFO)r->info=s->info;
  else if(!board_usb_host_available())ret=-EBUSY;
  else if(r->operation==BOARD_USB_HID_READ&&s->info.kind==BOARD_USB_HID)ret=dequeue(s,r);
  else if(r->operation==BOARD_USB_HID_REPORT&&s->info.kind==BOARD_USB_HID){
    if(r->report_type<1||r->report_type>3||r->report_id>255)ret=-EINVAL;
    else{memcpy(s->control,r->data,r->length);ret=usbh_hid_set_report(s->driver,r->report_type,r->report_id,s->control,r->length);
      if(ret<0)ret=-EIO;else{r->actual=r->length;ret=0;}}
  }
  else if(s->info.kind==BOARD_USB_AUDIO){
    if(s->owner&&s->owner!=file)ret=-EBUSY;
    else if(r->operation==BOARD_USB_AUDIO_START)ret=board_usb_audio_start(s,r->mode,file);
    else if(r->operation==BOARD_USB_AUDIO_STOP){board_usb_audio_stop(s);ret=0;}
    else if(s->owner!=file||!s->info.running)ret=-EBADF;
    else if(r->operation==BOARD_USB_AUDIO_READ&&s->endpoint&&(s->endpoint->bEndpointAddress&0x80))ret=dequeue(s,r);
    else if(r->operation==BOARD_USB_AUDIO_WRITE&&s->endpoint&&!(s->endpoint->bEndpointAddress&0x80)){
      if(r->length%(s->info.format[s->audio_interface].channels*2)){nxmutex_unlock(&s->lock);return -EINVAL;}
      irqstate_t flags=enter_critical_section();
      if(s->queued==8)ret=-EAGAIN;
      else{struct board_usb_packet *p=s->packets+(s->head+s->queued)%8;p->length=r->length;memcpy(p->data,r->data,r->length);s->queued++;r->actual=r->length;}
      leave_critical_section(flags);
    }else ret=-EINVAL;
  }else ret=-EINVAL;
  nxmutex_unlock(&s->lock);return ret;
}
static int control_close(struct file *file)
{
  nxmutex_lock(&registry);for(unsigned i=0;i<BOARD_USB_SLOTS;i++)if(slots[i]){
    struct board_usb_slot *s=slots[i];nxmutex_lock(&s->lock);
    if(s->owner==file)board_usb_audio_stop(s);nxmutex_unlock(&s->lock);}
  nxmutex_unlock(&registry);return 0;
}
const struct file_operations board_usb_ops={.close=control_close,.ioctl=control_ioctl};
bool board_usb_classes_busy(void)
{
  bool busy=false;nxmutex_lock(&registry);for(unsigned i=0;i<BOARD_USB_SLOTS;i++)if(slots[i]){
    nxmutex_lock(&slots[i]->lock);busy|=slots[i]->refs!=0||slots[i]->owner!=NULL;nxmutex_unlock(&slots[i]->lock);}
  nxmutex_unlock(&registry);return busy;
}
