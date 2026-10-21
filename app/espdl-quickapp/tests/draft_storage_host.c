/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include "glass_qpk_builder.c"

static void ok_write(unsigned revision)
{
  char json[256];
  snprintf(json,sizeof(json),"{\"name\":\"Preserved\",\"slug\":\"draft_test\",\"source\":\"ui.text('new');\",\"base_revision\":%u}",revision);
  char *text=glass_qpk_write_draft(json,NULL);assert(text);
  cJSON *result=cJSON_Parse(text);free(text);
  assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result,"ok")));cJSON_Delete(result);
}

int main(int argc,char **argv)
{
  assert(argc==2);
  const char *mode=argv[1];
  struct stat st;
  if(!strcmp(mode,"reload")) {
    assert(!glass_qpk_initialize());assert(info.revision==0&&!draft_source);
    assert(builder_volume==QPK_VOLUME_SD&&info.draft_volume==QPK_VOLUME_SD); /* SD tombstone wins over old internal draft. */
    puts("DRAFT_SD_RELOAD_OK");return 0;
  }
  if(!strcmp(mode,"corrupt")) {
    assert(glass_qpk_initialize()==-EBADMSG);assert(!info.loaded);
    puts("DRAFT_SD_CORRUPTION_NO_FALLBACK_OK");return 0;
  }
  char mount[256];strcpy(mount,QPK_VOLUME_INTERNAL_ROOT);*strrchr(mount,'/')=0;
  assert(!mkdir(mount,0700));assert(!mkdir(QPK_VOLUME_INTERNAL_ROOT,0700));
  if(!strcmp(mode,"no-sd")) {
    assert(!glass_qpk_initialize());assert(builder_volume==QPK_VOLUME_INTERNAL&&info.draft_volume==QPK_VOLUME_INTERNAL);
    ok_write(0);assert(!stat(LEGACY_DRAFT_DIR "/draft0.json",&st));
    puts("DRAFT_NO_SD_STILL_WORKS_OK");return 0;
  }
  strcpy(mount,QPK_VOLUME_SD_ROOT);*strrchr(mount,'/')=0;assert(!mkdir(mount,0700));
  assert(!mkdir(LEGACY_DRAFT_DIR,0700));
  struct draft_record legacy={.revision=3,.sequence=9,.source="ui.text('legacy');"};
  strcpy(legacy.name,"Preserved");strcpy(legacy.slug,"draft_test");
  char *packed=record_pack(&legacy);assert(packed);
  assert(!write_file(LEGACY_DRAFT_DIR "/draft0.json",packed,false));
  assert(!glass_qpk_initialize());assert(builder_volume==QPK_VOLUME_SD&&info.draft_volume==QPK_VOLUME_SD&&info.revision==3);
  assert(!strcmp(draft_source,legacy.source));
  assert(stat(QPK_BUILDER_ROOT "/.examples",&st)<0);
  char *examples=glass_qpk_list_examples();assert(examples&&strstr(examples,QPK_VOLUME_SD_ROOT));free(examples);
  assert(stat(QPK_BUILDER_ROOT "/.examples",&st)<0); /* No example seeding on Flash. */
  ok_write(3);assert(info.revision==4&&draft_sequence==10);
  assert(!stat(QPK_VOLUME_SD_ROOT "/.draft/draft0.json",&st));
  char *before=NULL;assert(!read_file(LEGACY_DRAFT_DIR "/draft0.json",SIZE_MAX-1,&before));
  assert(!strcmp(before,packed));free(before);
  char retired[280];snprintf(retired,sizeof(retired),"%s-retired",mount);
  assert(!rename(mount,retired));
  struct draft_record next={.revision=5,.sequence=11,.source="ui.text('must not reach Flash');"};
  strcpy(next.name,"Preserved");strcpy(next.slug,"draft_test");
  assert(persist_record(&next)<0);assert(info.revision==4);
  assert(!rename(retired,mount));
  assert(!glass_qpk_discard());
  assert(!read_file(LEGACY_DRAFT_DIR "/draft0.json",SIZE_MAX-1,&before));
  assert(!strcmp(before,packed));free(before);free(packed);
  puts("DRAFT_SD_STORAGE_OK: legacy read-only recovery; SD examples and dual-slot draft; removed card refuses Flash fallback; SD discard preserves legacy");
}
