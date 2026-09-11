"""Exercise the actual RPC serialization prefix with mocked protobuf sizes."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apps', type=Path)
    args = parser.parse_args()
    patch = Path(__file__).resolve().parents[1] / 'patches/c6/hosted-rpc-size.patch'
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        path = root / 'system/c6probe/esp_hosted_rpc.c'
        path.parent.mkdir(parents=True)
        path.write_bytes((args.apps / 'system/c6probe/esp_hosted_rpc.c').read_bytes())
        subprocess.run(['git', 'init', '-q', str(root)], check=True)
        command = ['git', '-C', str(root), 'apply']
        if subprocess.run(command + ['--check', str(patch)], capture_output=True).returncode == 0:
            subprocess.run(command + [str(patch)], check=True)
        else:
            subprocess.run(command + ['--reverse', '--check', str(patch)], check=True)
        text = path.read_text()
        function = 'rpc_transact_locked' if 'static FAR Rpc *rpc_transact_locked(' in text else 'rpc_transact'
        start = text.index('static FAR Rpc *' + function + '(')
        end = text.index('  printf("rpc: %s tx pb=', start)
        prefix = text[start:end].replace('rpc_transact_locked(', 'rpc_transact(', 1)
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define FAR
#define RPC_TLV_MAX 512
typedef struct {size_t size;} Rpc;
struct rpc_client_s {int unused;};
static struct rpc_client_s g_rpc;
static unsigned calls, composed;
static size_t rpc__get_packed_size(const Rpc *r) {return r->size;}
static size_t rpc__pack(const Rpc *r, uint8_t *buf) {
 calls++; memset(buf, 0, r->size); return r->size;
}
static int rpc_tlv_compose(uint8_t *dst, size_t cap, const uint8_t *src, uint16_t len) {
 (void)dst; (void)cap; (void)src; (void)len; composed++; return 0;
}
'''
        code += prefix + '\n  return (Rpc *)req;\n}\n'
        code += r'''
int main(void) {
 Rpc request = {257};
 assert(rpc_transact(&request,"test") == NULL && calls == 0 && composed == 0);
 request.size = 256;
 assert(rpc_transact(&request,"test") == &request && calls == 1 && composed == 1);
 request.size = 0;
 assert(rpc_transact(&request,"test") == &request && calls == 2 && composed == 2);
 return 0;
}
'''
        (root / 'test.c').write_text(code)
        subprocess.run(['cc', '-fsanitize=address,undefined', str(root / 'test.c'),
                        '-o', str(root / 'test')], check=True)
        subprocess.run([str(root / 'test')], check=True)
    print('PASS: RPC rejects oversized serialization before writing; boundary and empty cases pass')


if __name__ == '__main__':
    main()
