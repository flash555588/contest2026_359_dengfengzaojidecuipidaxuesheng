/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_system_monitor.h"

#include <errno.h>
#include <string.h>

#ifdef __NuttX__
#include <nuttx/config.h>
#include <nuttx/clock.h>
#include <malloc.h>

static int glass_system_cpu(unsigned *value)
{
#ifdef CONFIG_SCHED_CPULOAD_NONE
  (void)value;
  return ENOTSUP;
#else
  unsigned cores = 1;
#ifdef CONFIG_SMP
  cores = CONFIG_SMP_NCPUS;
#endif
  /* Match procfs: sum all idle PIDs, divide by one global total. Discard
   * mixed epochs when a scheduler tick/decay falls between the two reads.
   */
  for (unsigned attempt = 0; attempt < 3; attempt++)
    {
      uint64_t total = 0;
      uint64_t idle = 0;
      bool consistent = true;
      for (unsigned core = 0; core < cores; core++)
        {
          struct cpuload_s load;
          int result = clock_cpuload(core, &load);
          if (result < 0) return -result;
          if (!core) total = load.total;
          else if (total != (uint64_t)load.total) consistent = false;
          if ((uint64_t)load.active > UINT64_MAX - idle) return EOVERFLOW;
          idle += load.active;
        }
      if (!consistent) continue;
      if (!total) return EAGAIN;
      if (idle > total) idle = total;
      if (idle > UINT64_MAX / 1000) return EOVERFLOW;
      *value = 1000 - (unsigned)(idle * 1000 / total);
      return 0;
    }
  return EAGAIN;
#endif
}
#endif

void glass_system_sample_native(struct glass_system_sample *sample)
{
  memset(sample, 0, sizeof(*sample));
#ifdef __NuttX__
  sample->cpu_error = glass_system_cpu(&sample->cpu_tenths);
  /* mallinfo walks the allocator under its lock, so keep it off the UI. */
  struct mallinfo heap = mallinfo();
  if (!heap.arena || heap.fordblks > heap.arena || heap.mxordblk > heap.fordblks)
    sample->heap_error = EIO;
  else
    {
      sample->heap_total = heap.arena;
      sample->heap_free = heap.fordblks;
      sample->heap_largest = heap.mxordblk;
      sample->heap_peak = heap.usmblks;
    }
#else
  /* Host screenshots use explicit test fixtures, never host RAM as board RAM. */
  sample->cpu_error = ENOTSUP;
  sample->heap_error = ENOTSUP;
#endif
}
