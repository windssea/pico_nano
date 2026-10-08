#define _POSIX_C_SOURCE 200809L
/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：书目列表绘制，已就绪封面缩略图优先，否则格式占位卡；完整路径仅在控制器保留。
 * English: catalog-list drawing preferring ready cover thumbnails over format placeholder cards, with complete paths retained by the controller.
 */
#include "pn_shelf_view.h"
#include <stdio.h>
#include <string.h>
static pn_status_t rd(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){const char *s=ctx;size_t size=strlen(s);if(off>size)return PN_INVALID;size_t take=size-(size_t)off;if(take>cap)take=cap;memcpy(out,s+off,take);*n=take;return PN_OK;}
static pn_status_t label(pn_font_t *font,pn_frame_t *part,const char *s,unsigned lines){
    pn_text_source_t source={(void *)s,strlen(s),rd,NULL};pn_text_reader_t reader;pn_status_t status=pn_text_open(&reader,&source,PN_TEXT_UTF8);if(status!=PN_OK)return status;pn_text_char_t c;int x=0,y=font->pixels+4;unsigned row=1;
    while((status=pn_text_next(&reader,&c))==PN_OK){int32_t advance;pn_status_t measured=pn_font_advance(font,c.codepoint,&advance);if(measured==PN_EMPTY)advance=font->pixels*64;else if(measured!=PN_OK)return measured;
        if(advance<0 || advance>part->width*64)return PN_LIMIT;
        if(x>part->width*64-advance){row++;if(row>lines){for(int i=0;i<3;i++){pn_status_t dot=pn_font_draw(font,part,'.',(part->width-24+i*6)*64,y,PN_FONT_GRAY);if(dot!=PN_OK)return dot;}return PN_OK;}x=0;y+=font->pixels+12;}
        if(measured==PN_EMPTY){int px=x/64;pn_frame_rect(part,px,y-font->pixels,font->pixels-4,1,0);pn_frame_rect(part,px,y-4,font->pixels-4,1,0);pn_frame_rect(part,px,y-font->pixels,1,font->pixels-4,0);pn_frame_rect(part,px+font->pixels-5,y-font->pixels,1,font->pixels-4,0);}
        else {status=pn_font_draw(font,part,c.codepoint,x,y,PN_FONT_GRAY);if(status!=PN_OK)return status;}
        x+=advance;
    }
    return status==PN_EMPTY?PN_OK:status;
}
static pn_status_t at(pn_font_t *font,pn_frame_t *frame,const char *s,int x,int y,int w,int h,unsigned lines){pn_frame_t part={frame->pixels+(size_t)y*frame->stride+(size_t)x/2,w,h,frame->stride};return label(font,&part,s,lines);}
static const char *kind(pn_book_format_t f){switch(f){case PN_BOOK_TXT:return "TXT";case PN_BOOK_EPUB:return "EPUB";case PN_BOOK_PDF:return "PDF";case PN_BOOK_FB2:return "FB2";case PN_BOOK_CBZ:return "CBZ";default:return "?";}}
pn_status_t pn_shelf_render_covers(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,int selected,bool recent,bool transfer,const pn_shelf_covers_t *covers){
    if(!page || page->count>6 || !font || !font->impl || !frame || !frame->pixels || frame->width!=684 || frame->height!=1216 || frame->stride<342)return PN_INVALID;
    pn_frame_clear(frame,15);pn_status_t s=at(font,frame,"小纸 Pico",32,20,620,52,1);if(s!=PN_OK)return s;
    s=at(font,frame,recent?"最近阅读":"书架",32,90,620,50,1);if(s!=PN_OK)return s;pn_frame_rect(frame,32,144,620,1,7);
    s=at(font,frame,"继续",284,30,140,50,1);if(s!=PN_OK)return s;
    s=at(font,frame,recent?"全部":"最近",464,30,170,50,1);if(s!=PN_OK)return s;
    pn_frame_rect(frame,272,16,168,1,5);pn_frame_rect(frame,272,95,168,1,5);pn_frame_rect(frame,460,16,192,1,5);pn_frame_rect(frame,460,95,192,1,5);
    for(size_t i=0;i<page->count;i++){const pn_catalog_item_t *item=&page->items[i];if(strnlen(item->name,sizeof item->name)>=sizeof item->name)return PN_INVALID;int y=160+(int)i*144;
        pn_frame_t cover;bool drawn=pn_shelf_cover_frame(covers,i,&cover);
        if(drawn){for(int cy=0;cy<PN_COVER_HEIGHT;cy++)for(int cx=0;cx<PN_COVER_WIDTH;cx++)pn_frame_pixel(frame,32+cx,y+cy,pn_frame_get(&cover,cx,cy));}
        else pn_frame_rect(frame,32,y,72,100,13);
        pn_frame_rect(frame,32,y,72,1,4);pn_frame_rect(frame,32,y+99,72,1,4);pn_frame_rect(frame,32,y,1,100,4);pn_frame_rect(frame,103,y,1,100,4);
        if(!drawn){s=at(font,frame,kind(item->format),34,y+34,68,42,1);if(s!=PN_OK)return s;}
        s=at(font,frame,item->name,128,y+4,524,80,2);if(s!=PN_OK)return s;
        if((int)i==selected){pn_frame_rect(frame,120,y,2,132,0);pn_frame_rect(frame,648,y,2,132,0);}
        char details[96];if(item->identified && item->progress<=10000)snprintf(details,sizeof details,"%s  |  %llu KB  |  %u%%",kind(item->format),(unsigned long long)(item->size/1024+(item->size%1024!=0)),(unsigned)(item->progress/100));
        else snprintf(details,sizeof details,"%s  |  %llu KB",kind(item->format),(unsigned long long)(item->size/1024+(item->size%1024!=0)));
        s=at(font,frame,details,128,y+88,524,40,1);if(s!=PN_OK)return s;pn_frame_rect(frame,128,y+136,524,1,10);
    }
    if(!page->count){s=at(font,frame,"暂无书籍",32,240,620,60,1);if(s!=PN_OK)return s;}
    if(transfer){pn_frame_rect(frame,32,1040,620,1,5);pn_frame_rect(frame,32,1120,620,1,5);s=at(font,frame,"传书",284,1050,160,60,1);if(s!=PN_OK)return s;}
    const char *labels[]={"上一页","下一页"};for(int i=0;i<2;i++){int x=32+i*320;pn_frame_rect(frame,x,1130,300,1,5);pn_frame_rect(frame,x,1210,300,1,5);pn_frame_rect(frame,x,1130,1,80,5);pn_frame_rect(frame,x+299,1130,1,80,5);s=at(font,frame,labels[i],x+100,1150,180,45,1);if(s!=PN_OK)return s;}
    return PN_OK;
}
int pn_shelf_hit(const pn_catalog_page_t *page,int x,int y){if(!page || x<32 || x>=652 || y<0 || y>=1216)return -1;if(y>=16 && y<96){if(x>=460)return PN_SHELF_TOGGLE;if(x>=272 && x<440)return PN_SHELF_CONTINUE;}if(y>=160 && y<1024){int row=(y-160)/144;if((y-160)%144<136 && row<(int)page->count)return row;}if(y>=1130 && y<=1210){if(x<332)return PN_SHELF_PREVIOUS;if(x>=352)return PN_SHELF_NEXT;}return -1;}

pn_status_t pn_shelf_render(const pn_catalog_page_t *p,pn_font_t *f,pn_frame_t *b){return pn_shelf_render_selected(p,f,b,-1);}
pn_status_t pn_shelf_render_selected(const pn_catalog_page_t *p,pn_font_t *f,pn_frame_t *b,int selected){return pn_shelf_render_mode(p,f,b,selected,false);}

pn_status_t pn_shelf_render_mode(const pn_catalog_page_t *p,pn_font_t *f,pn_frame_t *b,int selected,bool recent){return pn_shelf_render_mode_with_transfer(p,f,b,selected,recent,false);}
pn_status_t pn_shelf_render_mode_with_transfer(const pn_catalog_page_t *p,pn_font_t *f,pn_frame_t *b,int selected,bool recent,bool transfer){return pn_shelf_render_covers(p,f,b,selected,recent,transfer,NULL);}
int pn_shelf_hit_with_transfer(const pn_catalog_page_t *p,int x,int y,bool enabled){if(enabled && p && x>=32 && x<652 && y>=1040 && y<1120)return PN_SHELF_TRANSFER;return pn_shelf_hit(p,x,y);}
