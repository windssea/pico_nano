/* SPDX-License-Identifier: Apache-2.0
 * 中文：日期边界、未知状态、字体恢复及书封绘制。/ English: date boundaries, unknown states, font restoration and cover drawing.
 */
#include "pn_bookmark_lock.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void date(uint32_t utc,unsigned y,unsigned m,unsigned d,unsigned w){
    unsigned year=0,month=0,day=0,week=0;assert(pn_bookmark_lock_date(utc,&year,&month,&day,&week));assert(year==y && month==m && day==d && week==w);
}
int main(int argc,char **argv){
    date(0,1970,1,1,3);date(946656000,2000,1,1,5);date(1709136000,2024,2,29,3);
    date(1792339200,2026,10,19,0);date(4107513600u,2100,3,1,0);
    assert(pn_bookmark_lock_month_days(2000,2)==29 && pn_bookmark_lock_month_days(2100,2)==28 && !pn_bookmark_lock_month_days(2026,13));
    pn_pool_t pool;assert(!pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL));pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();assert(pn_font_open(&font,&pool,&source,28)==PN_OK);
    uint8_t *pixels=malloc(342u*1216u),*before=malloc(342u*1216u),cover_pixels[PN_COVER_BYTES];assert(pixels && before);pn_frame_t frame,cover;
    assert(pn_frame_bind(&frame,pixels,342u*1216u,684,1216) && pn_frame_bind(&cover,cover_pixels,sizeof cover_pixels,184,256));pn_frame_clear(&cover,3);
    pn_bookmark_lock_t s={.battery=-1};assert(pn_bookmark_lock_render(&s,NULL,&font,"",&frame)==PN_OK && font.pixels==28);memcpy(before,pixels,342u*1216u);
    s=(pn_bookmark_lock_t){.book={"本地阅读书签测试","作者"},.basis=1423,.section=3,.count=31,.progress_known=true,.date_known=true,.utc=1792339200,.battery=86};
    assert(pn_bookmark_lock_render(&s,&cover,&font,"按电源键继续阅读",&frame)==PN_OK && font.pixels==28);
    assert(memcmp(before,pixels,342u*1216u) && pn_frame_get(&frame,480,210)==3 && pn_frame_get(&frame,40,556)==0 && pn_frame_get(&frame,640,556)==15);
    if(argc>1){FILE *f=fopen(argv[1],"wb");assert(f);fprintf(f,"P5\n684 1216\n255\n");for(int y=0;y<1216;y++)for(int x=0;x<684;x++)fputc(pn_frame_get(&frame,x,y)*17,f);assert(!fclose(f));}
    memcpy(before,pixels,342u*1216u);s.basis=10001;assert(pn_bookmark_lock_render(&s,NULL,&font,"",&frame)==PN_INVALID && !memcmp(before,pixels,342u*1216u));
    free(before);free(pixels);pn_font_close(&font);assert(!pool.used && !pool.live);puts("bookmark lock: Gregorian boundaries, unknown date, progress, cover and font restoration passed");return 0;
}
