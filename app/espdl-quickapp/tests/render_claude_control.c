/* SPDX-License-Identifier: Apache-2.0 */
#define main all_ui_reference_main
#include "render_all_ui.c"
#undef main
#ifndef CLAUDE_APP_SOURCE
#define CLAUDE_APP_SOURCE "/mnt/c/Users/11937/Documents/ChatGPT/vela/project/quickapp/claude_control/app.js"
#endif
int main(int argc,char **argv)
{
  assert(argc==3);review_out=argv[2];
  FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);size_t size=ftell(f);rewind(f);
  void *font=malloc(size);assert(font);assert(fread(font,1,size,f)==size);fclose(f);
  lv_init();lv_display_t *display=lv_display_create(1024,600);
  lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display,review_fb,NULL,sizeof(review_fb),LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display,review_flush);
  g_desktop.font16=lv_tiny_ttf_create_data(font,size,16);g_desktop.font20=lv_tiny_ttf_create_data(font,size,20);g_desktop.font28=lv_tiny_ttf_create_data(font,size,28);
  g_desktop.font16->fallback=glass_icon_font(16);g_desktop.font20->fallback=glass_icon_font(20);g_desktop.font28->fallback=glass_icon_font(24);
  g_glass.font48=lv_tiny_ttf_create_data(font,size,48);glass_home_fonts_init(font,size);
  g_glass.reduced_motion=true;g_glass.temperature_c=-999;g_glass.humidity_pct=-1;g_glass.pm25=-1;
  g_desktop.screen=glass_box(lv_screen_active(),0,GLASS_STATUS_HEIGHT,1024,GLASS_CONTENT_HEIGHT,0,0);lv_obj_remove_flag(g_desktop.screen,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_text_font(g_desktop.screen,zh_font(20),0);
  g_desktop.panel=glass_box(g_desktop.screen,0,0,1024,GLASS_CONTENT_HEIGHT,0,0);lv_obj_add_flag(g_desktop.panel,LV_OBJ_FLAG_HIDDEN);
  char path[1024];snprintf(path,sizeof(path),"%s/screens.json",review_out);review_manifest=fopen(path,"w");assert(review_manifest);fputs("[\n",review_manifest);
  f=fopen(CLAUDE_APP_SOURCE,"rb");assert(f);fseek(f,0,SEEK_END);size=ftell(f);rewind(f);
  char *source=calloc(1,size+1);assert(source);assert(fread(source,1,size,f)==size);fclose(f);
  const char *fixture=
    "globalThis.CLAUDE_BOOTSTRAP={url:'http://192.168.31.95:8787',token:'a'.repeat(64),project:'C:/projects/openvela'};"
    "globalThis.__task={id:'task1',state:'running',project:'C:/projects/openvela',prompt:'修复并验证编译错误',detail:'正在执行 Bash',session_id:'fixture',text:'正在检查项目结构和编译配置。\\n已找到错误位置，正在准备修改。',events:['调用工具：Read','工具已返回','调用工具：Bash'],approval:null};"
    "globalThis.__pending=null;system.claude={request:(url,token,action,body)=>{if(action!=='status')throw Error('Read-only render fixture');__pending={done:true,error:0,status:200,body:{ok:true,clis:[{id:'claude',name:'Claude Code',resume:true},{id:'codex',name:'Codex',resume:true},{id:'terminal',name:'其他 CLI',input:true}],projects:['C:/projects/openvela','D:/projects/demo'],tasks:[__task],task:__task}};return 1;},poll:()=>{const r=__pending;__pending=null;return r;}};";
  size_t capacity=strlen(fixture)+size+2;char *script=malloc(capacity);assert(script);snprintf(script,capacity,"%s\n%s",fixture,source);free(source);
  for(unsigned theme=0;theme<2;theme++){
    g_desktop.light_theme=theme!=0;glass_rebuild();
    lv_obj_t *card=qapp_page_prepare("Claude Code");
    assert(qpk_runtime_launch(card,"Claude Code","ai.claude_control","1.0","app.js",script,strlen(script),zh_font,show_toast,qpk_show_dialog,qpk_show_unresponsive)==0);
    qapp_page_commit(card);
    assert(qpk_review_eval("__reviewTick();__reviewTick();")==0);
    review_capture(display,"claude-running","Claude Code · 工作进度");
    assert(qpk_review_eval("snapshot.task.state='waiting';snapshot.task.detail='等待设备确认操作';snapshot.task.approval={id:'approve1',tool:'Bash',input:'{\\\"command\\\":\\\"python tools/check.py\\\"}'};view='approval';render();")==0);
    review_capture(display,"claude-approval","Claude Code · 单次操作确认");
    assert(qpk_review_eval("snapshot.task.state='done';snapshot.task.detail='本轮完成';snapshot.task.approval=null;snapshot.task.text='修改完成，项目检查已通过。\\n可以继续追问，或选择另一个项目创建任务。';view='text';render();")==0);
    review_capture(display,"claude-done","Claude Code · 结果与继续会话");
    assert(qpk_review_eval("controller.connected=false;render();")==0);
    review_capture(display,"claude-disconnected","Claude Code · 连接中断保留任务");
    assert(qpk_review_eval("snapshot.task.state='running';snapshot.task.interactive=true;snapshot.task.input=true;snapshot.task.text='Claude Code v2.1.302\\n> 继续输入任务\\nmanual mode on';controller.connected=true;render();")==0);
    review_capture(display,"cli-native","ESPClaw · 原生终端与按键");
    assert(qpk_review_eval("snapshot.task.activity='error';snapshot.task.last_result='模型不存在或无权访问。请在电脑检查模型设置。';snapshot.task.detail='模型本轮失败；终端仍保持在线';view='text';render();")==0);
    review_capture(display,"cli-turn-error","ESPClaw · 模型错误与终端在线分开显示");
    assert(qpk_review_eval("snapshot.task.text=Array.from({length:32},(_,i)=>'进度 '+i).join('\\n');view='screen';follow=false;page=0;render();if(page!==0)throw Error('Reader position moved');snapshot.task.text+='\\n最新进度';render();if(page!==0)throw Error('Update moved reader');follow=true;render();if(page===0)throw Error('Follow did not advance');")==0);
    review_capture(display,"cli-follow","ESPClaw · 手动阅读与恢复跟随");
    assert(qpk_review_eval("cfg.pending={action:'input',data:{request_id:'fixture-receipt'},url:cfg.url};render();")==0);
    review_capture(display,"cli-uncertain","ESPClaw · 输入回执待核对");
    panel_hide(NULL);
    glass_chat_show(false);
    review_capture(display,"cli-espclaw-entry","ESPClaw · 电脑 CLI 入口");
    review_click("电脑 CLI"); assert(chat_cli_active);
    lv_obj_t *cli_page=g_desktop.current_card;
    lv_obj_t *cli_content=lv_obj_get_child(cli_page,2);
    lv_obj_update_layout(cli_page);
    assert(lv_obj_get_y(cli_content)==QAPP_HEADER_HEIGHT);
    assert(lv_obj_get_height(cli_content)+QAPP_HEADER_HEIGHT==lv_obj_get_height(cli_page));
    review_capture(display,"cli-builtin","ESPClaw · 内置电脑 CLI");
    qapp_back(NULL); assert(!chat_cli_active && g_desktop.chat_page);
    review_capture(display,"cli-return","返回 ESPClaw 对话");
    panel_hide(NULL);
    glass_chat_show(true);review_click("电脑 CLI");
    assert(chat_cli_active && g_desktop.qapp_return_apps && !g_desktop.chat_page);
    qapp_back(NULL);assert(g_desktop.chat_page && g_desktop.chat_return_apps);
    glass_chat_back(NULL);assert(g_desktop.apps_page);panel_hide(NULL);
  }
  free(script);fputs("\n]\n",review_manifest);fclose(review_manifest);
  puts("CLAUDE_RENDER_OK: both themes; bounded pages; task list; running, approval, done and disconnected states");
}
