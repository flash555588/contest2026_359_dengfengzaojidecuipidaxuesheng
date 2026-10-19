/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include "camera_reference_pixels.h"

static const uint16_t *camera_reference_control_pixels(unsigned tile,
                                                       unsigned style,bool video)
{
  if(tile==CAMERA_REFERENCE_RECORD_TILE&&video)return camera_record_active;
  if(tile==CAMERA_REFERENCE_FILTER_TILE){
    if(style==1)return camera_filter_1;
    if(style==2)return camera_filter_2;
  }
  return camera_reference_tiles[tile].pixels;
}
