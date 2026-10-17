#!/usr/bin/env python3
"""Check production ASR signing against Python HMAC using public dummy inputs."""
import argparse,base64,hashlib,hmac,subprocess
from pathlib import Path
from urllib.parse import urlsplit,parse_qs,unquote
from render_all_ui import function
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[1]
source=repo/'app/espdl-quickapp/overlay/apps/system/desktop/glass_voice.c'
text=source.read_text();a.output.mkdir(parents=True,exist_ok=True)
prefix=r'''
#include <openssl/evp.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define VOICE_HOST "asr.cloud.tencent.com"
#define MBEDTLS_MD_SHA1 1
typedef int mbedtls_md_info_t;
static const mbedtls_md_info_t *mbedtls_md_info_from_type(int type) {static int info;return type==1?&info:NULL;}
static int mbedtls_md(const mbedtls_md_info_t *info,const void *input,size_t n,unsigned char *out)
{(void)info;unsigned len=0;return EVP_Digest(input,n,out,&len,EVP_sha1(),NULL)&&len==20?0:-1;}
static int mbedtls_base64_encode(unsigned char *out,size_t capacity,size_t *used,const unsigned char *input,size_t n)
{if(capacity<4*((n+2)/3)+1)return -1;*used=EVP_EncodeBlock(out,input,(int)n);return 0;}
struct portal_voice_config {char app_id[33],secret_id[129],secret_key[129];};
static int random_bytes(unsigned char *out,size_t n){for(size_t i=0;i<n;i++)out[i]=(unsigned char)i;return 0;}
static time_t fixture_time(time_t *out){time_t value=1792227600;if(out)*out=value;return value;}
#define time fixture_time
'''
suffix=r'''
int main(void) {
 unsigned char digest[20];unsigned char key[100];
 for(unsigned n=0;n<3;n++){
  size_t size=n==0?20:n==1?4:100;
  memset(key,n==0?0x0b:n==1?'J':0xaa,size);
  if(hmac_sha1(key,size,"Hi There",8,digest))return 1;
  printf("HMAC%u=",n);for(unsigned i=0;i<20;i++)printf("%02x",digest[i]);puts("");
 }
 struct portal_voice_config config={.app_id="1250000000",.secret_id="AKIDEXAMPLE",.secret_key="public-test-key"};
 char path[1152],encoded[32];
 if(voice_path(&config,path,sizeof(path)))return 2;
 if(url_encode(encoded,sizeof(encoded),"+/="))return 3;
 if(strcmp(encoded,"%2B%2F%3D"))return 4;
 puts(path);return 0;
}
'''
test=a.output/'voice-auth-host.c'
test.write_text(prefix+'\n'.join(function(text,name) for name in ['url_encode','hmac_sha1','voice_path'])+suffix)
executable=a.output/'voice-auth-host'
subprocess.run(['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
    str(test),'-l:libcrypto.so.3','-o',str(executable)],check=True)
lines=subprocess.check_output([str(executable)],text=True).splitlines()
for n,key in enumerate([b'\x0b'*20,b'JJJJ',b'\xaa'*100]):
    assert lines[n]==f'HMAC{n}='+hmac.new(key,b'Hi There',hashlib.sha1).hexdigest()
path=lines[3];unsigned,signature=path.rsplit('&signature=',1)
expected=base64.b64encode(hmac.new(b'public-test-key',('asr.cloud.tencent.com'+unsigned).encode(),hashlib.sha1).digest()).decode()
assert unquote(signature)==expected
params=parse_qs(urlsplit('https://asr.cloud.tencent.com'+unsigned).query)
assert list(params)==sorted(params)
assert int(params['expired'][0])-int(params['timestamp'][0])==86400
assert params['engine_model_type']==['16k_zh'] and params['voice_format']==['1']
print('VOICE_AUTH_HOST_OK: production HMAC short/long keys, sorted signed URL, timestamp/expiry, Base64 and signature URL encoding; dummy credentials only')
print('source_sha256='+hashlib.sha256(source.read_bytes()).hexdigest())
