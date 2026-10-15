/* SPDX-License-Identifier: Apache-2.0 */
/* Real service workers, HTTP/codec boundary mocked; never contacts a device. */
#include <assert.h>
#include <stdatomic.h>
#include "music_service_under_test.c"

static const char *mp3_path;
static atomic_bool hold_lyrics,lyrics_entered;
static atomic_uint opens,closes;

int portal_music_key(char *out,size_t cap)
{snprintf(out,cap,"offline-fixture");return 0;}
int qpk_storage_read(const char *root,const char *package,const char *key,char *out,size_t cap,size_t *size)
{(void)root;(void)package;(void)key;(void)out;(void)cap;*size=0;return -ENOENT;}
int qpk_storage_write(const char *root,const char *package,const char *key,const char *data,size_t size)
{(void)root;(void)package;(void)key;(void)data;(void)size;return 0;}

int music_http_get(const char *url,music_data_cb sink,void *ctx,music_cancel_cb cancel,unsigned *status)
{
  if(status)*status=200;
  if(strstr(url,"jkapi.com")) {
    const char *name=strstr(url,"&name=");assert(name);name+=6;
    if(!strcmp(name,"Missing"))return -EIO;
    char json[1024];snprintf(json,sizeof(json),"{\"code\":1,\"name\":\"%s\",\"artist\":\"Demo\",\"album\":\"Test\",\"music_url\":\"https://fixture/audio\"}",name);
    return sink(ctx,(const unsigned char *)json,strlen(json));
  }
  if(strstr(url,"api.lrc.cx")) {
    const char *name=strstr(url,"title=");assert(name);name+=6;
    char title[128];size_t n=strcspn(name,"&");assert(n<sizeof(title));memcpy(title,name,n);title[n]=0;
    if(!strcmp(title,"NoLyrics"))return -ENOENT;
    if(!strcmp(title,"A") && atomic_load(&hold_lyrics)) {
      atomic_store(&lyrics_entered,true);
      /* Deliberately complete a stale response despite cancellation. */
      while(atomic_load(&hold_lyrics))usleep(1000);
    }
    char json[2048];snprintf(json,sizeof(json),"[{\"title\":\"%s\",\"artist\":\"Wrong Artist\",\"lyrics\":\"[00:00]WRONG\"},{\"title\":\"%s\",\"artist\":\"Demo\",\"lyrics\":\"[00:00]%s intro\\n[00:00.50]%s current\\n[00:01.00]%s next\"}]",title,title,title,title,title);
    cJSON *metadata=cJSON_Parse(json);assert(metadata);
    cJSON_AddStringToObject(cJSON_GetArrayItem(metadata,0),"cover","https://fixture/wrong-artist.jpg");
    cJSON_AddStringToObject(cJSON_GetArrayItem(metadata,1),"cover_format","https://fixture/{w}x{h}.jpg");
    if(!strcmp(title,"CoverOnly"))cJSON_DeleteItemFromObjectCaseSensitive(cJSON_GetArrayItem(metadata,1),"lyrics");
    char *body=cJSON_PrintUnformatted(metadata);assert(body);
    int ret=sink(ctx,(const unsigned char *)body,strlen(body));cJSON_free(body);cJSON_Delete(metadata);return ret;
  }
  assert(!strcmp(url,"https://fixture/audio"));
  FILE *f=fopen(mp3_path,"rb");assert(f);unsigned char bytes[4096];int ret=0;size_t n;
  while((n=fread(bytes,1,sizeof(bytes),f))) {
    if(cancel(ctx)){ret=-ECANCELED;break;}
    ret=sink(ctx,bytes,n);if(ret)break;
  }
  fclose(f);return ret;
}

int music_pcm_open(struct music_pcm *pcm,unsigned rate,unsigned volume)
{(void)volume;pcm->fd=3;pcm->rate=rate;atomic_fetch_add(&opens,1);return 0;}
int music_pcm_write(struct music_pcm *pcm,const int16_t *samples,unsigned frames,unsigned channels)
{assert(samples && (channels==1||channels==2));usleep(5000);pcm->played_samples+=frames;return 0;}
int music_pcm_pause(struct music_pcm *pcm,bool pause){(void)pcm;(void)pause;return 0;}
int music_pcm_volume(struct music_pcm *pcm,unsigned volume){(void)pcm;assert(volume<=100);return 0;}
int music_pcm_drain(struct music_pcm *pcm){(void)pcm;return 0;}
void music_pcm_close(struct music_pcm *pcm){if(pcm->fd>=0)atomic_fetch_add(&closes,1);pcm->fd=-1;}

static struct music_now_playing wait_for(const char *name,enum music_state phase,enum music_lyrics_state lyric)
{
  struct music_now_playing s;
  for(unsigned i=0;i<3000;i++) {
    glass_music_now_playing(&s);
    if(!strcmp(s.name,name) && s.state==phase && s.lyrics_state==lyric)return s;
    usleep(1000);
  }
  fprintf(stderr,"timeout %s: state=%d lyric=%d\n",name,s.state,s.lyrics_state);abort();
}
static void play(const char *name)
{struct music_song song={0};snprintf(song.name,sizeof(song.name),"%s",name);assert(!glass_music_play(&song));}

static void parser_checks(void)
{
  struct music_lrc *lrc=calloc(1,sizeof(*lrc));assert(lrc);
  const char *sample="\xef\xbb\xbf[offset:-250]\r\n[00:02.000]最后一句\r\n[00:00.50][00:01.5]重复一句\n[00:01.75]\n";
  assert(!music_lrc_parse(sample,strlen(sample),lrc));assert(lrc->count==4);
  assert(lrc->lines[0].ms==250 && lrc->lines[1].ms==1250 && lrc->lines[2].ms==1500 && lrc->lines[3].ms==1750);
  assert(music_lrc_index(lrc,249)==-1 && music_lrc_index(lrc,250)==0 && music_lrc_index(lrc,1750)==3);
  assert(!strcmp(lrc->lines[3].text,"最后一句"));
  sample="[00:00.001]one\n[00:01.01]two\n[offset:+100]\n";
  assert(!music_lrc_parse(sample,strlen(sample),lrc));assert(lrc->lines[0].ms==101 && lrc->lines[1].ms==1110);
  assert(music_lrc_parse("<html>error</html>",18,lrc)==-ENODATA);
  assert(music_lrc_parse("[00:60]bad",10,lrc)==-ENODATA);
  assert(music_lrc_parse("[00:00]\xc0\xaf",9,lrc)==-EINVAL);
  assert(music_lrc_parse("[00:00]\0bad",11,lrc)==-EINVAL);
  char *large=calloc(1,30000);assert(large);
  strcpy(large,"[00:00]");for(unsigned i=0;i<300;i++)strcat(large,"中");
  assert(!music_lrc_parse(large,strlen(large),lrc));assert(music_lrc_utf8(lrc->lines[0].text,strlen(lrc->lines[0].text)));
  large[0]=0;for(unsigned i=0;i<193;i++)strcat(large,"[00:00]line\n");assert(music_lrc_parse(large,strlen(large),lrc)==-E2BIG);
  assert(music_lyrics_identity(" Demo ","demo"));assert(!music_lyrics_identity("", " "));assert(!music_lyrics_identity("Singer","Other"));
  struct music_song song={0};strcpy(song.name,"中文 &?");strcpy(song.artist,"A/B");char url[2048];assert(!music_lyrics_url(&song,url,sizeof(url)));
  assert(strstr(url,"%E4%B8%AD%E6%96%87%20%26%3F") && strstr(url,"A%2FB"));assert(music_lyrics_url(&song,url,8)==-E2BIG);
  unsigned seed=1;for(unsigned i=0;i<1000;i++){size_t n=i%255+1;for(size_t j=0;j<n;j++){seed=seed*1664525+1013904223;large[j]=seed>>24;}music_lrc_parse(large,n,lrc);}
  free(large);free(lrc);puts("LRC_PARSER_OK: timing, offsets, multiple tags, UTF-8, invalid/oversized input, identity, URL encoding and fuzz");
}

static void stream_checks(void)
{
  struct music_song song={0};strcpy(song.name,"Match");strcpy(song.artist,"Demo");
  struct music_lrc *lrc=calloc(1,sizeof(*lrc));assert(lrc);
  char cover[1024]={0};struct lyric_transfer t={.song=&song,.out=lrc,.cover=cover};t.transfer.body=malloc(LYRIC_OBJECT_MAX+1);assert(t.transfer.body);
  const char *json="[{\"title\":\"Match\",\"artist\":\"Other\",\"lrc\":\"[00:00]wrong\"},{\"extra\":\"quoted \\\" brace } and [\",\"title\":\"Match\",\"artist\":\"Demo\",\"lrc\":\"[00:00.1]right\"}]";
  int ret=0;for(size_t i=0;json[i]&&!ret;i++)ret=collect_lyrics(&t,(const unsigned char *)json+i,1);
  assert(ret==-EALREADY && t.found && lrc->lines[0].ms==100 && !strcmp(lrc->lines[0].text,"right"));
  cJSON *item=cJSON_Parse("{\"cover_format\":\"https://fixture/{w}x{h}.jpg\"}");assert(item);
  music_metadata_cover(item,cover);assert(!strcmp(cover,"https://fixture/320x320.jpg"));cJSON_Delete(item);
  item=cJSON_Parse("{\"cover\":\"http://fixture/unsafe.jpg\"}");assert(item);music_metadata_cover(item,cover);assert(!cover[0]);cJSON_Delete(item);
  free(t.transfer.body);free(lrc);puts("LYRICS_STREAM_OK: one-byte chunk boundaries, quoted braces/escapes, lrc/lyrics schema and artist identity");
}

static int live_response(const char *title,const char *artist)
{
  struct music_song song={0};snprintf(song.name,sizeof(song.name),"%s",title);snprintf(song.artist,sizeof(song.artist),"%s",artist);
  struct music_lrc *lrc=calloc(1,sizeof(*lrc));assert(lrc);
  char cover[1024]={0};struct lyric_transfer t={.song=&song,.out=lrc,.cover=cover};t.transfer.body=malloc(LYRIC_OBJECT_MAX+1);assert(t.transfer.body);
  unsigned char bytes[137];size_t n;int ret=0;
  while(!ret && (n=fread(bytes,1,sizeof(bytes),stdin)))ret=collect_lyrics(&t,bytes,n);
  assert(ret==-EALREADY && t.found && lrc->count>0);
  assert(!strncmp(cover,"https://",8));
  printf("LIVE_LYRIC_PARSE_OK: exact song/artist, %u candidates, %u timed lines, %zu response bytes consumed; lyric text not retained\n",t.candidates,lrc->count,t.total);
  free(t.transfer.body);free(lrc);return 0;
}

int main(int argc,char **argv)
{
  if(argc==4 && !strcmp(argv[1],"--lyrics-response"))return live_response(argv[2],argv[3]);
  assert(argc==2);mp3_path=argv[1];parser_checks();stream_checks();
  glass_music_start();glass_music_start();play("B");
  struct music_now_playing s=wait_for("B",MUSIC_PLAYING,MUSIC_LYRICS_READY);
  while(s.position_ms<600){usleep(5000);glass_music_now_playing(&s);}
  assert(!strcmp(s.lyrics[1],"B current"));
  assert(!strcmp(s.cover_url,"https://fixture/320x320.jpg"));
  glass_music_toggle();usleep(30000);glass_music_now_playing(&s);assert(s.state==MUSIC_PAUSED);
  unsigned position=s.position_ms;usleep(60000);glass_music_now_playing(&s);assert(s.position_ms==position);
  glass_music_toggle();wait_for("B",MUSIC_PLAYING,MUSIC_LYRICS_READY);
  /* Real workers keep advancing with no LVGL/music page alive. */
  usleep(50000);glass_music_now_playing(&s);assert(s.position_ms>position);
  atomic_store(&hold_lyrics,true);play("A");
  for(unsigned i=0;!atomic_load(&lyrics_entered)&&i<3000;i++)usleep(1000);
  assert(atomic_load(&lyrics_entered));play("C");glass_music_now_playing(&s);
  assert(!s.lyrics[1][0] && !s.cover_url[0] && s.state==MUSIC_LOADING);atomic_store(&hold_lyrics,false);
  s=wait_for("C",MUSIC_PLAYING,MUSIC_LYRICS_READY);assert(strstr(s.lyrics[1],"C ") && !strstr(s.lyrics[1],"A "));
  struct music_song favorite={0};strcpy(favorite.name,"B");strcpy(favorite.artist,"Demo");strcpy(favorite.query,"B");assert(!glass_music_favorite(&favorite));
  for(unsigned i=0;i<3000;i++){glass_music_now_playing(&s);if(s.can_skip)break;usleep(1000);}assert(s.can_skip);
  assert(!glass_music_skip(1));wait_for("B",MUSIC_PLAYING,MUSIC_LYRICS_READY);assert(glass_music_skip(0)==-EINVAL);
  play("CoverOnly");s=wait_for("CoverOnly",MUSIC_PLAYING,MUSIC_LYRICS_MISSING);assert(s.cover_url[0]);
  play("NoLyrics");wait_for("NoLyrics",MUSIC_PLAYING,MUSIC_LYRICS_MISSING);
  play("Missing");wait_for("Missing",MUSIC_ERROR,MUSIC_LYRICS_NONE);
  assert(!glass_music_stop_wait(3000));glass_music_now_playing(&s);assert(s.state==MUSIC_STOPPED && !s.lyrics[1][0]);
  assert(atomic_load(&opens)>2 && atomic_load(&opens)==atomic_load(&closes));
  printf("MUSIC_BACKGROUND_SERVICE_OK: real MP3 decoder/workers, pause freezes clock, resume, stale lyric cancellation, skip, missing lyrics, network failure and %u/%u PCM teardown\n",atomic_load(&opens),atomic_load(&closes));
  return 0;
}
