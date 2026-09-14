"""Read the application flash region in retryable chunks; never write flash."""

from pathlib import Path
import hashlib
import time

import esptool
from esptool.cmds import attach_flash, read_flash


root = Path(__file__).resolve().parent
destination = root / 'current-app-region.bin'
partial = root / 'current-app-region.bin.partial'
if destination.exists():
    raise SystemExit('Backup already exists; refusing to overwrite it')

esp = None
def connect(use_stub=True):
    device = esptool.detect_chip('COM23', baud=115200)
    if use_stub:
        device = esptool.cmds.run_stub(device)
    attach_flash(device)
    return device

try:
    existing = partial.stat().st_size if partial.exists() else 0
    if existing % 0x1000 or existing > 0x3fe000:
        raise ValueError('Invalid partial backup length')
    with partial.open('ab') as output:
        address = 0x2000 + existing
        while address < 0x400000:
            size = min(0x1000, 0x400000 - address)
            for attempt in range(3):
                try:
                    if esp is None:
                        esp = connect(use_stub=attempt == 0)
                    data = read_flash(esp, address, size, flash_size='16MB', no_progress=True)
                    if len(data) != size:
                        raise IOError('Short flash read')
                    break
                except Exception:
                    if esp is not None:
                        esp._port.close()
                        esp = None
                    if attempt == 2:
                        raise
                    time.sleep(0.2)
            output.write(data)
            output.flush()
            address += size
            if address % 0x10000 == 0 or address == 0x400000:
                print(f'Backed up through 0x{address:08x}', flush=True)
    partial.rename(destination)
    print(f'SHA256 {hashlib.sha256(destination.read_bytes()).hexdigest()}', flush=True)
finally:
    if esp is not None:
        esp.hard_reset()
        esp._port.close()
