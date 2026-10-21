/* SPDX-License-Identifier: Apache-2.0 */
#define QPK_VOLUME_SD_ROOT TEST_ROOT "/sd/qpk"
#define QPK_VOLUME_INTERNAL_ROOT TEST_ROOT "/data/qpk"
#define QPK_PHOTO_SD_ROOT TEST_ROOT "/sd/photos"
#define QPK_PHOTO_INTERNAL_ROOT TEST_ROOT "/data/photos"
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include "qpk_photo_storage.h"

static int sd=1;
static uint64_t sd_free=8*1024*1024,internal_free=139*1024;
int statfs(const char *path,struct statfs *fs)
{
  memset(fs,0,sizeof(*fs));fs->f_bsize=1;
  if(strstr(path,"/sd")){
    if(sd<0){errno=-sd;return -1;}
    fs->f_type=sd?MSDOS_SUPER_MAGIC:0;fs->f_bavail=sd_free;
  }else{fs->f_type=SMARTFS_MAGIC;fs->f_bavail=internal_free;}
  return 0;
}
int main(void)
{
  char path[128];struct stat st;
  assert(mkdir(TEST_ROOT "/sd",0755)==0);
  assert(mkdir(TEST_ROOT "/data",0755)==0);
  const size_t bytes=66+512*300*2;
  assert(qpk_photo_directory(NULL,sizeof(path),bytes,NULL)==-EINVAL);
  assert(qpk_photo_directory(path,8,bytes,NULL)==-ENAMETOOLONG);
  assert(qpk_photo_directory(path,sizeof(path),SIZE_MAX,NULL)==-EFBIG);
  assert(qpk_photo_directory(path,sizeof(path),bytes,NULL)==0);
  assert(!strcmp(path,QPK_PHOTO_SD_ROOT));
  assert(lstat(QPK_PHOTO_INTERNAL_ROOT,&st)==-1 && errno==ENOENT);
  sd_free=bytes+65535;
  assert(qpk_photo_directory(path,sizeof(path),bytes,NULL)==-ENOSPC);
  assert(lstat(QPK_PHOTO_INTERNAL_ROOT,&st)==-1 && errno==ENOENT);
  sd_free=bytes+65536;
  assert(qpk_photo_directory(path,sizeof(path),bytes,NULL)==0);
  sd=0;
  assert(qpk_photo_directory(path,sizeof(path),bytes,NULL)==-ENOSPC);
  internal_free=2*1024*1024;
  assert(qpk_photo_directory(path,sizeof(path),bytes,NULL)==0);
  assert(!strcmp(path,QPK_PHOTO_INTERNAL_ROOT));
  FILE *photo=fopen(QPK_PHOTO_INTERNAL_ROOT "/existing.bmp","wb");assert(photo);fputs("keep",photo);fclose(photo);
  sd=-EIO;
  assert(qpk_photo_directory(path,sizeof(path),bytes,NULL)==-EIO);
  sd=1;assert(rmdir(QPK_PHOTO_SD_ROOT)==0);
  assert(symlink(QPK_PHOTO_INTERNAL_ROOT,QPK_PHOTO_SD_ROOT)==0);
  assert(qpk_photo_directory(path,sizeof(path),bytes,NULL)==-EPERM);
  assert(lstat(QPK_PHOTO_INTERNAL_ROOT "/existing.bmp",&st)==0 && st.st_size==4);
  assert(unlink(QPK_PHOTO_SD_ROOT)==0);
  assert(rmdir(TEST_ROOT "/sd")==0);
  assert(symlink(TEST_ROOT "/data",TEST_ROOT "/sd")==0);
  assert(qpk_photo_directory(path,sizeof(path),bytes,NULL)==-EPERM);
  puts("PHOTO_STORAGE_OK: mounted SD preferred, reserve checked, no full/broken SD spill, low Flash rejected, existing photos retained, symlinks rejected");
  return 0;
}
