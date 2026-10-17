#!/usr/bin/env python3
"""Deploy verified speech assets to an attached board over its LAN.

No training, formatting, credentials or firmware flashing. Serve only locked
model files to the named board, stage on SD, then publish by directory rename.
Requires pyserial on the host and wget in the board's NSH console.
"""
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import ipaddress
import json
from pathlib import Path
import re
import secrets
import shutil
import threading
import time

import serial
from serial.tools import list_ports


def prepare_tts_transfer(prepared, destination):
    """Select only runtime files and notices, checking the compiled asset table.

    Keep this separate from serial access so integrity failures can be tested
    without touching a board. The device independently verifies runtime SHA256.
    """
    source=(prepared.resolve()/'sdcard/speech').resolve()
    manifest=json.loads((source/'manifest.json').read_text(encoding='utf-8'))
    header=(prepared/'vendor/speech_assets.h').read_text(encoding='utf-8')
    expected={}
    for name,size,digest in re.findall(r'\{"(sanotts-zh/[^"\n]+)",(\d+),\{([^}]+)\}\}',header):
        tokens=re.findall(r'0x([0-9a-fA-F]{2})',digest)
        if len(tokens)!=32 or name in expected:raise ValueError('Invalid compiled TTS asset table')
        expected[name]={'bytes':int(size),'sha256':''.join(tokens).lower()}
    names=['front.bin','dec.bin','g2p.js','lexicon.bin','phonemes.json']
    mapping={name:'sanotts-zh/'+name for name in names}
    mapping.update({name:name for name in ['licenses/sanoTTS-MIT.txt','licenses/sanoTTS-GPL3.txt']})
    verified={}
    for target,relative in mapping.items():
        path=source/relative;record=manifest['files'].get(relative)
        if not record or not path.resolve().is_relative_to(source):raise ValueError('Missing or escaping TTS asset: '+relative)
        if target in names and record!=expected.get(relative):raise ValueError('TTS manifest differs from compiled asset table: '+relative)
        if not isinstance(record.get('bytes'),int) or not 0<record['bytes']<=4*1024*1024 or not re.fullmatch('[0-9a-f]{64}',record.get('sha256','')):
            raise ValueError('Invalid TTS asset record: '+relative)
        if path.stat().st_size!=record['bytes'] or hashlib.sha256(path.read_bytes()).hexdigest()!=record['sha256']:
            raise ValueError('Corrupt TTS asset: '+relative)
        verified[target]=record
    destination.mkdir(parents=True,exist_ok=True)
    for target,relative in mapping.items():
        path=destination/target;path.parent.mkdir(parents=True,exist_ok=True)
        if path.exists():
            if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=verified[target]['sha256']:
                raise ValueError('Existing transfer file differs; choose a fresh output directory')
        else:
            with path.open('xb') as stream, (source/relative).open('rb') as original:shutil.copyfileobj(original,stream)
    return destination.resolve(),{'files':verified},list(mapping)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prepared',type=Path)
    p.add_argument('--tts-prepared',type=Path,help='Prepared Chinese TTS runtime and its verified manifest')
    p.add_argument('--custom-model',type=Path)
    p.add_argument('--command-grammar',type=Path,help='Pinned bootstrap grammar, commands_cn.txt')
    p.add_argument('--replace-existing',action='store_true',help='Preserve an existing custom model as a rollback directory')
    p.add_argument('--fixtures',type=Path,help='Generated PCM diagnostics only; never microphone recordings')
    p.add_argument('--expected-sha256')
    p.add_argument('--host',required=True)
    p.add_argument('--board-address',required=True)
    p.add_argument('--port',default='COM4')
    p.add_argument('--http-port',type=int,default=8765)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    for address in (a.host,a.board_address):
        ip=ipaddress.ip_address(address)
        if ip.version!=4 or not ip.is_private or ip.is_loopback:raise ValueError('Use private LAN IPv4 addresses')
    ports=[port for port in list_ports.comports() if port.device==a.port]
    if len(ports)!=1 or (ports[0].vid,ports[0].pid)!=(0x303a,0x1001):raise ValueError('Expected the attached ESP USB Serial/JTAG board')
    custom=a.custom_model is not None
    grammar=a.command_grammar is not None
    fixtures=a.fixtures is not None
    tts=a.tts_prepared is not None
    if sum([custom,grammar,fixtures,tts,a.prepared is not None])!=1:raise ValueError('Choose exactly one asset source')
    if a.replace_existing and not custom:raise ValueError('Replacement is restricted to the custom wake model')
    if tts:
        root,manifest,names=prepare_tts_transfer(a.tts_prepared,a.output/'tts-transfer')
    elif grammar:
        canonical=Path(__file__).resolve().parents[1]/'app/espdl-quickapp/speech/commands_cn.txt'
        root=a.command_grammar.resolve().parent;names=['commands_cn.txt']
        if a.command_grammar.name!=names[0] or a.command_grammar.read_bytes()!=canonical.read_bytes():
            raise ValueError('Grammar must exactly match the pinned application bootstrap file')
        manifest={'files':{names[0]:{'bytes':canonical.stat().st_size,'sha256':hashlib.sha256(canonical.read_bytes()).hexdigest()}}}
    elif fixtures:
        root=a.fixtures.resolve();names=['wake-positive.pcm','wake-negative.pcm']
        manifest={'files':{name:{'bytes':(root/name).stat().st_size,'sha256':hashlib.sha256((root/name).read_bytes()).hexdigest()} for name in names}}
        if any(record['bytes']>960000 for record in manifest['files'].values()):raise ValueError('Diagnostic PCM exceeds the board limit')
    elif custom:
        root=a.custom_model.resolve().parent
        if a.custom_model.name!='nihao_openvela.gkw' or not a.expected_sha256 or not re.fullmatch('[0-9a-f]{64}',a.expected_sha256):
            raise ValueError('Custom model requires its trained SHA256 and canonical filename')
        manifest={'files':{a.custom_model.name:{'bytes':a.custom_model.stat().st_size,'sha256':a.expected_sha256}}}
        names=[a.custom_model.name]
    else:
        if not a.prepared:raise ValueError('--prepared or --custom-model is required')
        root=a.prepared.resolve()/'sdcard/speech'
        manifest=json.loads((root/'manifest.json').read_text())
        names=sorted(name for name in manifest['files'] if name.startswith('esp-sr/') or name=='licenses/ESP-SR.txt')
    if not names:raise ValueError('No ESP-SR assets')
    records=[]
    for name in names:
        path=root/name;record=manifest['files'][name]
        if not path.resolve().is_relative_to(root) or path.stat().st_size!=record['bytes'] or hashlib.sha256(path.read_bytes()).hexdigest()!=record['sha256']:
            raise ValueError('Invalid asset: '+name)
        records.append({'path':name,**record})
    a.output.mkdir(parents=True,exist_ok=True)
    target='/sdcard/speech/sanotts-zh' if tts else '/sdcard/speech/esp-sr/fst' if grammar else '/sdcard/speech/test' if fixtures else '/sdcard/speech/custom' if custom else '/sdcard/speech'
    stage='/sdcard/.speech-'+secrets.token_hex(4)
    backup=None
    served=set()
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            match=re.fullmatch(r'/([0-9]+)',self.path)
            if self.client_address[0]!=a.board_address or not match or int(match[1])>=len(names):
                self.send_error(404);return
            index=int(match[1]);path=root/names[index]
            self.send_response(200);self.send_header('Content-Length',str(path.stat().st_size));self.end_headers()
            with path.open('rb') as stream:shutil.copyfileobj(stream,self.wfile)
            served.add(index)
        def log_message(self,*args):pass
    http=ThreadingHTTPServer((a.host,a.http_port),Handler)
    worker=threading.Thread(target=http.serve_forever,daemon=True);worker.start()
    console=serial.Serial(port=None,baudrate=115200,timeout=.08,write_timeout=3)
    console.dtr=False;console.rts=False;console.port=a.port
    published=False
    try:
        console.open()
        with (a.output/'deploy-console.log').open('wb') as log:
            def collect(seconds):
                data=bytearray();until=time.monotonic()+seconds
                while time.monotonic()<until:
                    chunk=console.read(console.in_waiting or 1)
                    if chunk:log.write(chunk);log.flush();data.extend(chunk)
                value=data.decode('utf-8',errors='replace')
                if re.search(r'PANIC|Assertion failed|access fault',value,re.I):raise RuntimeError('Board fault; inspect local log')
                return value
            def command(text,timeout=3):
                if len(text.encode())>=79:raise ValueError('NSH command exceeds line limit')
                console.write(b'\x15');time.sleep(.03)
                for byte in (text+'\r\n').encode():console.write(bytes([byte]));time.sleep(.008)
                data='';until=time.monotonic()+timeout
                while time.monotonic()<until:
                    data+=collect(.15)
                    if re.search(r'nsh>\s*(?:\x1b\[K)?',data):return data
                raise RuntimeError('Console timeout: '+text)
            collect(.5);command('')
            mounts=command('mount')
            if not re.search(r'/sdcard type (?:vfat|fatfs)',mounts):raise RuntimeError('A real FAT SD mount is required')
            existing=command('ls '+target)
            exists='No such file' not in existing
            if exists and not a.replace_existing:raise RuntimeError('Speech directory exists; preserve it and verify before replacement')
            def checked(text,timeout=3):
                response=command(text,timeout)
                if re.search(r'nsh:.*(?:failed|not found|invalid)',response,re.I):raise RuntimeError(response)
                return response
            if custom or tts:
                checked('desktop speech stop',5)
                status=checked('desktop speech status',3)
                if not re.search(r'speech active=0\b',status):raise RuntimeError('Stop microphone recognition before publishing a model')
            checked('mkdir '+stage);checked('cd '+stage)
            directories=sorted({str(Path(name).parent).replace('\\','/') for name in names},key=lambda n:(n.count('/'),n))
            all_dirs=set()
            for directory in directories:
                current=''
                if directory=='.':continue
                for part in directory.split('/'):
                    current=part if not current else current+'/'+part
                    if current not in all_dirs:checked('mkdir '+current);all_dirs.add(current)
            for i,name in enumerate(names):
                print('SPEECH_ASSET_TRANSFER',name,records[i]['bytes'],flush=True)
                checked(f'wget -o a{i}.part http://{a.host}:{a.http_port}/{i}',max(45,records[i]['bytes']/32768))
                listing=checked('ls -l a'+str(i)+'.part')
                if not re.search(r'\b'+str(records[i]['bytes'])+r'\b',listing):raise RuntimeError('Download size mismatch: '+name)
                checked(f'mv a{i}.part {name}')
            if served!=set(range(len(names))):raise RuntimeError('Not every asset was served successfully')
            checked('cd /')
            if exists:
                backup='/sdcard/.speech-old-'+secrets.token_hex(4)
                # Persist the recovery path before moving the working model.
                (a.output/'rollback.json').write_text(json.dumps({'target':target,'backup':backup,'staging':stage},indent=2)+'\n')
                checked('mv '+target+' '+backup)
            try:
                checked('mv '+stage+' '+target);published=True
            except Exception:
                if backup:checked('mv '+backup+' '+target)
                raise
    finally:
        console.close();http.shutdown();http.server_close();worker.join(timeout=3)
        report={'published':published,'kind':'pretrained SanoTTS Chinese runtime' if tts else 'bootstrap command grammar' if grammar else 'synthetic PCM diagnostics' if fixtures else 'custom trained wake model' if custom else 'pretrained ESP-SR; not a newly trained model','target':target,'staging':stage,'backup':backup,'models':records,'runtime_sha256_check_required':True}
        (a.output/'deployment.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('SPEECH_ASSETS_DEPLOYED',len(records),'files; verify loading on board',flush=True)


if __name__=='__main__':main()
