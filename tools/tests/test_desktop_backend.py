"""Compile the real backend with mocked radio entry points, no hardware IO."""
from pathlib import Path
import subprocess
import tempfile

TOOLS = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / 'c6net.h').write_text('''
#include <stdbool.h>
struct c6_link_snapshot;
int c6net_get_link_snapshot(struct c6_link_snapshot *out);
int c6net_connect(const char *ssid, const char *password);
int c6net_prepare(void);
''')
        (root / 'test.c').write_text('''
#include "desktop_backend.h"
#include "link_state.h"
#include <assert.h>
static bool initialized;
static int scans, connections;
static int prepares;
int c6net_prepare(void) { prepares++;return -EIO; }
int c6net_get_link_snapshot(struct c6_link_snapshot *out) {
 memset(out,0,sizeof(*out));out->initialized=initialized;return 0;
}
int esp_hosted_rpc_wifi_scan_results(struct c6_scan_ap *r,size_t n,size_t *c) {
 assert(n==C6_SCAN_LIMIT);scans++;*c=1;
 return c6_scan_ap_set(r,(const uint8_t *)"fixture",7,-55,1);
}
int c6net_connect(const char *s,const char *p) {
 assert(!strcmp(s,"fixture") && !strcmp(p,""));connections++;return -ETIMEDOUT;
}
int main(void) {
 struct c6_scan_ap records[C6_SCAN_LIMIT];size_t count=99;
 assert(g_c6_desktop_backend.scan(records,C6_SCAN_LIMIT,&count)==-EIO);
 assert(prepares==1);
 assert(count==0 && scans==0 && connections==0);
 initialized=true;
 assert(!g_c6_desktop_backend.scan(records,C6_SCAN_LIMIT,&count));
 assert(count==1 && scans==1);
 assert(g_c6_desktop_backend.connect("","")==-EINVAL);
 assert(connections==0);
 assert(g_c6_desktop_backend.connect("fixture","")==-ETIMEDOUT);
 assert(connections==1);
 return 0;
}
''')
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-pthread',
                        '-I', str(root), '-I', str(TOOLS / 'c6'),
                        str(TOOLS / 'c6/desktop_backend.c'), str(root / 'test.c'),
                        '-o', str(root / 'test')], check=True)
        subprocess.run([str(root / 'test')], check=True, timeout=10)
    print('PASS: real backend routing, scan precondition and connection errors')


if __name__ == '__main__':
    main()
