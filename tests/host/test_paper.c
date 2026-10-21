/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实图片/缓存/逐书设置与TXT/EPUB显示确认回归。 / English: real artwork, caches, per-book settings and TXT/EPUB presentation-receipt regression. */
#define _POSIX_C_SOURCE 200809L
#include "pn_background_gallery.h"
#include "pn_reader_app.h"
#include "pn_epub_app.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
static uint8_t saved[PN_WALLPAPER_BYTES],shown[PN_WALLPAPER_BYTES];
static const char *font_path;
typedef struct {bool fail;unsigned calls;} screen_t;
static pn_status_t present(void *ctx,const pn_frame_t *f,pn_refresh_t refresh){screen_t *s=ctx;assert(refresh==PN_REFRESH_GC16 || refresh==PN_REFRESH_GL16);s->calls++;if(s->fail)return PN_IO;memcpy(shown,f->pixels,sizeof shown);return PN_OK;}
static void reading(pn_pool_t *pool,const char *book,const char *state,const pn_style_t *paper,bool epub){
    pn_reader_app_t txt={0};pn_epub_app_t zip={0};screen_t screen={0};
    assert((epub?pn_epub_app_open(&zip,pool,book,font_path,state,44,1):pn_reader_app_open(&txt,pool,book,font_path,state,44,1))==PN_OK);
    assert((epub?pn_epub_app_step(&zip,PN_APP_OPEN,2,present,&screen):pn_reader_app_step(&txt,PN_APP_OPEN,2,present,&screen))==PN_OK);
    pn_style_t original,draft,got;assert((epub?pn_epub_app_style_get(&zip,&original):pn_reader_app_style_get(&txt,&original))==PN_OK);draft=original;
    draft.background=paper->background;strcpy(draft.background_name,paper->background_name);draft.background_identity=paper->background_identity;
    // 失败的面板回执不能保存选择，也不能留下新底图。/ Failed panel receipts must neither save a selection nor retain the candidate underlay.
    screen.fail=true;assert((epub?pn_epub_app_style_apply(&zip,&draft,3,present,&screen):pn_reader_app_style_apply(&txt,&draft,3,present,&screen))==PN_IO);screen.fail=false;
    assert((epub?pn_epub_app_style_get(&zip,&got):pn_reader_app_style_get(&txt,&got))==PN_OK && !memcmp(&got,&original,sizeof got));
    assert((epub?pn_epub_app_style_apply(&zip,&draft,4,present,&screen):pn_reader_app_style_apply(&txt,&draft,4,present,&screen))==PN_OK);
    assert((epub?pn_epub_app_style_get(&zip,&got):pn_reader_app_style_get(&txt,&got))==PN_OK && !memcmp(&got,&draft,sizeof got));
    memcpy(saved,shown,sizeof saved);
    assert((epub?pn_epub_app_step(&zip,PN_APP_NEXT,5,present,&screen):pn_reader_app_step(&txt,PN_APP_NEXT,5,present,&screen))==PN_OK);
    assert((epub?pn_epub_app_step(&zip,PN_APP_PREVIOUS,6,present,&screen):pn_reader_app_step(&txt,PN_APP_PREVIOUS,6,present,&screen))==PN_OK && !memcmp(saved,shown,sizeof saved));
    assert((epub?pn_epub_app_suspend(&zip,7):pn_reader_app_suspend(&txt,7))==PN_OK);
    assert((epub?pn_epub_app_resume(&zip,8,present,&screen):pn_reader_app_resume(&txt,8,present,&screen))==PN_OK && !memcmp(saved,shown,sizeof saved));
    assert((epub?pn_epub_app_close(&zip,9):pn_reader_app_close(&txt,9))==PN_OK);
    assert((epub?pn_epub_app_open(&zip,pool,book,font_path,state,44,10):pn_reader_app_open(&txt,pool,book,font_path,state,44,10))==PN_OK);
    assert((epub?pn_epub_app_style_get(&zip,&got):pn_reader_app_style_get(&txt,&got))==PN_OK && !memcmp(&got,&draft,sizeof got));
    assert((epub?pn_epub_app_step(&zip,PN_APP_OPEN,11,present,&screen):pn_reader_app_step(&txt,PN_APP_OPEN,11,present,&screen))==PN_OK && !memcmp(saved,shown,sizeof saved));
    assert((epub?pn_epub_app_close(&zip,12):pn_reader_app_close(&txt,12))==PN_OK);
    printf("paper reading %s: first NEXT/PREV, failed receipt, suspend/resume and cold reopen exact pixels passed\n",epub?"EPUB":"TXT");
}
int main(int argc,char **argv){
    assert(argc==6);font_path=getenv("PN_PAPER_FONT");pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    pn_background_gallery_t gallery={0};assert(pn_background_gallery_open(&gallery,&pool,&media)==PN_OK && pn_background_gallery_count(&gallery)==6);
    unsigned steps=0;pn_status_t s;while((s=pn_background_gallery_tick(&gallery))!=PN_EMPTY){assert(s==PN_OK || s==PN_BUSY);assert(++steps<20000);}
    assert(pn_background_gallery_can_page(&gallery,false));assert(pn_background_gallery_page(&gallery,false)==PN_OK && pn_background_gallery_count(&gallery)==1);assert(pn_background_gallery_can_page(&gallery,true));assert(pn_background_gallery_page(&gallery,true)==PN_OK);
    pn_style_t style=pn_style_default(44);assert(pn_background_gallery_choose(&gallery,0,&style)==PN_OK && pn_style_validate(&style)==PN_OK);pn_background_gallery_close(&gallery);
    pn_paper_t paper={0};assert(pn_paper_open(&paper,&pool,&media,&lease,&style)==PN_OK && pn_paper_matches(&paper,&style));
    pn_frame_t frame={shown,684,1216,342};assert(pn_paper_draw(&paper,style.background,&frame)==PN_OK);memcpy(saved,shown,sizeof saved);
    // 独立源图为16条原生灰带；检查每个像素及左右边缘，防止缩放/留白。/ The independent source contains 16 native gray bands: verify every pixel and both edges against scaling/margin regressions.
    for(int y=0;y<1216;y++)for(int x=0;x<684;x++)assert(pn_frame_get(&frame,x,y)==(uint8_t)((x/43)%16));
    char hidden[512];snprintf(hidden,sizeof hidden,"%s.hidden",argv[1]);assert(!rename(argv[1],hidden));size_t attempts=pool.attempts;
    memset(shown,0,sizeof shown);assert(pn_paper_draw(&paper,style.background,&frame)==PN_OK && !memcmp(shown,saved,sizeof saved));assert(pn_paper_region(&paper,style.background,&frame,0,32)==PN_OK && pool.attempts==attempts);assert(!rename(hidden,argv[1]));
    size_t used=pool.used;pool.fail_at=pool.attempts+1;pn_paper_t failed={0};assert(pn_paper_open(&failed,&pool,&media,&lease,&style)==PN_NO_MEMORY && !failed.frame.pixels && pool.used==used);pool.fail_at=0;
    pn_paper_close(&paper);assert(!pool.used);
    {struct stat st;assert(!stat(argv[1],&st));FILE *file=fopen(argv[1],"r+b");assert(file && !fseek(file,64,SEEK_SET));int original=fgetc(file);assert(original!=EOF && !fseek(file,64,SEEK_SET) && fputc(original^1,file)!=EOF && !fclose(file));struct utimbuf times={st.st_atime,st.st_mtime};assert(!utime(argv[1],&times));
     assert(pn_paper_open(&failed,&pool,&media,&lease,&style)==PN_STALE_JOB && !failed.frame.pixels && !pool.used);
     file=fopen(argv[1],"r+b");assert(file && !fseek(file,64,SEEK_SET) && fputc(original,file)!=EOF && !fclose(file) && !utime(argv[1],&times));}
    pn_style_t stale=style;stale.background_identity.sha256[0]^=1;assert(pn_paper_open(&failed,&pool,&media,&lease,&stale)==PN_STALE_JOB && !failed.frame.pixels && !pool.used);
    // 错误缓存必须重建；相同源预览仍逐像素一致。/ Corrupt caches must rebuild while preserving every source-preview pixel.
    FILE *cache=fopen(argv[2],"r+b");assert(cache && !fseek(cache,-1,SEEK_END));int c=fgetc(cache);assert(c!=EOF && !fseek(cache,-1,SEEK_END) && fputc(c^1,cache)!=EOF && !fclose(cache));
    assert(pn_paper_open(&paper,&pool,&media,&lease,&style)==PN_OK);pn_paper_close(&paper);assert(!pool.used);
    assert(pn_media_release(&media,&lease)==PN_OK);
    reading(&pool,argv[3],argv[5],&style,false);assert(!pool.used);
    reading(&pool,argv[4],argv[5],&style,true);assert(!pool.used && !pool.live);
    // 恢复缺图仍可预览/取消，不重写原排版引用。/ Restoring a missing source still allows preview/cancel without rewriting its typesetting reference.
    assert(!rename(argv[1],hidden));pn_reader_app_t absent={0};screen_t screen={0};pn_style_t kept;
    assert(pn_reader_app_open(&absent,&pool,argv[3],font_path,argv[5],44,20)==PN_OK && pn_reader_app_style_get(&absent,&kept)==PN_OK && kept.background==PN_READING_BACKGROUND_EXTERNAL);
    assert(pn_reader_app_step(&absent,PN_APP_OPEN,21,present,&screen)==PN_OK);
    assert(pn_reader_app_style_preview(&absent,&kept,22,present,&screen)==PN_OK && pn_reader_app_style_cancel(&absent,23,present,&screen)==PN_OK);
    assert(pn_reader_app_close(&absent,24)==PN_OK && !pool.used && !rename(hidden,argv[1]));
    printf("paper caches: %u bounded thumbnail steps; pool peak %zu of %zu bytes\n",steps,pool.peak,pool.limit);return 0;
}
