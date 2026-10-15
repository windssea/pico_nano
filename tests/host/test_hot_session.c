/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实TXT/EPUB会话所有权、像素、失败回执、预算及介质交接。
 * English: actual TXT/EPUB session ownership, pixels, failed receipts, budgets and media handoff. */
#define _POSIX_C_SOURCE 200809L
#include "pn_hot_session.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
static unsigned draws,reads;
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
#endif
    assert(pn_media_detach(&media)==PN_OK);
    assert(pn_hot_session_resume(&h,&media,book,PN_BOOK_TXT,NULL,1,6,&t,&e,present,&s)==PN_EMPTY && !t.impl);
    assert(pn_hot_session_drop(&h)==PN_OK && pn_font_system_close()==PN_OK && pool.used==0 && pool.live==0 && pn_media_active(&media)==0);
    assert(!unlink(path));free(s.pixels);
}
int main(int argc,char **argv){assert(argc==3);run(argv[1],false);run(argv[2],true);font_case(argv[1]);return 0;}
