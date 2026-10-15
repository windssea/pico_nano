/* 中文：扫描期（呈现回调内）重叠准备下一页：逐帧不变、前台光栅减少、呈现失败不泄漏推测邻页。
 * English: scan-time overlap (inside the present callback) preparing the next page: identical frames, fewer foreground rasters, no speculative neighbor leaking after a failed present. */
#define _POSIX_C_SOURCE 200809L
#include "pn_reader_app.h"
#include "pn_epub_app.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned renders,background_renders;static bool inside_overlap;
FT_Error __real_FT_Load_Glyph(FT_Face,FT_UInt,FT_Int32);
FT_Error __wrap_FT_Load_Glyph(FT_Face f,FT_UInt i,FT_Int32 flags){if(flags&FT_LOAD_RENDER){if(inside_overlap)background_renders++;else renders++;}return __real_FT_Load_Glyph(f,i,flags);}
#define TURNS 8
static pn_reader_app_t txt;static pn_epub_app_t epub;static unsigned kind;
static bool overlap,fail_now;static unsigned overlap_steps;
static uint64_t frames[TURNS+1];static unsigned shown;
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t refresh){(void)ctx;(void)refresh;uint64_t h=14695981039346656037ull;
    for(int y=0;y<frame->height;y++){const uint8_t *row=frame->pixels+(size_t)y*frame->stride;for(int x=0;x<frame->width/2;x++)h=(h^row[x])*1099511628211ull;}
    // 设备端在等扫描回执时调用overlap；这里在回调内模拟同一时机。/ The device calls overlap while awaiting the scan receipt; simulate the same moment inside the callback.
    inside_overlap=true;if(overlap)for(unsigned i=0;i<4096;i++){pn_status_t s=kind?pn_epub_app_overlap(&epub):pn_reader_app_overlap(&txt);if(s!=PN_OK)break;overlap_steps++;}inside_overlap=false;
    if(fail_now){fail_now=false;return PN_IO;}
    assert(shown<=TURNS);frames[shown++]=h;return PN_OK;}
/// fail_at为0不注入失败，否则第fail_at次翻页的呈现失败后重试同一步。/ fail_at zero injects nothing; otherwise the present of turn fail_at fails and the same step is retried.
static unsigned run(const char *path,bool with_overlap,unsigned fail_at,uint64_t out[TURNS+1]){
    pn_pool_t p;assert(!pn_pool_init(&p,6u*1024u*1024u,NULL,NULL,NULL));overlap=with_overlap;overlap_steps=0;shown=0;memset(frames,0,sizeof frames);
    assert((kind?pn_epub_app_open(&epub,&p,path,NULL,NULL,44,0):pn_reader_app_open(&txt,&p,path,NULL,NULL,44,0))==PN_OK);
    assert((kind?pn_epub_app_step(&epub,PN_APP_OPEN,1,present,NULL):pn_reader_app_step(&txt,PN_APP_OPEN,1,present,NULL))==PN_OK);
    renders=background_renders=0;uint64_t at=2;
    // 连续快翻：两次翻页之间不调用空闲prefetch，只有扫描期重叠。/ Rapid flipping: no idle prefetch between turns, only scan-time overlap.
    for(unsigned i=1;i<=TURNS;i++){
        if(i==fail_at){fail_now=true;assert((kind?pn_epub_app_step(&epub,PN_APP_NEXT,at++,present,NULL):pn_reader_app_step(&txt,PN_APP_NEXT,at++,present,NULL))==PN_IO);}
        assert((kind?pn_epub_app_step(&epub,PN_APP_NEXT,at++,present,NULL):pn_reader_app_step(&txt,PN_APP_NEXT,at++,present,NULL))==PN_OK);
    }
    unsigned r=renders;assert(shown==TURNS+1);memcpy(out,frames,sizeof frames);
    assert((kind?pn_epub_app_close(&epub,at):pn_reader_app_close(&txt,at))==PN_OK && !p.used && !p.live);return r;
}
int main(int argc,char **argv){
    assert(argc==3);
    for(kind=0;kind<2;kind++){const char *path=argv[1+kind];uint64_t a[TURNS+1],b[TURNS+1],c[TURNS+1],d[TURNS+1];
        unsigned plain=run(path,false,0,a),overlapped=run(path,true,0,b);unsigned steps=overlap_steps;
        assert(!memcmp(a,b,sizeof a));for(unsigned i=1;i<=TURNS;i++)assert(a[i]!=a[i-1]);
        printf("%s rapid turns: foreground rasters plain=%u overlap=%u background=%u overlap_steps=%u, identical frames\n",kind?"EPUB":"TXT",plain,overlapped,background_renders,steps);fflush(stdout);
        assert(steps>0 && overlapped*2<plain); // 剩余多为页脚数字（界面字体，不预热）/ Remaining renders are mostly footer digits (UI font, not warmed)
        // 第4次翻页呈现失败：推测邻页不能让重试或之后的页面不同。/ Turn 4 present fails: the speculative neighbor must not change the retry or later pages.
        (void)run(path,false,4,c);(void)run(path,true,4,d);assert(!memcmp(c,d,sizeof c) && !memcmp(a,c,sizeof a));
    }
    puts("Scan-time overlap: identical frames, fewer foreground rasters, failed presents keep pages unchanged");return 0;
}
