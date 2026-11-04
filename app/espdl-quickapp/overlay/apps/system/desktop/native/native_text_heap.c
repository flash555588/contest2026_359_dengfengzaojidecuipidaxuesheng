/* SPDX-License-Identifier: Apache-2.0 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <pthread.h>
#include <esp_cache.h>
#include <esp_mmu_map.h>

#ifndef OV_NATIVE_TEXT_SLOTS
#define OV_NATIVE_TEXT_SLOTS 8
#endif

#define PAGE 65536u

extern unsigned char _native_exec_start[], _native_exec_end[];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
/* One MMU page of module code: physical backing and its executable alias. */
struct text_page { void *backing, *mapped; };
/* A module's code is one contiguous alias [base, base+bytes) built from
 * count independently allocated physical pages, so loading never needs one
 * large contiguous heap block. */
static struct text_slot { uint8_t *base; size_t bytes; struct text_page *pages; unsigned count; }
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

/* Back one MMU page and map it executable. On success the page is mapped
 * inside the window. Returns false on failure; a page whose rollback unmap
 * failed keeps its backing forever (it may still be aliased). */
static bool map_page(struct text_page *page)
{
  void *storage = memalign(PAGE, PAGE);
  if (!storage) return false;
  esp_paddr_t physical; mmu_target_t target;
  if (esp_mmu_vaddr_to_paddr(storage, &physical, &target) != ESP_OK ||
      target != MMU_TARGET_PSRAM0 || (physical & (PAGE - 1))) goto free_storage;
  if (esp_cache_msync(storage, PAGE, ESP_CACHE_MSYNC_FLAG_DIR_C2M |
      ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_INVALIDATE) != ESP_OK)
    goto free_storage;
  struct mapping operation = {.physical=physical, .bytes=PAGE};
  if (esp32p4_internal_call(map_internal, &operation) != ESP_OK) goto free_storage;
  if (!inside_window((uintptr_t)operation.address, PAGE)) {
    operation.remove = true;
    if (esp32p4_internal_call(map_internal, &operation) != ESP_OK)
      return false; /* storage retained, never reused */
    goto free_storage;
  }
  page->backing = storage;
  page->mapped = operation.address;
  return true;
free_storage:
  free(storage);
  return false;
}

/* Returns false if the alias is still mapped; the backing is then kept. */
static bool unmap_page(struct text_page *page)
{
  if (esp_cache_msync(page->mapped, PAGE, ESP_CACHE_MSYNC_FLAG_DIR_C2M |
      ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_INVALIDATE) != ESP_OK)
    return false;
  struct mapping operation = {.address=page->mapped, .remove=true};
  if (esp32p4_internal_call(map_internal, &operation) != ESP_OK) return false;
  free(page->backing);
  page->backing = NULL;
  page->mapped = NULL;
  return true;
}

/* Unmap pages[0..count) from the top; stops at the first failure. */
static unsigned unmap_pages(struct text_page *pages, unsigned count)
{
  while (count && unmap_page(&pages[count - 1])) count--;
  return count;
}

/* Module code is backed on demand from ordinary PSRAM and exposed only through
 * the executable alias window. Nothing is reserved at boot. There is no memory
 * policy here: each module budgets itself, and an allocation the heap cannot
 * satisfy simply fails. Several modules may be resident at the same time.
 *
 * The physical backing is a set of separate 64 KiB pages, so a fragmented
 * heap (after an app has run for a while) can still load a module of any size
 * that fits in free memory. The SDK places each mapping at the lowest free
 * alias gap, so consecutive pages form one run unless a hole left by another
 * module ends it. Such a short run stays mapped as a filler while the module
 * is placed above it, and is released once the module's run is complete. */
void *up_textheap_memalign(size_t alignment, size_t bytes)
{
  size_t window = (size_t)(_native_exec_end - _native_exec_start);
  if (!alignment || (alignment & (alignment - 1)) ||
      alignment > PAGE || !bytes || bytes > window - (PAGE - 1))
    return NULL;
  unsigned need = (unsigned)((bytes + PAGE - 1) / PAGE);
  void *result = NULL;
  pthread_mutex_lock(&lock);
  struct text_slot *slot = NULL;
  for (unsigned i = 0; i < OV_NATIVE_TEXT_SLOTS && !slot; i++)
    if (!slots[i].pages) slot = &slots[i];
  struct text_page *run = slot ? calloc(need, sizeof(*run)) : NULL;
  struct text_page *fill = NULL;
  unsigned have = 0, filled = 0, fill_cap = 0;
  bool ok = run != NULL;
  while (ok && have < need) {
    struct text_page page;
    if (!map_page(&page)) { ok = false; break; }
    if (have && (uint8_t *)page.mapped != (uint8_t *)run[0].mapped + (size_t)have * PAGE) {
      /* The run filled a hole; keep it mapped as a filler and restart. */
      if (filled + have > fill_cap) {
        unsigned cap = (filled + have) * 2;
        struct text_page *grown = realloc(fill, cap * sizeof(*grown));
        if (!grown) { (void)unmap_page(&page); ok = false; break; }
        fill = grown;
        fill_cap = cap;
      }
      memcpy(&fill[filled], run, have * sizeof(*run));
      filled += have;
      have = 0;
    }
    run[have++] = page;
  }
  if (ok) {
    slot->base = run[0].mapped;
    slot->bytes = (size_t)need * PAGE;
    slot->pages = run;
    slot->count = need;
    result = slot->base;
  } else if (run) {
    /* Pages that cannot be unmapped are retained, never reused. */
    if (unmap_pages(run, have)) run = NULL; /* leaked deliberately */
    free(run);
  }
  /* Fillers are released either way; a failed unmap keeps its backing. */
  for (unsigned i = 0; i < filled; i++) (void)unmap_page(&fill[i]);
  free(fill);
  pthread_mutex_unlock(&lock);
  return result;
}

void up_textheap_free(void *ptr)
{
  if (!ptr) return;
  pthread_mutex_lock(&lock);
  for (unsigned i = 0; i < OV_NATIVE_TEXT_SLOTS; i++) {
    struct text_slot *slot = &slots[i];
    if (!slot->pages || slot->base != ptr) continue;
    /* On an unmap failure the slot stays mapped and can be freed again. */
    slot->count = unmap_pages(slot->pages, slot->count);
    if (!slot->count) {
      free(slot->pages);
      slot->pages = NULL; slot->base = NULL; slot->bytes = 0;
    }
    break;
  }
  pthread_mutex_unlock(&lock);
}

bool up_textheap_heapmember(void *ptr)
{
  bool member = false;
  pthread_mutex_lock(&lock);
  for (unsigned i = 0; i < OV_NATIVE_TEXT_SLOTS && !member; i++)
    member = slots[i].pages && (uintptr_t)ptr >= (uintptr_t)slots[i].base &&
             (uintptr_t)ptr - (uintptr_t)slots[i].base < slots[i].bytes;
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
