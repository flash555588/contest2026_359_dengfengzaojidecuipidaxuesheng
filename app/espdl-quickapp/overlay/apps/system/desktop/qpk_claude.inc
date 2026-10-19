/* SPDX-License-Identifier: Apache-2.0 */
#include "glass_claude.h"
static JSValue js_claude_request(JSContext *context,JSValueConst self,int argc,JSValueConst *argv)
{
  (void)self;
  if(!g_qpk.cli_control_access||argc!=4)return JS_ThrowTypeError(context,"Claude controller unavailable");
  const char *args[4]={NULL};int ret=-EINVAL;
  for(unsigned i=0;i<4;i++){if(!JS_IsString(argv[i]))goto done;args[i]=JS_ToCString(context,argv[i]);if(!args[i])goto done;}
  ret=glass_claude_request(args[0],args[1],args[2],args[3]);
done:
  for(unsigned i=0;i<4;i++)JS_FreeCString(context,args[i]);
  return JS_NewInt32(context,ret);
}
static JSValue js_claude_poll(JSContext *context,JSValueConst self,int argc,JSValueConst *argv)
{
  (void)self;int32_t id;
  if(!g_qpk.cli_control_access||argc!=1||JS_ToInt32(context,&id,argv[0])||id<=0)return JS_ThrowTypeError(context,"Invalid Claude request");
  char *text=glass_claude_poll(id);if(!text)return JS_ThrowOutOfMemory(context);
  JSValue value=JS_ParseJSON(context,text,strlen(text),"claude-result");free(text);return value;
}
static void qpk_claude_install(JSContext *context,JSValue system)
{
  if(!g_qpk.cli_control_access)return;
  JSValue object=JS_NewObject(context);
  JS_SetPropertyStr(context,object,"request",JS_NewCFunction(context,js_claude_request,"request",4));
  JS_SetPropertyStr(context,object,"poll",JS_NewCFunction(context,js_claude_poll,"poll",1));
  JS_SetPropertyStr(context,system,"cli",JS_DupValue(context,object));
  JS_SetPropertyStr(context,system,"claude",object);
}
