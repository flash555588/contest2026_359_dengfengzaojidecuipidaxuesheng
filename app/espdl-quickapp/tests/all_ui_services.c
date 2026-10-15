/* SPDX-License-Identifier: Apache-2.0 */
/* Service fixtures only. None of these functions opens a device/network. */
int netlib_get_ipv4addr(const char *name,struct in_addr *out)
{(void)name;out->s_addr=htonl(0xc0a80158);return 0;}
const struct c6_desktop_backend g_c6_desktop_backend={0};
static int review_music_mode,review_chat_mode,review_weather_mode,review_ble_mode;
static int review_grok_mode=-1;
void review_grok_fixture(int mode){review_grok_mode=mode;review_chat_mode=mode?1:0;}
static void review_grok_chat(struct chat_snapshot *out){
  if(review_grok_mode>=0)out->revision=(unsigned)review_grok_mode+100;
  if(review_grok_mode>0&&review_grok_mode<4){out->phase=CHAT_REQUESTING;out->turns[0].state=CHAT_WAITING;strcpy(out->turns[0].progress,review_grok_mode==1?"正在思考":review_grok_mode==2?"正在接收回复":"正在编写应用草稿");if(review_grok_mode!=2)out->turns[0].reply[0]=0;}
  else if(review_grok_mode==5){out->turns[0].state=CHAT_FAILED;out->turns[0].error=CHAT_ERROR_NETWORK;}
  else if(review_grok_mode==6)out->turns[0].state=CHAT_CANCELLED;
  else if(review_grok_mode==7)out->count=0;
}
static unsigned review_music_position=42000,review_music_duration=204000,review_music_revision;
static const char *review_draft_source;
static uint32_t review_previewed_revision;
void review_qpk_fixture(const char *source){review_draft_source=source;review_previewed_revision=0;}
void review_music_fixture(int mode){review_music_mode=mode;review_music_position=42000;review_music_duration=204000;review_music_revision++;}
void review_music_progress_fixture(unsigned position,unsigned duration){review_music_position=position;review_music_duration=duration;review_music_revision++;}
void review_chat_fixture(int mode){review_chat_mode=mode;}
void glass_chat_activity_get(struct chat_activity_snapshot *out){memset(out,0,sizeof(*out));out->loaded=true;out->configured=true;out->phase=CHAT_IDLE;out->state=CHAT_DONE;if(review_chat_mode)out->turn_id=1;if(review_grok_mode>0&&review_grok_mode<4){out->phase=CHAT_REQUESTING;out->state=CHAT_WAITING;out->activity=review_grok_mode==1?CHAT_ACTIVITY_THINK:review_grok_mode==2?CHAT_ACTIVITY_REPLY:CHAT_ACTIVITY_TOOL;}else if(review_grok_mode==5)out->state=CHAT_FAILED;else if(review_grok_mode==6)out->state=CHAT_CANCELLED;else if(review_grok_mode==7)out->turn_id=0;}
void review_weather_fixture(int mode){review_weather_mode=mode;}
void review_ble_fixture(int mode){review_ble_mode=mode;}
void glass_music_get(struct music_snapshot *out){memset(out,0,sizeof(*out));out->initialized=true;out->volume=40;out->revision=review_music_revision;if(review_music_mode){out->favorite_count=3;const char *names[]={"午后的小调","城市与晚风","熟悉的旋律"};for(unsigned i=0;i<3;i++){snprintf(out->favorites[i].name,sizeof(out->favorites[i].name),"%s",names[i]);strcpy(out->favorites[i].artist,"审核示例歌手");strcpy(out->favorites[i].album,"离线示例专辑");}out->current=out->result=out->favorites[0];out->has_result=true;out->search_revision=1;out->state=review_music_mode==2?MUSIC_PLAYING:MUSIC_IDLE;out->position_ms=review_music_position;out->duration_ms=review_music_duration;}}
bool glass_chat_get(struct chat_snapshot *out){memset(out,0,sizeof(*out));out->loaded=true;out->phase=CHAT_IDLE;out->configured=true;out->revision=review_chat_mode+1;strcpy(out->model,"离线审核 · 服务示例");if(review_chat_mode){out->count=1;out->turns[0].id=1;out->turns[0].state=CHAT_DONE;strcpy(out->turns[0].prompt,"帮我安排今天的学习计划。");strcpy(out->turns[0].reply,"可以先完成一项最重要的任务，再留出时间整理笔记。\n\n上午：专注学习与练习。\n下午：回顾进度，补充问题。\n晚上：整理收获，为明天留下一条清晰的待办。\n\n以上内容仅用于离线页面审核。");out->turns[0].clipped=true;strcpy(out->turns[0].archive,"review-response");}review_grok_chat(out);return true;}
bool glass_chat_page_get(struct chat_page *out){memset(out,0,sizeof(*out));out->revision=1;out->pages=2;strcpy(out->text,"完整回复（离线审核示例）\n\n1. 选择今天最重要的一件事。\n2. 在一个专注时段里完成它。\n3. 安排短休息，再检查进度。\n4. 保存笔记，整理明天的待办。\n\n这个页面沿用原有分页与返回操作。");return true;}
size_t glass_chat_copy_utf8(char *out,size_t cap,const char *text){size_t n=strlen(text);if(n>=cap)n=cap-1;while(n&&(text[n]&0xc0)==0x80)n--;memcpy(out,text,n);out[n]=0;return n;}
void glass_weather_get(struct glass_weather_snapshot *out){memset(out,0,sizeof(*out));out->state=WEATHER_WAIT_NET;if(review_weather_mode){out->valid=true;out->state=WEATHER_READY;out->revision=2;out->data.temperature=24;out->data.humidity=48;out->data.aqi=32;out->data.level=1;out->data.pm25=12;strcpy(out->data.condition,"多云");strcpy(out->data.category,"良好");strcpy(out->data.location,"当地天气");strcpy(out->data.report_time,"2026-10-13 14:00 · 审核示例");}}
void glass_ble_read(struct glass_ble_state *out){memset(out,0,sizeof(*out));out->started=true;out->ready=true;if(review_ble_mode){out->count=2;strcpy(out->devices[0].name,"蓝牙键盘 · 审核示例");strcpy(out->devices[1].name,"温湿度传感器 · 审核示例");for(unsigned i=0;i<2;i++){out->devices[i].rssi=-38-i*10;out->devices[i].address[0]=2;out->devices[i].address[5]=i+1;}}}
void glass_ble_hid_read(struct glass_ble_hid_state *out){memset(out,0,sizeof(*out));}
int c6_desktop_worker_start(struct c6_desktop_worker *w,const struct c6_desktop_backend *b){memset(w,0,sizeof(*w));return 0;}
int c6_desktop_worker_read(struct c6_desktop_worker *w,struct c6_desktop_result *out){memset(out,0,sizeof(*out));out->sequence=1;out->operation=C6_DESKTOP_STATUS;return 0;}
/* The keyboard/IME UI is production code; dictionary/voice backends stay offline. */
bool glass_pinyin_candidate(unsigned index,char *out,size_t capacity){(void)index;if(capacity)out[0]=0;return false;}
void glass_voice_get(struct glass_voice_snapshot *out){memset(out,0,sizeof(*out));if(review_grok_mode==7){out->active=true;out->phase=GLASS_VOICE_LISTENING;}}
const char *glass_voice_error_text(enum glass_voice_error error){(void)error;return "离线审核服务";}
bool glass_qpk_removable(const char *path,const char *package){return path&&package&&!strcmp(package,"review.fixture");}
void hass_status(bool *configured,bool *busy,unsigned *clients){if(configured)*configured=false;if(busy)*busy=false;if(clients)*clients=0;}
int portal_session_open(char *code,size_t cap){snprintf(code,cap,"246810");return 0;}
unsigned portal_session_seconds(void){return 240;}
void glass_qpk_get(struct qpk_draft_info *out){memset(out,0,sizeof(*out));out->loaded=true;if(review_draft_source){out->revision=1;out->previewed_revision=review_previewed_revision;strcpy(out->name,"你好快应用（审核示例草稿）");strcpy(out->slug,"hello");}}
const char *glass_qpk_error_text(int error){(void)error;return "离线审核服务";}
const char *glass_chat_error_text(enum chat_error error){(void)error;return "离线审核服务";}
void *qpk_mjpeg_create(void){return NULL;}
struct glass_file_job {struct glass_file_result result;};
int glass_file_submit(const struct glass_file_request *request,struct glass_file_job **job){*job=calloc(1,sizeof(**job));if(!*job)return ENOMEM;struct glass_file_result *r=&(*job)->result;r->total_bytes=64ULL*1024*1024;r->free_bytes=40ULL*1024*1024;if(request->action==GLASS_FILE_LIST){r->count=4;strcpy(r->entries[0].name,"music");r->entries[0].directory=true;strcpy(r->entries[1].name,"recordings");r->entries[1].directory=true;strcpy(r->entries[2].name,"notes.txt");strcpy(r->entries[3].name,"review-video.mp4");}else{r->size=256;strcpy(r->preview,"离线审核文件\n\n文件详情、新建、重命名和删除确认均沿用现有代码。\n此文件内容为模拟服务数据，不访问真实磁盘。");}return 0;}
int glass_file_poll(struct glass_file_job *job,struct glass_file_status *status){memset(status,0,sizeof(*status));status->done=job!=NULL;return 0;}
const struct glass_file_result *glass_file_get_result(struct glass_file_job *job){return job?&job->result:NULL;}
void glass_file_release(struct glass_file_job *job){free(job);}
int glass_system_read(struct glass_system_monitor *monitor,struct glass_system_status *out){(void)monitor;memset(out,0,sizeof(*out));out->ready=true;out->sample.cpu_tenths=125;out->sample.heap_total=32ULL*1024*1024;out->sample.heap_free=22ULL*1024*1024;out->sample.heap_largest=14ULL*1024*1024;out->sample.heap_peak=13ULL*1024*1024;return 0;}
int glass_preferences_get_status(struct glass_preferences_status *out){memset(out,0,sizeof(*out));return 0;}
char *glass_qpk_preview_source(struct qpk_draft_info *out){glass_qpk_get(out);return review_draft_source?strdup(review_draft_source):NULL;}
void glass_qpk_preview_result(uint32_t revision,const char *error){if(!error[0])review_previewed_revision=revision;}
