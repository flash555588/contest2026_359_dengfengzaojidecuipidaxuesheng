"""Flash the validated 16 MiB ESP-DL layout to the identified development board.

User authorized moving /data and discarding old contents. --execute is required;
without it this prints a concrete layout and validates artifact hashes only.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys
import serial.tools.list_ports
ws=Path(__file__).resolve().parent.parent
d=ws/'04-v3-20260913/espdl-quickapp'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--execute',action='store_true')
parser.add_argument('--log',default='flash-espdl.log')
parser.add_argument('--firmware-only',action='store_true',
                    help='Update program only; preserve models and formatted /data')
args=parser.parse_args()
v=json.loads((d/'evidence/firmware-validation.json').read_text())
assert not v.get('build_in_progress'),'Latest build is incomplete; refusing stale firmware'
m=json.loads((d/'models/manifest.json').read_text())
firmware=(d/'nuttx.bin').read_bytes();models=(d/'models.bin').read_bytes()
assert hashlib.sha256(firmware).hexdigest()==v['sha256']
assert hashlib.sha256(models).hexdigest()==m['bundle_sha256']
assert v['fits_program_partition'] and all(v['symbols'].values())
assert v['data_start']==0xc00000 and v['models_start']==m['flash_base']==0x800000
assert len(firmware)+0x2000<=0x800000 and len(models)<=0x400000
assert m['weight_layout']=='esp32p4-simd'
assert firmware[0]==0xe9 and firmware[12:14]==b'\x12\x00'
print(json.dumps({'firmware':{'offset':'0x2000','bytes':len(firmware)},
                  'models':{'offset':'0x800000','bytes':len(models)},
                  'data':{'offset':'0xc00000','bytes':0x400000,'action':'preserve' if args.firmware_only else 'erase; format SmartFS after boot'}},indent=2))
if not args.execute:raise SystemExit(0)
ports=[p for p in serial.tools.list_ports.comports() if p.device=='COM23']
assert len(ports)==1 and ports[0].serial_number=='E8:F6:0A:E3:A9:5F'
assert (ports[0].vid,ports[0].pid)==(0x303a,0x1001)
base=[sys.executable,'-m','esptool','--chip','esp32p4','--port','COM23','--baud','921600',
      '--before','usb-reset','--after','no-reset']
path=d/'evidence'/args.log
with path.open('x',encoding='utf-8') as log:
    commands=([base+['write-flash','0x2000',str(d/'nuttx.bin')]] if args.firmware_only else
              [base+['erase-region','0xc00000','0x400000'],
               base+['write-flash','0x2000',str(d/'nuttx.bin'),'0x800000',str(d/'models.bin')]])
    for command in commands:
        subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
print(path.read_text(encoding='utf-8',errors='replace')[-1800:])
