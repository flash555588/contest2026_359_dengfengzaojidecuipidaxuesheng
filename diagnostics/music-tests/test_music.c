#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "glass_music.h"
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#include "music_vendor/minimp3.h"

int main(int argc,char **argv)
{
  struct music_song song;
  char url[2048];
  assert(music_search_url("晴天 &?", "key &?",url,sizeof(url))==0);
  assert(strstr(url,"apiKey=key%20%26%3F&name=%E6%99%B4%E5%A4%A9%20%26%3F"));
  assert(music_search_url("晴天","k",url,12)<0);
  const char *valid="{\"code\":1,\"name\":\"晴天\",\"artist\":\"周杰伦\",\"album\":\"叶惠美\",\"music_url\":\"https://example.com/song.mp3\"}";
  assert(music_song_parse(valid,strlen(valid),"晴天",&song)==0);
  assert(!strcmp(song.name,"晴天") && !strcmp(song.artist,"周杰伦"));
  assert(music_song_parse(valid,strlen(valid)-1,"x",&song)<0);
  const char *bad[]={"{}","[]","{\"code\":0}","{\"code\":1,\"name\":2}","{\"code\":1,\"name\":\"x\",\"music_url\":\"http://example.com\"}","{\"code\":1} garbage"};
  for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) assert(music_song_parse(bad[i],strlen(bad[i]),"x",&song)<0);
  char nesting[200]; memset(nesting,'[',100); memset(nesting+100,']',100);
  assert(music_song_parse(nesting,sizeof(nesting),"x",&song)<0);
  uint32_t seed=1; char fuzz[512];
  for(int i=0;i<10000;i++) { for(unsigned j=0;j<sizeof(fuzz);j++){seed=seed*1664525+1013904223;fuzz[j]=seed>>24;} music_song_parse(fuzz,sizeof(fuzz),"x",&song); }
  puts("PASS: JSON bounds, invalid/truncated/deep input, UTF-8 query escaping, 10000 malformed responses");
  assert(argc==3);
  FILE *in=fopen(argv[1],"rb"),*out=fopen(argv[2],"wb"); assert(in&&out);
  unsigned char *input=malloc(262144); size_t n=fread(input,1,262144,in),offset=0;
  mp3dec_t *decoder=calloc(1,sizeof(*decoder)); mp3dec_init(decoder);
  mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME]; mp3dec_frame_info_t info;
  unsigned frames=0,rate=0,channels=0; uint64_t energy=0;
  while(offset<n) {
    int samples=mp3dec_decode_frame(decoder,input+offset,n-offset,pcm,&info);
    if(!info.frame_bytes) break;
    offset+=info.frame_bytes;
    if(samples) {
      if(rate) assert(rate==info.hz && channels==info.channels);
      rate=info.hz;channels=info.channels;frames+=samples;
      for(int i=0;i<samples*info.channels;i++) energy+=(int64_t)pcm[i]*pcm[i];
      fwrite(pcm,2,samples*info.channels,out);
    }
  }
  assert(frames>rate && energy>0);
  printf("PASS: real MP3 decoded rate=%u channels=%u frames=%u energy=%llu\n",rate,channels,frames,(unsigned long long)energy);
  free(input);free(decoder);fclose(in);fclose(out);
}
