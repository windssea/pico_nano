/* SPDX-License-Identifier: Apache-2.0
 * 中文：实际阅读器预绘制收敛后不得继续画字/分配或假报工作，回执和位置不变。
 * English: actual reader prerendering must converge without further glyph draws/allocations or false work reports, preserving receipts and positions. */
#define _POSIX_C_SOURCE 200809L
#include "pn_reader_app.h"
#include "pn_epub_app.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned draws,presents;static uint64_t frame_hash;
pn_status_t __real_pn_font_prerender(pn_font_t *,pn_frame_t *,uint32_t,int32_t,int,pn_font_render_t);
pn_status_t __wrap_pn_font_prerender(pn_font_t *f,pn_frame_t *b,uint32_t c,int32_t x,int y,pn_font_render_t m){draws++;return __real_pn_font_prerender(f,b,c,x,y,m);}
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t mode){
    (void)ctx;(void)mode;presents++;frame_hash=UINT64_C(14695981039346656037);
    for(int y=0;y<frame->height;y++)for(int x=0;x<frame->width/2;x++){frame_hash^=frame->pixels[(size_t)y*frame->stride+x];frame_hash*=UINT64_C(1099511628211);}
    return PN_OK;
}
int main(int argc,char **argv){
    assert(argc==3);
    for(unsigned mode=0;mode<4;mode++)for(unsigned kind=0;kind<2;kind++){
        pn_pool_t pool;assert(pn_pool_init(&pool,(mode&1u?2u:6u)*1024u*1024u,NULL,NULL,NULL)==PN_OK);pn_reader_app_t t={0};pn_epub_app_t e={0};
        char folder[]="/tmp/pn-quiet-XXXXXX",font_path[384];assert(mkdtemp(folder));pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
        if(mode&2u){snprintf(font_path,sizeof font_path,"%s/font.ttf",folder);FILE *f=fopen(font_path,"wb");assert(f);pn_text_source_t source=pn_font_builtin_source();uint8_t bytes[4096];for(uint64_t at=0;at<source.size;){size_t n;assert(source.read_at(source.ctx,at,bytes,sizeof bytes,&n)==PN_OK && n && fwrite(bytes,1,n,f)==n);at+=n;}assert(!fclose(f) && pn_font_system_open(&pool,&media,font_path)==PN_OK);}
        assert((kind?pn_epub_app_open_on_media(&e,&pool,&media,argv[2],NULL,folder,32,0):pn_reader_app_open_on_media(&t,&pool,&media,argv[1],NULL,folder,32,0))==PN_OK);
        assert((kind?pn_epub_app_step(&e,PN_APP_OPEN,1,present,NULL):pn_reader_app_step(&t,PN_APP_OPEN,1,present,NULL))==PN_OK);
        pn_txt_progress_t txt_before={0};pn_epub_progress_t epub_before={0};assert((kind?pn_epub_app_progress(&e,&epub_before):pn_reader_app_progress(&t,&txt_before))==PN_OK);
        unsigned loops=0;pn_page_bitmap_stats_t b={0};
        while(loops++<1000){pn_status_t s=kind?pn_epub_app_prefetch(&e):pn_reader_app_prefetch(&t);assert(s==PN_OK || s==PN_EMPTY);b=kind?pn_epub_app_prerender_stats(&e):pn_reader_app_prerender_stats(&t);if(b.completed)break;}
        printf("quiet_progress mode=%u kind=%u loops=%u complete=%llu steps=%llu\n",mode,kind,loops,(unsigned long long)b.completed,(unsigned long long)b.steps);fflush(stdout);
        assert(b.completed==1 && loops<1000);
        assert((kind?pn_epub_app_prefetch(&e):pn_reader_app_prefetch(&t))==PN_EMPTY);
        unsigned before=draws,receipts=presents;size_t attempts=pool.attempts;
        for(unsigned i=0;i<100;i++){assert((kind?pn_epub_app_prefetch(&e):pn_reader_app_prefetch(&t))==PN_EMPTY);assert(!(kind?pn_epub_app_warm_pending(&e):pn_reader_app_warm_pending(&t)));}
        b=kind?pn_epub_app_prerender_stats(&e):pn_reader_app_prerender_stats(&t);
        assert(b.completed==1 && draws==before && presents==receipts && pool.attempts==attempts);
        if(kind){pn_epub_progress_t after;assert(pn_epub_app_progress(&e,&after)==PN_OK && !strcmp(after.location.path,epub_before.location.path) && after.location.position.element==epub_before.location.position.element && after.location.position.run==epub_before.location.position.run && after.location.position.offset==epub_before.location.position.offset);}
        else{pn_txt_progress_t after;assert(pn_reader_app_progress(&t,&after)==PN_OK && after.source_offset==txt_before.source_offset);}
        // 必须真正消费已完成位图；A/B日志同时核对下一页完整帧摘要。/ Actually consume the completed bitmap; A/B logs also compare the whole next-frame digest.
        assert((kind?pn_epub_app_step(&e,PN_APP_NEXT,2,present,NULL):pn_reader_app_step(&t,PN_APP_NEXT,2,present,NULL))==PN_OK);
        pn_page_bitmap_stats_t consumed=kind?pn_epub_app_prerender_stats(&e):pn_reader_app_prerender_stats(&t);assert(consumed.hits>b.hits);
        printf("quiet_next mode=%u kind=%u frame=%016llx\n",mode,kind,(unsigned long long)frame_hash);
        assert((kind?pn_epub_app_close(&e,3):pn_reader_app_close(&t,3))==PN_OK && pn_font_system_close()==PN_OK && pool.used==0 && pool.live==0);
        printf("quiet_prefetch mode=%u kind=%u prep_steps=%u then_100_no_work=1\n",mode,kind,loops);
    }
    return 0;
}
