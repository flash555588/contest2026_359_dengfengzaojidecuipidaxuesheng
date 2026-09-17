/* SPDX-License-Identifier: Apache-2.0 */
#include "qpk_espdl.h"
#include "espdl_assets.h"
#include <errno.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
extern int esp_spiflash_read(uint32_t address, void *buffer, uint32_t length);

static FILE *asset_open(unsigned index)
{
  char path[128];
  snprintf(path,sizeof(path),"/sdcard/espdl/%s",g_qpk_dl_assets[index].file);
  return fopen(path,"rb");
}
int qpk_dl_model_available(unsigned index)
{
  if (index>=QPK_DL_ASSET_COUNT) return -EINVAL;
  FILE *f=asset_open(index);
  uint8_t header[4];
  int result;
  if (f) { result=fread(header,1,4,f)==4?0:-EIO; fclose(f); }
  else result=esp_spiflash_read(QPK_DL_FLASH_BASE+g_qpk_dl_assets[index].offset,header,4);
  if (result<0) return result;
  return memcmp(header,"EDL2",4)==0?0:-ENOENT;
}
int qpk_dl_model_load(unsigned index, void **data, size_t *length)
{
  if (!data || !length || index>=QPK_DL_ASSET_COUNT) return -EINVAL;
  *data=NULL; *length=0;
  const struct qpk_dl_model_asset *asset=&g_qpk_dl_assets[index];
  if (asset->offset>QPK_DL_FLASH_SIZE || asset->bytes>QPK_DL_FLASH_SIZE-asset->offset) return -EFBIG;
  uint8_t *buffer=memalign(64,asset->bytes);
  if (!buffer) return -ENOMEM;
  FILE *f=asset_open(index);
  int ret=0;
  if (f) {
    struct stat st;
    if (fstat(fileno(f),&st)<0 || st.st_size!=asset->bytes) ret=-EBADMSG;
  }
  for (size_t offset=0; !ret && offset<asset->bytes;) {
    if (qpk_dl_should_cancel()) { ret=-ECANCELED;break; }
    size_t n=asset->bytes-offset;
    if (n>4096) n=4096;
    if (f) { if (fread(buffer+offset,1,n,f)!=n) ret=-EIO; }
    else ret=esp_spiflash_read(QPK_DL_FLASH_BASE+asset->offset+offset,buffer+offset,n);
    offset+=n;
  }
  if (f) fclose(f);
  if (!ret) {
    uint8_t digest[32];
    qpk_dl_sha256(buffer,asset->bytes,digest);
    if (memcmp(digest,asset->sha256,32)) ret=-EBADMSG;
  }
  if (!ret && qpk_dl_should_cancel()) ret=-ECANCELED;
  if (ret) { free(buffer);return ret; }
  *data=buffer;*length=asset->bytes;
  return 0;
}
void qpk_dl_model_free(void *data) { free(data); }
