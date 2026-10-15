/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#ifdef __NuttX__
#include <nuttx/config.h>
#endif
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef __NuttX__
#include <sys/statfs.h>
#endif
#ifndef QPK_VOLUME_INTERNAL_ROOT
#ifdef QPK_BUILDER_ROOT
#define QPK_VOLUME_INTERNAL_ROOT QPK_BUILDER_ROOT
#elif defined(PORTAL_DATA_ROOT)
#define QPK_VOLUME_INTERNAL_ROOT PORTAL_DATA_ROOT "/qpk"
#elif defined(CONFIG_SYSTEM_DESKTOP_QPK_DIR)
#define QPK_VOLUME_INTERNAL_ROOT CONFIG_SYSTEM_DESKTOP_QPK_DIR
#else
#define QPK_VOLUME_INTERNAL_ROOT "/data/qpk"
#endif
#endif
#ifndef QPK_VOLUME_SD_ROOT
#define QPK_VOLUME_SD_ROOT "/sdcard/qpk"
#endif
enum qpk_volume {QPK_VOLUME_INTERNAL=0,QPK_VOLUME_SD=1};
static inline const char *qpk_volume_root(enum qpk_volume v)
{return v==QPK_VOLUME_INTERNAL?QPK_VOLUME_INTERNAL_ROOT:v==QPK_VOLUME_SD?QPK_VOLUME_SD_ROOT:NULL;}
static inline const char *qpk_volume_data_root(enum qpk_volume v)
{return v==QPK_VOLUME_SD?QPK_VOLUME_SD_ROOT "/.data":QPK_VOLUME_INTERNAL_ROOT "/.data";}
static inline enum qpk_volume qpk_volume_from_filename(const char *filename)
{return filename&&!strncmp(filename,QPK_VOLUME_SD_ROOT "/",sizeof(QPK_VOLUME_SD_ROOT))?QPK_VOLUME_SD:QPK_VOLUME_INTERNAL;}
/* Check the mount before mkdir: an empty mountpoint must never become an
 * accidental installation on the root filesystem. Host tests use real fixtures. */
static inline int qpk_volume_mounted(enum qpk_volume v)
{
  const char *root=qpk_volume_root(v);if(!root)return -EINVAL;
  char mount[256];if(strlen(root)>=sizeof(mount))return -ENAMETOOLONG;
  strcpy(mount,root);char *slash=strrchr(mount,'/');if(!slash||slash==mount)return -EINVAL;*slash=0;
  struct stat st;if(lstat(mount,&st))return -errno;
  if(!S_ISDIR(st.st_mode)||S_ISLNK(st.st_mode))return -ENODEV;
#ifdef __NuttX__
  struct statfs fs;if(statfs(mount,&fs))return -errno;
  if(v==QPK_VOLUME_SD&&fs.f_type!=MSDOS_SUPER_MAGIC&&fs.f_type!=FATFS_SUPER_MAGIC)return -ENODEV;
  if(v==QPK_VOLUME_INTERNAL&&fs.f_type!=SMARTFS_MAGIC)return -ENODEV;
#endif
  return 0;
}
static inline int qpk_volume_ready(enum qpk_volume v,bool create)
{
  int ret=qpk_volume_mounted(v);if(ret)return ret;
  const char *root=qpk_volume_root(v);struct stat st;
  if(lstat(root,&st)){
    if(errno!=ENOENT||!create)return -errno;
    if(mkdir(root,0700)&&errno!=EEXIST)return -errno;
    if(lstat(root,&st))return -errno;
  }
  return S_ISDIR(st.st_mode)&&!S_ISLNK(st.st_mode)?0:-EPERM;
}
