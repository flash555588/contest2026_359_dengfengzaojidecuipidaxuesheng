/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#define BOARD_USB_SLOTS 8
#define BOARD_USB_BYTES 512
#define BOARD_USB_FORMATS 16
#define BOARDUSBIOC_LIST 0x42550001
#define BOARDUSBIOC_TRANSFER 0x42550002
#define BOARDUSBIOC_MODE 0x42550003
enum board_usb_kind {BOARD_USB_MSC=1,BOARD_USB_HID,BOARD_USB_AUDIO};
enum board_usb_role {BOARD_USB_OFF,BOARD_USB_HOST,BOARD_USB_CDC};
enum board_usb_operation {BOARD_USB_INFO,BOARD_USB_HID_READ,BOARD_USB_HID_REPORT,
  BOARD_USB_AUDIO_START,BOARD_USB_AUDIO_READ,BOARD_USB_AUDIO_WRITE,BOARD_USB_AUDIO_STOP,
  BOARD_USB_CDC_READ,BOARD_USB_CDC_WRITE};
struct board_usb_format {uint32_t rate;uint16_t packet_bytes;uint8_t channels,bits,input,interface,alternate,endpoint,interval,rate_control;};
struct board_usb_info {
  uint32_t id,generation,blocks,block_size,packets,bytes,dropped;
  uint16_t vid,pid;
  uint8_t kind,protocol,formats;
  bool connected,running;
  int error;
  char path[32];
  struct board_usb_format format[BOARD_USB_FORMATS];
};
struct board_usb_list {uint32_t version,count;uint8_t hs_role,fs_role;int fs_error;struct board_usb_info items[BOARD_USB_SLOTS];};
struct board_usb_transfer {
  uint32_t id,generation,operation,mode,length,actual;
  uint16_t report_type,report_id;
  struct board_usb_info info;
  uint8_t data[BOARD_USB_BYTES];
};
struct board_usb_mode {uint8_t port,role;}; /* port0=HS Type-A/Type-C, port1=FS */
int board_usb_initialize(void);
bool board_usb_host_available(void);
