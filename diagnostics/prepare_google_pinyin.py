"""Vendor Apache-2.0 libgooglepinyin and build its frequency dictionary in WSL."""
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
ws=Path(__file__).resolve().parent.parent
original=ws/'diagnostics/music-reference/libgooglepinyin-0.1.2'
desktop=ws/'04-v3-20260913/espdl-quickapp/overlay/apps/system/desktop'
vendor=desktop/'music_vendor/googlepinyin'
for directory in ('include','src'):
    (vendor/directory).mkdir(parents=True,exist_ok=True)
    for path in (original/directory).glob('*'):
        if path.suffix in ('.h','.cpp'): shutil.copyfile(path,vendor/directory/path.name)
# Use the same 32-bit little-endian dictionary on the 64-bit builder and P4.
header=vendor/'include/dictdef.h'
s=header.read_text().replace('#define ___BUILD_MODEL___','#ifdef GOOGLE_PINYIN_BUILD_DICT\n#define ___BUILD_MODEL___\n#endif')
s=s.replace('size_t son_1st_off;', 'uint32 son_1st_off;').replace('size_t homo_idx_buf_off;', 'uint32 homo_idx_buf_off;')
header.write_text(s)
(vendor/'include/dict_io.h').write_text('''/* Local portability adapter, Apache-2.0. Fixed-width dictionary counts. */
#pragma once
#include <stdio.h>
#include <stdint.h>
static size_t gp_read_sizes(size_t *out, size_t count, FILE *fp) {
  for(size_t i=0;i<count;i++) { unsigned char b[4];
    if(fread(b,1,4,fp)!=4) return i;
    out[i]=(uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);
  } return count;
}
static size_t gp_write_sizes(const size_t *in, size_t count, FILE *fp) {
  for(size_t i=0;i<count;i++) { if(in[i]>UINT32_MAX) return i;
    uint32_t n=in[i]; unsigned char b[4]={(unsigned char)n,(unsigned char)(n>>8),(unsigned char)(n>>16),(unsigned char)(n>>24)};
    if(fwrite(b,1,4,fp)!=4) return i;
  } return count;
}
''')
for name in ('dicttrie','dictlist','ngram','spellingtrie'):
    path=vendor/'src'/f'{name}.cpp'; s=path.read_text()
    for op in ('read','write'):
        s=re.sub(r'f'+op+r'\(([^,]+), sizeof\(size_t\), ([^,]+), fp\)',r'gp_'+op+r'_sizes(\1, \2, fp)',s)
    s='#include "../include/dict_io.h"\n'+s
    if name=='dicttrie':
        s='extern "C" {\n#include <stdio.h>\nFILE *glass_pinyin_dictionary_open(void);\n}\n'+s
        s=s.replace('FILE *fp = fopen(filename, "rb");','FILE *fp = !strcmp(filename, "@builtin") ? glass_pinyin_dictionary_open() : fopen(filename, "rb");')
    path.write_text(s)
# Upstream 0.1.2 omitted two owned allocations from teardown.
path=vendor/'src/dicttrie.cpp'; s=path.read_text()
duplicate='''  if (NULL != nodes_ge1_)
    free(nodes_ge1_);
  nodes_ge1_ = NULL;

  if (NULL != nodes_ge1_)
    free(nodes_ge1_);
  nodes_ge1_ = NULL;'''
s=s.replace(duplicate,'''  if (NULL != nodes_ge1_) free(nodes_ge1_);
  nodes_ge1_ = NULL;
  if (NULL != lma_idx_buf_) free(lma_idx_buf_);
  lma_idx_buf_ = NULL;'''); path.write_text(s)
path=vendor/'src/userdict.cpp'; s=path.read_text()
s=s.replace('int UserDict::fuzzy_compare_spell_id(', 'int32 UserDict::fuzzy_compare_spell_id(')
s=s.replace('#ifdef ___SYNC_ENABLED___\n  syncs_ = NULL;', '#ifdef ___SYNC_ENABLED___\n  free(syncs_);\n  syncs_ = NULL;'); path.write_text(s)
shutil.copyfile(Path('/tmp/v3-desktop-espdl-20260915/nuttx/LICENSE'),vendor/'LICENSE')
build=Path('/tmp/google-pinyin-build'); build.mkdir(exist_ok=True)
shim=build/'shim.cpp'; shim.write_text('#include <stdio.h>\nextern "C" FILE *glass_pinyin_dictionary_open(void) { return NULL; }\n')
subprocess.run(['g++','-O2','-std=c++11','-DGOOGLE_PINYIN_BUILD_DICT',
    *map(str,(vendor/'src').glob('*.cpp')),str(original/'tools/pinyinime_dictbuilder.cpp'),str(shim),
    '-lpthread','-o',str(build/'dictbuilder')],check=True)
dictionary=vendor/'dict_pinyin.dat'
with (build/'dictionary-build.log').open('w') as log:
    subprocess.run([str(build/'dictbuilder'),str(original/'data/rawdict_utf16_65105_freq.txt'),
        str(original/'data/valid_utf16.txt'),str(dictionary)],stdout=log,stderr=log,check=True)
data=dictionary.read_bytes()
resource='/* Generated Google Pinyin frequency dictionary. Apache-2.0. */\nconst unsigned char glass_pinyin_dictionary[] = {\n'
resource+='\n'.join(','.join(str(b) for b in data[i:i+32])+',' for i in range(0,len(data),32))
resource+='\n};\nconst unsigned int glass_pinyin_dictionary_size = sizeof(glass_pinyin_dictionary);\n'
(desktop/'glass_pinyin_resource.c').write_text(resource)
manifest={'project':'libgooglepinyin','version':'0.1.2','license':'Apache-2.0',
 'source':'https://deb.debian.org/debian/pool/main/libg/libgooglepinyin/libgooglepinyin_0.1.2.orig.tar.bz2',
 'source_sha256':hashlib.sha256((ws/'diagnostics/downloads/libgooglepinyin-0.1.2.tar.bz2').read_bytes()).hexdigest(),
 'dictionary_bytes':len(data),'dictionary_sha256':hashlib.sha256(data).hexdigest(),
 'adaptations':['Fixed-width little-endian dictionary counts and node offsets','Read system dictionary through fmemopen','Disable dictionary builder code in firmware','Free dictionary index and user sync buffers on teardown']}
(vendor/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest,indent=2))
