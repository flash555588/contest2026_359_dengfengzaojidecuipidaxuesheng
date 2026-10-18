#!/usr/bin/env python3
"""Build a verified JIZURA SD tree with offline fonts and all 27 style presets."""
from pathlib import Path,PurePosixPath
import argparse,hashlib,json,re,shutil,subprocess,urllib.request,urllib.parse
from concurrent.futures import ThreadPoolExecutor

REPO=Path(__file__).resolve().parents[1];ROOT=REPO/'app/jizura';LOCK=ROOT/'fonts.lock.json'
FONTS=[('notosanssc','NotoSansSC[wght].ttf','Noto Sans SC','100 900'),
 ('notoserifsc','NotoSerifSC[wght].ttf','Noto Serif SC','200 900'),
 ('notosansjp','NotoSansJP[wght].ttf','Noto Sans JP','100 900'),
 ('notoserifjp','NotoSerifJP[wght].ttf','Noto Serif JP','200 900'),
 ('zcoolqingkehuangyou','ZCOOLQingKeHuangYou-Regular.ttf','ZCOOL QingKe HuangYou','400'),
 ('zcoolkuaile','ZCOOLKuaiLe-Regular.ttf','ZCOOL KuaiLe','400'),
 ('zcoolxiaowei','ZCOOLXiaoWei-Regular.ttf','ZCOOL XiaoWei','400'),
 ('mashanzheng','MaShanZheng-Regular.ttf','Ma Shan Zheng','400'),
 ('ibmplexmono','IBMPlexMono-Medium.ttf','IBM Plex Mono','500'),
 ('ibmplexmono','IBMPlexMono-SemiBold.ttf','IBM Plex Mono','600')]
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for part in iter(lambda:f.read(1024*1024),b''):h.update(part)
    return h.hexdigest()
def fetch(url):
    request=urllib.request.Request(url,headers={'User-Agent':'JIZURA-SD-builder'})
    with urllib.request.urlopen(request,timeout=90) as response:return response.read()
def fonts(cache,update=False):
    cache.mkdir(parents=True,exist_ok=True)
    if update:
        commit=json.loads(fetch('https://api.github.com/repos/google/fonts/commits/main'))['sha']
        if not re.fullmatch('[0-9a-f]{40}',commit):raise ValueError('Invalid Google Fonts commit')
        records={}
        for folder,name,family,weight in FONTS:
            for filename in [name,'OFL.txt']:
                relative=folder+'/'+filename
                records[relative]={'url':'https://raw.githubusercontent.com/google/fonts/'+commit+'/ofl/'+urllib.parse.quote(relative),
                                   'family':family,'weight':weight,'font':filename!='OFL.txt'}
        lock={'source':'https://github.com/google/fonts','commit':commit,'files':records}
    else:lock=json.loads(LOCK.read_text(encoding='utf-8'))
    def one(item):
        relative,record=item;target=cache/relative
        if not target.exists() or update:
            data=fetch(record['url']);target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
        size=target.stat().st_size;sha=digest(target)
        if not update and (size!=record['bytes'] or sha!=record['sha256']):raise ValueError('Font cache corrupt: '+relative)
        if record['font'] and target.read_bytes()[:4] not in [b'\x00\x01\x00\x00',b'OTTO']:raise ValueError('Not an SFNT font: '+relative)
        if not record['font'] and 'SIL OPEN FONT LICENSE' not in target.read_text(encoding='utf-8'):raise ValueError('Missing OFL license')
        record.update(bytes=size,sha256=sha);print('FONT_READY',relative,size,flush=True)
    with ThreadPoolExecutor(max_workers=3) as pool:list(pool.map(one,lock['files'].items()))
    if update:LOCK.write_text(json.dumps(lock,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return lock
def verify(root):
    root=root.resolve();manifest=json.loads((root/'manifest.json').read_text(encoding='utf-8'))
    for name,record in manifest['files'].items():
        p=PurePosixPath(name)
        if p.is_absolute() or '..' in p.parts or '\\' in name:raise ValueError('Invalid asset path')
        file=(root/name).resolve()
        if not file.is_relative_to(root) or not file.is_file() or file.stat().st_size!=record['bytes'] or digest(file)!=record['sha256']:raise ValueError('Invalid asset '+name)
    print('JIZURA_SD_VERIFIED',len(manifest['files']),sum(v['bytes'] for v in manifest['files'].values()),flush=True)
    return manifest
def build(source,cache,output):
    lock=json.loads(LOCK.read_text(encoding='utf-8'));output=output.resolve();root=output/'sdcard/jizura'
    if root.exists() and any(root.iterdir()):raise ValueError('Use a fresh SD package directory')
    root.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((source/'optimization-build.json').read_text(encoding='utf-8'))
    if digest(source/'optimized-engine.js')!=manifest['optimized_engine_sha256']:raise ValueError('Engine changed since validation build')
    engine=(source/'optimized-engine.js').read_text(encoding='utf-8')
    anchor='function attachFamily(spec) {'
    if engine.count(anchor)!=1:raise ValueError('Pinned font loader changed')
    engine=engine.replace(anchor,anchor+'\n  if (globalThis.JIZURA_OFFLINE_FONTS) return Promise.resolve();',1)
    (root/'engine.js').write_text(engine,encoding='utf-8')
    (root/'vendor').mkdir();shutil.copyfile(source/'vendor/mp4-muxer.min.js',root/'vendor/mp4-muxer.min.js')
    for filename in ['LICENSE','THIRD_PARTY_NOTICES.md']:shutil.copyfile(ROOT/'upstream'/filename,root/filename)
    css=[]
    for relative,record in lock['files'].items():
        original=cache/relative
        if not original.is_file() or original.stat().st_size!=record['bytes'] or digest(original)!=record['sha256']:raise ValueError('Font cache not verified: '+relative)
        dest=root/'fonts'/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(original,dest)
        if record['font']:css.append('@font-face{font-family:"'+record['family']+'";font-style:normal;font-weight:'+record['weight']+';font-display:block;src:url("fonts/'+relative+'")}')
    (root/'fonts.css').write_text('\n'.join(css)+'\n',encoding='utf-8')
    catalog=json.loads(subprocess.check_output(['node',str(ROOT/'tests/sd-catalog.cjs')],text=True,encoding='utf-8'))
    (root/'styles.json').write_text(json.dumps(catalog,ensure_ascii=False,separators=(',',':'))+'\n',encoding='utf-8')
    for name in ['sd-player.html','sd-player.js']:shutil.copyfile(ROOT/'port'/name,root/('index.html' if name.endswith('.html') else 'player.js'))
    (root/'projects').mkdir();(root/'textures').mkdir()
    (root/'textures/README.txt').write_text('Paper/grain/scan textures are generated deterministically by the original renderer. No external image download is required.\n',encoding='utf-8')
    project={'title':'慢下来的光','artist':'原创演示','lang':'zh-Hans','lyrics':'[00:00.00]把喧嚣留在身后\n[00:02.00]让今天闪闪发光\n[00:04.00]光穿过城市的窗\n[00:06.00]每一笔都有回响'}
    (root/'projects/demo.json').write_text(json.dumps(project,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    files={p.relative_to(root).as_posix():{'bytes':p.stat().st_size,'sha256':digest(p)} for p in sorted(root.rglob('*')) if p.is_file()}
    report={'version':1,'upstream_commit':'fc16bfe43ea4a6c25a21f1caf04d17326de14f00','optimized_build':manifest,'font_commit':lock['commit'],
            'styles':27,'offline_languages':['zh-Hans'],'target':'/sdcard/jizura','native_renderer_complete':False,'files':files}
    (root/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');verify(root)
    print('JIZURA_SD_READY',root,flush=True)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('command',choices=['fonts','build','verify']);p.add_argument('--cache',type=Path);p.add_argument('--update-lock',action='store_true');p.add_argument('--build',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.command=='fonts':fonts(a.output,a.update_lock)
    elif a.command=='verify':verify(a.output)
    else:build(a.build.resolve(),a.cache.resolve(),a.output)
