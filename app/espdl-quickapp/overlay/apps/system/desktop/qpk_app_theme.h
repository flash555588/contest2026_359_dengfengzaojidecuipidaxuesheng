/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <quickjs.h>
#include "glass_app_theme.h"
static void qpk_app_theme_bind(JSContext *context,JSValue ui,bool light)
{
  struct glass_app_palette p=glass_app_palette(light);
  JSValue theme=JS_NewObject(context);
  const char *names[]={"background","card","surface","border","primary","secondary",
    "accent","onAccent","success","warning","danger","cardRadius","controlRadius",
    "controlHeight","gap","padding","sidebarWidth","sidebarPad","sidebarItemWidth","sidebarItemHeight","sidebarGap"};
  const uint32_t values[]={p.canvas,p.paper,p.soft,p.line,p.ink,p.muted,p.accent,p.on_accent,
    p.success,p.warning,p.danger,GLASS_APP_CARD_RADIUS,GLASS_APP_CONTROL_RADIUS,
    GLASS_APP_CONTROL_HEIGHT,GLASS_APP_GAP,GLASS_APP_PADDING,GLASS_APP_SIDEBAR_WIDTH,
    GLASS_APP_SIDEBAR_PAD,GLASS_APP_SIDEBAR_ITEM_WIDTH,GLASS_APP_SIDEBAR_ITEM_HEIGHT,GLASS_APP_SIDEBAR_GAP};
  for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);i++)
    JS_SetPropertyStr(context,theme,names[i],JS_NewUint32(context,values[i]));
  JS_SetPropertyStr(context,theme,"light",JS_NewBool(context,light));
  JS_SetPropertyStr(context,ui,"theme",theme);
}
