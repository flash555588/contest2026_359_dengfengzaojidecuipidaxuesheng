/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
static unsigned comparisons;
static int counted_compare(const char *a,const char *b){comparisons++;return strcasecmp(a,b);}
#define strcasecmp counted_compare
#include "glass_file_types.h"
#undef strcasecmp
static unsigned old_comparisons,new_comparisons,max_comparisons;
static const struct files_design *linear(const char *name)
{
  static const struct files_design unknown={"",FILES_ICON_GENERIC,"其他文件"};
  const char *base=strrchr(name,'/');base=base?base+1:name;
  const char *ext=strrchr(base,'.');
  if(ext)for(size_t i=0;i<sizeof(files_designs)/sizeof(files_designs[0]);i++)
    {old_comparisons++;if(!strcasecmp(ext,files_designs[i].extension))return &files_designs[i];}
  return &unknown;
}
static void check(const char *name)
{
  const struct files_design *expected=linear(name);comparisons=0;
  const struct files_design *actual=files_design(name);
  assert(actual->kind==expected->kind&&!strcmp(actual->label,expected->label));
  assert(comparisons<=7);new_comparisons+=comparisons;
  if(comparisons>max_comparisons)max_comparisons=comparisons;
}
int main(void)
{
  size_t count=sizeof(files_designs)/sizeof(files_designs[0]);assert(count==92);
  for(size_t i=0;i<count;i++){
    if(i)assert(strcasecmp(files_designs[i-1].extension,files_designs[i].extension)<0);
    char name[512];snprintf(name,sizeof(name),"/data/folder.with.dot/中文文件%s",files_designs[i].extension);check(name);
    for(char *p=strrchr(name,'.');*p;p++)if(*p>='a'&&*p<='z')*p-=32;
    check(name);
  }
  const char *cases[]={"","no-extension","/data.dir/plain","/sdcard/.gitignore","a.","x.PnG","x.jpeg.backup",".unknown","/sdcard/a.b/长文件名","x.c++"};
  for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++)check(cases[i]);
  for(unsigned i=0;i<20000;i++){
    char name[512];snprintf(name,sizeof(name),"/data/path.with.dot/file-%u.%c%c%c%c%u",i,'a'+i%26,'a'+(i/7)%26,'a'+(i/13)%26,'a'+(i/17)%26,i%10);check(name);
  }
  printf("FILE_TYPES_OK: 92 mappings, mixed case, paths, hidden/unknown extensions and 20000 generated names; max=%u comparisons; linear=%u binary=%u\n",max_comparisons,old_comparisons,new_comparisons);
}
