#!/usr/bin/env python3
"""Flash only the verified application slot, retain data and a known rollback."""
import argparse,hashlib,json,re,subprocess,sys,time
from pathlib import Path
from serial.tools import list_ports
from grok_board_io import Console
def main():
    p=argparse.ArgumentParser();p.add_argument('--firmware',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--native-ui-proof',type=Path);p.add_argument('--music-service-proof',type=Path);a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=True);meta=json.loads((a.firmware/'verification.json').read_text());sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
    for name in ['nuttx.bin','rollback.bin']:assert sha(a.firmware/name)==meta['sha256'][name]
    repo=Path(__file__).resolve().parents[1]
    for name,digest in meta['source_sha256'].items():assert sha(repo/'app/espdl-quickapp/overlay/apps/system/desktop'/name)==digest,name
    assert int(meta['erase_end'],16)<0xc00000 and int(meta['ebss'],16)<=0x4ffaefc0 and not meta['tts_experiments_linked']
    if meta.get('lyric_renderer')=='native-three':
        assert a.native_ui_proof and 'MUSIC_FULL_NATIVE_OK: both themes; 3 live effects' in a.native_ui_proof.read_text(encoding='utf-8')
    else:assert 'MUSIC_SD_UI_OK' in (a.output.parent/'sd-ui.log').read_text()
    assert 'MUSIC_SD_SERVICE_OK' in (a.music_service_proof or a.output.parent/'music-service.log').read_text(encoding='utf-8')
    report=dict(board_written=False,boot_verified=False,rollback=False,data_written=False,candidate_sha256=meta['sha256']['nuttx.bin'],rollback_sha256=meta['sha256']['rollback.bin'])
    def save():(a.output/'deployment.json').write_text(json.dumps(report,indent=2))
    def wait_port():
        end=time.monotonic()+30
        while time.monotonic()<end:
            ports=[x for x in list_ports.comports() if x.device=='COM4' and (x.vid,x.pid)==(0x303a,0x1001) and (x.serial_number or '').lower()=='e8:f6:0a:e7:6f:f1']
            if len(ports)==1:time.sleep(1);return
            time.sleep(.3)
        raise TimeoutError('Expected board did not reconnect')
    def flash(cmd,name):
        wait_port()
        with (a.output/(name+'.log')).open('w') as log:subprocess.run([sys.executable,'-m','esptool','--chip','esp32p4','--port','COM4','--baud','460800','--before','usb-reset','--after','watchdog-reset',*cmd],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=200)
    try:
        flash(['verify-flash','0x2000',str(a.firmware/'rollback.bin')],'before-verify');report['running_image_verified']=True;save()
        print('RUNNING_FIRMWARE_VERIFIED',flush=True)
        flash(['write-flash','0x2000',str(a.firmware/'nuttx.bin')],'flash');assert 'Hash of data verified' in (a.output/'flash.log').read_text()
        report['board_written']=True;save();print('JIZURA_FIRMWARE_WRITTEN',flush=True);wait_port()
        with (a.output/'boot.log').open('w',encoding='utf-8') as log:
            c=Console(log=log)
            try:
                time.sleep(3);boot=c.serial.read(c.serial.in_waiting).decode(errors='replace');log.write(boot)
                assert not re.search(r'PANIC|Assertion failed|access fault',boot,re.I)
                c.command('');assert 'esp32p4-function-ev-board' in c.command('uname -a')
                mounts=c.command('mount');assert '/data type smartfs' in mounts and '/sdcard type vfat' in mounts
                assert 'music-pv active=0' in c.command('desktop ui music-pv-status')
                report['heap_after_boot']=c.command('free');report['boot_verified']=True;save()
            finally:c.close()
    except Exception as error:
        report['error']=str(error);save()
        if report['board_written'] and not report['boot_verified']:
            flash(['write-flash','0x2000',str(a.firmware/'rollback.bin')],'rollback');report['rollback']=True;save()
        raise
    meta.update(board_written=True,boot_verified=True);(a.firmware/'verification.json').write_text(json.dumps(meta,indent=2));print('JIZURA_DEVICE_BOOT_OK',flush=True)
if __name__=='__main__':main()
