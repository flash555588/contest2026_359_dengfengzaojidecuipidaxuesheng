"""Run short, explicit NSH diagnostics on the development board."""
from pathlib import Path
import argparse
import json
import time
import atexit
import serial
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--commands',nargs='*',default=[])
parser.add_argument('--reset',action='store_true')
parser.add_argument('--seconds',type=float,default=3)
args=parser.parse_args()
p=serial.Serial(port=None,baudrate=115200,timeout=.1,write_timeout=3)
p.dtr=False;p.rts=False;p.port='COM23'
results=[]
def save_results():
    args.output.write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
atexit.register(save_results)
def read_for(seconds, reconnect=False):
    out=bytearray();end=time.monotonic()+seconds
    while time.monotonic()<end:
        try:
            if not p.is_open:
                p.open(); p.write(b'\r')
            out.extend(p.read(max(1,p.in_waiting)))
        except serial.SerialException:
            if not reconnect: raise
            p.close(); time.sleep(.2)
    if not p.is_open: raise RuntimeError('USB console did not reappear after reset')
    return out.decode('utf-8',errors='replace').replace('\r','')
with p:
    if args.reset:
        from esptool.reset import HardReset
        HardReset(p,uses_usb=True)()
        time.sleep(1)
        try: p.write(b'\r')
        except serial.SerialException: p.close()
        results.append({'command':'reset','output':read_for(10,reconnect=True)})
        save_results()
    for command in args.commands:
        assert len(command.encode())<=78
        entry={'command':command,'output':''};results.append(entry)
        encoded = (command+'\r').encode('utf-8')
        for i in range(0,len(encoded),8):
            try:
                p.write(encoded[i:i+8]);time.sleep(.03)
            except serial.SerialException as error:
                entry['error']=str(error)
                raise
        entry['output']=read_for(args.seconds)
        save_results()
args.output.write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
for r in results:
    print((r['command']+' '+r['output']).encode('ascii',errors='backslashreplace').decode())
