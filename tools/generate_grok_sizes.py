#!/usr/bin/env python3
"""Generate isolated mini/medium Grok Ball candidates without updating app entries."""
from pathlib import Path
import hashlib,json

def replace_once(source,old,new):
    assert source.count(old)==1,old
    return source.replace(old,new)

def main():
    repo=Path(__file__).resolve().parents[1]; root=repo.parent
    out=root/'output/grok-sizes';out.mkdir(parents=True,exist_ok=True)
    adapter=(root/'output/grok-lossless/candidate-adapter.js').read_text(encoding='utf-8')
    adapter=replace_once(adapter,'mount:function(width,height) {root=new Node(\'mount\');scale=Math.min(height*0.56/228,width/420);',
        "mount:function(width,height,diameter) {root=new Node('mount');scale=Math.min(height*0.56/228,width/420,diameter/228);")
    app=(repo/'ouo/grok_app.js').read_text(encoding='utf-8')
    app=replace_once(app,'var mount=GrokPort.mount(view.width,view.height);',
        'var ballDiameter=Math.min(GrokSize.diameter,view.height*0.56,view.width*228/420);\n  var mount=GrokPort.mount(view.width,view.height,ballDiameter);')
    app=replace_once(app,'Math.min(view.height*0.56,view.width*228/420)/2+12','ballDiameter/2+GrokSize.captionGap')
    app=replace_once(app,"captionY,24,fg","captionY,GrokSize.captionFont,fg")
    app=replace_once(app,'captionY+38,16,muted','captionY+30,14,muted')
    app=replace_once(app,'r=Math.min(view.height*0.56,view.width*228/420)/2+16','r=Math.max(22,ballDiameter/2+16)')
    app=replace_once(app,'ball.setGaze((nx-0.5)*2,(ny-0.5)*2);',
        'ball.setGaze((nx-0.5)*view.width/(ballDiameter/2),(ny-0.43)*view.height/(ballDiameter/2));')
    app=replace_once(app,'showPicker:showPicker,closePicker:closePicker,',
        'showPicker:showPicker,closePicker:closePicker,\n    sizeVariant:GrokSize.name,ballDiameter:ballDiameter,')
    license=(repo/'ouo/vendor/GROK-BALL-LICENSE.txt').read_text(encoding='utf-8')
    engine=(repo/'ouo/vendor/grok-ball.js').read_text(encoding='utf-8')
    (out/'size-adapter.js').write_text(adapter,encoding='utf-8')
    (out/'size-app.js').write_text(app,encoding='utf-8')
    variants=[]
    for name,diameter,font,gap in [('mini',48,16,10),('medium',80,18,12)]:
        folder=out/name;folder.mkdir(exist_ok=True)
        config='var GrokSize='+json.dumps({'name':name,'diameter':diameter,'captionFont':font,'captionGap':gap})+';\n'
        source='\n'.join([f'/* SPDX-License-Identifier: Apache-2.0 AND MIT\n{license}\n*/\n',adapter,engine,config+app])
        (folder/'app.js').write_bytes(source.encode('utf-8'))
        # Reuse every existing regression assertion; adjust only the finger's
        # physical position to 65% of this smaller ball's radius.
        finger=0.5+0.65*diameter/2/1022
        tests=(repo/'app/espdl-quickapp/tests/test_grok_ball_app.js').read_text(encoding='utf-8')
        tests=tests.replace('0.58,0.4',f'{finger:.12f},0.43')
        tests=replace_once(tests,'const mood=ctx.VelaMood,ball=mood.engine;',
            f"const mood=ctx.VelaMood,ball=mood.engine;assert.equal(mood.ballDiameter,{diameter});assert.equal(mood.sizeVariant,'{name}');")
        # The old full-size hit area must no longer trigger spin or gaze.
        tests=replace_once(tests,"assert.equal(spins,0,'controls/outside taps must not spin the ball');",
            "assert.equal(spins,0,'controls/outside taps must not spin the ball');touch('down',0.65,0.43);touch('up',0.65,0.43);assert.equal(spins,0,'old large-ball area must not react');")
        (folder/'test-app.cjs').write_text(tests,encoding='utf-8')
        variants.append({'name':name,'diameter_px':diameter,'app_sha256':hashlib.sha256(source.encode('utf-8')).hexdigest()})
    polygon=root/'output/grok-lossless/candidate-polygon.inc'
    (out/'polygon.inc').write_bytes(polygon.read_bytes())
    paths=[repo/'ouo/app.js',repo/'ouo/vendor/grok-ball.js',repo/'app/espdl-quickapp/overlay/apps/system/desktop/qpk_polygon.inc']
    metadata={'variants':variants,'basis':'isolated strict-lossless cache candidate; full effects; 24 px gradient spans; 50 ms ticks',
        'production_applied':False,'flashed':False,'production_sha256':{str(p.relative_to(root)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}}
    (out/'SOURCES.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
    print('GROK_SIZE_CANDIDATES_OK',variants)
if __name__=='__main__':main()
