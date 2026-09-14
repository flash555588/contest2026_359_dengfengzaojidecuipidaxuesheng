"""Exercise actual NuttX NimBLE callout code against POSIX one-shot timers."""
from pathlib import Path
import json
import subprocess
import tempfile

workspace = Path(__file__).resolve().parent.parent
relative = 'apps/wireless/bluetooth/nimble/mynewt-nimble/porting/npl/nuttx/src/os_callout.c'
sources = {'baseline': workspace / 'diagnostics/ble-baseline' / relative,
           'fixed': workspace / '04-v3-20260913/ble-startup-fix/overlay' / relative}
header = r'''
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
typedef void *pthread_addr_t;
typedef uint32_t ble_npl_time_t;
typedef int ble_npl_error_t;
#define BLE_NPL_OK 0
#define BLE_NPL_EINVAL 1
struct ble_npl_event { void (*ev_cb)(struct ble_npl_event *); void *ev_arg; };
typedef void ble_npl_event_fn(struct ble_npl_event *);
struct ble_npl_eventq { int unused; };
struct ble_npl_callout {
    struct ble_npl_event c_ev;
    struct ble_npl_eventq *c_evq;
    ble_npl_time_t c_ticks;
    timer_t c_timer;
    bool c_active;
};
void ble_npl_eventq_put(struct ble_npl_eventq *, struct ble_npl_event *);
ble_npl_time_t ble_npl_time_get(void);
'''
test = r'''
#include <stdatomic.h>
#include <unistd.h>
#include "os_callout.c"
static atomic_int fired;
void ble_npl_eventq_put(struct ble_npl_eventq *q, struct ble_npl_event *e)
{ (void)q; e->ev_cb(e); }
ble_npl_time_t ble_npl_time_get(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec*1000u+t.tv_nsec/1000000u; }
static void expired(struct ble_npl_event *e) { (void)e; atomic_fetch_add(&fired, 1); }
static int await_count(int wanted)
{ for (int i=0;i<500 && atomic_load(&fired)<wanted;i++) usleep(1000); return atomic_load(&fired)==wanted; }
int main(void)
{
    struct ble_npl_callout c;
    struct ble_npl_eventq q;
    ble_npl_callout_init(&c, &q, expired, NULL);
    if (ble_npl_callout_is_active(&c)) return 1;
    for (int n=1;n<=3;n++) {
        if (ble_npl_callout_reset(&c, 30) || !ble_npl_callout_is_active(&c)) return 2;
        if (!await_count(n)) return 3;
        if (ble_npl_callout_is_active(&c)) { puts("FAIL: expired one-shot remains active"); return 4; }
    }
    ble_npl_callout_reset(&c, 100);
    ble_npl_callout_stop(&c);
    if (ble_npl_callout_is_active(&c)) return 5;
    usleep(150000);
    if (atomic_load(&fired) != 3) return 6;
    puts("PASS: expiry, repeated rearm and stop");
    return 0;
}
'''
report = {}
with tempfile.TemporaryDirectory(prefix='v3-ble-callout-') as name:
    root = Path(name)
    (root / 'nuttx').mkdir()
    (root / 'nimble').mkdir()
    (root / 'nuttx/config.h').write_text('#define CONFIG_NIMBLE_CALLOUT_THREAD_STACKSIZE 32768\n')
    (root / 'nimble/nimble_npl.h').write_text(header)
    (root / 'test.c').write_text(test)
    for label, source in sources.items():
        (root / 'os_callout.c').write_bytes(source.read_bytes())
        subprocess.run(['cc', '-D_GNU_SOURCE', '-I', str(root), '-pthread', str(root / 'test.c'), '-lrt', '-o', str(root / label)], check=True)
        run = subprocess.run([str(root / label)], capture_output=True, text=True, timeout=5)
        report[label] = {'returncode': run.returncode, 'output': run.stdout.strip()}
assert report['baseline']['returncode'] == 4, report
assert report['fixed']['returncode'] == 0, report
(workspace / '04-v3-20260913/ble-startup-fix/evidence/callout-test.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
