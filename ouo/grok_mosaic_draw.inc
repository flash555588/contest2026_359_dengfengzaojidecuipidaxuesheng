/* SPDX-License-Identifier: Apache-2.0 */
/* Optional per-polygon pixel raster, for the isolated mini preview. */
static bool grok_mosaic_on_stroke(const struct qpk_polygon_s *p,int x,int y);
static uint32_t grok_mosaic_color(const struct qpk_polygon_s *p,int x,int y)
{
  if(!p->gradient.kind)return p->color;
  double half=p->pixel_cell*0.5;
  if(p->gradient.kind==1) {
    /* A deliberate sprite palette: readable ivory highlights, warm shadow
     * bands and a contrasting rim rather than near-white quantisation. */
    if(grok_mosaic_on_stroke(p,x,y))return 0x8c8477;
    const struct qpk_polygon_gradient_s *g=&p->gradient;
    double t=hypot(x+half-g->x1,y+half-g->y1)/g->r;
    return t<0.36?0xfff5d9:t<0.55?0xeee3c4:t<0.73?0xd7c7a3:t<0.9?0xb7a68a:0x9e907d;
  }
  lv_color_t c=qpk_polygon_color(p,x+half,y+half);
  /* Sixteen levels per channel, without dithering or edge antialiasing. */
  return (((c.red+8)/17*17)<<16)|(((c.green+8)/17*17)<<8)|((c.blue+8)/17*17);
}

static bool grok_mosaic_on_stroke(const struct qpk_polygon_s *p,int x,int y)
{
  double half=p->pixel_cell*0.5,px=x+half,py=y+half;
  double r=p->stroke?fmax(p->stroke,p->pixel_cell)*0.5:1.6;
  for(unsigned i=0;i<p->count;i++) {
    lv_point_precise_t a=p->points[i],b=p->points[(i+1)%p->count];
    double dx=b.x-a.x,dy=b.y-a.y,denom=dx*dx+dy*dy;
    double t=denom>0?((px-a.x)*dx+(py-a.y)*dy)/denom:0;
    if(t<0)t=0;if(t>1)t=1;
    double ex=px-a.x-t*dx,ey=py-a.y-t*dy;
    if(ex*ex+ey*ey<=r*r)return true;
  }
  return false;
}

static void grok_mosaic_draw(lv_layer_t *layer,const struct qpk_polygon_s *p,const lv_area_t *coords)
{
  if(!p->opacity)return;
  int cell=p->pixel_cell;
  const lv_area_t *clip=&layer->_clip_area;
  int xmin=clip->x1-coords->x1,xmax=clip->x2-coords->x1;
  int ymin=clip->y1-coords->y1,ymax=clip->y2-coords->y1;
  int width=coords->x2-coords->x1+1,height=coords->y2-coords->y1+1;
  if(xmin<0)xmin=0;if(ymin<0)ymin=0;
  if(xmax>=width)xmax=width-1;if(ymax>=height)ymax=height-1;
  if(xmin>xmax||ymin>ymax)return;
  xmin=xmin/cell*cell;ymin=ymin/cell*cell;
  lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.radius=0;d.bg_opa=p->opacity;
  for(int y=ymin;y<=ymax;y+=cell) {
    struct {int l,r;} spans[QPK_POLYGON_POINTS];unsigned span_count=0;
    if(!p->stroke)for(int dy=0;dy<cell;dy++) {
      double sample=y+dy+0.5,cuts[QPK_POLYGON_POINTS];unsigned count=0;
      for(unsigned i=0;i<p->count;i++) {
        lv_point_precise_t a=p->points[i],b=p->points[(i+1)%p->count];
        if((a.y<=sample&&b.y>sample)||(b.y<=sample&&a.y>sample)) {
          double x=a.x+(sample-a.y)*(b.x-a.x)/(b.y-a.y);
          unsigned j=count++;while(j&&cuts[j-1]>x){cuts[j]=cuts[j-1];j--;}cuts[j]=x;
        }
      }
      for(unsigned i=0;i+1<count;i+=2) {
        int l=(int)floor(cuts[i]/cell)*cell,r=(int)ceil(cuts[i+1]/cell)*cell-cell;
        if(l<xmin)l=xmin;if(r>xmax)r=xmax/cell*cell;if(l>r)continue;
        /* Two subrows yield at most count/2 * 2 bounded spans. */
        unsigned j=span_count++;
        while(j&&spans[j-1].l>l){spans[j]=spans[j-1];j--;}
        spans[j].l=l;spans[j].r=r;
      }
    }
    /* Merge coverage before compositing, so a translucent ribbon is painted
     * once per cell, including overlaps between the two sampled subrows. */
    unsigned merged=0;
    for(unsigned i=0;i<span_count;i++) {
      if(merged&&spans[i].l<=spans[merged-1].r+cell){if(spans[i].r>spans[merged-1].r)spans[merged-1].r=spans[i].r;}
      else {spans[merged].l=spans[i].l;spans[merged++].r=spans[i].r;}
    }
    if(p->stroke){spans[0].l=xmin;spans[0].r=xmax;merged=1;}
    for(unsigned i=0;i<merged;i++) {
      int run=-1;uint32_t previous=0;
      for(int x=spans[i].l;x<=spans[i].r+cell;x+=cell) {
        bool covered=x<=spans[i].r&&(!p->stroke||grok_mosaic_on_stroke(p,x,y));
        uint32_t color=covered?grok_mosaic_color(p,x,y):0;
        if(run>=0&&(!covered||color!=previous)) {
          lv_area_t area={coords->x1+run,coords->y1+y,coords->x1+x-1,coords->y1+y+cell-1};
          d.bg_color=lv_color_hex(previous);lv_draw_rect(layer,&d,&area);run=-1;
        }
        if(covered&&run<0){run=x;previous=color;}
      }
    }
  }
}
