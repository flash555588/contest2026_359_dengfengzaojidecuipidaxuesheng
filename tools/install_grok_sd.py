#!/usr/bin/env python3
"""Install predecoded banks by existing NSH wget; stage then rename on SD."""
import argparse,json,re,time
from pathlib import Path
import serial

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port',default='COM4');p.add_argument('--host',required=True);p.add_argument('--root',type=Path,required=True);p.add_argument('--log',type=Path,required=True)
    a=p.parse_args();manifest=json.loads((a.root/'manifest.json').read_text())
    ser=serial.Serial(port=None,baudrate=115200,timeout=.1);ser.dtr=ser.rts=False;ser.port=a.port;ser.open()
    with a.log.open('w',encoding='utf-8') as log:
        def command(cmd,limit=20,allow_missing=False):
            # CRLF produces two NSH prompts. Consume the idle tail before the
            # next command, and require its echo before accepting completion.
            time.sleep(.3);ser.reset_input_buffer();ser.write((cmd+'\r').encode());ser.flush();began=time.monotonic();buf=bytearray();last=began
            while time.monotonic()-began<limit:
                data=ser.read(ser.in_waiting or 1)
                if data:
                    buf.extend(data);log.write(data.decode('utf-8',errors='replace'));log.flush()
                    if b'nsh>' in buf[-64:] and (not cmd or cmd.encode() in buf):break
                if time.monotonic()-last>20:print(cmd.split('/')[-1],round(time.monotonic()-began),'seconds',flush=True);last=time.monotonic()
            else:raise RuntimeError('NSH operation still running; do not rename incomplete file: '+cmd)
            text=buf.decode('utf-8',errors='replace')
            if not allow_missing and re.search(r'(failed|ERROR|not found|Connection refused|No route)',text,re.I):raise RuntimeError(text[-1000:])
            return text
        command('');command('mkdir /sdcard/grok',allow_missing=True)
        for bank in manifest['banks']:
            name=bank['file'];started=time.monotonic();print('INSTALL',name,bank['bytes'],flush=True)
            text=command('ls -l /sdcard/grok/'+name,allow_missing=True)
            if re.search(r'\b'+str(bank['bytes'])+r'\b',text):print('EXISTS expected size',name,flush=True);continue
            command('wget -o /sdcard/grok/'+name+'.tmp http://'+a.host+'/'+name,1800)
            listing=command('ls -l /sdcard/grok/'+name+'.tmp')
            if not re.search(r'\b'+str(bank['bytes'])+r'\b',listing):raise RuntimeError('Downloaded length mismatch: '+listing)
            command('mv /sdcard/grok/'+name+'.tmp /sdcard/grok/'+name)
            print('INSTALLED',name,round(time.monotonic()-started,1),'s',flush=True)
        command('wget -o /sdcard/grok/manifest.json http://'+a.host+'/manifest.json',20)
    ser.close();print('GROK_SD_INSTALL_OK: lengths checked; device digest verification still required',flush=True)
if __name__=='__main__':main()
