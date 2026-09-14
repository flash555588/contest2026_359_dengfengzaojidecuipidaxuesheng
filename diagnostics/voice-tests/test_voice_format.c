/* Test actual WAV/multipart/response code, including binary zero bytes. */
#include "claw_voice.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_wav(void)
{
  size_t size = 44 + CLAW_VOICE_RATE * 2 * 15;
  unsigned char *wav = calloc(1, size), *body;
  size_t length;
  claw_voice_wav_header(wav, size - 44);
  assert(!memcmp(wav, "RIFF", 4) && !memcmp(wav+8,"WAVEfmt ",8));
  assert(wav[20]==1 && wav[22]==1 && wav[32]==2 && wav[34]==16);
  unsigned rate = wav[24] | wav[25]<<8 | wav[26]<<16 | wav[27]<<24;
  assert(rate==16000);
  wav[44] = 0; wav[45] = 0xff; wav[size-1] = 0x7f;
  assert(!claw_voice_multipart("test-transcriber",wav,size,&body,&length));
  assert(length > size && length < size + 800);
  unsigned char *start = memmem(body,length,"RIFF",4);
  assert(start && !memcmp(start,wav,size));
  assert(!memcmp(start+size,"\r\n--",4));
  const char field[] = "name=\"model\"\r\n\r\ntest-transcriber";
  assert(memmem(body,length,field,sizeof(field)-1));
  FILE *f = fopen("voice-test.wav","wb"); assert(f); assert(fwrite(wav,1,size,f)==size); fclose(f);
  f=fopen("voice-test.multipart","wb"); assert(f); assert(fwrite(body,1,length,f)==length); fclose(f);
  free(body);
  assert(claw_voice_multipart("x\r\nInjected: yes",wav,size,&body,&length)<0 && !body);
  assert(claw_voice_multipart("model",wav,size+1,&body,&length)<0);
  free(wav);
}

static void test_text(void)
{
  char text[4097];
  assert(!claw_voice_parse_transcript("{\"text\":\"  你好，打开相机  \"}",text,sizeof(text)));
  assert(!strcmp(text,"你好，打开相机"));
  const char *bad[]={"{}","[]","{\"text\":1}","{\"text\":\"   \"}",
    "{\"text\":\"x\",\"error\":{}}","{\"text\":\"x\"} garbage",
    "{\"text\":\"\xff\"}","{\"text\":\"\xc0\x80\"}"};
  for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++) {
    strcpy(text,"old"); assert(claw_voice_parse_transcript(bad[i],text,sizeof(text))<0); assert(!*text);
  }
  assert(!claw_voice_valid_text("\xed\xa0\x80",10));
  assert(!claw_voice_valid_text("\xf4\x90\x80\x80",10));
  assert(!claw_voice_valid_text("\xe4\xb8",10));
  assert(claw_voice_parse_transcript("{\"text\":\"too long\"}",text,4)<0);
  assert(claw_voice_valid_url("https://example.com/v1/audio/transcriptions"));
  const char *urls[]={"http://a","https://","https:///a","https://a@b/x", "https://a?q=x",
    "https://a/#x","https://a\\b","https://a\r\nAuthorization: x"};
  for(size_t i=0;i<sizeof(urls)/sizeof(*urls);i++) assert(!claw_voice_valid_url(urls[i]));
  /* Deterministic malformed inputs under ASan/UBSan. */
  unsigned seed=0x12345;
  for(int k=0;k<20000;k++) {
    char input[128];
    for(int j=0;j<127;j++) {seed=seed*1664525+1013904223;input[j]=(seed>>24)|1;}
    input[127]=0; claw_voice_parse_transcript(input,text,sizeof(text));
    claw_voice_valid_text(input,127);
  }
}
int main(void) { test_wav(); test_text(); puts("PASS: WAV, binary multipart, UTF-8, transcript and 20000 malformed inputs"); }
