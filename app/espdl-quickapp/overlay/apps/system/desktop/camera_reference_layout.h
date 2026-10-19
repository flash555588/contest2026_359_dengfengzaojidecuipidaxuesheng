/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
/* The same dimensions drive LVGL and direct RGB565 composition. */
static const int camera_reference_guides[][4] = {
  {126,22,28,2},{126,22,2,28},{668,22,28,2},{694,22,2,28},
  {126,576,28,2},{126,550,2,28},{668,576,28,2},{694,550,2,28},
  {180,123,4,354},{643,123,4,354},{216,74,390,4},
  {216,526,102,4},{505,526,101,4}
};

/* Coordinates are shared by the visual compositor and hit testing. */
static int camera_reference_hit(int x,int y)
{
  if(x>=700&&x<764&&y>=160&&y<440)return 5;
  if(x>=836&&x<948&&y>=244&&y<356)return 1;
  if(x>=988&&x<1024&&y>=210&&y<390)return 2;
  if(x>=856&&x<934&&y>=24&&y<96)return 3;
  if(x>=700&&x<764&&y>=28&&y<100)return 6;
  if(x>=856&&x<934&&y>=504&&y<582)return 7;
  if(x>=856&&x<934&&y>=128&&y<202)return 4;
  return 0;
}

static const unsigned camera_reference_zoom_values[4]={100,150,200,300};
static int camera_reference_zoom_index(unsigned percent)
{
  for(int i=0;i<4;i++)if(camera_reference_zoom_values[i]==percent)return i;
  return -1;
}
static int camera_reference_zoom_clamp(int index)
{return index<0?0:index>3?3:index;}
static int camera_reference_zoom_drag(int start,int down,int current)
{return camera_reference_zoom_clamp(start+(down-current)/48);}
static int camera_reference_zoom_tap(int y)
{return camera_reference_zoom_clamp(3-(y-172)/64);}
