#!/usr/bin/env python3
"""Prepare four isolated native AI-card alternatives for user selection."""
from pathlib import Path
import argparse,json,subprocess

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--node',required=True);a=p.parse_args()
    root=Path(__file__).resolve().parents[2];out=root/'output/home-ball-options';out.mkdir(exist_ok=True)
    variants=[
        {'id':'a','name':'均衡靠左','diameter':96,'centerX':76,'occlusion':0.25,'description':'适中的存在感，左侧与头顶都有余量。'},
        {'id':'b','name':'大球靠左','diameter':108,'centerX':86,'occlusion':0.25,'description':'小球更醒目，保留四分之一遮挡。'},
        {'id':'c','name':'贴边探头','diameter':112,'centerX':72,'occlusion':1/3,'description':'更靠左，遮挡三分之一，头顶留白更明显。'},
        {'id':'d','name':'内收留白','diameter':104,'centerX':104,'occlusion':0.25,'description':'从左边缘略向内收，构图更舒展。'},
    ]
    for variant in variants:
        folder=out/variant['id'];folder.mkdir(exist_ok=True)
        config=folder/'config.json';config.write_text(json.dumps(variant,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        subprocess.run([a.node,str(root/'project/tools/export_home_ball.cjs'),str(root/'output/grok-styles/mosaic/app.js'),str(folder/'scenes.json'),str(config)],check=True)
    (out/'OPTIONS.json').write_text(json.dumps({'recommended':'b','variants':variants,'flashed':False,'production_applied':False},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('FOUR_HOME_OPTIONS_READY')
if __name__=='__main__':main()
