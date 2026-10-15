#!/usr/bin/env python3
"""Generate separate pixel and mosaic candidates without touching app entries."""
from pathlib import Path
import hashlib,json
from generate_grok_sizes import replace_once

def main():
    root=Path(__file__).resolve().parents[2];base=root/'output/grok-sizes';out=root/'output/grok-styles';out.mkdir(exist_ok=True)
    previous=(root/'output/grok-mosaic/polygon.inc').read_text(encoding='utf-8')
    begin=previous.index('/* SPDX-License-Identifier: Apache-2.0 */',previous.index('static lv_color_t qpk_polygon_color'))
    end=previous.index('static void qpk_polygon_draw',begin)
    helper=(root/'project/ouo/grok_mosaic_draw.inc').read_text(encoding='utf-8')
    helper=replace_once(helper,'spans[QPK_POLYGON_POINTS]','spans[QPK_POLYGON_POINTS*2]')
    color_begin=helper.index('  if(p->gradient.kind==1) {');color_end=helper.index('  lv_color_t c=qpk_polygon_color',color_begin)
    helper=helper[:color_begin]+'''  if(p->gradient.kind==1&&p->pixel_cell==1) {
    if(grok_mosaic_on_stroke(p,x,y))return 0x989b95;
    lv_color_t c=qpk_polygon_color(p,x+half,y+half);
    int brightness=(c.red+c.green+c.blue)/3;
    return brightness>244?0xf7f6f0:brightness>233?0xebe9e1:brightness>221?0xdddbd2:brightness>205?0xc8c7bd:0xb2b3ac;
  }
'''+helper[color_end:]
    helper=replace_once(helper,'p->pixel_cell)*0.5:1.6','p->pixel_cell)*0.5:0.7')
    needle='    for(unsigned i=0;i<merged;i++) {\n      int run=-1;'
    tiles='''    if(p->pixel_cell==4&&p->gradient.kind==1) {
      /* Non-overlapping tile and grout rectangles preserve one alpha
       * compositing operation per pixel, including partially dirty cells. */
      for(unsigned i=0;i<merged;i++)for(int x=spans[i].l;x<=spans[i].r;x+=cell) {
        lv_area_t tile={coords->x1+x,coords->y1+y,coords->x1+x+cell-2,coords->y1+y+cell-2};
        d.bg_color=lv_color_hex(grok_mosaic_color(p,x,y));lv_draw_rect(layer,&d,&tile);
        d.bg_color=lv_color_hex(0xcdd0cc);
        lv_area_t side={coords->x1+x+cell-1,coords->y1+y,coords->x1+x+cell-1,coords->y1+y+cell-1};
        lv_draw_rect(layer,&d,&side);
        lv_area_t bottom={coords->x1+x,coords->y1+y+cell-1,coords->x1+x+cell-2,coords->y1+y+cell-1};
        lv_draw_rect(layer,&d,&bottom);
      }
      continue;
    }
'''
    helper=replace_once(helper,needle,tiles+needle)
    polygon=previous[:begin]+helper+'\n'+previous[end:]
    polygon=replace_once(polygon,'if(cell<0||cell>2)return JS_ThrowRangeError(c,"pixel cell must be 0, 1 or 2");','if(cell!=0&&cell!=1&&cell!=2&&cell!=4)return JS_ThrowRangeError(c,"pixel cell must be 0, 1, 2 or 4");')
    (out/'polygon.inc').write_bytes(polygon.encode('utf-8'))
    (out/'style-draw.inc').write_bytes(helper.encode('utf-8'))
    variants=[]
    for name,old_name,diameter in [('pixel','mini',48),('mosaic','medium',80)]:
        folder=out/name;folder.mkdir(exist_ok=True)
        source=(base/old_name/'app.js').read_text(encoding='utf-8')
        expression='1' if name=='pixel' else '(grad&&grad[0]===1?4:2)'
        source=replace_once(source,'var grad=gradient(fill,m,points,packed);',f'var grad=gradient(fill,m,points,packed);\n        var pixelCell={expression};')
        for call in ['ui.polygonBuffer(points.buffer,c,opa,width,grad)','ui.polygon(points,c,opa,width,grad)',
                     'ui.polygonSetBuffer(n.handles[k],points.buffer,c,opa,width,grad)','ui.polygonSet(n.handles[k],points,c,opa,width,grad)']:
            source=replace_once(source,call,call[:-1]+',pixelCell)')
        (folder/'app.js').write_bytes(source.encode('utf-8'))
        tests=(base/old_name/'test-app.cjs').read_text(encoding='utf-8')
        expected='1' if name=='pixel' else '(gradient&&gradient[0]===1?4:2)'
        tests=replace_once(tests,'function polygon(points,color,opacity,width,gradient){',f'function polygon(points,color,opacity,width,gradient,pixelCell){{assert.equal(pixelCell,{expected});')
        (folder/'test-app.cjs').write_text(tests,encoding='utf-8')
        variants.append({'name':name,'diameter_px':diameter,'body_cell_px':1 if name=='pixel' else 4,'detail_cell_px':1 if name=='pixel' else 2,'app_sha256':hashlib.sha256(source.encode()).hexdigest()})
    sources=json.loads((base/'SOURCES.json').read_text())
    report={'variants':variants,'flashed':False,'production_applied':False,'full_effects':True,
        'polygon_sha256':hashlib.sha256((out/'polygon.inc').read_bytes()).hexdigest(),'production_sha256':sources['production_sha256']}
    (out/'SOURCES.json').write_text(json.dumps(report,indent=2)+'\n')
    guards=(root/'output/grok-mosaic/guards.c').read_text()
    # Only the after candidate checks the optional style parameter.
    (out/'guards.c').write_text(guards)
    print('GROK_TWO_STYLES_OK',variants)
if __name__=='__main__':main()
