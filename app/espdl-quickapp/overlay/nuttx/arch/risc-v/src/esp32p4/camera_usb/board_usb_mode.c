/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <nuttx/fs/fs.h>
#include "board_usb_internal.h"
#include <errno.h>
#include <stdatomic.h>
extern const struct file_operations board_usb_ops;
extern bool board_usb_classes_busy(void);
extern bool camera_usb_busy(void);
static mutex_t mode_lock=NXMUTEX_INITIALIZER;
static atomic_uint hs_role=BOARD_USB_HOST,fs_role=BOARD_USB_OFF;
static atomic_bool switching;
static int fs_error;
unsigned board_usb_hs_role(void){return hs_role;}
unsigned board_usb_fs_role(void){return fs_role;}
int board_usb_fs_error(void){return fs_error;}
bool board_usb_host_available(void){return !switching&&hs_role==BOARD_USB_HOST;}
bool board_usb_cdc_available(unsigned bus)
{return bus==1?fs_role==BOARD_USB_CDC:!switching&&hs_role==BOARD_USB_CDC;}
int board_usb_set_mode(struct board_usb_mode *mode)
{
  if(mode->port>1||mode->role>BOARD_USB_CDC||(mode->port==1&&mode->role==BOARD_USB_HOST))return -EINVAL;
  nxmutex_lock(&mode_lock);int ret=0;
  if(mode->port==1){
    /* The factory display uses GPIO26/27. Do not switch their FSLS PHY pads
     * while the display reset/backlight still owns them. */
#ifdef CONFIG_ESPRESSIF_MIPI_DSI
    if(mode->role!=BOARD_USB_OFF){fs_error=-EBUSY;ret=-EBUSY;goto done;}
#endif
    ret=board_usb_cdc_mode(1,mode->role==BOARD_USB_CDC);if(!ret)fs_role=mode->role;fs_error=ret;goto done;
  }
  if(mode->role==hs_role)goto done;
  switching=true;
  if(board_usb_classes_busy()||camera_usb_busy()||board_usb_cdc_busy(0)){ret=-EBUSY;goto restore;}
  unsigned old=hs_role;
  if(old==BOARD_USB_HOST)ret=usbh_deinitialize(0);
  else if(old==BOARD_USB_CDC)ret=board_usb_cdc_mode(0,false);
  if(ret<0){ret=-EIO;goto restore;}
  hs_role=BOARD_USB_OFF;
  if(mode->role==BOARD_USB_HOST)ret=usbh_initialize(0,0x50000000,NULL);
  else if(mode->role==BOARD_USB_CDC)ret=board_usb_cdc_mode(0,true);
  if(!ret)hs_role=mode->role;
  else if(old==BOARD_USB_HOST&&usbh_initialize(0,0x50000000,NULL)==0)hs_role=BOARD_USB_HOST;
  else if(old==BOARD_USB_CDC&&board_usb_cdc_mode(0,true)==0)hs_role=BOARD_USB_CDC;
  if(ret<0)ret=-EIO;
restore:switching=false;
done:nxmutex_unlock(&mode_lock);return ret;
}
int board_usb_initialize(void)
{return register_driver("/dev/boardusb",&board_usb_ops,0660,NULL);}
