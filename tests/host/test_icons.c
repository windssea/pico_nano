/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：图标与抗锯齿图元：每个图标都会落笔且不越界；PN_ICON_SHEET指定路径时另存图标表供审查。
 * English: icons and anti-aliased primitives: every icon draws and stays in bounds; PN_ICON_SHEET names a path to save a review sheet.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "pn_widgets.h"
#include "pn_wallpaper.h"
static unsigned dark(const pn_frame_t *f,int x,int y,int w,int h){unsigned n=0;for(int j=y;j<y+h;j++)for(int i=x;i<x+w;i++)if(pn_frame_get(f,i,j)<15)n++;return n;}
int main(void){
    pn_pool_t pool;assert(pn_pool_init(&pool,1024*1024,NULL,NULL,NULL)==0);
    uint8_t *bytes=pn_alloc(&pool,684*1216/2);pn_frame_t frame;assert(pn_frame_bind(&frame,bytes,684*1216/2,684,1216));
    pn_frame_clear(&frame,PN_UI_PAPER);
    /* 每个图标在自己的格子里落笔，且不画出格子外一像素以外。/ Every icon marks its own cell and stays within one pixel of it. */
    bool bad=false;
    for(int i=0;i<PN_ICON_COUNT;i++){
        int x=40+(i%5)*120,y=40+(i/5)*120;pn_w_icon(&frame,(pn_icon_t)i,x,y,64,PN_UI_INK);
        assert(dark(&frame,x-1,y-1,66,66)>40);
        bool inside=dark(&frame,x-14,y-14,92,10)==0 && dark(&frame,x-14,y+68,92,10)==0 && dark(&frame,x-14,y-4,10,76)==0 && dark(&frame,x+68,y-4,10,76)==0;
        if(!inside)fprintf(stderr,"icon %d leaves its cell\n",i);
        bad|=!inside;
    }
    bool icons_ok=!bad;
    /* 抗锯齿：圆的边缘出现中间灰阶，实心区为纯黑。/ Anti-aliasing: a circle's edge shows intermediate grays and its core is pure black. */
    pn_frame_clear(&frame,PN_UI_PAPER);pn_w_dot(&frame,100.0f,100.0f,20.0f,0);
    unsigned grays=0;for(int y=70;y<130;y++)for(int x=70;x<130;x++){uint8_t v=pn_frame_get(&frame,x,y);if(v>0 && v<15)grays++;}
    assert(pn_frame_get(&frame,100,100)==0 && grays>20 && pn_frame_get(&frame,60,60)==15);
    /* 反相：白底黑字变黑底白字，边角圆润。/ Inversion: black text on white becomes white on black with rounded corners. */
    pn_frame_clear(&frame,PN_UI_PAPER);pn_frame_rect(&frame,120,120,10,10,0);pn_w_invert_round(&frame,100,100,100,60,12);
    assert(pn_frame_get(&frame,150,150)==0 && pn_frame_get(&frame,125,125)==15 && pn_frame_get(&frame,100,100)==15 && pn_frame_get(&frame,90,90)==15);
    /* 圆角遮罩：封面四角被涂成纸色。/ Corner mask: the corners of a cover turn paper-coloured. */
    pn_frame_clear(&frame,0);pn_w_mask_corners(&frame,100,100,100,100,12,PN_UI_PAPER);
    assert(pn_frame_get(&frame,100,100)==15 && pn_frame_get(&frame,150,150)==0 && pn_frame_get(&frame,150,100)==0);
    pn_frame_clear(&frame,PN_UI_PAPER);pn_w_battery(&frame,100,100,44,22,60,0);assert(dark(&frame,100,100,50,22)>60);
    const char *path=getenv("PN_ICON_SHEET");
    if(path){
        pn_frame_clear(&frame,PN_UI_PAPER);
        for(int i=0;i<PN_ICON_COUNT;i++){int x=40+(i%5)*120,y=40+(i/5)*120;pn_w_icon(&frame,(pn_icon_t)i,x,y,64,PN_UI_INK);pn_w_icon(&frame,(pn_icon_t)i,x+70,y+14,28,PN_UI_INK);}
        pn_w_battery(&frame,40,700,44,22,60,0);pn_w_battery(&frame,100,700,44,22,15,0);
        FILE *f=fopen(path,"wb");assert(f && fprintf(f,"P5\n684 1216\n255\n")>0);
        for(int y=0;y<1216;y++){uint8_t row[684];for(int x=0;x<684;x++)row[x]=(uint8_t)(pn_frame_get(&frame,x,y)*17);assert(fwrite(row,1,sizeof row,f)==sizeof row);}
        assert(fclose(f)==0);
    }
    const char *lock_path=getenv("PN_LOCK_SHEET");
    if(lock_path){
        pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();assert(pn_font_open(&font,&pool,&source,32)==PN_OK);
        pn_lock_selection_t selection={.mode=PN_LOCK_DEFAULT};pn_frame_clear(&frame,PN_UI_PAPER);
        assert(pn_lock_render(&selection,&font,"再按电源键继续阅读",&frame)==PN_OK);
        FILE *f=fopen(lock_path,"wb");assert(f && fprintf(f,"P5\n684 1216\n255\n")>0);
        for(int y=0;y<1216;y++){uint8_t row[684];for(int x=0;x<684;x++)row[x]=(uint8_t)(pn_frame_get(&frame,x,y)*17);assert(fwrite(row,1,sizeof row,f)==sizeof row);}
        assert(fclose(f)==0);pn_font_close(&font);
    }
    assert(icons_ok);
    pn_free(bytes);assert(!pool.live && !pool.used);return 0;
}
