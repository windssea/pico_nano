/* SPDX-License-Identifier: Apache-2.0
 * 中文：仅清理测试临时状态中的索引子目录。/ English: clean only the index child of a test's temporary state directory. */
#pragma once
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static inline void pn_test_remove_indexes(const char *state){
    char folder[1024];int n=snprintf(folder,sizeof folder,"%s/indexes",state);assert(n>0 && (size_t)n<sizeof folder);DIR *dir=opendir(folder);if(!dir){assert(errno==ENOENT);return;}
    struct dirent *entry;while((entry=readdir(dir))){if(entry->d_name[0]=='.')continue;char path[2048];n=snprintf(path,sizeof path,"%s/%s",folder,entry->d_name);assert(n>0 && (size_t)n<sizeof path && !unlink(path));}assert(!closedir(dir) && !rmdir(folder));
}
