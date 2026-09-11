"""Test the patched transaction wrapper without radio access."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apps', type=Path)
    args = parser.parse_args()
    patch = Path(__file__).resolve().parents[1] / 'patches/c6/hosted-rpc-serialize.patch'
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source = root / 'system/c6probe/esp_hosted_rpc.c'
        source.parent.mkdir(parents=True)
        source.write_bytes((args.apps / 'system/c6probe/esp_hosted_rpc.c').read_bytes())
        subprocess.run(['git', 'init', '-q', str(root)], check=True)
        command = ['git', '-C', str(root), 'apply']
        if subprocess.run(command + ['--check', str(patch)], capture_output=True).returncode == 0:
            subprocess.run(command + [str(patch)], check=True)
        else:
            subprocess.run(command + ['--reverse', '--check', str(patch)], check=True)
        text = source.read_text()
        start = text.index('static pthread_mutex_t g_rpc_transaction_lock')
        end = text.index('\n/****************************************************************************', start)
        code = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#define FAR
typedef struct { int failure; } Rpc;
static atomic_int active, completed;
static Rpc *rpc_transact_locked(const Rpc *req, const char *label) {
 (void)label;
 assert(atomic_fetch_add(&active, 1) == 0);
 struct timespec pause = {0, 100000}; nanosleep(&pause, NULL);
 assert(atomic_fetch_sub(&active, 1) == 1);
 completed++;
 return req->failure ? NULL : (Rpc *)req;
}
'''
        code += text[start:end]
        code += r'''
static void *caller(void *arg) {
 (void)arg;
 for (int i = 0; i < 50; i++) {
  Rpc req = {.failure = i % 2};
  assert(rpc_transact(&req, "test") == (req.failure ? NULL : &req));
 }
 return NULL;
}
int main(void) {
 pthread_t threads[4];
 for (int i = 0; i < 4; i++) assert(!pthread_create(&threads[i], NULL, caller, NULL));
 for (int i = 0; i < 4; i++) assert(!pthread_join(threads[i], NULL));
 assert(completed == 200 && active == 0);
 return 0;
}
'''
        (root / 'test.c').write_text(code)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread',
                        str(root / 'test.c'), '-o', str(root / 'test')], check=True)
        subprocess.run([str(root / 'test')], check=True, timeout=10)
    print('PASS: 200 calls serialized across four threads, including failure returns')


if __name__ == '__main__':
    main()
