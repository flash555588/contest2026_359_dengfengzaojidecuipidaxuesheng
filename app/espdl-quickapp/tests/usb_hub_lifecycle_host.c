/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../overlay/nuttx/arch/risc-v/src/esp32p4/camera_usb/cherryusb/class/hub/usbh_hub.c"
void *test_pool_alloc(size_t n){return calloc(1,n);}
static unsigned queues_deleted,sems_deleted,controller_stops;static bool joined;
static struct usbh_bus *active;
static usb_thread_entry_t thread_entry;
int usbh_submit_urb(struct usbh_urb *u){(void)u;abort();}
int usbh_kill_urb(struct usbh_urb *u){(void)u;abort();}
int usbh_roothub_control(struct usbh_bus *b,struct usb_setup_packet *s,uint8_t *p){(void)b;(void)s;(void)p;abort();}
int usbh_control_transfer(struct usbh_hubport *h,struct usb_setup_packet *s,uint8_t *p){(void)h;(void)s;(void)p;abort();}
void usbh_hubport_release(struct usbh_hubport *h){(void)h;}
void usb_osal_timer_delete(struct usb_osal_timer *t){(void)t;}
void usb_osal_timer_start(struct usb_osal_timer *t){(void)t;}
struct usb_osal_timer *usb_osal_timer_create(const char *n,uint32_t ms,usb_timer_handler_t h,void *a,bool period){(void)n;(void)ms;(void)h;(void)a;(void)period;abort();}
void usb_osal_msleep(uint32_t t){(void)t;}
size_t usb_osal_enter_critical_section(void){return 0;}
void usb_osal_leave_critical_section(size_t flags){(void)flags;}
int usb_hc_init(struct usbh_bus *b){assert(b==active);return 0;}
int usb_hc_deinit(struct usbh_bus *b){assert(b==active);controller_stops++;return 0;}
usb_osal_mq_t usb_osal_mq_create(uint32_t n){assert(n==7);return malloc(8);}
void usb_osal_mq_delete(usb_osal_mq_t q){assert(joined);queues_deleted++;free(q);}
usb_osal_sem_t usb_osal_sem_create(uint32_t n){assert(!n);return malloc(8);}
void usb_osal_sem_delete(usb_osal_sem_t s){sems_deleted++;free(s);}
int usb_osal_sem_give(usb_osal_sem_t s){assert(s==active->hub_sem);joined=true;return 0;}
int usb_osal_sem_take(usb_osal_sem_t s,uint32_t timeout){assert(s==active->hub_sem&&timeout==USB_OSAL_WAITING_FOREVER&&joined);return 0;}
usb_osal_thread_t usb_osal_thread_create(const char *name,uint32_t stack,uint32_t priority,usb_thread_entry_t entry,void *arg)
{(void)name;(void)stack;(void)priority;assert(arg==active);thread_entry=entry;return (void *)1;}
void usb_osal_thread_delete(usb_osal_thread_t t){assert(!t&&joined);}
int usb_osal_mq_recv(usb_osal_mq_t q,uintptr_t *addr,uint32_t timeout){assert(q==active->hub_mq&&timeout==USB_OSAL_WAITING_FOREVER);*addr=0;return 0;}
int usb_osal_mq_send(usb_osal_mq_t q,uintptr_t addr)
{assert(q==active->hub_mq&&!addr);char value[32];snprintf(value,sizeof(value),"%zx",(size_t)active);char *argv[]={"hub",value};thread_entry(2,argv);return 0;}
static void event(uint8_t bus,uint8_t hub,uint8_t port,uint8_t intf,uint8_t kind)
{(void)bus;(void)hub;(void)port;(void)intf;(void)kind;}
int main(void)
{
 struct usbh_bus *b=calloc(1,sizeof(*b));assert(b);active=b;b->event_handler=event;
 for(unsigned i=1;i<=100;i++){joined=false;assert(!usbh_hub_initialize(b));b->hcd.roothub.nports=0;
  assert(!usbh_hub_deinitialize(b));assert(!b->hub_mq&&!b->hub_sem&&queues_deleted==i&&sems_deleted==i&&controller_stops==i);}
 free(b);puts("USB_HUB_LIFECYCLE_OK: 100 restarts, controller exits before join, exactly one queue/sem release");return 0;
}
