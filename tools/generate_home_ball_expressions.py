#!/usr/bin/env python3
"""Export the user-selected A layout with common states from the pinned engine."""
from pathlib import Path
import argparse,json,subprocess

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--node',required=True);a=p.parse_args()
    root=Path(__file__).resolve().parents[2];out=root/'output/home-ball-expressions';out.mkdir(exist_ok=True)
    config=json.loads((root/'output/home-ball-options/a/config.json').read_text(encoding='utf-8'))
    config['selected_by_user']=True
    config['separateWideEyes']=True
    config['enterEffects']=True
    config['scenes']=[{'emotion':emotion,'key':key,**({'warmupTicks':2} if emotion in ['01','33','34'] else {})} for emotion,key in [
        ('02','idle'),('00','sleep'),('01','wake'),('03','curious'),('10','happy'),('11','puzzled'),('13','surprised'),('30','thinking'),
        ('35','listening'),('32','busy'),('33','done'),('34','error'),('39','replying'),('40','searching'),('36','loading'),('41','stopped')]]
    (out/'config.json').write_text(json.dumps(config,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    subprocess.run([a.node,str(root/'project/tools/export_home_ball.cjs'),str(root/'output/grok-styles/mosaic/app.js'),str(out/'scenes.json'),str(out/'config.json')],check=True)
    print('HOME_A_COMMON_EXPRESSIONS_READY: 16 engine states; selected A 96 px; no firmware changes')

if __name__=='__main__':main()
