/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：等待画面布局、无损像素姿态与低频调度边界。
 * English: waiting-view layout, pixel-pose bounds and slow scheduling boundaries.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pn_reader_chrome.h"
#include "pn_widgets.h"

static void centered(const pn_frame_t *f,int y0,int y1){
    int left=f->width,right=-1;
    for(int y=y0;y<y1;y++)for(int x=0;x<f->width;x++)if(pn_frame_get(f,x,y)<=PN_UI_MUTED){if(x<left)left=x;if(x>right)right=x;}
    assert(right>=left && abs(left+right-(f->width-1))<=16);
}
static void capture(const pn_frame_t *f,const char *directory,unsigned phase){
    char path[1024];snprintf(path,sizeof path,"%s/loading-%u.pgm",directory,phase);FILE *out=fopen(path,"wb");assert(out);
    assert(fprintf(out,"P5\n%d %d\n255\n",f->width,f->height)>0);
    for(int y=0;y<f->height;y++)for(int x=0;x<f->width;x++){uint8_t gray=pn_frame_get(f,x,y)*17;assert(fwrite(&gray,1,1,out)==1);}
    assert(!fclose(out));
}
int main(void){
    pn_loading_motion_t motion={0};assert(!pn_loading_motion_poll(&motion,10000));
    pn_loading_motion_begin(&motion,100);assert(motion.phase==0 && !pn_loading_motion_poll(&motion,199));
    assert(pn_loading_motion_poll(&motion,200) && motion.phase==2);assert(!pn_loading_motion_poll(&motion,200));
    assert(!pn_loading_motion_poll(&motion,949));assert(pn_loading_motion_poll(&motion,950) && motion.phase==3);
    assert(pn_loading_motion_poll(&motion,100000) && motion.phase==0);assert(!pn_loading_motion_poll(&motion,50));
    assert(!pn_loading_motion_poll(&motion,799));assert(pn_loading_motion_poll(&motion,800) && motion.phase==1);
    pn_loading_motion_begin(&motion,900);assert(!motion.advanced && !pn_loading_motion_poll(&motion,999));
    motion.active=false;assert(!pn_loading_motion_poll(&motion,200000));
    pn_pool_t pool;assert(pn_pool_init(&pool,1024*1024,NULL,NULL,NULL)==PN_OK);
    uint8_t *bytes=pn_alloc(&pool,684*1216/2),*previous=pn_alloc(&pool,684*1216/2);pn_frame_t frame;
    assert(bytes && previous && pn_frame_bind(&frame,bytes,684*1216/2,684,1216));
    for(unsigned phase=0;phase<4;phase++){
        pn_frame_clear(&frame,15);size_t before=pool.used;pn_w_loading_panda(&frame,214,470,phase);assert(pool.used==before);
        if(phase){assert(memcmp(previous,bytes,684*1216/2));}
        memcpy(previous,bytes,684*1216/2);
        for(int y=0;y<1216;y++)for(int x=0;x<684;x++)if(x<214 || x>=470 || y<470 || y>=614)assert(pn_frame_get(&frame,x,y)==15);
    }
    pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();assert(pn_font_open(&font,&pool,&source,28)==PN_OK);
    pn_frame_clear(&frame,15);const char *detail="A quiet moment while the next chapter is getting ready";
    assert(pn_w_loading_phase(&font,&frame,"Opening your book",detail,-1,-1,1)==PN_OK && font.pixels==28);
    int top=(1216-300)/2;centered(&frame,top,top+144);centered(&frame,top+150,top+188);centered(&frame,top+202,top+229);centered(&frame,top+234,top+261);
    // 只换插画与完整重绘逐像素一致，包括旧姿态擦除和文字/边框保留。/ Artwork-only updates equal full redraws pixel for pixel, including old-pose erasure and retained text/borders.
    pn_frame_t full;assert(pn_frame_bind(&full,previous,684*1216/2,684,1216));
    for(int y=-1;y<=600;y+=601){
        pn_frame_clear(&frame,15);assert(pn_w_loading_phase(&font,&frame,"Opening your book",detail,-1,y,0)==PN_OK);memcpy(previous,bytes,684*1216/2);
        for(unsigned phase=1;phase<8;phase++){
            size_t before=pool.used;pn_w_loading_pose(&frame,y,phase);assert(pool.used==before);
            pn_frame_clear(&full,15);assert(pn_w_loading_phase(&font,&full,"Opening your book",detail,-1,y,phase)==PN_OK);assert(!memcmp(previous,bytes,684*1216/2));
        }
    }
    pn_free(previous);pn_frame_clear(&frame,15);assert(pn_w_loading_phase(&font,&frame,"Opening your book",detail,-1,-1,0)==PN_OK);
    for(int y=0;y<1216;y++)for(int x=0;x<684;x++)if(y<top || y>=top+300 || x<32 || x>=652)assert(pn_frame_get(&frame,x,y)==15);
    assert(pn_w_loading(&font,&frame,"Working",detail,0,600)==PN_OK);assert(pn_w_loading(&font,&frame,"Working",detail,100,600)==PN_OK);
    assert(pn_w_loading(&font,&frame,"Working",detail,101,600)==PN_INVALID);
    // 开书反馈静态，原生封面逐像素一致，缺封面与长元数据也可绘制。/ Opening feedback is static; native covers match every pixel, with missing-cover and long-metadata fallbacks.
    pn_book_preview_t *preview=calloc(1,sizeof *preview);assert(preview);preview->ready=true;
    strcpy(preview->display.title,"A book with a deliberately long title to wrap across two lines");strcpy(preview->display.author,"A verified author");
    pn_frame_t thumb;assert(pn_frame_bind(&thumb,preview->pixels,PN_COVER_BYTES,PN_COVER_WIDTH,PN_COVER_HEIGHT));
    for(int y=0;y<PN_COVER_HEIGHT;y++)for(int x=0;x<PN_COVER_WIDTH;x++)pn_frame_pixel(&thumb,x,y,(unsigned)(x+y)%16);
    assert(pn_reader_opening_render(&font,&frame,"fallback.epub",preview,NULL)==PN_OK && font.pixels==28);
    for(int y=0;y<PN_COVER_HEIGHT;y++)for(int x=0;x<PN_COVER_WIDTH;x++)assert(pn_frame_get(&frame,250+x,270+y)==pn_frame_get(&thumb,x,y));
    uint8_t *static_frame=malloc(415872);assert(static_frame);assert(pn_reader_loading_render_phase(&font,&frame,"fallback.epub",0)==PN_OK);memcpy(static_frame,bytes,415872);
    for(unsigned phase=1;phase<4;phase++){assert(pn_reader_loading_render_phase(&font,&frame,"fallback.epub",phase)==PN_OK);assert(!memcmp(static_frame,bytes,415872));}
    free(static_frame);free(preview);
    const char *directory=getenv("PN_LOADING_PREVIEW");
    pn_w_font_scope_t scope={0};
    if(directory){
        assert(pn_w_font_scope_begin(&scope,&pool,NULL)==PN_OK);
        for(unsigned phase=0;phase<4;phase++){assert(pn_reader_loading_render_phase(&font,&frame,"朝花夕拾（鲁迅散文集）.epub",phase)==PN_OK);capture(&frame,directory,phase);}
        pn_w_font_scope_end(&scope);
    }
    pn_font_close(&font);pn_free(bytes);assert(!pool.live && !pool.used);return 0;
}
