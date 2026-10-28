/* SPDX-License-Identifier: Apache-2.0
 * 中文：前台/预绘制完整像素、真实位置与字形绘制调用对照，含跨章、重排、失败、低预算和未完成帧。
 * English: compare complete pixels, real locations and foreground glyph draws, including chapters, reflow, failures, low budgets and incomplete frames. */
#define _POSIX_C_SOURCE 200809L
#include "pn_reader_app.h"
#include "pn_epub_app.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static pn_reader_app_t txt;static pn_epub_app_t epub;static unsigned kind,foreground,background,loops,frames,badge;
static bool cancel_arm,cancel_pending;
static bool stop(void *ctx){(void)ctx;return cancel_pending;}
static bool inside_present,control,fail_present;static FILE *oracle;static pn_pool_t *current_pool;
pn_status_t __real_pn_font_draw(pn_font_t *,pn_frame_t *,uint32_t,int32_t,int,pn_font_render_t);
pn_status_t __real_pn_font_prerender(pn_font_t *,pn_frame_t *,uint32_t,int32_t,int,pn_font_render_t);
pn_status_t __wrap_pn_font_draw(pn_font_t *font,pn_frame_t *frame,uint32_t cp,int32_t x,int baseline,pn_font_render_t mode){if(inside_present)background++;else foreground++;return __real_pn_font_draw(font,frame,cp,x,baseline,mode);}
pn_status_t __wrap_pn_font_prerender(pn_font_t *font,pn_frame_t *frame,uint32_t cp,int32_t x,int baseline,pn_font_render_t mode){if(inside_present)background++;else foreground++;pn_status_t s=__real_pn_font_prerender(font,frame,cp,x,baseline,mode);if(s==PN_OK && cancel_arm && inside_present){cancel_arm=false;cancel_pending=true;}return s;}
static pn_status_t layer(void *ctx,pn_frame_t *frame){(void)ctx;pn_frame_rect(frame,4,4,7,7,(uint8_t)(badge++%16));return PN_OK;}
static pn_status_t step(pn_reader_action_t action,uint64_t at,pn_reader_present_fn present){return kind?pn_epub_app_step(&epub,action,at,present,NULL):pn_reader_app_step(&txt,action,at,present,NULL);}
static pn_status_t overlap(void){return kind?pn_epub_app_overlap(&epub):pn_reader_app_overlap(&txt);}
static pn_page_bitmap_stats_t stats(void){return kind?pn_epub_app_prerender_stats(&epub):pn_reader_app_prerender_stats(&txt);}
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t refresh){
    (void)ctx;(void)refresh;size_t count=(size_t)frame->width/2*frame->height;uint8_t *snapshot=malloc(count);assert(snapshot);
    for(int y=0;y<frame->height;y++)memcpy(snapshot+(size_t)y*frame->width/2,frame->pixels+(size_t)y*frame->stride,(size_t)frame->width/2);
    inside_present=true;for(unsigned i=0;i<loops;i++){pn_status_t s=overlap();assert(s==PN_OK || s==PN_EMPTY);if(s!=PN_OK)break;}inside_present=false;
    // 同时比较扫描期间的真实提交帧，后台不能改变任意一行。/ Compare the actual submitted frame during overlapping; background work must never change any row.
    for(int y=0;y<frame->height;y++)assert(!memcmp(snapshot+(size_t)y*frame->width/2,frame->pixels+(size_t)y*frame->stride,(size_t)frame->width/2));
    pn_cache_snapshot(current_pool);size_t sum=0;for(unsigned k=0;k<PN_CACHE_COUNT;k++){assert(current_pool->cache_stats[k].bytes<=current_pool->cache_caps[k]);sum+=current_pool->cache_stats[k].bytes;}assert(sum<=current_pool->cache_limit && current_pool->used<=current_pool->limit);
    if(fail_present){fail_present=false;free(snapshot);return PN_IO;}
    if(control)assert(fwrite(snapshot,1,count,oracle)==count);
    else{uint8_t *expected=malloc(count);assert(expected && fread(expected,1,count,oracle)==count);assert(!memcmp(snapshot,expected,count));free(expected);}
    frames++;free(snapshot);return PN_OK;
}
typedef struct {pn_txt_progress_t txt;pn_epub_progress_t epub;} progress_t;
static progress_t progress(void){progress_t p={0};assert((kind?pn_epub_app_progress(&epub,&p.epub):pn_reader_app_progress(&txt,&p.txt))==PN_OK);return p;}
static bool same(progress_t a,progress_t b){return kind?!strcmp(a.epub.location.path,b.epub.location.path) && a.epub.location.chapter_start==b.epub.location.chapter_start && a.epub.location.position.element==b.epub.location.position.element && a.epub.location.position.run==b.epub.location.position.run && a.epub.location.position.offset==b.epub.location.position.offset && a.epub.location.position.kind==b.epub.location.position.kind:a.txt.source_offset==b.txt.source_offset && a.txt.encoding==b.txt.encoding && a.txt.paragraph_version==b.txt.paragraph_version;}
static pn_status_t style_apply(pn_style_t *style,uint64_t at){return kind?pn_epub_app_style_apply(&epub,style,at,present,NULL):pn_reader_app_style_apply(&txt,style,at,present,NULL);}
static void run(const char *path,bool enabled,unsigned mode){
    pn_pool_t pool;assert(!pn_pool_init(&pool,mode>=3 && mode<6?2u*1024u*1024u:6u*1024u*1024u,NULL,NULL,NULL));current_pool=&pool;
    if(mode==3)pool.cache_caps[PN_CACHE_BITMAP]=0;
    foreground=background=frames=badge=0;loops=mode>=6?0:mode==1?1:4096;control=!enabled;
    assert((kind?pn_epub_app_open(&epub,&pool,path,NULL,NULL,44,0):pn_reader_app_open(&txt,&pool,path,NULL,NULL,44,0))==PN_OK);
    assert((kind?pn_epub_app_prerender(&epub,enabled):pn_reader_app_prerender(&txt,enabled))==PN_OK);
    pn_reader_body_layer_t decoration={.paint=layer};assert((kind?pn_epub_app_body_layer(&epub,&decoration):pn_reader_app_body_layer(&txt,&decoration))==PN_OK);
    assert(step(PN_APP_OPEN,1,present)==PN_OK);uint64_t at=2;
    if(mode==5 || mode>=6){pn_style_t paper;assert((kind?pn_epub_app_style_get(&epub,&paper):pn_reader_app_style_get(&txt,&paper))==PN_OK);paper.background=7;assert(style_apply(&paper,at++)==PN_OK);}
    foreground=background=0;
    for(unsigned i=0;i<8;i++){
        if(enabled && mode>=6){
            // 同源真实部分预绘；取消发生在完成一个字形后，旧位置保持，NEXT必须消费保留的前缀。
            // Exercise real partial prerendering; cancel after one completed glyph, preserve location, and require NEXT to consume the retained prefix.
            pn_page_bitmap_stats_t before_stats=stats();progress_t before=progress();inside_present=true;
            for(unsigned n=0;stats().steps==before_stats.steps;n++){assert(n<4096);pn_status_t warmed=overlap();assert(warmed==PN_OK || warmed==PN_EMPTY);}
            if(mode==7){cancel_pending=false;cancel_arm=true;pn_cancel_scope_t scope;pn_cancel_enter(&scope,stop,NULL);(void)overlap();pn_cancel_leave(&scope);assert(cancel_pending && !cancel_arm);cancel_pending=false;pn_epub_app_cache_activity(&epub,at);}
            inside_present=false;assert(same(before,progress()) && stats().completed==before_stats.completed);
        }
        if((mode==2 || mode==8) && i==3){progress_t before=progress();fail_present=true;assert(step(PN_APP_NEXT,at++,present)==PN_IO && same(before,progress()));}
        assert(step(PN_APP_NEXT,at++,present)==PN_OK);
    }
    unsigned front=foreground,back=background;pn_page_bitmap_stats_t s=stats();
    if(kind)assert(!strcmp(progress().epub.location.path,"b.xhtml"));
    printf("%s mode=%u enabled=%d: front_glyph_draws=%u background=%u frame_hits=%zu completed=%zu steps=%zu denied=%zu bytes=%zu peak=%zu\n",kind?"EPUB":"TXT",mode,enabled,front,back,s.hits,s.completed,s.steps,s.denied,s.bytes,pool.peak);fflush(stdout);
    if(enabled && !mode)assert(s.hits>=6 && s.completed>=6);
    if(enabled && (mode==1 || mode==3))assert(!s.hits);
    if(enabled && mode>=6)assert(s.hits>=8 && s.completed>=8);
    static unsigned baseline;if(control)baseline=front;else if(!mode)assert(front*3<baseline);
    // 反向与重排后保持原位置和完整像素，阅读线跨8节点边界。/ Preserve positions and complete pixels after reversal/reflow, with guides crossing 8-node chunks.
    assert(step(PN_APP_PREVIOUS,at++,present)==PN_OK);assert(step(PN_APP_NEXT,at++,present)==PN_OK);
    pn_style_t style;assert((kind?pn_epub_app_style_get(&epub,&style):pn_reader_app_style_get(&txt,&style))==PN_OK);style.pixels=36;style.reading_line=2;style.reading_line_offset=2;
    assert(style_apply(&style,at++)==PN_OK);assert(step(PN_APP_NEXT,at++,present)==PN_OK);assert(step(PN_APP_PREVIOUS,at++,present)==PN_OK);
    assert(step(PN_APP_BEGINNING,at++,present)==PN_OK);assert(step(PN_APP_NEXT,at++,present)==PN_OK);
    progress_t before=progress();assert((kind?pn_epub_app_media_lost(&epub):pn_reader_app_media_lost(&txt))==PN_OK);assert(step(PN_APP_NEXT,at++,present)==PN_STALE_MEDIA && same(before,progress()));
    assert((kind?pn_epub_app_close(&epub,at):pn_reader_app_close(&txt,at))==PN_OK && !pool.used && !pool.live && !pool.caches);
}
int main(int argc,char **argv){
    assert(argc==3);for(kind=0;kind<2;kind++)for(unsigned mode=0;mode<9;mode++){
        if(!kind && mode>=6)continue;
        oracle=tmpfile();assert(oracle);run(argv[1+kind],false,mode);unsigned count=frames;assert(!fflush(oracle) && !fseek(oracle,0,SEEK_SET));run(argv[1+kind],true,mode);assert(frames==count && fgetc(oracle)==EOF && !fclose(oracle));
    }
    puts("Prerender: complete frame memcmp, unchanged scanning pixels/failed positions, cross-chapter mixed sizes, live layers, guides/reflow/reversal, partial/low-budget fallback and clean release");return 0;
}
