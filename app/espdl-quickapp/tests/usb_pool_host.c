/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
static bool allocation_fails;
static unsigned allocations;
void *test_pool_alloc(size_t n){allocations++;return allocation_fails?NULL:calloc(1,n);}
#ifdef TEST_HUB_POOL
#include "../overlay/nuttx/arch/risc-v/src/esp32p4/camera_usb/cherryusb/class/hub/usbh_hub.c"
/* ASan keeps the vendor class registry alive. Hardware/OS boundaries must
 * never run in this allocator test; any unexpected call fails immediately. */
int usbh_submit_urb(struct usbh_urb *u){(void)u;abort();}
int usbh_kill_urb(struct usbh_urb *u){(void)u;abort();}
int usbh_roothub_control(struct usbh_bus *b,struct usb_setup_packet *s,uint8_t *p){(void)b;(void)s;(void)p;abort();}
int usbh_control_transfer(struct usbh_hubport *h,struct usb_setup_packet *s,uint8_t *p){(void)h;(void)s;(void)p;abort();}
void usbh_hubport_release(struct usbh_hubport *h){(void)h;abort();}
void usb_osal_timer_delete(struct usb_osal_timer *t){(void)t;abort();}
void usb_osal_timer_start(struct usb_osal_timer *t){(void)t;abort();}
struct usb_osal_timer *usb_osal_timer_create(const char *n,uint32_t ms,usb_timer_handler_t h,void *a,bool period){(void)n;(void)ms;(void)h;(void)a;(void)period;abort();}
void usb_osal_msleep(uint32_t t){(void)t;abort();}
int usb_osal_mq_send(usb_osal_mq_t q,uintptr_t a){(void)q;(void)a;abort();}
#else
#include "../overlay/nuttx/arch/risc-v/src/esp32p4/camera_usb/cherryusb/core/usbh_core.c"
const struct usbh_class_info camera_usb_classes[2]={{0},{0}};
const unsigned camera_usb_class_count=2;
int usbh_hub_initialize(struct usbh_bus *bus){assert(bus&&bus->hcd.reg_base==0x50000000);return 0;}
int usbh_hub_deinitialize(struct usbh_bus *bus){assert(bus);return 0;}
#endif
int main(void)
{
 allocation_fails=true;
#ifdef TEST_HUB_POOL
 assert(!usbh_hub_class_alloc()&&allocations==1);
 allocation_fails=false;struct usbh_hub *h=usbh_hub_class_alloc();assert(h&&allocations==2);
 assert(!usbh_hub_class_alloc()&&allocations==2);usbh_hub_class_free(h);
 struct usbh_hub *again=usbh_hub_class_alloc();assert(again==h&&allocations==2&&again->index==2);
 usbh_hub_class_free(again);free(g_hub_class);g_hub_class=NULL;
 puts("USB_HUB_POOL_OK: allocation failure, bounded slots, disconnect/reconnect reuse and one lifetime allocation");
#else
 assert(usbh_initialize(0,0x50000000,NULL)==-USB_ERR_NOMEM&&allocations==1&&!g_usbhost_bus);
 allocation_fails=false;assert(!usbh_initialize(0,0x50000000,NULL)&&allocations==2);
 struct usbh_bus *pool=g_usbhost_bus;assert(pool&&pool->event_handler);
 assert((uintptr_t)ep0_request_buffer%64==0&&(uintptr_t)g_setup_buffer%64==0);
 assert(!usbh_deinitialize(0));assert(!usbh_initialize(0,0x50000000,NULL)&&g_usbhost_bus==pool&&allocations==2);
 assert(!usbh_deinitialize(0));free(g_usbhost_bus);g_usbhost_bus=NULL;
 puts("USB_BUS_POOL_OK: allocation failure, restart reuses metadata, DMA buffers retain 64-byte alignment");
#endif
 return 0;
}
