#!/usr/bin/env python3
"""Render the existing home with a native ball inside its AI card, review only."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess
from render_refined_home import png565
from generate_grok_sizes import replace_once

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for k in ['apps','cache','output','scenes']:p.add_argument('--'+k,type=Path,required=True)
    p.add_argument('--compact',action='store_true',help='Capture card animation frames and one full home per scene')
    a=p.parse_args();root=Path(__file__).resolve().parents[2];desktop=root/'project/app/espdl-quickapp/overlay/apps/system/desktop'
    out=a.output.resolve();work=out/'source';work.mkdir(parents=True,exist_ok=True)
    for base in [a.apps/'system/desktop',desktop]:
        for path in base.iterdir():
            if path.suffix in ['.h','.inc']:shutil.copy2(path,work/path.name)
    ui=(desktop/'glass_ui.inc').read_text();helpers=[]
    for begin,end in [('static lv_obj_t *glass_box(', 'static lv_obj_t *glass_number('),('static struct glass_display_s','static bool g_preferences_save_failed;'),('static void glass_update(lv_timer_t *timer)','static void glass_palette_select(')]:
        first=ui.index(begin);last=ui.index(end,first);helpers.append(ui[first:last])
    (work/'production_home_helpers.inc').write_text('\n'.join(helpers))
    home=(desktop/'glass_home_ui.inc').read_text()
    first=home.index('  home_icon(card, GLASS_ICON_SPARKLES, 25, 20, 20, accent);');last=home.index('  home_button(card, "开始对话"',first)
    home=home[:first]+'''  g_home_ball_view=lv_obj_create(card);
  lv_obj_remove_style_all(g_home_ball_view);
  lv_obj_set_pos(g_home_ball_view,0,0);lv_obj_set_size(g_home_ball_view,285,89);
  lv_obj_clear_flag(g_home_ball_view,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  home_arrow(card,glass_chat_open);
  home_rule(card,21,89,243,accent);
  home_text(card,"AI 助手",21,95,100,16,ink);
  lv_obj_t *ai_hint=home_text(card,"文字 · 语音",128,98,136,12,muted);
  lv_obj_set_style_text_align(ai_hint,LV_TEXT_ALIGN_RIGHT,0);
'''+home[last:]
    (work/'glass_home_ui.inc').write_text(home)
    polygon=(root/'output/grok-styles/polygon.inc').read_text();polygon=polygon[:polygon.index('static JSValue qpk_polygon_update')]
    (work/'home_ball_polygon.inc').write_text(polygon)
    harness=(root/'project/app/espdl-quickapp/tests/render_refined_home.c').read_text()
    harness=harness[:harness.index('int main(int argc,char **argv)')]
    harness=harness.replace('#include "glass_home_ui.inc"','static lv_obj_t *g_home_ball_view;\n#include "glass_home_ui.inc"')
    harness=harness.replace('#include <time.h>','#include <time.h>\n#include <math.h>\n#include <cJSON.h>')
    harness+='''
#include "home_ball_polygon.inc"
static lv_obj_t *ball_nodes[64];static struct qpk_polygon_s *ball_parts[64];
static lv_obj_t *ball_texts[8];
static void ball_clear(void){for(int i=0;i<64;i++){if(ball_nodes[i])lv_obj_delete(ball_nodes[i]);free(ball_parts[i]);ball_nodes[i]=NULL;ball_parts[i]=NULL;}for(int i=0;i<8;i++){if(ball_texts[i])lv_obj_delete(ball_texts[i]);ball_texts[i]=NULL;}}
static void ball_text_frame(const cJSON *texts){
  int n=cJSON_GetArraySize(texts);assert(n<=8);
  for(int i=0;i<8;i++){
    if(i>=n){if(ball_texts[i])lv_obj_add_flag(ball_texts[i],LV_OBJ_FLAG_HIDDEN);continue;}
    if(!ball_texts[i]){ball_texts[i]=lv_label_create(g_home_ball_view);lv_obj_clear_flag(ball_texts[i],LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);}
    const cJSON *p=cJSON_GetArrayItem(texts,i);lv_obj_t *label=ball_texts[i];lv_obj_remove_flag(label,LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(label,cJSON_GetObjectItem(p,"text")->valuestring);
    int size=cJSON_GetObjectItem(p,"size")->valueint;lv_obj_set_style_text_font(label,size<=14?g_home_font14:zh_font(size),0);
    lv_obj_set_style_text_color(label,lv_color_hex(cJSON_GetObjectItem(p,"color")->valueint),0);lv_obj_set_style_opa(label,cJSON_GetObjectItem(p,"opacity")->valueint,0);
    lv_obj_set_pos(label,cJSON_GetObjectItem(p,"x")->valueint,cJSON_GetObjectItem(p,"y")->valueint);lv_obj_move_foreground(label);
  }
}
static void capture_card(lv_display_t *d,const char *directory,const char *name){
  lv_refr_now(d);char path[1024];snprintf(path,sizeof(path),"%s/%s.card565",directory,name);FILE *f=fopen(path,"wb");assert(f);
  for(int y=374;y<552;y++)assert(fwrite(fb+(y*1024+108)*2,2,285,f)==285);fclose(f);
}
static void ball_frame(const cJSON *frame){
  int n=cJSON_GetArraySize(frame);assert(n>2&&n<64);
  for(int i=0;i<64;i++){
    if(i>=n){if(ball_nodes[i])lv_obj_add_flag(ball_nodes[i],LV_OBJ_FLAG_HIDDEN);continue;}
    if(!ball_nodes[i]){ball_parts[i]=calloc(1,sizeof(*ball_parts[i]));assert(ball_parts[i]);ball_nodes[i]=lv_obj_create(g_home_ball_view);lv_obj_remove_style_all(ball_nodes[i]);lv_obj_clear_flag(ball_nodes[i],LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_event_cb(ball_nodes[i],qpk_polygon_draw,LV_EVENT_DRAW_MAIN,ball_parts[i]);}
    lv_obj_remove_flag(ball_nodes[i],LV_OBJ_FLAG_HIDDEN);
    struct qpk_polygon_s *p=ball_parts[i];memset(p,0,sizeof(*p));
    const cJSON *part=cJSON_GetArrayItem(frame,i),*points=cJSON_GetObjectItem(part,"points"),*g=cJSON_GetObjectItem(part,"gradient");
    p->count=cJSON_GetArraySize(points);assert(p->count>=3&&p->count<=128);
    p->color=cJSON_GetObjectItem(part,"color")->valuedouble;p->opacity=cJSON_GetObjectItem(part,"opacity")->valueint;p->stroke=cJSON_GetObjectItem(part,"width")->valueint;p->pixel_cell=cJSON_GetObjectItem(part,"cell")->valueint;
    int minx=8192,miny=8192,maxx=-8192,maxy=-8192;
    for(unsigned j=0;j<p->count;j++){const cJSON *pair=cJSON_GetArrayItem(points,j);int x=cJSON_GetArrayItem(pair,0)->valueint,y=cJSON_GetArrayItem(pair,1)->valueint;p->points[j].x=x;p->points[j].y=y;if(x<minx)minx=x;if(y<miny)miny=y;if(x>maxx)maxx=x;if(y>maxy)maxy=y;}
    int cell=p->pixel_cell;minx=floor((double)minx/cell)*cell;miny=floor((double)miny/cell)*cell;maxx=ceil((double)(maxx+1)/cell)*cell-1;maxy=ceil((double)(maxy+1)/cell)*cell-1;
    for(unsigned j=0;j<p->count;j++){p->points[j].x-=minx;p->points[j].y-=miny;}
    if(cJSON_IsArray(g)){p->gradient.kind=cJSON_GetArrayItem(g,0)->valueint;p->gradient.count=cJSON_GetArrayItem(g,1)->valueint;assert(p->gradient.count<=5);p->gradient.x1=cJSON_GetArrayItem(g,2)->valuedouble-minx;p->gradient.y1=cJSON_GetArrayItem(g,3)->valuedouble-miny;p->gradient.x2=cJSON_GetArrayItem(g,4)->valuedouble-minx;p->gradient.y2=cJSON_GetArrayItem(g,5)->valuedouble-miny;p->gradient.r=cJSON_GetArrayItem(g,6)->valuedouble;for(unsigned j=0;j<p->gradient.count;j++){p->gradient.offset[j]=cJSON_GetArrayItem(g,7+j*2)->valuedouble;p->gradient.color[j]=cJSON_GetArrayItem(g,8+j*2)->valuedouble;}}
    lv_obj_set_pos(ball_nodes[i],minx,miny);lv_obj_set_size(ball_nodes[i],maxx-minx+1,maxy-miny+1);lv_obj_move_foreground(ball_nodes[i]);lv_obj_invalidate(ball_nodes[i]);
  }
}
static void *read_file(const char *path,size_t *bytes){FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);*bytes=ftell(f);rewind(f);char *data=calloc(1,*bytes+1);assert(data&&fread(data,1,*bytes,f)==*bytes);fclose(f);return data;}
int main(int argc,char **argv){
  assert(argc==4);size_t bytes,json_bytes;void *data=read_file(argv[1],&bytes);char *text=read_file(argv[3],&json_bytes);cJSON *scenes=cJSON_Parse(text);assert(scenes);free(text);
  lv_init();lv_display_t *d=lv_display_create(1024,600);lv_display_set_color_format(d,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(d,fb,NULL,sizeof(fb),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(d,flush);
  g_desktop.font16=lv_tiny_ttf_create_data(data,bytes,16);g_desktop.font20=lv_tiny_ttf_create_data(data,bytes,20);g_desktop.font28=lv_tiny_ttf_create_data(data,bytes,28);assert(g_desktop.font16&&g_desktop.font20&&g_desktop.font28);g_desktop.font16->fallback=glass_icon_font(16);g_desktop.font20->fallback=glass_icon_font(20);g_desktop.font28->fallback=glass_icon_font(24);g_desktop.screen=lv_screen_active();g_glass.reduced_motion=true;glass_home_fonts_init(data,bytes);
  for(int theme=0;theme<2;theme++){
    g_desktop.light_theme=theme==0;glass_rebuild();lv_obj_update_layout(g_glass.home);
    lv_area_t card,view;lv_obj_get_coords(g_glass.home_cards[1],&card);lv_obj_get_coords(g_home_ball_view,&view);assert(view.x1==card.x1&&view.y1==card.y1&&lv_area_get_width(&view)==285&&lv_area_get_height(&view)==89);
    FILE *layout=fopen(theme?"layout-dark.txt":"layout-light.txt","w");assert(layout);validate(g_glass.home,layout);fclose(layout);
    const cJSON *all=cJSON_GetObjectItem(scenes,"scenes");
    for(int scene=0;scene<cJSON_GetArraySize(all);scene++){
      const cJSON *entry=cJSON_GetArrayItem(all,scene),*frames=cJSON_GetObjectItem(entry,"frames"),*texts=cJSON_GetObjectItem(entry,"textFrames");assert(cJSON_GetArraySize(frames)==30);
      const cJSON *key=cJSON_GetObjectItem(entry,"key");const char *scene_name=key?key->valuestring:scene?"thinking":"idle";
      for(int frame=0;frame<30;frame++){ball_frame(cJSON_GetArrayItem(frames,frame));ball_text_frame(cJSON_GetArrayItem(texts,frame));char name[100];snprintf(name,sizeof(name),"%s-%s-%03d",theme?"dark":"light",scene_name,frame);if(HOME_CAPTURE_COMPACT)capture_card(d,argv[2],name);if(!HOME_CAPTURE_COMPACT||frame==12)screenshot(d,argv[2],name);}
    }
    lv_obj_t *button=click_text(g_glass.home,"开始对话");assert(button);lv_obj_send_event(button,LV_EVENT_CLICKED,NULL);assert(!strcmp(last_action,"glass_chat_open"));lv_area_t bounds;lv_obj_get_coords(button,&bounds);assert(bounds.y1>=card.y1+89&&bounds.y2<=card.y2);
    ball_clear();
  }
  cJSON_Delete(scenes);lv_obj_delete(g_desktop.screen);lv_tiny_ttf_destroy(g_desktop.font16);lv_tiny_ttf_destroy(g_desktop.font20);lv_tiny_ttf_destroy(g_desktop.font28);lv_tiny_ttf_destroy(g_home_font12);lv_tiny_ttf_destroy(g_home_font14);lv_tiny_ttf_destroy(g_home_font32);lv_tiny_ttf_destroy(g_home_font34);lv_tiny_ttf_destroy(g_home_font40);lv_tiny_ttf_destroy(g_home_font64);lv_display_delete(d);lv_deinit();free(data);
  puts("HOME_BALL_NATIVE_OK embedded ball in 285x89 upper card; lower chat callback retained; light/dark; all scene frames; layout checks");return 0;
}
'''
    harness=harness.replace('#include "home_ball_polygon.inc"','#define HOME_CAPTURE_COMPACT '+str(int(a.compact))+'\n#include "home_ball_polygon.inc"')
    (work/'home-ball.c').write_text(harness)
    flags=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-DLV_CONF_INCLUDE_SIMPLE']
    flags+=['-I'+str(k) for k in [work,desktop,a.apps/'system/desktop',a.apps/'graphics/lvgl',a.apps/'netutils/cjson/cJSON',a.cache]]
    command=['gcc',*flags,str(work/'home-ball.c'),str(desktop/'glass_icons.c'),str(a.apps/'netutils/cjson/cJSON/cJSON.c'),*map(str,sorted(a.cache.glob('lvgl-*.o'))),'-lm','-o',str(out/'render-home-ball')]
    with (out/'compile.log').open('w') as log:result=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT)
    if result.returncode:print((out/'compile.log').read_text());raise SystemExit(result.returncode)
    env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=1';env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
    with (out/'native.log').open('w') as log:result=subprocess.run([str(out/'render-home-ball'),str(a.apps/'system/desktop/AlibabaPuHuiTi-3-55-Regular.otf'),str(out),str(a.scenes.resolve())],cwd=out,stdout=log,stderr=subprocess.STDOUT,env=env)
    if result.returncode:print((out/'native.log').read_text());raise SystemExit(result.returncode)
    scene_model=json.loads(a.scenes.read_text())
    for theme in ['light','dark']:png565(out/(theme+'-thinking-012.rgb565'))
    (out/'VALIDATION.json').write_text(json.dumps({'flashed':False,'production_applied':False,'firmware_compiled':False,'board_tested':False,'native_frames':len(scene_model['scenes'])*60,'capture_format':'card565-and-full-frame-012' if a.compact else 'full-rgb565','scenes':[{'key':s.get('key','idle' if s['emotion']=='02' else 'thinking'),'emotion':s['emotion'],'name':s.get('name'),'group':s.get('group')} for s in scene_model['scenes']],'card':[108,374,285,178],'ball_viewport':[0,0,285,89],'ball_diameter':scene_model['diameter'],'ball_center':scene_model.get('center',[142.5,44.5]),'nominal_headroom_px':scene_model.get('nominalHeadroom',4.5),'nominal_hidden_bottom_px':scene_model.get('nominalHiddenBottom',0),'scene_sha256':hashlib.sha256(a.scenes.read_bytes()).hexdigest(),'body_fill':scene_model.get('bodyFill'),'body_fill_style':scene_model.get('bodyFillStyle'),'face_angle':scene_model.get('faceAngle'),'body_roll_degrees':scene_model.get('bodyRollDegrees'),'renderer':'existing native LVGL home with replay of pinned engine vector scene as child objects, not a bitmap overlay','prototype_only':True,'production_home_sha256':hashlib.sha256((desktop/'glass_home_ui.inc').read_bytes()).hexdigest()},indent=2)+'\n')
    print('HOME_BALL_REVIEW_RENDER_OK',out)
if __name__=='__main__':main()
