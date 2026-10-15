#!/usr/bin/env python3
"""Build an isolated pixel-art mini candidate with a per-polygon opt-in."""
from pathlib import Path
import hashlib,json
from generate_grok_sizes import replace_once

def main():
    repo=Path(__file__).resolve().parents[1];root=repo.parent;old=root/'output/grok-sizes';out=root/'output/grok-mosaic';out.mkdir(exist_ok=True)
    source=(old/'mini/app.js').read_text(encoding='utf-8')
    source=replace_once(source,'var grad=gradient(fill,m,points,packed);','var grad=gradient(fill,m,points,packed);\n        var pixelCell=grad&&grad[0]===1?2:1;')
    for call in ['ui.polygonBuffer(points.buffer,c,opa,width,grad)','ui.polygon(points,c,opa,width,grad)',
                 'ui.polygonSetBuffer(n.handles[k],points.buffer,c,opa,width,grad)','ui.polygonSet(n.handles[k],points,c,opa,width,grad)']:
        source=replace_once(source,call,call[:-1]+',pixelCell)')
    (out/'app.js').write_text(source,encoding='utf-8')
    polygon=(old/'polygon.inc').read_text(encoding='utf-8')
    polygon=replace_once(polygon,'unsigned stroke;','unsigned stroke,pixel_cell;')
    helper=(repo/'ouo/grok_mosaic_draw.inc').read_text(encoding='utf-8')
    polygon=replace_once(polygon,'static void qpk_polygon_draw(lv_event_t *e)',helper+'\nstatic void qpk_polygon_draw(lv_event_t *e)')
    polygon=replace_once(polygon,'lv_layer_t *layer=lv_event_get_layer(e);','lv_layer_t *layer=lv_event_get_layer(e);\n  if(p->pixel_cell){grok_mosaic_draw(layer,p,&coords);return;}')
    polygon=replace_once(polygon,'if(handle&&!qpk_widget_current(handle,generation))',
        'int32_t cell=0;\n  if(argc>offset+5&&!JS_IsUndefined(argv[offset+5])&&JS_ToInt32(c,&cell,argv[offset+5])<0)return JS_EXCEPTION;\n  if(cell<0||cell>2)return JS_ThrowRangeError(c,"pixel cell must be 0, 1 or 2");\n  if(cell){minx=(int32_t)floor((double)minx/cell)*cell;miny=(int32_t)floor((double)miny/cell)*cell;maxx=(int32_t)ceil((double)(maxx+1)/cell)*cell-1;maxy=(int32_t)ceil((double)(maxy+1)/cell)*cell-1;}\n  if(handle&&!qpk_widget_current(handle,generation))')
    polygon=replace_once(polygon,'p->stroke==sw&&','p->stroke==sw&&p->pixel_cell==(unsigned)cell&&')
    polygon=replace_once(polygon,'p->opacity=opa;p->stroke=sw;','p->opacity=opa;p->stroke=sw;p->pixel_cell=cell;')
    (out/'polygon.inc').write_text(polygon,encoding='utf-8')
    tests=(old/'mini/test-app.cjs').read_text(encoding='utf-8')
    tests=replace_once(tests,'function polygon(points,color,opacity,width,gradient){','function polygon(points,color,opacity,width,gradient,pixelCell){assert.equal(pixelCell,gradient&&gradient[0]===1?2:1);')
    (out/'test-app.cjs').write_text(tests,encoding='utf-8')
    sources=json.loads((old/'SOURCES.json').read_text())
    report={'diameter_px':48,'pixel_cell_px':2,'detail_cell_px':1,'body_palette':['#FFF5D9','#EEE3C4','#D7C7A3','#B7A68A','#9E907D'],'rim_color':'#8C8477','per_polygon_opt_in':True,'full_effects':True,'flashed':False,'production_applied':False,
        'source_sha256':{'app':hashlib.sha256((out/'app.js').read_bytes()).hexdigest(),'polygon':hashlib.sha256((out/'polygon.inc').read_bytes()).hexdigest()},
        'medium_app_sha256':hashlib.sha256((old/'medium/app.js').read_bytes()).hexdigest(),'production_sha256':sources['production_sha256']}
    (out/'SOURCES.json').write_text(json.dumps(report,indent=2)+'\n')
    print('GROK_MOSAIC_CANDIDATE_OK',report['source_sha256'])
if __name__=='__main__':main()
