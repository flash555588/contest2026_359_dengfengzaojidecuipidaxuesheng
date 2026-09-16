/* Real QuickJS bindings and production async service, mock physical devices. */
#include "qpk_hardware.h"
#include <quickjs.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static struct { char package[48]; } g_qpk = {"diagnostic.hardware"};
#include "qpk_hardware_js.inc"
static atomic_int active, released;
void *qpk_hw_platform_create(const char *p) { assert(!strcmp(p, g_qpk.package)); atomic_fetch_add(&active, 1); return malloc(1); }
void qpk_hw_platform_destroy(void *p) { free(p); atomic_fetch_sub(&active, 1); atomic_fetch_add(&released, 1); }
char *qpk_hw_capabilities(void) { return strdup("{\"version\":1,\"audio\":{\"record\":true}}"); }
int qpk_hw_platform_execute(void *p, const char *op, const cJSON *a, cJSON *out, struct qpk_hw_progress *progress)
{
  assert(p && a);
  if (!strcmp(op, "audio.record")) {
    while (!atomic_load(&progress->cancel) && !atomic_load(&progress->finish)) {
      atomic_fetch_add(&progress->bytes, 32); atomic_fetch_add(&progress->elapsed_ms, 1); usleep(1000);
    }
  }
  if (!strcmp(op, "gpio.write")) return -EBADF;
  cJSON_AddStringToObject(out, "operation", op);
  return 0;
}
static JSValue evaluate(JSContext *c, const char *code)
{
  JSValue v = JS_Eval(c, code, strlen(code), "hardware-test", JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(v)) { JSValue e = JS_GetException(c); const char *s = JS_ToCString(c,e); fprintf(stderr,"%s\n",s); abort(); }
  return v;
}
static int request(JSContext *c, const char *code)
{
  JSValue v = evaluate(c, code); int32_t id = 0;
  assert(!JS_ToInt32(c, &id, v) && id > 0); JS_FreeValue(c,v); return id;
}
static unsigned finishing;
static cJSON *wait_result(unsigned id)
{
  for (unsigned i = 0; i < 5000; i++) {
    if (id == finishing) assert(!qpk_hw_cancel(g_qpk_hardware, id, true));
    char *s = qpk_hw_poll(g_qpk_hardware,id); assert(s);
    cJSON *v=cJSON_Parse(s); free(s); assert(v);
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(v,"done"))) return v;
    cJSON_Delete(v); usleep(1000);
  }
  assert(!"hardware job timed out"); return NULL;
}
int main(int argc, char **argv)
{
  JSRuntime *r=JS_NewRuntime(); JSContext *c=JS_NewContext(r);
  if (argc > 1) {
    FILE *file = fopen(argv[1], "rb"); assert(file);
    assert(!fseek(file, 0, SEEK_END)); long size = ftell(file); assert(size > 0); rewind(file);
    char *source = malloc(size + 1); assert(source); assert(fread(source, 1, size, file) == (size_t)size); fclose(file); source[size] = 0;
    JSValue compiled = JS_Eval(c, source, size, "recorder/app.js", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    assert(!JS_IsException(compiled)); JS_FreeValue(c, compiled); free(source);
  }
  JSValue system=JS_NewObject(c), global=JS_GetGlobalObject(c); qpk_hw_install(c,system); JS_SetPropertyStr(c,global,"system",system); JS_FreeValue(c,global);
  JSValue v=evaluate(c,"if (!system.hardware.capabilities().audio.record) throw Error('missing'); ['gpio','i2c','spi','uart','pwm','servo','audio','ble'].forEach(function(k){if(typeof system[k]!=='object')throw Error(k);});"); JS_FreeValue(c,v);
  int record=request(c,"system.audio.record({clip:'test'})");
  usleep(5000);
  int gpio=request(c,"system.gpio.open({pin:1})");
  cJSON *result=wait_result(gpio); assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result,"ok"))); cJSON_Delete(result);
  char control[64]; snprintf(control,sizeof(control),"system.audio.stop(%d)",record); v=evaluate(c,control); JS_FreeValue(c,v);
  finishing=record;
  result=wait_result(record); assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result,"ok"))); assert(cJSON_GetObjectItemCaseSensitive(result,"bytes")->valueint>0); cJSON_Delete(result);
  finishing=0;
  int listing=request(c,"system.audio.list()"); result=wait_result(listing);
  assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result,"ok"))); cJSON_Delete(result);
  gpio=request(c,"system.gpio.write({pin:2,value:1})"); result=wait_result(gpio); assert(cJSON_GetObjectItemCaseSensitive(result,"error")->valueint==-EBADF); cJSON_Delete(result);
  char *consumed=qpk_hw_poll(g_qpk_hardware,gpio); assert(strstr(consumed,"unknown")); free(consumed);
  record=request(c,"system.audio.record({})"); assert(!qpk_hw_cancel(g_qpk_hardware,record,false)); result=wait_result(record); assert(cJSON_GetObjectItemCaseSensitive(result,"error")->valueint==-ECANCELED); cJSON_Delete(result);
  for (unsigned i=0;i<20;i++) {
    request(c,"system.audio.record({})"); request(c,"system.gpio.read({pin:1})");
    qpk_hw_release(g_qpk_hardware); g_qpk_hardware=NULL;
    for(unsigned n=0;n<2000 && atomic_load(&active);n++) usleep(1000);
    assert(!atomic_load(&active));
  }
  /* A standalone NSH diagnostic must finish cleanup before its task group
   * exits; detached workers would otherwise be killed mid-close by NuttX. */
  request(c,"system.audio.record({})");
  request(c,"system.audio.record({clip:'queued'})");
  request(c,"system.gpio.read({pin:1})");
  qpk_hw_release_wait(g_qpk_hardware); g_qpk_hardware=NULL;
  assert(!atomic_load(&active));
  JS_FreeContext(c); JS_FreeRuntime(r);
  assert(atomic_load(&released)==21);
  puts("PASS: real QuickJS API registration, async result/error propagation, audio stop/cancel, GPIO during recording, consumed results, repeated exit with active/queued jobs and synchronous diagnostic cleanup");
  return 0;
}
