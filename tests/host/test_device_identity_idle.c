/* SPDX-License-Identifier: Apache-2.0
 * 中文：编译真实设备身份后台，统计提示/文件探测，证明空闲不重复IO且忙碌/取消可重试。
 * English: compile actual device identity work and count probes, checking idle I/O deduplication and BUSY/cancel retry. */
#define _POSIX_C_SOURCE 200809L
#define main idle_boot_fixture_main
#include "test_device_boot.c"
#undef main
#include "pn_identity_cache.h"
#include "pn_source_blocks.h"
static unsigned stat_calls,hint_calls;
static bool hints_present=true;
static pn_status_t forced_status=PN_OK;
static bool counted_stat(const char *path,uint64_t *size,int64_t *mtime){stat_calls++;clock_us+=1000;return pn_identity_cache_stat(path,size,mtime);}
static bool counted_hint(const char *tf,const char *state,const pn_book_id_t *book){(void)tf;(void)state;(void)book;hint_calls++;clock_us+=260000;return hints_present;}
static pn_status_t counted_job(pn_identity_cache_job_t *job,pn_pool_t *p,pn_media_t *m,const char *path){return forced_status==PN_OK?pn_identity_cache_job_begin(job,p,m,path):forced_status;}
static pn_status_t counted_warm(pn_identity_warm_t *w,pn_identity_cache_job_t *j,pn_pool_t *p,pn_media_t *m,const char *path,uint64_t activity){return forced_status==PN_OK?pn_identity_cache_warm_begin(w,j,p,m,path,activity):forced_status;}
#define pn_identity_cache_stat counted_stat
#define pn_source_blocks_hint counted_hint
#define pn_identity_cache_job_begin counted_job
#define pn_identity_cache_warm_begin counted_warm
#ifndef PN_DEVICE_IDLE_MAIN
#define PN_DEVICE_IDLE_MAIN "../../main/app_main.c"
#endif
#include PN_DEVICE_IDLE_MAIN
#undef pn_identity_cache_stat
#undef pn_source_blocks_hint
#undef pn_identity_cache_job_begin
#undef pn_identity_cache_warm_begin
static void cached(const char *path){uint64_t size;int64_t mtime;pn_book_id_t weak={{7}};assert(pn_identity_cache_stat(path,&size,&mtime));pn_identity_cache_put(&identity_cache,path,size,mtime,&weak);identity_cache.dirty=false;}
static void recent_path(const char *path){recent_snapshot->count=1;strcpy(recent_snapshot->items[0].path,path);recent_snapshot->items[0].format=PN_BOOK_EPUB;}
int main(void){
    char folder[]="/tmp/pn-idle-identity-XXXXXX";assert(mkdtemp(folder));char first[512],second[512];
    snprintf(first,sizeof first,"%s/cached.epub",folder);snprintf(second,sizeof second,"%s/cold.epub",folder);
    for(unsigned i=0;i<2;i++){FILE *f=fopen(i?second:first,"wb");assert(f);for(unsigned n=0;n<100000;n++)assert(fputc('a'+n%26,f)!=EOF);assert(!fclose(f));}
    assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);pn_media_init(&sd_media);pn_media_init(&data_media);
    assert(pn_media_attach(&sd_media,1)==PN_OK && pn_media_attach(&data_media,2)==PN_OK);
    shelf_page=pn_alloc(&pool,sizeof *shelf_page);recent_snapshot=pn_alloc(&pool,sizeof *recent_snapshot);assert(shelf_page && recent_snapshot);
    memset(shelf_page,0,sizeof *shelf_page);memset(recent_snapshot,0,sizeof *recent_snapshot);
    shelf_page->count=1;shelf_page->items[0].identified=true;strcpy(shelf_page->items[0].path,first);shelf_page->items[0].format=PN_BOOK_EPUB;
    data_ready=identity_cache_loaded=home_mode=shelf_draw_valid=panel_known=true;last_input=0;uint64_t now=1000;cached(first);recent_path(first);
    assert(!tick_identity(now) && !identity_job.impl && hint_calls==1 && stat_calls==2);
    size_t used=pool.used;unsigned stats=stat_calls,hints=hint_calls;
    for(unsigned i=0;i<1000;i++){now+=250;assert(!tick_identity(now) && !identity_job.impl);}
    assert(stat_calls==stats && hint_calls==hints && pool.used==used && !pn_media_active(&sd_media));
    // 输入/WRITE/换卡恢复探测，但普通CACHE_WRITE不能替换书源，不触发重复检查。
    // Input/WRITE/card changes re-enable probes; ordinary CACHE_WRITE cannot replace books and does not repeat them.
    last_input=now;now+=1000;assert(!tick_identity(now) && hint_calls==++hints);
    pn_media_lease_t lease={0};assert(pn_media_acquire(&sd_media,PN_MEDIA_CACHE_WRITE,&lease)==PN_OK && pn_media_release(&sd_media,&lease)==PN_OK);
    stats=stat_calls;now+=250;assert(!tick_identity(now) && stat_calls==stats && hint_calls==hints);
    assert(pn_media_acquire(&sd_media,PN_MEDIA_WRITE,&lease)==PN_OK && pn_media_release(&sd_media,&lease)==PN_OK);
    now+=250;assert(!tick_identity(now) && hint_calls==++hints);
    sd_media.usb_pending=true;stats=stat_calls;now+=250;assert(!tick_identity(now) && stat_calls==stats);sd_media.usb_pending=false;
    assert(pn_media_detach(&sd_media)==PN_OK && pn_media_attach(&sd_media,3)==PN_OK);now+=250;assert(!tick_identity(now) && hint_calls==++hints);
    // 无输入的新候选仍能进入；不是整页或整个输入周期永久关闭探测。
    // A new candidate remains eligible without input; never disable an entire page/input cycle permanently.
    cached(second);recent_path(second);now+=250;assert(!tick_identity(now) && hint_calls==++hints);
    // 真实写租约造成BUSY，释放后同输入周期必须能启动完整预校验。
    // A real write lease causes BUSY; releasing it must allow full preverification in the same input cycle.
    pn_identity_cache_forget(&identity_cache,second);identity_cache.dirty=false;hints_present=false;
    assert(pn_media_acquire(&sd_media,PN_MEDIA_WRITE,&lease)==PN_OK);now+=250;assert(!tick_identity(now) && !identity_job.impl && !identity_warm.count);
    assert(pn_media_release(&sd_media,&lease)==PN_OK);now+=250;assert(tick_identity(now) && identity_job.impl && identity_warm.count==1);
    unsigned count=identity_cache.count;assert(pn_identity_cache_job_step(&identity_job,&identity_cache)==PN_BUSY && identity_cache.count==count);identity_job_stop();
    // 取消不能变成完成；撤销取消后不需要新输入即可重试。
    // Cancellation is not completion; retry without requiring a fresh input once cancellation clears.
    assert(pn_media_acquire(&sd_media,PN_MEDIA_WRITE,&lease)==PN_OK && pn_media_release(&sd_media,&lease)==PN_OK);
    forced_status=PN_CANCELLED;now+=250;assert(!tick_identity(now) && !identity_job.impl);forced_status=PN_OK;
    now+=250;assert(tick_identity(now) && identity_job.impl);identity_job_stop();
    // 建表类别也覆盖BUSY和失败：不把失败路径永久绑定到整次开机。
    // Identity-only work retries BUSY/failure after context changes, never blacklisting a path for the whole boot.
    home_mode=false;recent_snapshot->count=0;strcpy(selected_path,second);selected_format=PN_BOOK_TXT;selected_identified=false;
    last_input=now;forced_status=PN_BUSY;now+=1000;assert(!tick_identity(now) && !identity_job.impl);forced_status=PN_OK;
    now+=250;assert(tick_identity(now) && identity_job.impl);identity_job_stop();
    last_input=now;forced_status=PN_IO;now+=1000;assert(!tick_identity(now));stats=stat_calls;
    for(unsigned i=0;i<100;i++){now+=250;assert(!tick_identity(now));}assert(stat_calls==stats);
    forced_status=PN_OK;last_input=now;now+=1000;assert(tick_identity(now) && identity_job.impl);identity_job_stop();
    pn_free(shelf_page);shelf_page=NULL;pn_free(recent_snapshot);recent_snapshot=NULL;assert(!pool.used && !pool.live && !pn_media_active(&sd_media));
    assert(!unlink(first) && !unlink(second) && !rmdir(folder));
    puts("device identity idle: 1000 empty rounds without repeated stat/hint I/O; input/WRITE/card/new-path invalidation; BUSY/cancel retry; zero leaks");return 0;
}
