/* SPDX-License-Identifier: Apache-2.0 */
/* Bounded, filled vector contours for the SVG-to-LVGL Grok Ball adapter. */
#define QPK_POLYGON_POINTS 128
#include <math.h>
struct qpk_polygon_gradient_s {
  unsigned kind,count;
  double x1,y1,x2,y2,r,offset[5];
  uint32_t color[5];
  /* ESP32-P4's FPU is single precision. Validate the JS contract in double,
   * then prepare these draw-only values once, rather than emulate double
   * sqrt/division for every gradient endpoint on every refresh. */
  float fx1,fy1,dx,dy,inverse,stops[5];
  bool fast;
};
struct qpk_polygon_s {
  lv_point_precise_t points[QPK_POLYGON_POINTS];
  unsigned count;
  uint32_t color;
  lv_opa_t opacity;
  unsigned stroke,pixel_cell;
  struct qpk_polygon_gradient_s gradient;
};

static lv_color_t qpk_polygon_color_precise(const struct qpk_polygon_s *p,double x,double y)
{
  const struct qpk_polygon_gradient_s *g=&p->gradient;
  double t;
  if(g->kind==1)t=hypot(x-g->x1,y-g->y1)/g->r;
  else {double dx=g->x2-g->x1,dy=g->y2-g->y1,denom=dx*dx+dy*dy;t=denom>0.0001?((x-g->x1)*dx+(y-g->y1)*dy)/denom:0;}
  if(t<=g->offset[0])return lv_color_hex(g->color[0]);
  for(unsigned i=1;i<g->count;i++)if(t<=g->offset[i]) {
    double range=g->offset[i]-g->offset[i-1],mix=range>0?(t-g->offset[i-1])/range:1;
    uint32_t result=0;
    for(unsigned shift=0;shift<=16;shift+=8){double a=(g->color[i-1]>>shift)&255,b=(g->color[i]>>shift)&255;result|=(uint32_t)lround(a+(b-a)*mix)<<shift;}
    return lv_color_hex(result);
  }
  return lv_color_hex(g->color[g->count-1]);
}

static lv_color_t qpk_polygon_color(const struct qpk_polygon_s *p,float x,float y)
{
  const struct qpk_polygon_gradient_s *g=&p->gradient;
  if(!g->kind)return lv_color_hex(p->color);
  /* Extremely small but valid JS radii/stop intervals retain their existing
   * double behaviour; normal artwork uses the single-precision fast path. */
  if(!g->fast)return qpk_polygon_color_precise(p,x,y);
  float t;
  if(g->kind==1){float dx=x-g->fx1,dy=y-g->fy1;t=sqrtf(dx*dx+dy*dy)*g->inverse;}
  else {
    t=((x-g->fx1)*g->dx+(y-g->fy1)*g->dy)*g->inverse;
  }
  if(t<=g->stops[0])return lv_color_hex(g->color[0]);
  for(unsigned i=1;i<g->count;i++)if(t<=g->stops[i]) {
    float range=g->stops[i]-g->stops[i-1];
    float mix=range>0?(t-g->stops[i-1])/range:1;
    if(mix<0)mix=0;if(mix>1)mix=1;
    uint32_t result=0;
    for(unsigned shift=0;shift<=16;shift+=8) {
      float a=(g->color[i-1]>>shift)&255,b=(g->color[i]>>shift)&255;
      result|=(uint32_t)(a+(b-a)*mix+0.5f)<<shift;
    }
    return lv_color_hex(result);
  }
  return lv_color_hex(g->color[g->count-1]);
}

static void qpk_polygon_row(lv_layer_t *layer,lv_draw_rect_dsc_t *d,lv_area_t *row,
                           const struct qpk_polygon_s *p,const lv_area_t *coords)
{
  const lv_area_t *clip=&layer->_clip_area;
  if(row->x1>row->x2||row->x2<clip->x1||row->x1>clip->x2)return;
  if(!p->gradient.kind) {lv_draw_rect(layer,d,row);return;}
  /* Short horizontal gradients approximate the SVG radial field while keeping
   * the complete contour fill and using LVGL's normal gradient renderer. */
  int end=row->x2;
  /* Skip invisible spans, preserving the original 24 px segment origin and
   * endpoints so partial refreshes reproduce a full refresh's pixels. */
  if(row->x1<clip->x1)row->x1+=(clip->x1-row->x1)/24*24;
  d->bg_grad.dir=LV_GRAD_DIR_HOR;d->bg_grad.stops_count=2;
  d->bg_grad.stops[0].frac=0;d->bg_grad.stops[1].frac=255;
  d->bg_grad.stops[0].opa=d->bg_grad.stops[1].opa=LV_OPA_COVER;
  while(row->x1<=end&&row->x1<=clip->x2) {
    row->x2=row->x1+23;if(row->x2>end)row->x2=end;
    d->bg_grad.stops[0].color=qpk_polygon_color(p,row->x1-coords->x1+0.5f,row->y1-coords->y1+0.5f);
    d->bg_grad.stops[1].color=qpk_polygon_color(p,row->x2-coords->x1+0.5f,row->y1-coords->y1+0.5f);
    d->bg_color=d->bg_grad.stops[0].color;
    lv_draw_rect(layer,d,row);row->x1=row->x2+1;
  }
  d->bg_grad.dir=LV_GRAD_DIR_NONE;
}

/* SPDX-License-Identifier: Apache-2.0 */
/* Optional per-polygon pixel raster, for the isolated mini preview. */
static bool grok_mosaic_on_stroke(const struct qpk_polygon_s *p,int x,int y);
static uint32_t grok_mosaic_color(const struct qpk_polygon_s *p,int x,int y)
{
  if(!p->gradient.kind)return p->color;
  double half=p->pixel_cell*0.5;
  if(p->gradient.kind==1&&p->pixel_cell==1) {
    lv_color_t middle=lv_color_hex(p->gradient.color[p->gradient.count/2]);
    bool dark_body=(middle.red+middle.green+middle.blue)<288;
    if(grok_mosaic_on_stroke(p,x,y))return dark_body?0x111111:0x989b95;
    lv_color_t c=qpk_polygon_color(p,x+half,y+half);
    int brightness=(c.red+c.green+c.blue)/3;
    if(dark_body)return brightness>60?0x444444:brightness>40?0x353535:brightness>26?0x262626:brightness>15?0x1a1a1a:0x111111;
    return brightness>244?0xf7f6f0:brightness>233?0xebe9e1:brightness>221?0xdddbd2:brightness>205?0xc8c7bd:0xb2b3ac;
  }
  lv_color_t c=qpk_polygon_color(p,x+half,y+half);
  /* Sixteen levels per channel, without dithering or edge antialiasing. */
  return (((c.red+8)/17*17)<<16)|(((c.green+8)/17*17)<<8)|((c.blue+8)/17*17);
}

static bool grok_mosaic_on_stroke(const struct qpk_polygon_s *p,int x,int y)
{
  double half=p->pixel_cell*0.5,px=x+half,py=y+half;
  double r=p->stroke?fmax(p->stroke,p->pixel_cell)*0.5:0.7;
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
    struct {int l,r;} spans[QPK_POLYGON_POINTS*2];unsigned span_count=0;
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
    if(p->pixel_cell==4&&p->gradient.kind==1) {
      /* Non-overlapping tile and grout rectangles preserve one alpha
       * compositing operation per pixel, including partially dirty cells. */
      for(unsigned i=0;i<merged;i++)for(int x=spans[i].l;x<=spans[i].r;x+=cell) {
        lv_area_t tile={coords->x1+x,coords->y1+y,coords->x1+x+cell-2,coords->y1+y+cell-2};
        d.bg_color=lv_color_hex(grok_mosaic_color(p,x,y));lv_draw_rect(layer,&d,&tile);
        /* Charcoal tiles need dark grout; ivory keeps its established grout. */
        uint32_t middle=p->gradient.color[p->gradient.count/2];
        bool dark_body=(((middle>>16)&255)+((middle>>8)&255)+(middle&255))<288;
        d.bg_color=lv_color_hex(dark_body?0x111111:0xcdd0cc);
        lv_area_t side={coords->x1+x+cell-1,coords->y1+y,coords->x1+x+cell-1,coords->y1+y+cell-1};
        lv_draw_rect(layer,&d,&side);
        lv_area_t bottom={coords->x1+x,coords->y1+y+cell-1,coords->x1+x+cell-2,coords->y1+y+cell-1};
        lv_draw_rect(layer,&d,&bottom);
      }
      continue;
    }
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

static void qpk_polygon_draw(lv_event_t *e)
{
  struct qpk_polygon_s *p=lv_event_get_user_data(e);
  lv_area_t coords;lv_obj_get_coords(lv_event_get_target(e),&coords);
  lv_layer_t *layer=lv_event_get_layer(e);
  if(p->pixel_cell){grok_mosaic_draw(layer,p,&coords);return;}
  if(p->stroke) {
    lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);
    d.color=lv_color_hex(p->color);d.opa=p->opacity;d.width=p->stroke;
    d.round_start=d.round_end=true;
    for(unsigned i=0;i<p->count;i++) {
      d.p1=p->points[i];d.p2=p->points[(i+1)%p->count];
      d.p1.x+=coords.x1;d.p1.y+=coords.y1;d.p2.x+=coords.x1;d.p2.y+=coords.y1;
      lv_draw_line(layer,&d);
    }
  } else {
    /* Fill each contour once. Independently antialiased triangle edges leave
     * seams inside opaque eyes and overlap when ribbons are translucent. */
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=lv_color_hex(p->color);d.radius=0;
    double cuts[QPK_POLYGON_POINTS];
    int height=coords.y2-coords.y1+1;
    int first=layer->_clip_area.y1-coords.y1,last=layer->_clip_area.y2-coords.y1+1;
    if(first<0)first=0;if(last>height)last=height;
    /* Queue only edges that can intersect the dirty rows. A round body has
     * two active edges per row; rescanning all contour vertices each time
     * spends most of the refresh rejecting edges outside that row. Retain
     * the original intersection formula to keep edge antialiasing exact. */
    struct {unsigned index;int first,last;} edges[QPK_POLYGON_POINTS];
    unsigned edge_count=0,active[QPK_POLYGON_POINTS],active_count=0,next=0;
    for(unsigned i=0;i<p->count;i++) {
      lv_point_precise_t a=p->points[i],b=p->points[(i+1)%p->count];
      if(a.y==b.y)continue;
      int start=a.y<b.y?a.y:b.y,end=a.y>b.y?a.y:b.y;
      if(start<first)start=first;if(end>last)end=last;
      if(start>=end)continue;
      unsigned j=edge_count++;
      while(j&&edges[j-1].first>start){edges[j]=edges[j-1];j--;}
      edges[j].index=i;edges[j].first=start;edges[j].last=end;
    }
    for(int y=first;y<last;y++) {
      unsigned count=0;double sample=y+0.5;
      for(unsigned i=0;i<active_count;) {
        if(edges[active[i]].last<=y)active[i]=active[--active_count];else i++;
      }
      while(next<edge_count&&edges[next].first<=y)active[active_count++]=next++;
      for(unsigned i=0;i<active_count;i++) {
        unsigned index=edges[active[i]].index;
        lv_point_precise_t a=p->points[index],b=p->points[(index+1)%p->count];
        double x=a.x+(sample-a.y)*(b.x-a.x)/(b.y-a.y);
        unsigned j=count++;while(j&&cuts[j-1]>x){cuts[j]=cuts[j-1];j--;}cuts[j]=x;
      }
      for(unsigned i=0;i+1<count;i+=2) {
        double left=cuts[i],right=cuts[i+1];
        int l=(int)left,r=(int)right;
        lv_area_t row={coords.x1+l+1,coords.y1+y,coords.x1+r-1,coords.y1+y};
        d.bg_opa=p->opacity;
        qpk_polygon_row(layer,&d,&row,p,&coords);
        row.x1=row.x2=coords.x1+l;
        if(row.x1>=layer->_clip_area.x1&&row.x1<=layer->_clip_area.x2){
          d.bg_color=qpk_polygon_color(p,row.x1-coords.x1+0.5f,row.y1-coords.y1+0.5f);
          d.bg_opa=(lv_opa_t)(p->opacity*(l==r?right-left:l+1-left));
          if(d.bg_opa)lv_draw_rect(layer,&d,&row);
        }
        if(l!=r) {
          row.x1=row.x2=coords.x1+r;d.bg_opa=(lv_opa_t)(p->opacity*(right-r));
          if(row.x1>=layer->_clip_area.x1&&row.x1<=layer->_clip_area.x2){
            d.bg_color=qpk_polygon_color(p,row.x1-coords.x1+0.5f,row.y1-coords.y1+0.5f);
            if(d.bg_opa)lv_draw_rect(layer,&d,&row);
          }
        }
      }
    }
  }
}

