#!/usr/bin/env python3
"""Deploy a verified P4 v3 ESP-TTS image, smoke-test audio, roll back on failure.

Requires a saved application-region backup and successful verify_esp_tts.py
report for this exact image. SD voice deployment is a separate preceding step.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time
from datetime import datetime,timezone
from serial.tools import list_ports
from music_device_console import Console


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware',type=Path,required=True)
    parser.add_argument('--verification',type=Path,required=True)
    parser.add_argument('--backup',type=Path,required=True)
    parser.add_argument('--expected-serial',required=True)
    parser.add_argument('--port',default='COM4')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--check-listen',action='store_true',help='Verify configured cloud ASR can acquire the microphone after TTS')
    parser.add_argument('--sync-clock',action='store_true',help='Set board UTC from this host before testing TLS/ASR; record the clock source')
    args=parser.parse_args();folder=args.firmware.resolve();image=folder/'nuttx.bin'
    metadata=json.loads((folder/'BUILD-METADATA.json').read_text())
    proof=json.loads(args.verification.read_text())
    backup=json.loads(args.backup.with_name('rollback.json').read_text())
    if not proof.get('passed') or proof.get('firmware',{}).get('sha256')!=sha(image):raise ValueError('Successful verification of this exact image is required')
    if metadata.get('tts_backend')!='esp-tts' or sha(image)!=metadata['sha256']['nuttx.bin']:raise ValueError('Invalid ESP-TTS image')
    limit=int(metadata['data_offset'],0)
    if image.stat().st_size+0x2000>limit:raise ValueError('Firmware overlaps board data')
    if sha(args.backup)!=backup['sha256'] or args.backup.stat().st_size!=limit-0x2000:raise ValueError('Invalid application-region rollback backup')
    ports=[p for p in list_ports.comports() if p.device==args.port]
    if len(ports)!=1 or (ports[0].vid,ports[0].pid)!=(0x303a,0x1001) or (ports[0].serial_number or '').lower()!=args.expected_serial.lower():
        raise ValueError('Attached board identity differs')
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    report={'passed':False,'board_written':False,'restored':False,'audio_quality_passed':None,
            'candidate_sha256':sha(image),'backup_sha256':backup['sha256'],'port':args.port,'runs':[]}
    def save():(out/'deployment.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    def flash(path,label):
        with (out/(label+'-flash.log')).open('w') as log:
            subprocess.run([sys.executable,'-m','esptool','--chip','esp32p4','--port',args.port,'--baud','460800',
                '--before','usb-reset','--after','watchdog-reset','write-flash','0x2000',str(path)],
                stdout=log,stderr=subprocess.STDOUT,check=True,timeout=240)
        if 'Hash of data verified' not in (out/(label+'-flash.log')).read_text():raise RuntimeError('Flash write checksum confirmation missing')
    class TranscriptConsole(Console):
        transcript=''
        def collect(self,seconds):
            text=super().collect(seconds);self.transcript+=text;return text
    def connect(log):
        deadline=time.monotonic()+30
        while True:
            try:return TranscriptConsole(log,port=args.port)
            except OSError:
                if time.monotonic()>=deadline:raise
                time.sleep(.3)
    def idle(console):
        deadline=time.monotonic()+8
        while time.monotonic()<deadline:
            text=console.command('desktop speech status')
            state=re.search(r'speech active=(\d+) phase=(\d+) error=(-?\d+)',text)
            if state and state[1]=='0':
                if int(state[3]):raise RuntimeError('Speech worker error: '+state[3])
                return
        raise RuntimeError('Speech worker did not release audio')
    save();attempted=False
    try:
        with (out/'preflight.log').open('w',encoding='utf-8') as log:
            c=connect(log)
            try:
                filesystem=c.command('df')
                rows=re.findall(r'^\s*(\d+)\s+(\d+)\s+\d+\s+\d+\s+/data\s*$',filesystem,re.M)
                if len(rows)!=1 or int(rows[0][0])*int(rows[0][1])!=int(metadata['data_size'],0):raise RuntimeError('Current data partition differs from candidate')
                c.command('desktop ui voice-end');c.command('desktop speech stop')
            finally:c.close()
        attempted=True;flash(image,'candidate');report['board_written']=True;save()
        with (out/'device.log').open('w',encoding='utf-8') as log:
            c=connect(log)
            try:
                c.collect(8);c.command('desktop ui home');idle(c)
                if args.sync_clock:
                    stamp=datetime.now(timezone.utc).strftime('%b %d %H:%M:%S %Y')
                    c.command('date -u -s "'+stamp+'"');report['clock_source']='host UTC during deployment validation';save()
                if 'backend=esp-tts' not in c.command('desktop speech status'):raise RuntimeError('Unexpected live speech backend')
                c.command('desktop speech speed 3')
                if 'volume=75 range=0..100' not in c.command('desktop speech volume'):raise RuntimeError('Unexpected speech volume default')
                for text in ['你好，语音测试。','如果明天晴天我们就去公园散步。','晚饭后，我们沿着河边散步。']:
                    mark=len(c.transcript);c.command('desktop speech say "'+text+'"');deadline=time.monotonic()+30
                    while time.monotonic()<deadline:
                        segment=c.transcript[mark:]
                        done=re.search(r'tts done result=(-?\d+) elapsed_ms=(\d+) played_frames=(\d+)',segment)
                        if done:break
                        c.collect(.15)
                    else:raise RuntimeError('TTS did not finish')
                    timing=re.search(r'playback frames=(\d+) expected_ms=(\d+) elapsed_ms=(\d+) result=0',segment)
                    if int(done[1]) or not int(done[3]) or not timing or 'backend=esp-tts rate=16000' not in segment:raise RuntimeError('TTS synthesis/playback failed')
                    if re.search(r'I2S transfer failed|Failed to allocate.*DMA',segment):raise RuntimeError('Audio DMA failure')
                    expected,actual=int(timing[2]),int(timing[3])
                    if not expected*.9-100<=actual<=expected*1.2+200:raise RuntimeError('Playback duration differs from PCM duration')
                    report['runs'].append({'frames':int(done[3]),'expected_ms':expected,'actual_ms':actual,'elapsed_ms':int(done[2])});save();idle(c)
                    print('ESP_TTS_BOARD_PLAY_OK',len(report['runs']),flush=True)
                mark=len(c.transcript);c.command('desktop speech say "如果明天晴天我们就去公园散步。"');c.collect(.2)
                c.command('desktop speech stop');idle(c)
                segment=c.transcript[mark:]
                done=re.search(r'tts done result=(-?\d+) elapsed_ms=(\d+) played_frames=(\d+)',segment)
                if not done or int(done[3])>=report['runs'][1]['frames']:raise RuntimeError('Cancellation did not interrupt playback')
                report['cancelled_frames']=int(done[3]);report['cancel_passed']=True;save()
                if args.check_listen:
                    mark=len(c.transcript)
                    c.command('desktop ui voice');c.command('desktop ui voice-tap');deadline=time.monotonic()+60
                    stable_since=None
                    while time.monotonic()<deadline:
                        state=c.command('desktop ui voice-status')
                        if re.search(r'I2S transfer failed|Failed to allocate.*DMA',c.transcript[mark:]):
                            raise RuntimeError('Capture DMA failed after TTS')
                        if re.search(r'VOICE_CAPTURE active=1 phase=2 error=0',state):
                            if stable_since is None:stable_since=time.monotonic()
                            if time.monotonic()-stable_since>=2:break
                        else:stable_since=None
                        if re.search(r'VOICE_STATUS page=0',state):
                            c.command('desktop ui voice');c.command('desktop ui voice-tap')
                        if re.search(r'VOICE_STATUS page=1 session=0',state):c.command('desktop ui voice-tap')
                        c.collect(.3)
                    else:raise RuntimeError('Microphone/ASR handoff failed')
                    report['listening_passed']=True
                    c.command('desktop ui voice-end');c.command('desktop speech stop');idle(c)
                c.command('desktop ui chat')
                report['passed']=True;save()
            finally:c.close()
    except Exception as error:
        report['error']=repr(error);save();raise
    finally:
        if attempted and not report['passed']:
            flash(args.backup.resolve(),'restore');report['restored']=True;save()
    print('ESP_TTS_BOARD_VERIFIED',out/'deployment.json',flush=True)


if __name__=='__main__':main()
