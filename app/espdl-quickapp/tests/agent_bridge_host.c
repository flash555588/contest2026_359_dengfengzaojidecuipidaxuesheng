/* SPDX-License-Identifier: Apache-2.0 */
#include "../overlay/apps/system/espclaw/port/espclaw_qpk.c"
#include <assert.h>
static unsigned providers,events;
static enum glass_agent_mode expected_mode;
static char *saved_config;
static bool legacy_called;
esp_err_t claw_core_add_context_provider(claw_core_handle_t core,const claw_core_context_provider_t *provider)
{
  (void)core;claw_core_request_t request={0};claw_core_context_t context={0};
  assert(provider->user_ctx);assert(provider->collect(&request,&context,provider->user_ctx)==ESP_OK);
  assert(context.content);if(context.kind==CLAW_CORE_CONTEXT_KIND_TOOLS){assert((strstr(context.content,"qpk_write_draft")!=NULL)==(expected_mode==AGENT_MODE_CREATE));
    assert((strstr(context.content,"skill_read")!=NULL)==(expected_mode!=AGENT_MODE_CHAT));
    assert((strstr(context.content,"mcp_call_tool")!=NULL)==(expected_mode==AGENT_MODE_ASSISTANT));}
  if(context.kind==CLAW_CORE_CONTEXT_KIND_SYSTEM_PROMPT){
    assert(strstr(context.content,glass_agent_mode_title(expected_mode)));
    assert((strstr(context.content,"Validated quick app guide")!=NULL)==(expected_mode==AGENT_MODE_CREATE));
  }
  free(context.content);providers++;return ESP_OK;
}
static char last_card[256];static unsigned card_events;
void claw_stream_emit(enum claw_stream_kind kind,const char *text)
{
  if(kind==CLAW_STREAM_CARD){assert(text&&*text&&strlen(text)<sizeof(last_card));strcpy(last_card,text);card_events++;return;}
  assert(kind==CLAW_STREAM_TOOL&&text);events++;
}
const char *glass_qpk_guide(void){return "Validated quick app guide";}
const char *glass_qpk_tools(void){return "[{\"type\":\"function\",\"function\":{\"name\":\"qpk_write_draft\"}}]";}
char *glass_qpk_list_examples(void){return strdup("{\"ok\":true}");}
char *glass_qpk_read_draft(void){return strdup("{\"ok\":true}");}
char *glass_qpk_read_example(const char *e,const char *f,unsigned start,unsigned max){(void)e;(void)f;(void)start;(void)max;return strdup("{\"ok\":true}");}
char *glass_qpk_write_draft(const char *text,atomic_bool *cancel){(void)cancel;assert(strlen(text)>AGENT_TOOL_BYTES);legacy_called=true;return strdup("{\"ok\":true}");}
int portal_agent_load(cJSON **out){*out=cJSON_Parse(saved_config?saved_config:"{\"skills_enabled\":1,\"mcp1_enabled\":1,\"mcp1_url\":\"https://mcp.example/mcp\"}");return *out?0:-1;}
int glass_mcp_http_perform(struct glass_mcp_http *r){(void)r;assert(!"No MCP endpoint configured: must not connect");return -1;}
/* The desktop build registers the real smart home provider at this hook. */
static const char fake_home_catalog[]=
  "{\"name\":\"home_test\",\"description\":\"Fixture card tool.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}},"
  "{\"name\":\"home_status\",\"description\":\"Fixture status tool.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}}";
static char *fake_home_call(const char *tool,const cJSON *args,atomic_bool *cancel)
{
  (void)args;(void)cancel;
  if(!strcmp(tool,"home_test"))return strdup("{\"ok\":true,\"entity\":\"light.desk\",\"card\":{\"kind\":\"light\",\"name\":\"书桌灯\",\"state\":\"已开启\",\"level\":80,\"on\":true}}");
  if(!strcmp(tool,"music_test"))return strdup("{\"ok\":true,\"accepted\":true,\"card\":{\"entity\":\"music.player\",\"kind\":\"music\",\"name\":\"测试歌曲\",\"state\":\"已请求播放\",\"level\":-1,\"on\":false}}");
  if(!strcmp(tool,"home_status"))return strdup("{\"ok\":true,\"configured\":true}");
  return strdup("{\"ok\":false,\"error\":\"fixture_failure\"}");
}
static const struct glass_board_home fake_home={fake_home_catalog,fake_home_call};
static const struct glass_board_home fake_music={"{\"name\":\"music_test\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}",fake_home_call};
int main(void)
{
  atomic_bool cancel=false;struct claw_qpk_session session={.cancel=&cancel};claw_core_config_t config={0};
  claw_qpk_configure(&config,&session);assert(config.supports_tools&&config.call_cap&&config.max_tool_iterations==8);
  assert(claw_qpk_attach(NULL,&session)==ESP_OK&&providers==2);
  char *out=NULL;assert(config.call_cap("skill_list","{}",NULL,&out,config.cap_user_ctx)==ESP_OK);assert(strstr(out,"quickapp")&&!strstr(out,"conversation"));free(out);
  assert(config.call_cap("skill_read","{\"id\":\"quickapp\"}",NULL,&out,config.cap_user_ctx)==ESP_OK);assert(strstr(out,"Validated"));free(out);
  assert(config.call_cap("skill_read","{\"id\":\"missing\"}",NULL,&out,config.cap_user_ctx)!=ESP_OK);free(out);
  char *input=malloc(AGENT_TOOL_BYTES+101);memcpy(input,"{\"source\":\"",11);memset(input+11,'x',AGENT_TOOL_BYTES+50);strcpy(input+11+AGENT_TOOL_BYTES+50,"\"}");
  assert(config.call_cap("qpk_write_draft",input,NULL,&out,config.cap_user_ctx)==ESP_OK&&legacy_called);free(input);free(out);
  atomic_store(&cancel,true);unsigned before=events;assert(config.call_cap("skill_list","{}",NULL,&out,config.cap_user_ctx)!=ESP_OK&&events==before);free(out);
  claw_qpk_close(&session);assert(!session.agent);atomic_store(&cancel,false);
  for(unsigned mode=0;mode<AGENT_MODE_COUNT;mode++){
    expected_mode=mode;session.mode=mode;memset(&config,0,sizeof(config));
    claw_qpk_configure(&config,&session);assert(session.agent);
    assert(config.supports_tools==(mode!=AGENT_MODE_CHAT));assert(claw_qpk_attach(NULL,&session)==ESP_OK);
    const char *names[]={"qpk_list_examples","qpk_read_draft","qpk_read_example","qpk_write_draft","mcp_list_tools","mcp_call_tool","skill_list"};
    for(unsigned i=0;i<7;i++){
      bool denied=mode==AGENT_MODE_CHAT||(i<4&&mode!=AGENT_MODE_CREATE)||(i>=4&&i<6&&mode!=AGENT_MODE_ASSISTANT);
      if(!denied)continue;
      unsigned before=events;assert(config.call_cap(names[i],"{}",NULL,&out,config.cap_user_ctx)!=ESP_OK);
      assert(strstr(out,"mode_denied")&&events==before);free(out);
    }
    if(mode==AGENT_MODE_ASSISTANT){
      assert(config.call_cap("skill_read","{\"id\":\"quickapp\"}",NULL,&out,config.cap_user_ctx)!=ESP_OK);free(out);
      assert(config.call_cap("skill_read","{\"id\":\"conversation\"}",NULL,&out,config.cap_user_ctx)==ESP_OK);free(out);
    }
    claw_qpk_close(&session);
  }
  /* Board smart home tools: cards leave as stream events, stripped from text. */
  glass_agent_set_board_home(&fake_home);
  glass_agent_set_board_music(&fake_music);
  session.mode=AGENT_MODE_ASSISTANT;memset(&config,0,sizeof(config));
  claw_qpk_configure(&config,&session);assert(session.agent);
  assert(config.call_cap("mcp_list_tools","{\"server\":\"board\"}",NULL,&out,config.cap_user_ctx)==ESP_OK);
  assert(strstr(out,"home_test")&&strstr(out,"home_status")&&strstr(out,"music_test"));free(out);
  assert(config.call_cap("mcp_call_tool","{\"server\":\"board\",\"tool\":\"home_test\",\"arguments\":{}}",NULL,&out,config.cap_user_ctx)==ESP_OK);
  assert(card_events==1&&strstr(last_card,"\"kind\":\"light\"")&&strstr(last_card,"书桌灯"));
  assert(!strstr(out,"card")&&strstr(out,"\"ok\":true"));free(out);
  assert(config.call_cap("mcp_call_tool","{\"server\":\"board\",\"tool\":\"home_broken\",\"arguments\":{}}",NULL,&out,config.cap_user_ctx)==ESP_FAIL);
  assert(card_events==1&&strstr(out,"fixture_failure"));free(out);
  assert(config.call_cap("mcp_call_tool","{\"server\":\"board\",\"tool\":\"music_test\",\"arguments\":{}}",NULL,&out,config.cap_user_ctx)==ESP_OK);
  assert(card_events==2&&strstr(last_card,"\"kind\":\"music\"")&&!strstr(out,"card"));free(out);
  glass_agent_set_board_music(NULL);glass_agent_set_board_home(NULL);claw_qpk_close(&session);
  puts("AGENT_BRIDGE_OK: real core context/tool callbacks; skill calls; cancellation; existing large QPK arguments preserved; board card events; cleanup");return 0;
}
