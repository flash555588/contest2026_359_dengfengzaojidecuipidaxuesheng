/* SPDX-License-Identifier: Apache-2.0 */
static JSValue qpk_polygon_update(JSContext *c,lv_obj_t *obj,struct qpk_polygon_s *p,
                                  int argc,JSValueConst *argv,int offset,int handle,uint64_t generation,bool packed)
{
  int32_t count;int ret=0;const uint8_t *bytes=NULL;
  if(argc<=offset)return JS_ThrowTypeError(c,"missing polygon points");
  if(packed){
    size_t size=0;bytes=JS_GetArrayBuffer(c,&size,argv[offset]);
    if(!bytes)return JS_ThrowTypeError(c,"polygon buffer must be an ArrayBuffer");
    if(size<3*8||size>QPK_POLYGON_POINTS*8||size%8)return JS_ThrowRangeError(c,"invalid polygon buffer length");
    count=size/8;
  }else{
    if(!JS_IsArray(c,argv[offset]))return JS_ThrowTypeError(c,"polygon points must be an array");
    JSValue length=JS_GetPropertyStr(c,argv[offset],"length");
    ret=JS_ToInt32(c,&count,length);JS_FreeValue(c,length);
    if(ret<0)return JS_EXCEPTION;
  }
  if(count<3||count>QPK_POLYGON_POINTS)return JS_ThrowRangeError(c,"invalid polygon points");
  lv_point_precise_t points[QPK_POLYGON_POINTS];
  int32_t minx=32767,miny=32767,maxx=-32767,maxy=-32767;
  for(int i=0;i<count;i++) {
    int32_t px,py;
    if(packed){
      /* Snapshot native-endian Int32 x/y pairs before converting any other
       * arguments: their getters may detach the buffer or delete the widget. */
      memcpy(&px,bytes+i*8,4);memcpy(&py,bytes+i*8+4,4);
    }else{
      JSValue pair=JS_GetPropertyUint32(c,argv[offset],i);
      JSValue x=JS_GetPropertyUint32(c,pair,0),y=JS_GetPropertyUint32(c,pair,1);
      ret=JS_ToInt32(c,&px,x);if(!ret)ret=JS_ToInt32(c,&py,y);
      JS_FreeValue(c,x);JS_FreeValue(c,y);JS_FreeValue(c,pair);
      if(ret<0)return JS_EXCEPTION;
    }
    if(px<-8192||px>8192||py<-8192||py>8192)return JS_ThrowRangeError(c,"polygon coordinate out of range");
    points[i].x=px;points[i].y=py;
    if(px<minx)minx=px;if(px>maxx)maxx=px;if(py<miny)miny=py;if(py>maxy)maxy=py;
  }
  uint32_t color=0xffffff;int32_t opacity=255,stroke=0;
  /* Conversions can run user code. Do not commit a fallback colour or style
   * when a getter throws, and keep generation validation after conversions. */
  if(argc>offset+1&&JS_ToUint32(c,&color,argv[offset+1])<0)return JS_EXCEPTION;
  if(argc>offset+2&&JS_ToInt32(c,&opacity,argv[offset+2])<0)return JS_EXCEPTION;
  if(argc>offset+3&&JS_ToInt32(c,&stroke,argv[offset+3])<0)return JS_EXCEPTION;
  struct qpk_polygon_gradient_s gradient={0};
  if(argc>offset+4&&!JS_IsNull(argv[offset+4])&&!JS_IsUndefined(argv[offset+4])) {
    JSValueConst input=argv[offset+4];
    if(!JS_IsArray(c,input))return JS_ThrowTypeError(c,"gradient must be an array");
    JSValue size=JS_GetPropertyStr(c,input,"length");int32_t n;
    ret=JS_ToInt32(c,&n,size);JS_FreeValue(c,size);if(ret<0)return JS_EXCEPTION;
    if(n<11||n>17||!(n&1))return JS_ThrowRangeError(c,"invalid gradient length");
    double values[17];
    for(int i=0;i<n;i++) {
      JSValue value=JS_GetPropertyUint32(c,input,i);ret=JS_ToFloat64(c,&values[i],value);JS_FreeValue(c,value);
      if(ret<0)return JS_EXCEPTION;
      if(!isfinite(values[i]))return JS_ThrowRangeError(c,"non-finite gradient");
    }
    if((values[0]!=1&&values[0]!=2)||values[1]!=(n-7)/2)return JS_ThrowRangeError(c,"invalid gradient type/count");
    for(int i=2;i<7;i++)if(fabs(values[i])>32768)return JS_ThrowRangeError(c,"gradient coordinate out of range");
    if(values[0]==1&&values[6]<=0)return JS_ThrowRangeError(c,"invalid gradient radius");
    gradient.kind=values[0];gradient.count=values[1];
    gradient.x1=values[2];gradient.y1=values[3];gradient.x2=values[4];gradient.y2=values[5];gradient.r=values[6];
    for(unsigned i=0;i<gradient.count;i++) {
      double position=values[7+i*2],color_value=values[8+i*2];
      if(position<0||position>1||(i&&position<gradient.offset[i-1])||color_value<0||color_value>0xffffff)
        return JS_ThrowRangeError(c,"invalid gradient stop");
      gradient.offset[i]=position;gradient.color[i]=(uint32_t)color_value;
    }
  }
  int32_t cell=0;
  if(argc>offset+5&&!JS_IsUndefined(argv[offset+5])&&JS_ToInt32(c,&cell,argv[offset+5])<0)return JS_EXCEPTION;
  if(cell!=0&&cell!=1&&cell!=2&&cell!=4)return JS_ThrowRangeError(c,"pixel cell must be 0, 1, 2 or 4");
  if(cell){minx=(int32_t)floor((double)minx/cell)*cell;miny=(int32_t)floor((double)miny/cell)*cell;maxx=(int32_t)ceil((double)(maxx+1)/cell)*cell-1;maxy=(int32_t)ceil((double)(maxy+1)/cell)*cell-1;}
  if(handle&&!QPK_POLYGON_CURRENT(c,handle,generation))return JS_ThrowRangeError(c,"polygon changed during conversion");
  gradient.x1-=minx;gradient.x2-=minx;gradient.y1-=miny;gradient.y2-=miny;
  unsigned opa=opacity<0?0:opacity>255?255:opacity,sw=stroke<0?0:stroke>8?8:stroke;
  bool unchanged=p->count==(unsigned)count&&p->color==color&&p->opacity==opa&&p->stroke==sw&&p->pixel_cell==(unsigned)cell&&
    p->gradient.kind==gradient.kind&&p->gradient.count==gradient.count&&
    p->gradient.x1==gradient.x1&&p->gradient.y1==gradient.y1&&
    p->gradient.x2==gradient.x2&&p->gradient.y2==gradient.y2&&p->gradient.r==gradient.r&&
    lv_obj_get_x(obj)==minx&&lv_obj_get_y(obj)==miny;
  for(int i=0;unchanged&&i<count;i++)unchanged=p->points[i].x==points[i].x-minx&&p->points[i].y==points[i].y-miny;
  for(unsigned i=0;unchanged&&i<gradient.count;i++)unchanged=p->gradient.offset[i]==gradient.offset[i]&&p->gradient.color[i]==gradient.color[i];
  if(unchanged)return JS_UNDEFINED;
  for(int i=0;i<count;i++) { p->points[i].x=points[i].x-minx;p->points[i].y=points[i].y-miny; }
  p->count=count;p->color=color;
  gradient.fx1=(float)gradient.x1;gradient.fy1=(float)gradient.y1;
  gradient.dx=(float)(gradient.x2-gradient.x1);gradient.dy=(float)(gradient.y2-gradient.y1);
  if(gradient.kind==1)gradient.inverse=1.0f/(float)gradient.r;
  else if(gradient.kind==2){double dx=gradient.x2-gradient.x1,dy=gradient.y2-gradient.y1,denom=dx*dx+dy*dy;gradient.inverse=denom>0.0001?(float)(1.0/denom):0;}
  gradient.fast=isfinite(gradient.inverse);
  for(unsigned i=0;i<gradient.count;i++){
    gradient.stops[i]=(float)gradient.offset[i];
    if(i&&gradient.offset[i]>gradient.offset[i-1]&&gradient.stops[i]==gradient.stops[i-1])gradient.fast=false;
  }
  p->gradient=gradient;
  p->opacity=opa;p->stroke=sw;p->pixel_cell=cell;
  lv_obj_set_pos(obj,minx,miny);lv_obj_set_size(obj,maxx-minx+1,maxy-miny+1);
  lv_obj_invalidate(obj);return JS_UNDEFINED;
}

