#!/usr/bin/env python3
"""Exercise production live HTTP through the existing production shared TLS."""
import argparse
import os
from pathlib import Path
import subprocess

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--apps',type=Path,required=True)
    p.add_argument('--tls',type=Path,required=True)
    p.add_argument('--tls-build',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--decoder-objects',type=Path,required=True)
    p.add_argument('--room',default='12005649')
    p.add_argument('--platform',default='0',choices=['0','1','2'])
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    src=a.apps/'system/bilibili_live';desktop=a.apps/'system/desktop'
    stub=out/'include/nuttx';stub.mkdir(parents=True,exist_ok=True)
    (stub/'config.h').write_text('#pragma once\n#define FAR\n#define CODE\n#define CONFIG_NET_IPv4 1\n#define CONFIG_LIBC_NETDB 1\n')
    flags=['-D_GNU_SOURCE','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread',
           '-DMBEDTLS_THREADING_C','-DMBEDTLS_THREADING_PTHREAD']
    for path in [out/'include',src,desktop,a.apps/'include',a.apps/'netutils/cjson/cJSON',
                 a.tls/'include',a.tls/'tf-psa-crypto/include',a.tls/'tf-psa-crypto/drivers/builtin/include']:
        flags+=['-I'+str(path)]
    libs=[a.tls_build/'library/libmbedtls.a',a.tls_build/'library/libmbedx509.a',a.tls_build/'tf-psa-crypto/core/libtfpsacrypto.a']
    env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=1';env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
    def run(cmd):subprocess.run(list(map(str,cmd)),check=True,env=env)
    run(['cc',*flags,src/'tests/test_http.c',desktop/'glass_https.c',*libs,'-o',out/'http'])
    run([out/'http'])
    objs=[]
    for name in ['live_platform.c','bili_api.c','bili_flv.c','bili_http.c','bili_sps.c','tests/probe_live.c']:
        obj=out/(Path(name).stem+'.o');run(['cc',*flags,'-c',src/name,'-o',obj]);objs.append(obj)
    for source in [desktop/'glass_https.c',a.apps/'netutils/cjson/cJSON/cJSON.c']:
        obj=out/(source.stem+'.o');run(['cc',*flags,'-c',source,'-o',obj]);objs.append(obj)
    parser_objects=[o for o in objs if o.stem in ['live_platform','bili_api','bili_http','glass_https','cJSON']]
    run(['cc',*flags,src/'tests/test_platform.c',*parser_objects,*libs,'-lm','-o',out/'platform-parser'])
    run([out/'platform-parser'])
    # Same unchanged OpenH264 wrapper/vendor from the previously sanitized run.
    decoder=sorted(a.decoder_objects.glob('decoder-*.o'))
    if not decoder:raise RuntimeError('Run decoder host validation first')
    run(['g++','-fsanitize=address,undefined','-pthread',*objs,
         *[o for o in decoder if o.name!='decoder-1.o'],*libs,'-lm','-o',out/'probe'])
    run([out/'probe',a.room,a.platform])
    print('PASS production HTTP framing + real native live platform using existing glass_https')
if __name__=='__main__':main()
