/* SPDX-License-Identifier: Apache-2.0
 * 中文：千本书库、重启、查询、损坏、取消及低预算。/ English: thousand-book catalog, restart, queries, damage, cancellation and low budgets. */
#define _POSIX_C_SOURCE 200809L
#include "pn_catalog_index.h"
#include "pn_async.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#ifdef PN_TEST_FAT_MOUNT
static char fat_mount[256];
static unsigned mount_mkdir_calls;
int __real_mkdir(const char *,mode_t);
// IDF6.1 FatFS的挂载根mkdir返回EINVAL，stat仍报告真实目录。/ IDF 6.1 FatFS returns EINVAL for mkdir at the mount root while stat still reports a real directory.
int __wrap_mkdir(const char *path,mode_t mode){if(*fat_mount && !strcmp(path,fat_mount)){mount_mkdir_calls++;errno=EINVAL;return -1;}return __real_mkdir(path,mode);}
#endif
#ifdef PN_TEST_FAT_RENAME
int __real_rename(const char *,const char *);
// 主机桩只替换rename的覆盖语义，不替代文件读写。/ The host stub replaces only rename-overwrite semantics, retaining real file I/O.
int __wrap_rename(const char *source,const char *dest){struct stat info;if(!lstat(dest,&info)){errno=EEXIST;return -1;}return __real_rename(source,dest);}
#endif
static void same(const pn_catalog_page_t *a,const pn_catalog_page_t *b){assert(a->count==b->count && a->total==b->total && a->index==b->index && a->more==b->more && a->per_page==b->per_page);for(size_t j=0;j<a->count;j++)assert(!strcmp(a->items[j].name,b->items[j].name) && !strcmp(a->items[j].path,b->items[j].path) && a->items[j].size==b->items[j].size && a->items[j].format==b->items[j].format);}
int main(void){
    char root[]="/tmp/pn-library-XXXXXX";assert(mkdtemp(root));char books[256],cache[256],file[384];snprintf(books,sizeof books,"%s/books",root);snprintf(cache,sizeof cache,"%s/cache",root);assert(!mkdir(books,0700));
#ifdef PN_TEST_FAT_MOUNT
    strcpy(fat_mount,root);
#endif
    for(unsigned j=0;j<1000;j++){snprintf(file,sizeof file,"%s/Book%04u.txt",books,(j*317)%1000);FILE *f=fopen(file,"wb");assert(f && fputs("hello",f)!=EOF && !fclose(f));}
    pn_pool_t pool;assert(!pn_pool_init(&pool,256*1024,NULL,NULL,NULL));pn_media_t media;pn_media_init(&media);assert(!pn_media_attach(&media,1));pn_catalog_index_t index={0};assert(!pn_catalog_index_init(&index,&pool,&media,books,cache));pn_media_lease_t lease;pn_catalog_page_t a,b;
    assert(!pn_media_acquire(&media,PN_MEDIA_READ,&lease));assert(pn_catalog_index_page(&index,&lease,"",false,0,NULL,6,&a)==PN_EMPTY);assert(pn_catalog_index_build(&index)==PN_BUSY);assert(!pn_media_release(&media,&lease));assert(!pn_catalog_index_build(&index));
    assert(!pn_media_acquire(&media,PN_MEDIA_READ,&lease));pn_catalog_index_stats_t before,after;pn_catalog_index_stats(&index,&before);assert(!pn_catalog_index_page(&index,&lease,"Book0500.txt",false,0,NULL,8,&a));pn_catalog_index_stats(&index,&after);assert(after.records_read-before.records_read<25 && a.count==8 && a.total==1000 && a.index==501);assert(!pn_catalog_page_n(&media,&lease,books,"Book0500.txt",8,&b));same(&a,&b);printf("1000 books page records=%llu total=%zu\n",(unsigned long long)(after.records_read-before.records_read),a.total);
    assert(!pn_catalog_index_page(&index,&lease,"Book0500.txt",true,0,NULL,5,&a) && !pn_catalog_page_before_n(&media,&lease,books,"Book0500.txt",5,&b));same(&a,&b);
    assert(!pn_catalog_index_page(&index,&lease,"",false,'z',NULL,6,&a) && !pn_catalog_page_from_n(&media,&lease,books,'z',6,&b));same(&a,&b);
    assert(!pn_catalog_index_page(&index,&lease,"Book0500.txt",false,0,"5",6,&a) && !pn_catalog_search_page_n(&media,&lease,books,"5","Book0500.txt",6,&b));same(&a,&b);
    assert(!pn_catalog_index_page(&index,&lease,"Book0500.txt",true,0,"5",6,&a) && !pn_catalog_search_page_before_n(&media,&lease,books,"5","Book0500.txt",6,&b));same(&a,&b);
    assert(!pn_media_release(&media,&lease));pn_catalog_index_close(&index);assert(!pn_catalog_index_init(&index,&pool,&media,books,cache));assert(pn_catalog_index_build(&index)==PN_EMPTY);assert(!pn_media_acquire(&media,PN_MEDIA_READ,&lease) && !pn_catalog_index_page(&index,&lease,"",false,0,NULL,6,&a));assert(!pn_media_release(&media,&lease));
    snprintf(file,sizeof file,"%s/Added.txt",books);FILE *f=fopen(file,"wb");assert(f && !fclose(f));assert(!pn_media_acquire(&media,PN_MEDIA_READ,&lease) && pn_catalog_index_page(&index,&lease,"",false,0,NULL,6,&a)==PN_EMPTY && !pn_media_release(&media,&lease));assert(!pn_catalog_index_build(&index));
    snprintf(file,sizeof file,"%s/library.pni",cache);f=fopen(file,"r+b");assert(f && !fseek(f,96+500*784,SEEK_SET) && fputc(255,f)!=EOF && !fclose(f));assert(!pn_media_acquire(&media,PN_MEDIA_READ,&lease) && pn_catalog_index_page(&index,&lease,"Book0500.txt",false,0,NULL,6,&a)==PN_EMPTY && !pn_media_release(&media,&lease));assert(!pn_catalog_index_build(&index));
    pn_catalog_index_invalidate(&index);pn_async_t q;pn_async_init(&q);pn_async_scope_t scope;pn_async_begin(&q,&scope,true);pn_async_event_t event={.bytes=1};pn_async_send(&q,&event);assert(pn_catalog_index_build(&index)==PN_CANCELLED && !pn_media_active(&media));pn_async_end(&scope);
    pool.fail_at=pool.attempts+1;assert(pn_catalog_index_build(&index)==PN_NO_MEMORY && !pn_media_active(&media));pool.fail_at=0;assert(!pn_catalog_index_build(&index));pn_catalog_index_close(&index);assert(!pool.used && !pool.live);
    assert(!unlink(file));for(unsigned j=0;j<1000;j++){snprintf(file,sizeof file,"%s/Book%04u.txt",books,j);assert(!unlink(file));}snprintf(file,sizeof file,"%s/Added.txt",books);assert(!unlink(file));assert(!rmdir(books) && !rmdir(cache) && !rmdir(root));
#ifdef PN_TEST_FAT_MOUNT
    assert(!mount_mkdir_calls);puts("FatFS mount root: index build/reopen/update/cancel/fault paths passed without mkdir on the mount root");
#endif
    return 0;
}
