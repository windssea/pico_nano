/* EPUB本地资源路径解析。/ EPUB local resource path resolution. */
#pragma once
#include "pn_zip.h"
#define PN_RESOURCE_FRAGMENT_MAX 256
/// 相对base文件解析href，percent解码一次；拒绝外部URI/绝对路径/越界，错误输出不变。
/// Resolve href relative to the base file, decoding percent once; reject external URIs/absolute paths/root escapes and preserve outputs on errors.
pn_status_t pn_resource_resolve(const char *base,const char *href,char path[PN_ZIP_PATH_MAX],char fragment[PN_RESOURCE_FRAGMENT_MAX]);
