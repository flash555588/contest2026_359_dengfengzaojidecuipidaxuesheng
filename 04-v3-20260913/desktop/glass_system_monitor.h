/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GLASS_SYSTEM_MONITOR_H
#define GLASS_SYSTEM_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

struct glass_system_sample
{
  unsigned cpu_tenths;
  int cpu_error;
  int heap_error;
  uint64_t heap_total;
  uint64_t heap_free;
  uint64_t heap_largest;
  uint64_t heap_peak;
};

struct glass_system_status
{
  bool ready;
  bool stopped;
  int error;
  struct glass_system_sample sample;
};

struct glass_system_monitor;

/* One monitor at a time. Returns a positive errno, including retryable EBUSY.
 * Samples once per second while owned; no filesystem I/O or LVGL calls.
 */
int glass_system_start(struct glass_system_monitor **monitor);
int glass_system_read(struct glass_system_monitor *monitor,
                      struct glass_system_status *status);
bool glass_system_busy(void);

/* Sole-owner release cancels and wakes the worker without joining it.
 * An in-flight sample may finish later; it retains its own reference.
 */
void glass_system_release(struct glass_system_monitor *monitor);

/* Platform provider; called only by the worker. */
void glass_system_sample_native(struct glass_system_sample *sample);

#endif
