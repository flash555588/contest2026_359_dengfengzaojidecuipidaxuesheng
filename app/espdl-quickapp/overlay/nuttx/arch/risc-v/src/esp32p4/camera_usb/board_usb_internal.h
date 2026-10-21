/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <nuttx/usb/board_usb.h>
#include <nuttx/mutex.h>
#include "usbh_core.h"
struct board_usb_packet {uint16_t length;uint8_t data[BOARD_USB_BYTES];};
struct board_usb_slot {
  mutex_t lock;
  struct board_usb_info info;
  struct usbh_hubport *port;
  void *driver,*owner;
  unsigned refs,head,queued;
  struct board_usb_packet packets[8];
  struct usbh_urb urb;
  uint8_t *dma,*control; /* Internal DMA memory, separate from PSRAM queues. */
  struct usb_endpoint_descriptor *endpoint;
  uint8_t audio_interface;
  uint32_t phase;
};
struct board_usb_slot *board_usb_attach(unsigned kind,struct usbh_hubport *port,void *driver);
void board_usb_detach(struct board_usb_slot *slot);
void board_usb_enqueue(struct board_usb_slot *slot,const uint8_t *data,unsigned length);
int board_usb_audio_start(struct board_usb_slot *,unsigned mode,void *owner);
void board_usb_audio_stop(struct board_usb_slot *);
int board_usb_cdc_mode(unsigned port,bool enable);
int board_usb_cdc_transfer(unsigned port,struct board_usb_transfer *);
bool board_usb_cdc_busy(unsigned port);
void board_usb_cdc_state(unsigned port,bool *configured);
int board_usb_set_mode(struct board_usb_mode *);
unsigned board_usb_hs_role(void);
unsigned board_usb_fs_role(void);
int board_usb_fs_error(void);
