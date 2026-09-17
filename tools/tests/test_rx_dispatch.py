"""Compile the adapted poll/register functions with deterministic RX contention."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import argparse

TOOLS = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('adapter', TOOLS / 'adapt_c6_rx_dispatch.py')
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    args = parser.parse_args()
    text = adapter.adapt(args.source.read_text())
    start = text.index('static pthread_mutex_t g_rx_dispatch_lock')
    end = text.index('/****************************************************************************', text.index('int esp_hosted_poll(void)'))
    code = r'''
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#define FAR
#define OK 0
#define ESP_HOSTED_IF_MAX 8
typedef void (*esp_hosted_rx_cb_t)(void);
static struct {
 pthread_mutex_t lock;
 bool up;
 esp_hosted_rx_cb_t rx_cb[8];
 void *rx_arg[8];
} g_hosted = {.lock=PTHREAD_MUTEX_INITIALIZER, .up=true};
static uint8_t g_frame_buf[8];
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static bool entered, released;
static int read_error;
static uint32_t read_size = 1;
#define nxmutex_lock(m) (-pthread_mutex_lock(m))
#define nxmutex_unlock(m) pthread_mutex_unlock(m)
static int hosted_read_frame(uint32_t *size) {
 *size=read_size; g_frame_buf[0]=42; return read_error;
}
static void hosted_dispatch_stream(uint8_t *buf, uint32_t size) {
 assert(size==1);
 assert(pthread_mutex_trylock(&g_hosted.lock)==0);
 pthread_mutex_unlock(&g_hosted.lock);
 pthread_mutex_lock(&gate);
 entered=true; pthread_cond_broadcast(&condition);
 while (!released) pthread_cond_wait(&condition, &gate);
 assert(buf[0]==42);
 pthread_mutex_unlock(&gate);
}
'''
    code += text[start:end]
    code += r'''
static void *poller(void *unused) {
 (void)unused; assert(esp_hosted_poll()==1); return NULL;
}
int main(void) {
 pthread_t thread;
 assert(!pthread_create(&thread,NULL,poller,NULL));
 pthread_mutex_lock(&gate);
 while(!entered) pthread_cond_wait(&condition,&gate);
 assert(esp_hosted_poll()==0);
 assert(g_frame_buf[0]==42);
 released=true; pthread_cond_broadcast(&condition);
 pthread_mutex_unlock(&gate);
 assert(!pthread_join(thread,NULL));
 assert(esp_hosted_register(8,NULL,NULL)==-EINVAL);
 assert(esp_hosted_register(1,NULL,NULL)==0);
 read_error=-EIO; assert(esp_hosted_poll()==-EIO);
 read_error=0; read_size=0; assert(esp_hosted_poll()==0);
 g_hosted.up=false; assert(esp_hosted_poll()==-ENOTCONN);
 assert(esp_hosted_register(1,NULL,NULL)==0);
 return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / 'test.c').write_text(code)
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-pthread',
                        str(root / 'test.c'), '-o', str(root / 'test')], check=True)
        subprocess.run([str(root / 'test')], check=True, timeout=10)
    print('PASS: RX contention, bus unlocked during callback, error cleanup and registration')


if __name__ == '__main__':
    main()
