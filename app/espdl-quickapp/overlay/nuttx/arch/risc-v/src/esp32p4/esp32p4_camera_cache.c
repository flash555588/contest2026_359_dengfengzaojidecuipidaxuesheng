/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_cache.h"

/* CSI owns the complete page until DQBUF. Discard stale CPU lines before
 * composing the camera controls. Generic up_invalidate_dcache() is a no-op
 * when CONFIG_ARCH_DCACHE is unset on this P4 HAL-managed cache build. */
int esp_camera_invalidate_framebuffer(void *address, size_t bytes)
{
  const size_t line=CONFIG_ESPRESSIF_CACHE_L2_CACHE_LINE_SIZE;
  if(!address||!bytes||((uintptr_t)address%line)||(bytes%line))return -EINVAL;
  uint8_t *current=address;
  while(bytes){
    size_t chunk=bytes>16384?16384:bytes;
    if(esp_cache_msync(current,chunk,ESP_CACHE_MSYNC_FLAG_DIR_M2C|
                       ESP_CACHE_MSYNC_FLAG_TYPE_DATA)!=ESP_OK)return -EIO;
    current+=chunk;bytes-=chunk;
  }
  return 0;
}
