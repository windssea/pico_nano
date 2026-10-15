/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实TXT/EPUB会话所有权、像素、失败回执、预算及介质交接。
 * English: actual TXT/EPUB session ownership, pixels, failed receipts, budgets and media handoff. */
#define _POSIX_C_SOURCE 200809L
#include "pn_hot_session.h"
#include "pn_cancel.h"
#include "pn_identity.h"
#include "pn_profile.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
static unsigned draws,reads,prerenders;
static unsigned cancel_prerender;static bool paint_cancelled;
pn_status_t __real_pn_font_prerender(pn_font_t *,pn_frame_t *,uint32_t,int32_t,int,pn_font_render_t);
pn_status_t __wrap_pn_font_prerender(pn_font_t *f,pn_frame_t *b,uint32_t cp,int32_t x,int y,pn_font_render_t m){prerenders++;pn_status_t s=__real_pn_font_prerender(f,b,cp,x,y,m);if(cancel_prerender && !--cancel_prerender)paint_cancelled=true;return s;}
static unsigned first_layouts;static bool cancel_layout,layout_cancelled;
static uint64_t phase_clock(void *ctx){(void)ctx;return 0;}
static void phase_done(void *ctx,const pn_profile_event_t *e){
    (void)ctx;if(e->status==PN_OK && (!strcmp(e->phase,"txt.prepare") || !strcmp(e->phase,"txt.jump") || !strcmp(e->phase,"epub.prepare") || !strcmp(e->phase,"epub.jump"))){first_layouts++;if(cancel_layout)layout_cancelled=true;}
}
static bool after_layout(void *ctx){(void)ctx;return layout_cancelled;}
pn_status_t __real_pn_font_draw(pn_font_t *,pn_frame_t *,uint32_t,int32_t,int,pn_font_render_t);
pn_status_t __wrap_pn_font_draw(pn_font_t *f,pn_frame_t *b,uint32_t cp,int32_t x,int y,pn_font_render_t m){draws++;return __real_pn_font_draw(f,b,cp,x,y,m);}
size_t __real_fread(void *,size_t,size_t,FILE *);
size_t __wrap_fread(void *p,size_t a,size_t b,FILE *f){reads++;return __real_fread(p,a,b,f);}
typedef struct {uint8_t *pixels;bool compare,fail;unsigned calls;} screen_t;
static pn_status_t present(void *ctx,const pn_frame_t *f,pn_refresh_t mode){
    screen_t *s=ctx;s->calls++;assert(mode==PN_REFRESH_GC16 || mode==PN_REFRESH_GL16);
    for(int y=0;y<f->height;y++){uint8_t *row=s->pixels+(size_t)y*342u;if(s->compare)assert(!memcmp(row,f->pixels+(size_t)y*f->stride,342u));else memcpy(row,f->pixels+(size_t)y*f->stride,342u);}
    return s->fail?PN_IO:PN_OK;
}
static pn_status_t step(pn_reader_app_t *t,pn_epub_app_t *e,pn_reader_action_t a,uint64_t now,screen_t *s){return e->impl?pn_epub_app_step(e,a,now,present,s):pn_reader_app_step(t,a,now,present,s);}
static void run(const char *path,bool is_epub){
    pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_reader_app_t t={0};pn_epub_app_t e={0};pn_hot_session_t h={0};char state[]="/tmp/pn-hot-state-XXXXXX";assert(mkdtemp(state));
    screen_t s={.pixels=malloc(415872)};assert(s.pixels);
    assert((is_epub?pn_epub_app_open_on_media(&e,&pool,&media,path,NULL,state,32,0):pn_reader_app_open_on_media(&t,&pool,&media,path,NULL,state,32,0))==PN_OK);
    assert(step(&t,&e,PN_APP_OPEN,1,&s)==PN_OK && step(&t,&e,PN_APP_NEXT,2,&s)==PN_OK);
    assert((is_epub?pn_epub_app_prepare_unshown(&e):pn_reader_app_prepare_unshown(&t))==PN_BUSY);
    assert((is_epub?pn_epub_app_verify_unshown(&e):pn_reader_app_verify_unshown(&t))==PN_BUSY);
    assert((is_epub?pn_epub_app_discard_unshown(&e):pn_reader_app_discard_unshown(&t))==PN_BUSY);
    pn_book_id_t id;assert((is_epub?pn_epub_app_identity(&e,&id):pn_reader_app_identity(&t,&id))==PN_OK);
    void *original=is_epub?e.impl:t.impl;
    assert(pn_hot_session_park(&h,&pool,&media,"/different",1,3,&t,&e)==PN_STALE_JOB && (t.impl || e.impl));
    pn_media_t other;pn_media_init(&other);assert(pn_media_attach(&other,1)==PN_OK);
    assert(pn_hot_session_park(&h,&pool,&other,path,1,3,&t,&e)==PN_STALE_JOB && (t.impl || e.impl));
    assert(pn_hot_session_park(&h,&pool,&media,path,1,3,&t,&e)==PN_OK && !t.impl && !e.impl && h.parks==1);
    assert((is_epub?h.epub.impl:h.txt.impl)==original);
    pn_media_lease_t write;assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&write)==PN_BUSY);
    unsigned before=s.calls;
    assert(pn_hot_session_resume(&h,&media,"/different",is_epub?PN_BOOK_EPUB:PN_BOOK_TXT,&id,1,4,&t,&e,present,&s)==PN_EMPTY);
    assert(pn_hot_session_resume(&h,&media,path,is_epub?PN_BOOK_TXT:PN_BOOK_EPUB,&id,1,4,&t,&e,present,&s)==PN_EMPTY);
    assert(pn_hot_session_resume(&h,&media,path,is_epub?PN_BOOK_EPUB:PN_BOOK_TXT,&id,2,4,&t,&e,present,&s)==PN_EMPTY);
    pn_book_id_t wrong=id;wrong.sha256[0]^=1;assert(pn_hot_session_resume(&h,&media,path,is_epub?PN_BOOK_EPUB:PN_BOOK_TXT,&wrong,1,4,&t,&e,present,&s)==PN_STALE_JOB && s.calls==before);
    s.compare=true;s.fail=true;draws=reads=0;
    pn_status_t status=pn_hot_session_resume(&h,&media,path,is_epub?PN_BOOK_EPUB:PN_BOOK_TXT,&id,1,5,&t,&e,present,&s);
    assert(status==PN_IO && !t.impl && !e.impl && h.media && !h.hits && !draws);
    s.fail=false;draws=reads=0;
    assert(pn_hot_session_resume(&h,&media,path,is_epub?PN_BOOK_EPUB:PN_BOOK_TXT,&id,1,6,&t,&e,present,&s)==PN_OK && h.hits==1 && !h.media && !draws);
#ifdef PN_TEST_IMMUTABLE_FILE
    assert(!reads); // 模拟受租约保护的TF，不能用来宣称实机耗时。/ Emulate lease-protected TF; this is not device timing.
#else
    assert(reads); // PC在外部写入不受租约约束时重新核验内容。/ Host content is reverified because outside writes do not honor leases.
#endif
    assert((is_epub?e.impl:t.impl)==original);
    s.compare=false;assert(step(&t,&e,PN_APP_NEXT,7,&s)==PN_OK);
    pn_txt_progress_t txt_saved={0};pn_epub_progress_t epub_saved={0};
    assert((is_epub?pn_epub_app_progress(&e,&epub_saved):pn_reader_app_progress(&t,&txt_saved))==PN_OK);
    assert(pn_hot_session_park(&h,&pool,&media,path,1,8,&t,&e)==PN_OK);
    assert(pn_hot_session_expire(&h,8+PN_HOT_SESSION_MS-1)==PN_EMPTY && h.media);
    assert(pn_hot_session_expire(&h,8+PN_HOT_SESSION_MS)==PN_OK && !h.media && pool.used==0 && pool.live==0);
    assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&write)==PN_OK && pn_media_release(&media,&write)==PN_OK);
    assert((is_epub?pn_epub_app_open_on_media(&e,&pool,&media,path,NULL,state,32,130000):pn_reader_app_open_on_media(&t,&pool,&media,path,NULL,state,32,130000))==PN_OK);
    assert(step(&t,&e,PN_APP_OPEN,130001,&s)==PN_OK);
    if(is_epub){pn_epub_progress_t p;assert(pn_epub_app_progress(&e,&p)==PN_OK && !strcmp(p.location.path,epub_saved.location.path) && p.location.position.element==epub_saved.location.position.element && p.location.position.run==epub_saved.location.position.run && p.location.position.offset==epub_saved.location.position.offset);}
    else{pn_txt_progress_t p;assert(pn_reader_app_progress(&t,&p)==PN_OK && p.source_offset==txt_saved.source_offset);}
    size_t limit=pool.limit;pool.limit=3u*1024u*1024u;assert(pn_hot_session_park(&h,&pool,&media,path,1,130002,&t,&e)==PN_EMPTY && (t.impl || e.impl));pool.limit=limit;
    size_t fill=pool.limit-pool.used-PN_HOT_HEADROOM+64;void *pressure=pn_alloc(&pool,fill);assert(pressure);
    assert(pn_hot_session_park(&h,&pool,&media,path,1,130002,&t,&e)==PN_EMPTY && (t.impl || e.impl));pn_free(pressure);
    assert(pn_hot_session_park(&h,&pool,&media,path,1,130003,&t,&e)==PN_OK);
#ifndef PN_TEST_IMMUTABLE_FILE
    if(!is_epub){
        struct stat info;assert(!stat(path,&info));FILE *f=fopen(path,"r+b");assert(f);int ch=fgetc(f);assert(ch!=EOF && !fseek(f,0,SEEK_SET) && fputc(ch^1,f)!=EOF && !fclose(f));
        struct timespec times[2]={info.st_atim,info.st_mtim};assert(!utimensat(AT_FDCWD,path,times,0));before=s.calls;
        assert(pn_hot_session_resume(&h,&media,path,PN_BOOK_TXT,&id,1,130004,&t,&e,present,&s)==PN_STALE_JOB && s.calls==before && !t.impl);
        f=fopen(path,"r+b");assert(f && fputc(ch,f)!=EOF && !fclose(f));
        // 原句柄未变而路径被同大小/时间的新文件替换时，也必须拒绝。
        // Reject equal-size/time replacement even when the original handle stays unchanged.
        f=fopen(path,"rb");assert(f);size_t count=(size_t)info.st_size;uint8_t *replacement=malloc(count);assert(replacement && fread(replacement,1,count,f)==count && !fclose(f));replacement[0]^=1;
        char alternate[512];assert(snprintf(alternate,sizeof alternate,"%s.replacement",path)>0);f=fopen(alternate,"wb");assert(f && fwrite(replacement,1,count,f)==count && !fclose(f));assert(!utimensat(AT_FDCWD,alternate,times,0) && !rename(alternate,path));
        assert(pn_hot_session_resume(&h,&media,path,PN_BOOK_TXT,&id,1,130005,&t,&e,present,&s)==PN_STALE_JOB && s.calls==before && !t.impl);
        replacement[0]^=1;f=fopen(path,"wb");assert(f && fwrite(replacement,1,count,f)==count && !fclose(f));free(replacement);
    }
#endif
    assert(pn_media_request_usb(&media)==PN_OK && pn_media_acquire(&media,PN_MEDIA_USB,&write)==PN_BUSY);
    assert(pn_hot_session_drop(&h)==PN_OK && !h.media && pool.used==0 && pool.live==0);
    assert(pn_media_acquire(&media,PN_MEDIA_USB,&write)==PN_OK && pn_media_release(&media,&write)==PN_OK);
    assert(pn_media_cancel_usb(&media)==PN_OK && pn_media_active(&media)==0);
    free(s.pixels);printf("hot_session kind=%s exact_pixels=1 foreground_draws=0 saved_resume=1\n",is_epub?"epub":"txt");
}
static void font_case(const char *book){
    pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    char directory[]="/tmp/pn-hot-font-XXXXXX",path[384];assert(mkdtemp(directory));snprintf(path,sizeof path,"%s/font.ttf",directory);FILE *f=fopen(path,"wb");assert(f);
    pn_text_source_t source=pn_font_builtin_source();uint8_t bytes[4096];for(uint64_t at=0;at<source.size;){size_t n;assert(source.read_at(source.ctx,at,bytes,sizeof bytes,&n)==PN_OK && n && fwrite(bytes,1,n,f)==n);at+=n;}assert(!fclose(f));
    assert(pn_font_system_open(&pool,&media,path)==PN_OK);
    pn_reader_app_t t={0};pn_epub_app_t e={0};pn_hot_session_t h={0};screen_t s={.pixels=malloc(415872)};assert(s.pixels);
    assert(pn_reader_app_open_on_media(&t,&pool,&media,book,NULL,NULL,32,0)==PN_OK && step(&t,&e,PN_APP_OPEN,1,&s)==PN_OK);
    assert(pn_hot_session_park(&h,&pool,&media,book,1,2,&t,&e)==PN_OK);s.compare=true;draws=reads=0;
    assert(pn_hot_session_resume(&h,&media,book,PN_BOOK_TXT,NULL,1,3,&t,&e,present,&s)==PN_OK && !draws);
#ifdef PN_TEST_IMMUTABLE_FILE
    assert(!reads);
#endif
    assert(pn_hot_session_park(&h,&pool,&media,book,1,4,&t,&e)==PN_OK);
#ifndef PN_TEST_IMMUTABLE_FILE
    f=fopen(path,"r+b");assert(f);int ch=fgetc(f);assert(ch!=EOF && !fseek(f,0,SEEK_SET) && fputc(ch^1,f)!=EOF && !fclose(f));unsigned before=s.calls;
    assert(pn_hot_session_resume(&h,&media,book,PN_BOOK_TXT,NULL,1,5,&t,&e,present,&s)==PN_STALE_JOB && s.calls==before && h.media);
    f=fopen(path,"r+b");assert(f && fputc(ch,f)!=EOF && !fclose(f));
#endif
    assert(pn_media_detach(&media)==PN_OK);
    assert(pn_hot_session_resume(&h,&media,book,PN_BOOK_TXT,NULL,1,6,&t,&e,present,&s)==PN_EMPTY && !t.impl);
    assert(pn_hot_session_drop(&h)==PN_OK && pn_font_system_close()==PN_OK && pool.used==0 && pool.live==0 && pn_media_active(&media)==0);
    assert(!pn_media_attach(&media,2));pn_media_lease_t guard;pn_catalog_item_t item={.format=PN_BOOK_TXT,.identified=true};assert(strlen(book)<sizeof item.path);strcpy(item.path,book);
    assert(!pn_media_acquire(&media,PN_MEDIA_READ,&guard) && !pn_identity_file(&media,&guard,book,PN_TEXT_FILE_MAX_BYTES,&item.expected) && !pn_media_release(&media,&guard));
    assert(!pn_hot_session_prepare(&h,&pool,&media,&item,path,NULL,32,1,10,10));reads=0;
    assert(!pn_hot_session_take_prepared(&h,&media,book,PN_BOOK_TXT,&item.expected,1,11,&t,&e));
#ifdef PN_TEST_IMMUTABLE_FILE
    assert(!reads);
#endif
    assert(!pn_reader_app_discard_unshown(&t) && !pool.used && !pool.live && !pn_media_active(&media));
    assert(!unlink(path));free(s.pixels);
}
typedef struct {pn_cache_owner_t owner;void *bytes;unsigned drops;} menu_cache_t;
static size_t menu_bytes(void *ctx){return pn_alloc_size(((menu_cache_t *)ctx)->bytes);}
static void menu_drop(void *ctx){menu_cache_t *m=ctx;pn_free(m->bytes);m->bytes=NULL;m->drops++;}
static size_t file_count(const char *path){DIR *d=opendir(path);assert(d);size_t n=0;struct dirent *entry;while((entry=readdir(d)))if(strcmp(entry->d_name,".") && strcmp(entry->d_name,".."))n++;assert(!closedir(d));return n;}
static size_t reading_records(const char *path){DIR *d=opendir(path);assert(d);size_t n=0;struct dirent *entry;while((entry=readdir(d)))if(strcmp(entry->d_name,".") && strcmp(entry->d_name,"..") && strcmp(entry->d_name,"indexes"))n++;assert(!closedir(d));return n;}
static bool cancel_after(void *ctx){unsigned *left=ctx;return !--*left;}
static bool cancel_after_read(void *ctx){(void)ctx;return reads!=0;}
static bool cancel_after_paint(void *ctx){(void)ctx;return paint_cancelled;}
static void paint_all(pn_hot_session_t *h,pn_pool_t *pool,uint64_t now){
    unsigned steps=0;size_t reserve=pool->cache_reserve;pn_cache_class_t kind=pool->speculative_kind;
    while(pn_hot_session_paint_pending(h)){
        unsigned before=prerenders;assert(++steps<4096 && pn_hot_session_paint(h,now,99)==PN_OK && prerenders-before<=8);
        assert(pool->cache_reserve==reserve && !pool->speculative && pool->speculative_kind==kind);
    }
    assert(steps && pn_hot_session_paint(h,now,8)==PN_EMPTY);
}
static void prepare_case(const char *path,bool epub){
    pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));
    pn_media_t media;pn_media_init(&media);assert(!pn_media_attach(&media,1));pn_media_lease_t lease;
    pn_catalog_item_t item={.format=epub?PN_BOOK_EPUB:PN_BOOK_TXT,.identified=true};assert(strlen(path)<sizeof item.path);strcpy(item.path,path);
    assert(!pn_media_acquire(&media,PN_MEDIA_READ,&lease) && !pn_identity_file(&media,&lease,path,PN_TEXT_FILE_MAX_BYTES,&item.expected) && !pn_media_release(&media,&lease));
    char state[]="/tmp/pn-prepare-state-XXXXXX";assert(mkdtemp(state));
    menu_cache_t menu={0};menu.bytes=pn_cache_alloc(&pool,PN_CACHE_GLYPH,PN_MEM_FONT,128u*1024u);assert(menu.bytes);memset(menu.bytes,0x5a,128u*1024u);pn_cache_attach(&pool,&menu.owner,PN_CACHE_GLYPH,&menu,menu_bytes,menu_drop);
    size_t used=pool.used,live=pool.live;pn_hot_session_t h={0};pn_reader_app_t t={0};pn_epub_app_t e={0};
    first_layouts=0;assert(!pn_hot_session_prepare(&h,&pool,&media,&item,NULL,state,32,1,0,1) && h.prepared && h.preparations==1 && !h.hits && !menu.drops && !menu.owner.pins && first_layouts==1);
    assert((epub?pn_epub_app_prepare_unshown(&h.epub):pn_reader_app_prepare_unshown(&h.txt))==PN_OK && first_layouts==1);
    paint_all(&h,&pool,0);assert(!menu.drops && !menu.owner.pins);
    void *original=epub?h.epub.impl:h.txt.impl;pn_txt_progress_t tp;pn_epub_progress_t ep;
    assert((epub?pn_epub_app_progress(&h.epub,&ep):pn_reader_app_progress(&h.txt,&tp))==PN_EMPTY && !file_count(state));
    size_t attempts=pool.attempts;assert(pn_hot_session_prepare(&h,&pool,&media,&item,NULL,state,32,1,1,1)==PN_EMPTY && pool.attempts==attempts);
    assert(pn_hot_session_take_prepared(&h,&media,"/different",item.format,&item.expected,1,1,&t,&e)==PN_EMPTY);
    assert(pn_hot_session_take_prepared(&h,&media,path,epub?PN_BOOK_TXT:PN_BOOK_EPUB,&item.expected,1,1,&t,&e)==PN_EMPTY);
    assert(pn_hot_session_take_prepared(&h,&media,path,item.format,&item.expected,2,1,&t,&e)==PN_EMPTY);
    pn_book_id_t wrong=item.expected;wrong.sha256[0]^=1;
    assert(pn_hot_session_take_prepared(&h,&media,path,item.format,&wrong,1,1,&t,&e)==PN_STALE_JOB && h.prepared && !t.impl && !e.impl);
    for(unsigned when=1;when<=2;when++){unsigned left=when;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel_after,&left);assert(pn_hot_session_take_prepared(&h,&media,path,item.format,&item.expected,1,1,&t,&e)==PN_CANCELLED);pn_cancel_leave(&scope);assert(h.prepared && !t.impl && !e.impl && !file_count(state));}
#ifndef PN_TEST_IMMUTABLE_FILE
    if(!epub){struct stat info;assert(!stat(path,&info));FILE *f=fopen(path,"r+b");assert(f);int ch=fgetc(f);assert(ch!=EOF && !fseek(f,0,SEEK_SET) && fputc(ch^1,f)!=EOF && !fclose(f));struct timespec times[2]={info.st_atim,info.st_mtim};assert(!utimensat(AT_FDCWD,path,times,0));
        assert(pn_hot_session_take_prepared(&h,&media,path,item.format,&item.expected,1,1,&t,&e)==PN_STALE_JOB && h.prepared && !file_count(state));
        f=fopen(path,"r+b");assert(f && fputc(ch,f)!=EOF && !fclose(f));}
#endif
    reads=draws=0;assert(!pn_hot_session_take_prepared(&h,&media,path,item.format,&item.expected,1,1,&t,&e) && !h.media && !h.prepared && h.takes==1 && (epub?e.impl:t.impl)==original && !draws);
#ifdef PN_TEST_IMMUTABLE_FILE
    assert(!reads);
#else
    assert(reads);
#endif
    screen_t screen={.pixels=malloc(415872),.fail=true};assert(screen.pixels);
    assert(step(&t,&e,PN_APP_OPEN,2,&screen)==PN_IO && screen.calls==1 && !file_count(state));
    assert(first_layouts==1);
    assert((epub?pn_epub_app_progress(&e,&ep):pn_reader_app_progress(&t,&tp))==PN_EMPTY);
    pn_status_t closed=epub?pn_epub_app_close(&e,3):pn_reader_app_close(&t,3);
    // 前台失败OPEN可能创建可重建indexes目录，但不得产生阅读记录；后台仍不得新增任何根条目。
    // A failed foreground OPEN may create the rebuildable indexes directory, never reading records; idle preparation must add no root entries.
    assert(closed==PN_OK && pool.used==used && pool.live==live && !pn_media_active(&media) && !reading_records(state));size_t entries=file_count(state);
    // 部分正文或取消后的像素不得冒充完整首屏；正常绘制仍匹配完整结果。
    // Partial or cancelled pixels must never masquerade as a complete first body; normal painting still matches the full result.
    for(unsigned cancel=0;cancel<3;cancel++){
        assert(!pn_hot_session_prepare(&h,&pool,&media,&item,NULL,state,32,1,4+cancel,40+cancel));
        unsigned before=prerenders;assert(!pn_hot_session_paint(&h,4+cancel,1) && prerenders-before<=1 && pn_hot_session_paint_pending(&h));
        if(cancel==1){unsigned left=1;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel_after,&left);assert(pn_hot_session_paint(&h,5,8)==PN_CANCELLED);pn_cancel_leave(&scope);assert(!pn_hot_session_paint_pending(&h));}
        if(cancel==2){paint_cancelled=false;cancel_prerender=1;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel_after_paint,NULL);assert(pn_hot_session_paint(&h,6,8)==PN_CANCELLED && paint_cancelled);pn_cancel_leave(&scope);assert(!pn_hot_session_paint_pending(&h));paint_cancelled=false;}
        assert(!pn_hot_session_take_prepared(&h,&media,path,item.format,&item.expected,1,6,&t,&e));screen.compare=true;
        assert(step(&t,&e,PN_APP_OPEN,7,&screen)==PN_IO);
        assert((epub?pn_epub_app_close(&e,8):pn_reader_app_close(&t,8))==PN_OK && !reading_records(state) && pool.used==used && pool.live==live);
    }
    // 将分配故障落到新增正文绘制阶段，检查旧菜单和未显示析构，不局限于构造早期。
    // Inject faults in the new body stage, checking old menus and unshown disposal beyond early construction.
    for(unsigned n=1;n<=12;n++){
        assert(!pn_hot_session_prepare(&h,&pool,&media,&item,NULL,state,32,1,9,60+n));pool.fail_at=pool.attempts+n;unsigned steps=0;
        while(pn_hot_session_paint_pending(&h)){assert(++steps<4096);pn_status_t p=pn_hot_session_paint(&h,9,8);assert(p==PN_OK || p==PN_NO_MEMORY || p==PN_EMPTY);}
        pool.fail_at=0;assert(!pool.speculative && !menu.drops && !menu.owner.pins);
        assert(!pn_hot_session_take_prepared(&h,&media,path,item.format,&item.expected,1,10,&t,&e));screen.compare=true;
        assert(step(&t,&e,PN_APP_OPEN,11,&screen)==PN_IO && (epub?pn_epub_app_close(&e,12):pn_reader_app_close(&t,12))==PN_OK);
        assert(pool.used==used && pool.live==live && !reading_records(state) && file_count(state)==entries);
    }
    // 每个早期分配失败都必须归还准备对象，且不能驱逐已存在的菜单缓存。
    // Every early allocation failure must release preparation resources without evicting existing menu caches.
    for(unsigned n=1;n<=24;n++){pool.fail_at=pool.attempts+n;pn_status_t status=pn_hot_session_prepare(&h,&pool,&media,&item,NULL,state,32,1,10+n,10+n);pool.fail_at=0;assert(status==PN_OK || status==PN_NO_MEMORY || status==PN_EMPTY);assert(!pn_hot_session_drop(&h) && pool.used==used && pool.live==live && !pn_media_active(&media) && !menu.drops && !menu.owner.pins && file_count(state)==entries && !reading_records(state));}
    reads=0;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel_after_read,NULL);assert(pn_hot_session_prepare(&h,&pool,&media,&item,NULL,state,32,1,90,90)==PN_CANCELLED);pn_cancel_leave(&scope);
    assert(reads && !h.txt.impl && !h.epub.impl && !h.media && !h.prepared && pool.used==used && pool.live==live && !pn_media_active(&media) && !menu.drops && !menu.owner.pins && file_count(state)==entries);
    cancel_layout=true;layout_cancelled=false;pn_cancel_enter(&scope,after_layout,NULL);
    assert(pn_hot_session_prepare(&h,&pool,&media,&item,NULL,state,32,1,89,89)==PN_CANCELLED);pn_cancel_leave(&scope);cancel_layout=false;
    assert(layout_cancelled && !h.txt.impl && !h.epub.impl && pool.used==used && pool.live==live && !pn_media_active(&media) && !menu.drops && !menu.owner.pins && file_count(state)==entries);
    char missing[512];assert(snprintf(missing,sizeof missing,"%s/missing",state)>0);assert(pn_hot_session_prepare(&h,&pool,&media,&item,NULL,missing,32,1,91,91)==PN_EMPTY && access(missing,F_OK)!=0 && pool.used==used && !h.media);
    size_t limit=pool.limit;pool.limit=pool.used+PN_HOT_PREPARE_HEADROOM-1;reads=0;assert(pn_hot_session_prepare(&h,&pool,&media,&item,NULL,state,32,1,100,100)==PN_EMPTY && !reads);pool.limit=limit;
    assert(!pn_hot_session_prepare(&h,&pool,&media,&item,NULL,state,32,1,200,200));
    assert(!pn_hot_session_expire(&h,200+PN_HOT_SESSION_MS) && !h.media && !pn_media_active(&media));
    assert(pn_hot_session_prepare(&h,&pool,&media,&item,NULL,state,32,1,201+PN_HOT_SESSION_MS,200)==PN_EMPTY);
    assert(!pn_hot_session_prepare(&h,&pool,&media,&item,NULL,state,32,1,300+PN_HOT_SESSION_MS,300));
    assert(!pn_media_request_usb(&media) && pn_media_acquire(&media,PN_MEDIA_USB,&lease)==PN_BUSY);
    assert(!pn_hot_session_drop(&h) && file_count(state)==entries && !reading_records(state) && !pn_media_acquire(&media,PN_MEDIA_USB,&lease) && !pn_media_release(&media,&lease) && !pn_media_cancel_usb(&media));
    assert(!pn_hot_session_prepare(&h,&pool,&media,&item,NULL,state,32,1,400+PN_HOT_SESSION_MS,400));
    assert(!pn_media_detach(&media) && pn_hot_session_take_prepared(&h,&media,path,item.format,&item.expected,1,401+PN_HOT_SESSION_MS,&t,&e)==PN_EMPTY && !pn_hot_session_drop(&h));
    assert(!pn_media_active(&media) && pool.used==used && pool.live==live && !menu.drops && file_count(state)==entries && !reading_records(state));
    // 用真实显示/关闭生成非首屏进度，再检查未显示候选原位恢复及显式回书首。
    // Generate a non-first-page record by real test presentation/close, then check unshown resume and explicit beginning.
    assert(!pn_media_attach(&media,2));screen.fail=false;screen.compare=false;
    assert((epub?pn_epub_app_open_on_media(&e,&pool,&media,path,NULL,state,32,0):pn_reader_app_open_on_media(&t,&pool,&media,path,NULL,state,32,0))==PN_OK);
    assert(!step(&t,&e,PN_APP_OPEN,1,&screen));uint8_t *first=malloc(415872);assert(first);memcpy(first,screen.pixels,415872);
    assert(!step(&t,&e,PN_APP_NEXT,2,&screen));pn_txt_progress_t saved_txt;pn_epub_progress_t saved_epub;
    assert((epub?pn_epub_app_progress(&e,&saved_epub):pn_reader_app_progress(&t,&saved_txt))==PN_OK);
    assert((epub?pn_epub_app_close(&e,3):pn_reader_app_close(&t,3))==PN_OK);
    first_layouts=0;assert(!pn_hot_session_prepare(&h,&pool,&media,&item,NULL,state,32,1,4,500) && first_layouts==1);
    paint_all(&h,&pool,4);
    assert((epub?pn_epub_app_progress(&h.epub,&ep):pn_reader_app_progress(&h.txt,&tp))==PN_EMPTY);
    assert(!pn_hot_session_take_prepared(&h,&media,path,item.format,&item.expected,1,5,&t,&e));screen.compare=true;
    assert(!step(&t,&e,PN_APP_OPEN,6,&screen) && first_layouts==1);
    assert((epub?pn_epub_app_progress(&e,&ep):pn_reader_app_progress(&t,&tp))==PN_OK);
    if(epub)assert(!strcmp(ep.location.path,saved_epub.location.path) && ep.location.position.element==saved_epub.location.position.element && ep.location.position.run==saved_epub.location.position.run && ep.location.position.offset==saved_epub.location.position.offset);
    else assert(tp.source_offset==saved_txt.source_offset && tp.source_offset>0);
    assert((epub?pn_epub_app_close(&e,7):pn_reader_app_close(&t,7))==PN_OK);
    first_layouts=0;assert(!pn_hot_session_prepare(&h,&pool,&media,&item,NULL,state,32,1,8,501));
    paint_all(&h,&pool,8);
    assert(!pn_hot_session_take_prepared(&h,&media,path,item.format,&item.expected,1,9,&t,&e));memcpy(screen.pixels,first,415872);
    assert(!step(&t,&e,PN_APP_BEGINNING,10,&screen) && first_layouts==2);
    assert((epub?pn_epub_app_close(&e,11):pn_reader_app_close(&t,11))==PN_OK);
    assert(!pn_hot_session_prepare(&h,&pool,&media,&item,NULL,state,32,1,12,502) && !pn_hot_session_take_prepared(&h,&media,path,item.format,&item.expected,1,13,&t,&e));
    unsigned shown=screen.calls;assert(!pn_media_detach(&media) && step(&t,&e,PN_APP_OPEN,14,&screen)==PN_STALE_MEDIA && screen.calls==shown);
    assert((epub?pn_epub_app_close(&e,15):pn_reader_app_close(&t,15))==PN_OK);
    assert(pool.used==used && pool.live==live && !pn_media_active(&media));free(first);
    // 两种阅读线在跨8节点分片后仍与普通完整绘制相同，页脚保持正常提交态。
    // Both guide styles match ordinary full painting across eight-node chunks, with a normally committed footer.
    for(unsigned line=1;line<=2;line++){
        assert(!pn_media_attach(&media,3+line));screen.fail=false;screen.compare=false;
        assert((epub?pn_epub_app_open_on_media(&e,&pool,&media,path,NULL,state,32,0):pn_reader_app_open_on_media(&t,&pool,&media,path,NULL,state,32,0))==PN_OK && !step(&t,&e,PN_APP_OPEN,1,&screen));
        pn_style_t style;assert((epub?pn_epub_app_style_get(&e,&style):pn_reader_app_style_get(&t,&style))==PN_OK);style.reading_line=(uint8_t)line;style.reading_line_shade=2;style.reading_line_offset=2;
        assert((epub?pn_epub_app_style_apply(&e,&style,2,present,&screen):pn_reader_app_style_apply(&t,&style,2,present,&screen))==PN_OK && !step(&t,&e,PN_APP_OPEN,3,&screen));
        assert((epub?pn_epub_app_close(&e,4):pn_reader_app_close(&t,4))==PN_OK);
        assert(!pn_hot_session_prepare(&h,&pool,&media,&item,NULL,state,32,1,5,600+line));paint_all(&h,&pool,5);
        assert(!pn_hot_session_take_prepared(&h,&media,path,item.format,&item.expected,1,6,&t,&e));screen.compare=true;
        assert(!step(&t,&e,PN_APP_OPEN,7,&screen) && (epub?pn_epub_app_close(&e,8):pn_reader_app_close(&t,8))==PN_OK);
        assert(!pn_media_detach(&media) && !pn_media_active(&media) && pool.used==used && pool.live==live);
    }
    assert(((uint8_t *)menu.bytes)[0]==0x5a && ((uint8_t *)menu.bytes)[128u*1024u-1]==0x5a);pn_cache_detach(&menu.owner);pn_free(menu.bytes);assert(!pool.used && !pool.live);
    free(screen.pixels);puts("idle preparation: no early records, independent ownership, protected menus, cancel/budget/fault/TTL/USB/media checks passed");
}
int main(int argc,char **argv){assert(argc==3);pn_profile_port(phase_clock,phase_done,NULL);run(argv[1],false);run(argv[2],true);font_case(argv[1]);prepare_case(argv[1],false);prepare_case(argv[2],true);return 0;}
