"""Exercise adapted event functions with concurrent registration and dispatch."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from adapt_c6_wifi_events import adapt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    args = parser.parse_args()
    text = adapt(args.source.read_text())
    begin = text.index('static esp_hosted_wifi_event_cb_t g_wifi_event_cb;')
    end = text.index('/****************************************************************************', begin)
    setter = text.index('int esp_hosted_rpc_set_wifi_event_cb(')
    setter_end = text.index('/****************************************************************************', setter)
    code = '''
#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#include <stdatomic.h>
#define FAR
typedef void (*esp_hosted_wifi_event_cb_t)(void *, bool);
'''
    code += text[begin:end] + text[setter:setter_end]
    code += '''
static int a, b;
static atomic_int calls;
static void first(void *arg, bool connected) {
 (void)connected; assert(arg == &a); calls++;
}
static void second(void *arg, bool connected) {
 (void)connected; assert(arg == &b); calls++;
}
static void *notify(void *unused) {
 (void)unused;
 for (int i=0; i<20000; i++) rpc_wifi_event_notify(i % 2);
 return NULL;
}
int main(void) {
 pthread_t thread;
 assert(!esp_hosted_rpc_set_wifi_event_cb(first, &a));
 rpc_wifi_event_notify(true);
 assert(calls==1);
 assert(!pthread_create(&thread, NULL, notify, NULL));
 for (int i=0; i<10000; i++) {
  assert(!esp_hosted_rpc_set_wifi_event_cb(second, &b));
  assert(!esp_hosted_rpc_set_wifi_event_cb(first, &a));
 }
 assert(!esp_hosted_rpc_set_wifi_event_cb(NULL, &a));
 int stopped = calls;
 assert(!pthread_join(thread, NULL));
 rpc_wifi_event_notify(false);
 assert(calls==stopped && g_wifi_event_arg==NULL);
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        (root / 'test.c').write_text(code)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-pthread', str(root / 'test.c'), '-o', str(root / 'test')],
                       check=True)
        subprocess.run([str(root / 'test')], check=True, timeout=15)
    print('PASS: callback/argument pairing and unregister across concurrent dispatch')


if __name__ == '__main__':
    main()
