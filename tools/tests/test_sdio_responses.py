"""Compile actual SDMMC response functions against mocked register access."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import re


def extract(source, name):
    match = re.search(r'static\s+int\s+' + re.escape(name) + r'\([^;{}]*\)\s*\{', source)
    if match is None:
        raise ValueError('Function definition not found: ' + name)
    start = match.start()
    brace = match.end() - 1
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('nuttx', type=Path)
    args = parser.parse_args()
    source = (args.nuttx / 'arch/risc-v/src/esp32p4/esp32p4_sdmmc.c').read_text()
    # Use the target's own encodings, not guessed response identifiers.
    harness = '''
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <errno.h>
#define CONFIG_DEBUG_FEATURES 1
#define OK 0
#define mcinfo(...) ((void)0)
#define mcerr(...) ((void)0)
#define ESP32P4_SDMMC_RINTSTS 1
#define ESP32P4_SDMMC_RESP0 2
#define SDMMC_INT_RTO 1
#define SDMMC_INT_RCRC 2
#define SDCARD_RESPDONE_CLEAR 4
#define SDCARD_CMDDONE_CLEAR 8
static uint32_t status;
static uint32_t esp32p4_getreg(uint32_t reg) { return reg == 1 ? status : 0x80ff8000; }
static void esp32p4_putreg(uint32_t value, uint32_t reg) { (void)value; (void)reg; }
'''
    harness += '#include <nuttx/sdio.h>\n' + extract(source, 'esp32p4_recvshortcrc') + '\n' + extract(source, 'esp32p4_recvshort')
    harness += '''
int main(void) {
 uint32_t response = 0;
 assert(esp32p4_recvshort(NULL, MMCSD_R4_RESPONSE | 5, &response) == 0);
 assert(response == 0x80ff8000);
 assert(esp32p4_recvshortcrc(NULL, MMCSD_R5_RESPONSE | 52, &response) == 0);
 assert(esp32p4_recvshort(NULL, MMCSD_R1_RESPONSE, &response) == -EINVAL);
 assert(esp32p4_recvshortcrc(NULL, MMCSD_R4_RESPONSE, &response) == -EINVAL);
 status = SDMMC_INT_RTO;
 assert(esp32p4_recvshort(NULL, MMCSD_R4_RESPONSE, &response) == -ETIMEDOUT);
 assert(esp32p4_recvshortcrc(NULL, MMCSD_R5_RESPONSE, &response) == -ETIMEDOUT);
 status = SDMMC_INT_RCRC;
 assert(esp32p4_recvshortcrc(NULL, MMCSD_R5_RESPONSE, &response) == -EIO);
 assert(esp32p4_recvshort(NULL, MMCSD_R4_RESPONSE, &response) == 0);
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        (path / 'test.c').write_text(harness)
        subprocess.run(['cc', '-Wall', '-Werror', '-I', str(args.nuttx / 'include'),
                        str(path / 'test.c'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)
    print('PASS: actual R4/R5 response functions, invalid types, timeout and CRC checks')


if __name__ == '__main__':
    main()
