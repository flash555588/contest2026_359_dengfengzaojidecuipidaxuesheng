"""Wrap the approved interactive fragment without changing its layout or data."""
import argparse
from pathlib import Path

repo=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
fragment=(repo/'app/homeassistant/preview/unified-device-cards.html').read_text(encoding='utf-8')
prefix='''<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>米家 HA · 可操作设计预览</title>
<style>:root{color-scheme:dark}body{margin:0;max-width:1024px}.preview-note{font:14px system-ui;padding:14px;color:#a6b0b7}</style>
<script>window.openai={widgetState:{privateContent:{device:"bedside",view:"detail",page:0,values:{}}}};</script>
<script src="https://unpkg.com/lucide@1.17.0/dist/umd/lucide.js"></script>
'''
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(prefix+fragment+'<footer class="preview-note">可点按、可拖动的原设计预览 · 模拟数据 · 不连接真实设备</footer></html>',encoding='utf-8',newline='\n')
print('INTERACTIVE_DESIGN_PREVIEW_OK',args.output.resolve())
