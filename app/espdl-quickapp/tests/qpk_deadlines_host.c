/* SPDX-License-Identifier: Apache-2.0 */
#include <quickjs.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static struct { JSRuntime *runtime; JSContext *context; uint64_t deadline_ms;
                bool hang_fired, hang_reported; uint64_t last_hang_ms;
                void (*unresponsive_cb)(bool); } g_qpk;
static unsigned errors, hangs, recovered;
static void hang_stub(bool unresponsive){ if (unresponsive) hangs++; else recovered++; }
static void qpk_show_error(const char *prefix)
{
  (void)prefix; errors++;
  JS_FreeValue(g_qpk.context, JS_GetException(g_qpk.context));
}
#define QPK_HANG_BUDGET 5000
#include "qpk_deadlines_subset.inc"

static JSValue slow(JSContext *ctx, JSValueConst self, int argc, JSValueConst *argv)
{ (void)ctx;(void)self;(void)argc;(void)argv;usleep(15000);return JS_UNDEFINED; }
static JSValue failed_job(JSContext *ctx,int argc,JSValueConst *argv)
{ (void)argc;(void)argv;return JS_ThrowInternalError(ctx,"job failure"); }
static JSValue eval(const char *s)
{ JSValue v=JS_Eval(g_qpk.context,s,strlen(s),"test.js",JS_EVAL_TYPE_GLOBAL);assert(!JS_IsException(v));return v; }
static void fresh(void)
{
  if(g_qpk.context)JS_FreeContext(g_qpk.context);
  if(g_qpk.runtime)JS_FreeRuntime(g_qpk.runtime);
  memset(&g_qpk,0,sizeof(g_qpk));errors=0;hangs=0;recovered=0;g_qpk.unresponsive_cb=hang_stub;
  g_qpk.runtime=JS_NewRuntime();assert(g_qpk.runtime);
  JS_SetMemoryLimit(g_qpk.runtime,16*1024*1024);
  JS_SetInterruptHandler(g_qpk.runtime,qpk_interrupt,&g_qpk);
  g_qpk.context=JS_NewContext(g_qpk.runtime);assert(g_qpk.context);
  JSValue global=JS_GetGlobalObject(g_qpk.context);
  JS_SetPropertyStr(g_qpk.context,global,"slow",JS_NewCFunction(g_qpk.context,slow,"slow",0));
  JS_FreeValue(g_qpk.context,global);
}
int main(void)
{
  fresh();
  uint64_t outer=qpk_deadline_begin(100),parent=g_qpk.deadline_ms;
  uint64_t inner=qpk_deadline_begin(500);assert(g_qpk.deadline_ms==parent);
  qpk_deadline_end(inner);assert(g_qpk.deadline_ms==parent);
  qpk_deadline_end(outer);assert(g_qpk.deadline_ms==0);
  puts("DEADLINE_NESTING_OK");

  fresh();
  outer=qpk_deadline_begin(10000);
  JSValue compiled=JS_Eval(g_qpk.context,"globalThis.executed=true",24,"test.js",JS_EVAL_TYPE_GLOBAL|JS_EVAL_FLAG_COMPILE_ONLY);
  assert(!JS_IsException(compiled));usleep(30000);qpk_deadline_end(outer);
  outer=qpk_deadline_begin(20);
  JSValue result=JS_EvalFunction(g_qpk.context,compiled);
  assert(!JS_IsException(result)&&qpk_run_jobs()==0);qpk_deadline_end(outer);JS_FreeValue(g_qpk.context,result);
  puts("COMPILE_EXECUTION_SEPARATION_OK");

  fresh();JSValue fn=eval("(function(){while(true){}})");
  uint64_t began=qpk_now_ms();result=qpk_call(fn,20);
  assert(JS_IsException(result)&&hangs==1&&qpk_now_ms()-began<500&&g_qpk.deadline_ms==0);
  JS_FreeValue(g_qpk.context,result);JS_FreeValue(g_qpk.context,fn);puts("SCRIPT_RUNAWAY_INTERRUPTED_OK");

  fresh();fn=eval("(function(){function loop(){Promise.resolve().then(loop)};loop()})");
  began=qpk_now_ms();result=qpk_call(fn,20);
  assert(JS_IsException(result)&&hangs==1&&qpk_now_ms()-began<500&&g_qpk.deadline_ms==0);
  JS_FreeValue(g_qpk.context,result);JS_FreeValue(g_qpk.context,fn);puts("PROMISE_RUNAWAY_INTERRUPTED_OK");

  fresh();fn=eval("(function(){slow();Promise.resolve().then(slow).then(function(){return 1})})");
  result=qpk_call(fn,25);assert(JS_IsException(result)&&hangs==1&&g_qpk.deadline_ms==0);
  JS_FreeValue(g_qpk.context,result);JS_FreeValue(g_qpk.context,fn);puts("CALLBACK_AND_JOBS_SHARE_BUDGET_OK");

  fresh();outer=qpk_deadline_begin(1);usleep(3000);
  assert(qpk_run_jobs()==0&&errors==0&&hangs==0);qpk_deadline_end(outer);
  puts("NO_PHANTOM_TIMEOUT_WITH_EMPTY_JOB_QUEUE_OK");

  fresh();fn=eval("(function(){})");
  assert(JS_EnqueueJob(g_qpk.context,failed_job,0,NULL)==0);
  result=qpk_call_args(fn,100,0,NULL);assert(JS_IsException(result)&&errors==1&&hangs==0&&g_qpk.deadline_ms==0);
  JS_FreeValue(g_qpk.context,result);JS_FreeValue(g_qpk.context,fn);puts("ASYNC_ERROR_PROPAGATION_OK");
  JS_FreeContext(g_qpk.context);JS_FreeRuntime(g_qpk.runtime);puts("QPK_DEADLINES_OK");return 0;
}
