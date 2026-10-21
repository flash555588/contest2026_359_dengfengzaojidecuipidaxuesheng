/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* One contract for native pages, embedded Quick Apps and media tools. */
#define GLASS_APP_CARD_RADIUS 18
#define GLASS_APP_CONTROL_RADIUS 12
#define GLASS_APP_CONTROL_HEIGHT 44
#define GLASS_APP_GAP 12
#define GLASS_APP_PADDING 20
#define GLASS_APP_SIDEBAR_WIDTH 164
#define GLASS_APP_SIDEBAR_PAD 12
#define GLASS_APP_SIDEBAR_ITEM_WIDTH 140
#define GLASS_APP_SIDEBAR_ITEM_HEIGHT 44
#define GLASS_APP_SIDEBAR_GAP 8

struct glass_app_palette {
  uint32_t canvas,paper,soft,line,ink,muted,accent,on_accent,success,warning,danger;
};
static inline struct glass_app_palette glass_app_palette(bool light)
{
  return light ? (struct glass_app_palette){
    0xf4f5f8,0xffffff,0xf3f2f7,0xe8e9ef,0x292c38,0x808491,
    0x77658f,0xffffff,0x39816a,0x98672e,0xb34d63
  } : (struct glass_app_palette){
    0x15171e,0x22252f,0x2e303d,0x383b48,0xeeeef5,0xa2a7b7,
    0xc1aedb,0x22252f,0x82c6ad,0xe7ba80,0xf0a1b1
  };
}
