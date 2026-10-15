#!/usr/bin/env python3
"""Compile the reviewed public quick-card catalog into presentation rules."""
from pathlib import Path
import argparse,json,re,hashlib

def main():
    p=argparse.ArgumentParser();p.add_argument('references',type=Path);a=p.parse_args()
    repo=Path(__file__).resolve().parents[1];rows=json.loads(a.references.read_text(encoding='utf-8'))
    unique={};rules={};catalog=[]
    for row in rows:
        digest=hashlib.sha256((a.references.parent/row['file']).read_bytes()).hexdigest()
        layout=unique.setdefault(digest,len(unique)+1)
        requirements=re.findall(r'服务:([^\s]+) (属性|动作):([^\n\t]+)',row['properties'])
        requirements=[s+'.'+n.strip() for s,kind,n in requirements]
        from PIL import Image
        with Image.open(a.references.parent/row['file']) as image:w,h=image.size
        rule=[row['variant'],layout,w,h,requirements]
        rules.setdefault(row['type'],[]).append(rule)
        catalog.append({'type':row['type'],'name':row['properties'].split('\t')[1],'variant':row['variant'],'layout':layout,'size':[w,h],'requires':requirements,'reference':row['source'],'file':row['file']})
    result={'reference':'https://iot.mi.com/v2/new/doc/resources-and-services/design/feature-define/quick-card','types':list(rules),'layouts':len(unique),'variants':catalog}
    (repo/'app/homeassistant/quick-card-catalog.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    source=repo/'app/espdl-quickapp/overlay/apps/system/desktop/homeassistant/app.js';text=source.read_text(encoding='utf-8')
    block='  /* MI_QUICK_RULES_BEGIN */\n  const quickReferenceRules = '+json.dumps(rules,ensure_ascii=False,separators=(',',':'))+';\n  /* MI_QUICK_RULES_END */'
    if '/* MI_QUICK_RULES_BEGIN */' in text:
        text=re.sub(r'  /\* MI_QUICK_RULES_BEGIN \*/.*?  /\* MI_QUICK_RULES_END \*/',lambda m:block,text,flags=re.S)
    else:text=text.replace('  const thumbnailCodes =',block+'\n  const thumbnailCodes =')
    source.write_text(text,encoding='utf-8');print('MI_QUICK_RULES_OK',len(rules),len(catalog),len(unique))
if __name__=='__main__':main()
