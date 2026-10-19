/* SPDX-License-Identifier: Apache-2.0 */
/* Feasibility probe only: parser/registry, explicitly no fake Canvas renderer. */
#include "quickjs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char *read_file(const char *name,size_t *size)
{
  FILE *file=fopen(name,"rb");if(!file)return NULL;
  fseek(file,0,SEEK_END);*size=ftell(file);rewind(file);
  char *data=malloc(*size+1);if(!data)exit(2);
  if(fread(data,1,*size,file)!=*size)exit(2);fclose(file);data[*size]=0;return data;
}

static int error(JSContext *ctx,const char *phase)
{
  JSValue exception=JS_GetException(ctx);const char *message=JS_ToCString(ctx,exception);
  fprintf(stderr,"PROBE_ERROR %s %s\n",phase,message?message:"allocation failure");
  JS_FreeCString(ctx,message);JS_FreeValue(ctx,exception);return 1;
}

static void memory(JSRuntime *rt,const char *phase)
{
  JSMemoryUsage m;JS_ComputeMemoryUsage(rt,&m);
  printf("MEMORY %s malloc=%lld used=%lld objects=%lld functions=%lld\n",phase,
    (long long)m.malloc_size,(long long)m.memory_used_size,(long long)m.obj_count,(long long)m.js_func_count);
}

int main(int argc,char **argv)
{
  if(argc!=5)return 2;
  size_t size;char *source=read_file(argv[2],&size);if(!source)return 2;
  JSRuntime *rt=JS_NewRuntime();JS_SetMemoryLimit(rt,strtoul(argv[3],NULL,10)*1024u*1024u);
  JS_SetMaxStackSize(rt,strtoul(argv[4],NULL,10)*1024u);
  JSContext *ctx=JS_NewContext(rt);if(!ctx)return 2;
  const char *shim="globalThis.window=globalThis;globalThis.console={log(){},warn(){},error(){}};"
    "globalThis.document={head:null,getElementById(){return null},createElement(){return{getContext(){return{measureText(){throw Error('Canvas font metrics backend required')}}}}}};"
    "globalThis.TextEncoder=class{encode(s){return Uint8Array.from(unescape(encodeURIComponent(s)),c=>c.charCodeAt(0))}};";
  JSValue v=JS_Eval(ctx,shim,strlen(shim),"probe-environment",JS_EVAL_TYPE_GLOBAL);
  if(JS_IsException(v))return error(ctx,"shim");JS_FreeValue(ctx,v);
  memory(rt,"shim");clock_t began=clock();
  if(!strcmp(argv[1],"bytecode"))v=JS_ReadObject(ctx,(const uint8_t *)source,size,JS_READ_OBJ_BYTECODE);
  else v=JS_Eval(ctx,source,size,"jizura-full.js",JS_EVAL_TYPE_GLOBAL|JS_EVAL_FLAG_COMPILE_ONLY);
  free(source);
  if(JS_IsException(v))return error(ctx,"load");
  printf("LOAD_SECONDS %.6f\n",(double)(clock()-began)/CLOCKS_PER_SEC);memory(rt,"loaded");
  if(strcmp(argv[1],"bytecode")){
    size_t bytes;uint8_t *code=JS_WriteObject(ctx,&bytes,v,JS_WRITE_OBJ_BYTECODE);
    if(!code)return error(ctx,"serialize");
    char path[1024];snprintf(path,sizeof(path),"%s.qjbc",argv[2]);FILE *out=fopen(path,"wb");
    if(!out)return 2;fwrite(code,1,bytes,out);fclose(out);js_free(ctx,code);printf("BYTECODE_BYTES %zu\n",bytes);
  }
  v=JS_EvalFunction(ctx,v);if(JS_IsException(v))return error(ctx,"execute");JS_FreeValue(ctx,v);
  memory(rt,"registered");
  const char *inspect="JSON.stringify({counts:Object.fromEntries(J.GROUP_KEYS.map(g=>[g,J.order(g).length])),styles:J.STYLE_ORDER.length,"
    "parse:J.parseLyrics('[00:00.00]hello / light\\n[00:02.00]*world*!').lines.length,"
    "plan:{cuts:J.plan(J.defaultProject(),null).cuts.length}})";
  v=JS_Eval(ctx,inspect,strlen(inspect),"probe-inspection",JS_EVAL_TYPE_GLOBAL);
  if(JS_IsException(v))return error(ctx,"inspect");
  const char *json=JS_ToCString(ctx,v);printf("RESULT %s\n",json?json:"null");
  JS_FreeCString(ctx,json);JS_FreeValue(ctx,v);memory(rt,"inspected");
  JS_FreeContext(ctx);JS_FreeRuntime(rt);return 0;
}
