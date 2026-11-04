/* SPDX-License-Identifier: Apache-2.0 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <malloc.h>
#include <pthread.h>
#include <esp_cache.h>
#include <esp_mmu_map.h>

#ifndef OV_NATIVE_TEXT_SLOTS
#define OV_NATIVE_TEXT_SLOTS 8
#endif

extern unsigned char _native_exec_start[], _native_exec_end[];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
/* failed: an unmap failed, so the backing must never be reused (it would
 * alias live executable memory). The slot stays closed until reboot. */
static struct text_slot { void *backing, *mapped; size_t bytes; bool failed; }
	slots[OV_NATIVE_TEXT_SLOTS];

/* The SDK's MMU update may disable caches. Run it on the existing internal
 * kernel-stack dispatcher, never on the module loader's PSRAM pthread stack. */
extern int esp32p4_internal_call(int (*call)(void *), void *argument);
struct mapping { esp_paddr_t physical; size_t bytes; void *address; bool remove; };
static int map_internal(void *argument)
{
  struct mapping *m = argument;
  if (m->remove) return esp_mmu_unmap(m->address);
  return esp_mmu_map(m->physical, m->bytes, MMU_TARGET_PSRAM0,
                    MMU_MEM_CAP_EXEC | MMU_MEM_CAP_READ, 0, &m->address);
}

static bool inside_window(uintptr_t address, size_t bytes)
{
  return address >= (uintptr_t)_native_exec_start &&
         address <= (uintptr_t)_native_exec_end - bytes;
}

/* Module code is backed on demand from ordinary PSRAM and exposed only through
 * the executable alias window. Nothing is reserved at boot. There is no memory
 * policy here: each module budgets itself, and an allocation the heap cannot
 * satisfy simply fails. Several modules may be resident at the same time. */
void *up_textheap_memalign(size_t alignment, size_t bytes)
{
  size_t window = (size_t)(_native_exec_end - _native_exec_start);
  if (!alignment || (alignment & (alignment - 1)) ||
      alignment > 65536 || !bytes || bytes > window - 65535u)
    return NULL;
  size_t rounded = (bytes + 65535u) & ~(size_t)65535u;
  pthread_mutex_lock(&lock);
  struct text_slot *slot = NULL;
  for (unsigned i = 0; i < OV_NATIVE_TEXT_SLOTS && !slot; i++)
    if (!slots[i].mapped && !slots[i].failed) slot = &slots[i];
  void *storage = slot ? memalign(65536, rounded) : NULL;
  if (!storage) goto unlock;
  esp_paddr_t physical; mmu_target_t target;
  if (esp_mmu_vaddr_to_paddr(storage, &physical, &target) != ESP_OK ||
      target != MMU_TARGET_PSRAM0 || (physical & 65535u)) goto free_storage;
  if (esp_cache_msync(storage, rounded, ESP_CACHE_MSYNC_FLAG_DIR_C2M |
      ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_INVALIDATE) != ESP_OK)
    goto free_storage;
  struct mapping operation = {.physical=physical, .bytes=rounded};
  if (esp32p4_internal_call(map_internal, &operation) != ESP_OK) goto free_storage;
  if (!inside_window((uintptr_t)operation.address, rounded)) {
    operation.remove = true;
    if (esp32p4_internal_call(map_internal, &operation) != ESP_OK) {
      slot->backing = storage; slot->failed = true; /* retained, never reused */
      storage = NULL; goto unlock;
    }
    goto free_storage;
  }
  slot->backing = storage; slot->mapped = operation.address; slot->bytes = rounded;
  pthread_mutex_unlock(&lock);
  return operation.address;
free_storage:
  free(storage);
  storage = NULL;
unlock:
  pthread_mutex_unlock(&lock);
  return NULL;
}

void up_textheap_free(void *ptr)
{
  if (!ptr) return;
  pthread_mutex_lock(&lock);
  for (unsigned i = 0; i < OV_NATIVE_TEXT_SLOTS; i++) {
    struct text_slot *slot = &slots[i];
    if (slot->mapped != ptr) continue;
    if (esp_cache_msync(slot->mapped, slot->bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M |
        ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_INVALIDATE) != ESP_OK)
      break;
    struct mapping operation = {.address=slot->mapped, .remove=true};
    if (esp32p4_internal_call(map_internal, &operation) != ESP_OK) break;
    free(slot->backing);
    slot->backing = NULL; slot->mapped = NULL; slot->bytes = 0;
    break;
  }
  pthread_mutex_unlock(&lock);
}

bool up_textheap_heapmember(void *ptr)
{
  bool member = false;
  pthread_mutex_lock(&lock);
  for (unsigned i = 0; i < OV_NATIVE_TEXT_SLOTS && !member; i++)
    member = slots[i].mapped && (uintptr_t)ptr >= (uintptr_t)slots[i].mapped &&
             (uintptr_t)ptr - (uintptr_t)slots[i].mapped < slots[i].bytes;
  pthread_mutex_unlock(&lock);
  return member;
}

#ifdef __NuttX__
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
struct probe {void *code; int result,cpu;};
static void *probe_cpu(void *argument)
{
  struct probe *p=argument;
  int (*function)(void)=(int (*)(void))p->code;
  p->cpu=sched_getcpu();p->result=function();
  return NULL;
}
/* Explicit diagnostic only. No application is installed or started. Test a
 * tiny return function through the actual alias on each CPU, then unmap/free.
 * Normal module unloading follows the module's own quiesce contract. */
int ov_native_text_heap_test(void)
{
  size_t before=mallinfo().fordblks;
  void *code=up_textheap_memalign(64,64);int ret=code?0:-ENOMEM;
  unsigned mask=0;bool quiescent=true;
  if(code){
    const uint32_t instructions[]={0x02a00513,0x00008067}; /* return 42 */
    memcpy(code,instructions,sizeof(instructions));
    if(esp_cache_msync(code,64,ESP_CACHE_MSYNC_FLAG_DIR_C2M|ESP_CACHE_MSYNC_FLAG_TYPE_DATA)!=ESP_OK ||
       esp_cache_msync(code,64,ESP_CACHE_MSYNC_FLAG_DIR_M2C|ESP_CACHE_MSYNC_FLAG_TYPE_INST)!=ESP_OK)ret=-EIO;
    for(unsigned cpu=0;cpu<2&&!ret;cpu++){
      pthread_attr_t attr;pthread_t thread;cpu_set_t affinity;
      int error=pthread_attr_init(&attr);if(error){ret=-error;break;}
      CPU_ZERO(&affinity);CPU_SET(cpu,&affinity);
      error=pthread_attr_setstacksize(&attr,4096);
      if(!error)error=pthread_attr_setaffinity_np(&attr,sizeof(affinity),&affinity);
      struct probe *probe=calloc(1,sizeof(*probe));
      if(!probe)error=ENOMEM;
      else {probe->code=code;probe->cpu=-1;}
      if(!error){error=pthread_create(&thread,&attr,probe_cpu,probe);if(!error)quiescent=false;}
      pthread_attr_destroy(&attr);
      if(!error){error=pthread_join(thread,NULL);if(!error)quiescent=true;}
      if(error)ret=-error;
      else if(probe->cpu!=(int)cpu||probe->result!=42)ret=-EFAULT;
      else mask|=1u<<cpu;
      if(quiescent)free(probe); /* A failed join retains both argument and code. */
    }
    if(quiescent){up_textheap_free(code);if(up_textheap_heapmember(code))ret=-EIO;}
  }
  printf("NATIVE_HEAP_TEST result=%d cpu_mask=%u physical_reserved=0 before_free=%lu after_free=%lu\n",
         ret,mask,(unsigned long)before,(unsigned long)mallinfo().fordblks);
  return ret;
}
#endif
