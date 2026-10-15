#!/usr/bin/env python3
"""One-time extraction of the bounded polygon renderer for embedded Grok reuse."""
from pathlib import Path

root=Path(__file__).resolve().parents[2]
desktop=root/'project/app/espdl-quickapp/overlay/apps/system/desktop'
source=(desktop/'qpk_polygon.inc').read_text(encoding='utf-8')
if 'qpk_polygon_draw.inc' in source:
    raise SystemExit('Vector renderer already extracted')
split=source.index('static JSValue qpk_polygon_update')
draw=source[:split].replace('static bool qpk_widget_current(int handle,uint64_t generation);\n','').replace('unsigned stroke;','unsigned stroke,pixel_cell;')
pixel=(root/'output/grok-styles/style-draw.inc').read_text(encoding='utf-8')
draw=draw.replace('static void qpk_polygon_draw(lv_event_t *e)',pixel+'\nstatic void qpk_polygon_draw(lv_event_t *e)')
draw=draw.replace('lv_layer_t *layer=lv_event_get_layer(e);','lv_layer_t *layer=lv_event_get_layer(e);\n  if(p->pixel_cell){grok_mosaic_draw(layer,p,&coords);return;}')
(desktop/'qpk_polygon_draw.inc').write_text(draw,encoding='utf-8')
create=source.index('static JSValue qpk_polygon_create')
convert=source[split:create].replace('qpk_widget_current(handle,generation)','QPK_POLYGON_CURRENT(c,handle,generation)')
convert=convert.replace('  if(handle&&!QPK_POLYGON_CURRENT', '''  int32_t cell=0;
  if(argc>offset+5&&!JS_IsUndefined(argv[offset+5])&&JS_ToInt32(c,&cell,argv[offset+5])<0)return JS_EXCEPTION;
  if(cell!=0&&cell!=1&&cell!=2&&cell!=4)return JS_ThrowRangeError(c,"pixel cell must be 0, 1, 2 or 4");
  if(cell){minx=(int32_t)floor((double)minx/cell)*cell;miny=(int32_t)floor((double)miny/cell)*cell;maxx=(int32_t)ceil((double)(maxx+1)/cell)*cell-1;maxy=(int32_t)ceil((double)(maxy+1)/cell)*cell-1;}
  if(handle&&!QPK_POLYGON_CURRENT''')
convert=convert.replace('p->stroke==sw&&','p->stroke==sw&&p->pixel_cell==(unsigned)cell&&').replace('p->opacity=opa;p->stroke=sw;','p->opacity=opa;p->stroke=sw;p->pixel_cell=cell;')
(desktop/'qpk_polygon_convert.inc').write_text('/* SPDX-License-Identifier: Apache-2.0 */\n'+convert,encoding='utf-8')
(desktop/'qpk_polygon.inc').write_text('''/* SPDX-License-Identifier: Apache-2.0 */
static bool qpk_widget_current(int handle,uint64_t generation);
#include "qpk_polygon_draw.inc"
#define QPK_POLYGON_CURRENT(c,h,g) qpk_widget_current(h,g)
#include "qpk_polygon_convert.inc"
#undef QPK_POLYGON_CURRENT
'''+source[create:],encoding='utf-8')
print('BOUNDED_VECTOR_RENDERER_EXTRACTED')
