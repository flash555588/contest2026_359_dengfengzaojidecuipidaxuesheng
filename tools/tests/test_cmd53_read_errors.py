"""Compile the real CMD53 helper with deterministic driver failures."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('nuttx', type=Path)
    args = parser.parse_args()
    patch = Path(__file__).resolve().parents[1] / 'patches/c6/openvela-cmd53-read-errors.patch'
    original = (args.nuttx / 'drivers/mmcsd/sdio.c').read_text()
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        target = root / 'drivers/mmcsd/sdio.c'
        target.parent.mkdir(parents=True)
        target.write_text(original)
        subprocess.run(['git', 'init', '-q', str(root)], check=True)
        command = ['git', '-C', str(root), 'apply']
        forward = subprocess.run(command + ['--check', str(patch)], capture_output=True)
        if forward.returncode == 0:
            subprocess.run(command + [str(patch)], check=True)
        else:
            # Already-fixed input is valid only if the exact patch reverses.
            subprocess.run(command + ['--reverse', '--check', str(patch)], check=True)
        source = target.read_text()
        types = source[source.index('begin_packed_struct struct sdio_cmd52'):source.index('/*', source.index('union sdio_cmd5x'))]
        start = source.index('int sdio_io_rw_extended(')
        end = source.index('\nint sdio_set_wide_bus', start)
        function = source[start:end]
        harness = r'''
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#define FAR
#define begin_packed_struct
#define end_packed_struct __attribute__((packed))
#define OK 0
#define wlinfo(...) ((void)0)
#define wlerr(...) ((void)0)
#define SDIOWAIT_TRANSFERDONE 1
#define SDIOWAIT_TIMEOUT 2
#define SDIOWAIT_ERROR 4
#define SDIO_CMD53_TIMEOUT_MS 1000
#define SDIO_CAPS_DMABEFOREWRITE 1
#define SD_ACMD53RD 53
#define SD_ACMD53WR 53
#define SD_ACMD52ABRT 52
typedef unsigned sdio_eventset_t;
struct sdio_dev_s { int unused; };
static int setup_result, send_result, cancels, unlocks, sends, waits, responses;
static int sdio_takelock(struct sdio_dev_s *d) { (void)d; return 0; }
static void sdio_givelock(struct sdio_dev_s *d) { (void)d; unlocks++; }
static int response(uint32_t *data) { responses++; *data = 0; return 0; }
#define SDIO_BLOCKSETUP(...) ((void)0)
#define SDIO_WAITENABLE(...) ((void)0)
#define SDIO_CAPABILITIES(...) 1
#define SDIO_DMARECVSETUP(...) (setup_result)
#define SDIO_DMASENDSETUP(...) (setup_result)
#define SDIO_SENDCMD(...) (sends++, send_result)
#define SDIO_EVENTWAIT(...) (waits++, SDIOWAIT_TRANSFERDONE)
#define SDIO_RECVR5(d,c,p) response(p)
#define SDIO_RECVR1(d,c,p) response(p)
#define SDIO_CANCEL(...) ((void)cancels++)
#define sdio_sendcmdpoll(...) (sends++, 0)
'''
        harness += types + function + r'''
int main(void) {
 struct sdio_dev_s dev; uint8_t buf[28];
 setup_result = -ENOSYS;
 assert(sdio_io_rw_extended(&dev, false, 1, 0x1f7e6, true, buf, 28, 0) == -ENOSYS);
 assert(cancels == 1 && unlocks == 1 && sends == 0 && waits == 0 && responses == 0);
 setup_result = 0; send_result = -EIO;
 assert(sdio_io_rw_extended(&dev, false, 1, 0x1f7e6, true, buf, 28, 0) == -EIO);
 assert(cancels == 2 && unlocks == 2 && sends == 1 && waits == 0 && responses == 0);
 send_result = 0;
 assert(sdio_io_rw_extended(&dev, false, 1, 0x1f7e6, true, buf, 28, 0) == 0);
 assert(cancels == 2 && unlocks == 3 && waits == 1 && responses == 2);
 return 0;
}
'''
        (root / 'test.c').write_text(harness)
        subprocess.run(['cc', '-std=c11', '-Werror=implicit-function-declaration',
                        str(root / 'test.c'), '-o', str(root / 'test')], check=True)
        subprocess.run([str(root / 'test')], check=True)
    print('PASS: real CMD53 read setup/send failure propagation, cleanup and success')


if __name__ == '__main__':
    main()
