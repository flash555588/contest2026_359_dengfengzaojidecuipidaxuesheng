/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GLASS_FILE_TYPES_H
#define GLASS_FILE_TYPES_H
#include <stddef.h>
#include <string.h>
#include <strings.h>
/* Appearance does not imply that a decoder is available. Keep this table sorted. */
enum files_icon_kind { FILES_ICON_IMAGE, FILES_ICON_VIDEO, FILES_ICON_AUDIO, FILES_ICON_TEXT, FILES_ICON_PDF, FILES_ICON_DOCUMENT, FILES_ICON_SPREADSHEET, FILES_ICON_PRESENTATION, FILES_ICON_ARCHIVE, FILES_ICON_PACKAGE, FILES_ICON_CODE, FILES_ICON_FONT, FILES_ICON_GENERIC };
struct files_design {const char *extension;enum files_icon_kind kind;const char *label;};
static const struct files_design files_designs[] = {
  {".3gp",FILES_ICON_VIDEO,"3GP"},
  {".7z",FILES_ICON_ARCHIVE,"7Z · 压缩包"},
  {".aac",FILES_ICON_AUDIO,"AAC · 音频"},
  {".aiff",FILES_ICON_AUDIO,"AIFF · 音频"},
  {".apk",FILES_ICON_PACKAGE,"APK · 应用"},
  {".avi",FILES_ICON_VIDEO,"AVI"},
  {".avif",FILES_ICON_IMAGE,"AVIF"},
  {".bin",FILES_ICON_GENERIC,"BIN"},
  {".bmp",FILES_ICON_IMAGE,"BMP"},
  {".bz2",FILES_ICON_ARCHIVE,"BZ2 · 压缩包"},
  {".c",FILES_ICON_CODE,"C · 代码"},
  {".cfg",FILES_ICON_CODE,"CFG · 配置"},
  {".cjs",FILES_ICON_CODE,"CJS · 代码"},
  {".conf",FILES_ICON_CODE,"CONF · 配置"},
  {".cpp",FILES_ICON_CODE,"C++ · 代码"},
  {".css",FILES_ICON_CODE,"CSS · 代码"},
  {".csv",FILES_ICON_SPREADSHEET,"CSV · 表格"},
  {".dat",FILES_ICON_GENERIC,"DAT"},
  {".dng",FILES_ICON_IMAGE,"DNG"},
  {".doc",FILES_ICON_DOCUMENT,"DOC · 文档"},
  {".docx",FILES_ICON_DOCUMENT,"DOCX · 文档"},
  {".elf",FILES_ICON_GENERIC,"ELF"},
  {".flac",FILES_ICON_AUDIO,"FLAC · 音频"},
  {".flv",FILES_ICON_VIDEO,"FLV"},
  {".gif",FILES_ICON_IMAGE,"GIF"},
  {".gitignore",FILES_ICON_CODE,"配置 · 代码"},
  {".gz",FILES_ICON_ARCHIVE,"GZIP · 压缩包"},
  {".h",FILES_ICON_CODE,"H · 代码"},
  {".heic",FILES_ICON_IMAGE,"HEIC"},
  {".heif",FILES_ICON_IMAGE,"HEIF"},
  {".hpp",FILES_ICON_CODE,"C++ · 代码"},
  {".htm",FILES_ICON_CODE,"HTML · 代码"},
  {".html",FILES_ICON_CODE,"HTML · 代码"},
  {".ico",FILES_ICON_IMAGE,"ICO"},
  {".ini",FILES_ICON_CODE,"INI · 配置"},
  {".jpe",FILES_ICON_IMAGE,"JPEG"},
  {".jpeg",FILES_ICON_IMAGE,"JPEG"},
  {".jpg",FILES_ICON_IMAGE,"JPEG"},
  {".js",FILES_ICON_CODE,"JS · 代码"},
  {".json",FILES_ICON_CODE,"JSON · 配置"},
  {".jsx",FILES_ICON_CODE,"JSX · 代码"},
  {".log",FILES_ICON_TEXT,"LOG · 日志"},
  {".m4a",FILES_ICON_AUDIO,"M4A · 音频"},
  {".m4v",FILES_ICON_VIDEO,"M4V"},
  {".md",FILES_ICON_TEXT,"Markdown"},
  {".mjs",FILES_ICON_CODE,"MJS · 代码"},
  {".mkv",FILES_ICON_VIDEO,"MKV"},
  {".mov",FILES_ICON_VIDEO,"MOV"},
  {".mp3",FILES_ICON_AUDIO,"MP3 · 音频"},
  {".mp4",FILES_ICON_VIDEO,"MP4"},
  {".odp",FILES_ICON_PRESENTATION,"ODP · 演示"},
  {".ods",FILES_ICON_SPREADSHEET,"ODS · 表格"},
  {".odt",FILES_ICON_DOCUMENT,"ODT · 文档"},
  {".ogg",FILES_ICON_AUDIO,"OGG · 音频"},
  {".opus",FILES_ICON_AUDIO,"Opus · 音频"},
  {".otf",FILES_ICON_FONT,"OTF · 字体"},
  {".pdf",FILES_ICON_PDF,"PDF"},
  {".png",FILES_ICON_IMAGE,"PNG"},
  {".ppt",FILES_ICON_PRESENTATION,"PPT · 演示"},
  {".pptx",FILES_ICON_PRESENTATION,"PPTX · 演示"},
  {".py",FILES_ICON_CODE,"Python · 代码"},
  {".qpk",FILES_ICON_PACKAGE,"QPK · 快应用"},
  {".qt",FILES_ICON_VIDEO,"MOV"},
  {".rar",FILES_ICON_ARCHIVE,"RAR · 压缩包"},
  {".raw",FILES_ICON_IMAGE,"RAW"},
  {".rtf",FILES_ICON_DOCUMENT,"RTF · 文档"},
  {".sh",FILES_ICON_CODE,"Shell · 代码"},
  {".sql",FILES_ICON_CODE,"SQL · 代码"},
  {".svg",FILES_ICON_IMAGE,"SVG"},
  {".tar",FILES_ICON_ARCHIVE,"TAR · 压缩包"},
  {".tgz",FILES_ICON_ARCHIVE,"TGZ · 压缩包"},
  {".tif",FILES_ICON_IMAGE,"TIFF"},
  {".tiff",FILES_ICON_IMAGE,"TIFF"},
  {".toml",FILES_ICON_CODE,"TOML · 配置"},
  {".ts",FILES_ICON_CODE,"TS · 代码"},
  {".tsx",FILES_ICON_CODE,"TSX · 代码"},
  {".ttf",FILES_ICON_FONT,"TTF · 字体"},
  {".txt",FILES_ICON_TEXT,"TXT · 文本"},
  {".vue",FILES_ICON_CODE,"Vue · 代码"},
  {".wav",FILES_ICON_AUDIO,"WAV · 音频"},
  {".webm",FILES_ICON_VIDEO,"WebM"},
  {".webp",FILES_ICON_IMAGE,"WebP"},
  {".wma",FILES_ICON_AUDIO,"WMA · 音频"},
  {".woff",FILES_ICON_FONT,"WOFF · 字体"},
  {".woff2",FILES_ICON_FONT,"WOFF2 · 字体"},
  {".xls",FILES_ICON_SPREADSHEET,"XLS · 表格"},
  {".xlsx",FILES_ICON_SPREADSHEET,"XLSX · 表格"},
  {".xml",FILES_ICON_CODE,"XML · 代码"},
  {".xz",FILES_ICON_ARCHIVE,"XZ · 压缩包"},
  {".yaml",FILES_ICON_CODE,"YAML · 配置"},
  {".yml",FILES_ICON_CODE,"YAML · 配置"},
  {".zip",FILES_ICON_ARCHIVE,"ZIP · 压缩包"},
};
static inline const struct files_design *files_design(const char *name)
{
  static const struct files_design other={"",FILES_ICON_GENERIC,"其他文件"};
  const char *base=strrchr(name,'/');base=base?base+1:name;
  const char *ext=strrchr(base,'.');
  size_t first=0,end=sizeof(files_designs)/sizeof(files_designs[0]);
  if(!ext)return &other;
  while(first<end) {
    size_t middle=first+(end-first)/2;
    int order=strcasecmp(ext,files_designs[middle].extension);
    if(!order)return &files_designs[middle];
    if(order<0)end=middle;else first=middle+1;
  }
  return &other;
}
#endif
