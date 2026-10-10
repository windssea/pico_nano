#define _POSIX_C_SOURCE 200809L
#include <strings.h>
/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：书目列表绘制，已就绪封面缩略图优先，否则格式占位卡；完整路径仅在控制器保留。
 * English: catalog-list drawing preferring ready cover thumbnails over format placeholder cards, with complete paths retained by the controller.
 */
#include "pn_shelf_view.h"
#include <stdio.h>
#include <string.h>
#include "pn_widgets.h"
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

/* 版式常量（Mono Glass V2，1u≈1.9px）：状态带、标题行、继续阅读卡、分类、排序行、3列封面网格、分页、固定底栏。
 * Layout constants (Mono Glass V2, 1u≈1.9 px): status band, title row, continue card, categories, sort row, a three-column cover grid, paging and the fixed bottom bar. */
#define CELL_X0 32
#define COVER_W 176
#define COVER_H 244
#define CELL_PITCH 222
#define CELL_W 176
#define GRID_Y 428
#define ROW_PITCH 312
#define LIST_ROW_PITCH 122
#define LIST_ROWS 5
#define TAB_Y 1104
#define TAB_H 112
#define INFO_Y 1046
#define SEARCH_X 500
#define LAYOUT_X 560
#define CONT_Y 150
#define CONT_H 156
#define CHIP_Y 322
#define CHIP_H 58
#define CHIP_W 124
#define SORT_BASE 412
static const pn_shelf_options_t default_options={.battery_percent=-1};
/* 书名：去掉已识别的扩展名。/ Title: strip a recognised extension. */
static void title_of(const pn_catalog_item_t *item,char *out,size_t cap){
    snprintf(out,cap,"%s",item->name);
    char *dot=strrchr(out,'.');
    if(dot && (!strcasecmp(dot,".txt") || !strcasecmp(dot,".epub") || !strcasecmp(dot,".pdf") || !strcasecmp(dot,".fb2") || !strcasecmp(dot,".cbz")))*dot=0;
}
/* 条目的第二行：“27% · EPUB”“已开始 · EPUB”或“未读 · TXT”。/ Second line of an entry: "27% · EPUB", "started · EPUB" or "unread · TXT". */
static void meta_of(const pn_catalog_item_t *item,char *out,size_t cap){
    if(item->identified || item->has_progress){
        if(item->progress<=10000)snprintf(out,cap,"%u%% · %s",(unsigned)(item->progress/100),kind(item->format));
        else snprintf(out,cap,"已开始 · %s",kind(item->format));
    }else snprintf(out,cap,"未读 · %s",kind(item->format));
}
/* 指向frame内矩形的子画布，图元在其边界处自动裁切；x须为偶数（4bpp两像素一字节）。
 * A sub-canvas over a rectangle inside frame so primitives clip at its edges; x must be even (two 4 bpp pixels per byte). */
static pn_frame_t view_of(pn_frame_t *frame,int x,int y,int w,int h){
    x&=~1;if(x<0)x=0;if(y<0)y=0;if(x+w>frame->width)w=frame->width-x;if(y+h>frame->height)h=frame->height-y;
    return (pn_frame_t){frame->pixels+(size_t)y*frame->stride+(size_t)x/2,w,h,frame->stride};
}
static uint32_t name_hash(const char *s){uint32_t h=2166136261u;for(;*s;s++){h^=(unsigned char)*s;h*=16777619u;}return h;}
/* 无封面时的抽象几何占位封面：按书名散列选图案与灰阶，书名压在上部浅色区，格式标签在左下。
 * Abstract geometric placeholder cover: the title hash picks a pattern and grays; the title sits on the light upper area and the format tag at the lower left. */
static pn_status_t abstract_cover(pn_font_t *font,pn_frame_t *frame,int x,int y,int w,int h,const char *title,const char *tag,bool large){
    pn_frame_t v=view_of(frame,x,y,w,h);uint32_t seed=name_hash(title && *title?title:tag);
    pn_frame_rect(&v,0,0,v.width,v.height,PN_UI_SURFACE); // 不能用clear：它按整行步长清屏 / Not clear: it wipes whole rows by stride
    float fw=(float)v.width,fh=(float)v.height;uint8_t dark=(uint8_t)(3+seed%3),mid=(uint8_t)(7+(seed>>3)%3),light=(uint8_t)(11+(seed>>5)%2);
    switch((seed>>8)%4){
    case 0: for(int i=0;i<9;i++)pn_w_ring(&v,fw*0.92f,fh*0.92f,fh*0.16f+(float)i*fh*0.06f,fh*0.018f,i%3==0?dark:mid);break;
    case 1: pn_w_dot(&v,fw*0.35f,fh*0.70f,fw*0.30f,mid);pn_w_dot(&v,fw*0.70f,fh*0.80f,fw*0.26f,dark);pn_w_dot(&v,fw*0.30f,fh*0.95f,fw*0.18f,light);break;
    case 2: for(int i=-2;i<7;i++)pn_w_line(&v,fw*(-0.2f+0.18f*(float)i),fh*1.05f,fw*(0.5f+0.18f*(float)i),fh*0.42f,fw*0.07f,i%2?mid:light);break;
    default: pn_frame_rect(&v,0,(int)(fh*0.55f),v.width,v.height,mid);pn_frame_rect(&v,(int)(fw*0.55f),(int)(fh*0.40f),(int)(fw*0.30f),v.height,dark);pn_frame_rect(&v,(int)(fw*0.12f),(int)(fh*0.68f),(int)(fw*0.22f),v.height,light);break;
    }
    pn_status_t s=PN_OK;
    if(large && title && *title){s=pn_font_size(font,26);if(s==PN_OK)s=pn_w_text_lines_ex(font,&v,title,14,40,v.width-28,3,32,PN_UI_INK,true);}
    if(s==PN_OK && tag && *tag){
        int tw=0;s=pn_font_size(font,large?20:16);if(s==PN_OK)s=pn_w_text_width(font,tag,&tw);
        int ch=large?30:24,cy=v.height-ch-(large?10:6);
        if(s==PN_OK){pn_w_round_fill(&v,large?10:6,cy,tw+(large?20:14),ch,ch/2,PN_UI_PAPER);s=pn_w_text(font,&v,tag,large?20:13,cy+ch-(large?8:6),tw+2,PN_ALIGN_LEFT);}
    }
    return s;
}
/* 把已就绪封面逐行拷入；x为偶数时按字节拷贝。/ Copy a ready cover row by row; byte copies when x is even. */
static void blit(pn_frame_t *frame,const pn_frame_t *cover,int x,int y){
    for(int cy=0;cy<cover->height;cy++){
        if(!(x&1) && y+cy>=0 && y+cy<frame->height && x>=0 && x+cover->width<=frame->width)memcpy(frame->pixels+(size_t)(y+cy)*frame->stride+(size_t)x/2,cover->pixels+(size_t)cy*cover->stride,(size_t)cover->width/2);
        else{for(int cx=0;cx<cover->width;cx++)pn_frame_pixel(frame,x+cx,y+cy,pn_frame_get(cover,cx,cy));}
    }
}
/* 面积平均缩放到w×h。/ Area-averaged scaling to w by h. */
static void blit_scaled(pn_frame_t *frame,const pn_frame_t *cover,int x,int y,int w,int h){
    if(w==cover->width && h==cover->height){blit(frame,cover,x,y);return;}
    for(int dy=0;dy<h;dy++){
        int y0=dy*cover->height/h,y1=(dy+1)*cover->height/h;if(y1<=y0)y1=y0+1;
        for(int dx=0;dx<w;dx++){
            int x0=dx*cover->width/w,x1=(dx+1)*cover->width/w;if(x1<=x0)x1=x0+1;
            unsigned sum=0,n=0;
            for(int sy=y0;sy<y1;sy++){for(int sx=x0;sx<x1;sx++){sum+=pn_frame_get(cover,sx,sy);n++;}}
            pn_frame_pixel(frame,x+dx,y+dy,(uint8_t)((sum+n/2)/n));
        }
    }
}
/* 进度条：圆头浅灰轨加深色进度。/ Progress bar: a rounded light rail with a dark fill. */
static void progress_bar(pn_frame_t *frame,int x,int y,int w,unsigned basis){
    int fill=(int)((unsigned long)w*basis/10000u);pn_w_round_fill(frame,x,y,w,8,4,PN_UI_STROKE);
    if(fill>=8)pn_w_round_fill(frame,x,y,fill,8,4,PN_UI_INK);else if(fill>0)pn_w_round_fill(frame,x,y,8,8,4,PN_UI_INK);
}
/* 封面加圆角、细边与下沿浅影。/ Give a cover rounded corners, a hairline and a light under-edge shade. */
static void frame_cover(pn_frame_t *frame,int x,int y,int w,int h,int radius){
    pn_w_mask_corners(frame,x,y,w,h,radius,PN_UI_PAPER);pn_w_round_stroke(frame,x,y,w,h,radius,2.0f,PN_UI_STROKE);
}
static pn_status_t cover_or_placeholder(pn_font_t *font,pn_frame_t *frame,const pn_shelf_covers_t *covers,size_t slot,const pn_catalog_item_t *item,int x,int y,int w,int h,bool large,int radius){
    pn_frame_t cover;pn_status_t s=PN_OK;char title[PN_CATALOG_NAME_MAX];title_of(item,title,sizeof title);
    pn_w_round_fill(frame,x+2,y+5,w-4,h,radius,PN_UI_SELECT); // 下沿浅影，先画以免盖住封面 / Under-edge shade, drawn first so it never covers the cover
    if(pn_shelf_cover_frame(covers,slot,&cover))blit_scaled(frame,&cover,x,y,w,h);
    else s=abstract_cover(font,frame,x,y,w,h,large?title:"",kind(item->format),large);
    if(s==PN_OK)frame_cover(frame,x,y,w,h,radius);
    return s;
}
/* 继续阅读卡（拟玻璃）：“正在阅读”、书名、已读进度与深色“继续阅读 →”。没有阅读记录时改为一句引导。
 * Continue-reading card (faux glass): "reading now", title, progress and a dark "continue →"; a one-line hint when there is no history. */
static pn_status_t continue_card(const pn_shelf_covers_t *covers,pn_font_t *font,pn_frame_t *frame){
    pn_status_t s=PN_OK;
    pn_w_glass(frame,32,CONT_Y,620,CONT_H,PN_UI_CARD_RADIUS);
    if(covers && covers->has_last){
        const pn_catalog_item_t *last=&covers->last;char title[PN_CATALOG_NAME_MAX];title_of(last,title,sizeof title);
        s=cover_or_placeholder(font,frame,covers,PN_COVER_SLOT_LAST,last,48,CONT_Y+14,92,128,false,10);
        if(s==PN_OK)s=pn_font_size(font,22);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,"正在阅读",160,CONT_Y+40,300,PN_ALIGN_LEFT,PN_UI_MUTED,false);
        if(s==PN_OK)s=pn_font_size(font,34);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,title,160,CONT_Y+84,476,PN_ALIGN_LEFT,PN_UI_INK,true);
        char meta[64];bool known=(last->identified || last->has_progress) && last->progress<=10000;
        if(known)snprintf(meta,sizeof meta,"已读 %u%% · %s",(unsigned)(last->progress/100),kind(last->format));
        else if(last->identified || last->has_progress)snprintf(meta,sizeof meta,"已开始 · %s",kind(last->format));
        else snprintf(meta,sizeof meta,"%s",kind(last->format));
        if(s==PN_OK)s=pn_font_size(font,24);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,meta,160,CONT_Y+118,280,PN_ALIGN_LEFT,PN_UI_MUTED,false);
        if(known)progress_bar(frame,160,CONT_Y+132,250,last->progress);
        if(s==PN_OK)s=pn_font_size(font,26);
        if(s==PN_OK)s=pn_w_button(font,frame,"继续阅读 →",458,CONT_Y+86,180,56,PN_W_SELECTED);
    }else{
        pn_w_icon(frame,PN_ICON_SHELF,60,CONT_Y+46,64,PN_UI_INK);
        s=pn_font_size(font,32);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,"开始阅读",148,CONT_Y+66,490,PN_ALIGN_LEFT,PN_UI_INK,true);
        if(s==PN_OK)s=pn_font_size(font,24);
        if(s==PN_OK)s=pn_w_text_lines_ex(font,frame,"点封面开始阅读，读到哪里会记在这里。",148,CONT_Y+106,480,2,32,PN_UI_MUTED,false);
    }
    return s;
}
/* 网格：176×244封面，下方加粗书名与次要色“进度 · 格式”。/ Grid: 176×244 cover, a bold title and a secondary "progress · format" line below. */
static pn_status_t grid_item(const pn_catalog_item_t *item,const pn_shelf_covers_t *covers,size_t i,pn_font_t *font,pn_frame_t *frame,bool selected){
    int x=CELL_X0+(int)(i%3)*CELL_PITCH,y=GRID_Y+(int)(i/3)*ROW_PITCH;char title[PN_CATALOG_NAME_MAX],meta[64];title_of(item,title,sizeof title);meta_of(item,meta,sizeof meta);
    pn_status_t s=cover_or_placeholder(font,frame,covers,i,item,x,y,COVER_W,COVER_H,true,PN_UI_RADIUS);
    if(s==PN_OK && (item->identified || item->has_progress) && item->progress>0 && item->progress<=10000)progress_bar(frame,x+12,y+COVER_H-20,COVER_W-24,item->progress);
    if(s==PN_OK)s=pn_font_size(font,26);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,title,x,y+COVER_H+34,CELL_W-34,PN_ALIGN_LEFT,PN_UI_INK,true);
    // 书名右侧“⋯”：打开书籍操作（收藏、信息、删除），长按封面同样可以。/ The "⋯" right of the title opens the book actions (favorite, info, delete); a long press on the cover does the same.
    pn_w_icon(frame,PN_ICON_MORE,x+CELL_W-28,y+COVER_H+12,28,PN_UI_INK);
    if(s==PN_OK)s=pn_font_size(font,22);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,meta,x,y+COVER_H+62,CELL_W,PN_ALIGN_LEFT,PN_UI_MUTED,false);
    if(selected)pn_w_round_outline(frame,x-6,y-6,COVER_W+12,COVER_H+12,PN_UI_RADIUS+4,3,PN_UI_INK);
    return s;
}
/* 列表：80×112封面、全名最多两行、“进度 · 格式 · 大小”，整行可点。/ List: 80×112 cover, full name up to two lines, "progress · format · size"; the whole row is tappable. */
static pn_status_t list_item(const pn_catalog_item_t *item,const pn_shelf_covers_t *covers,size_t i,pn_font_t *font,pn_frame_t *frame,bool selected){
    int y=GRID_Y+(int)i*LIST_ROW_PITCH;char title[PN_CATALOG_NAME_MAX],meta[96],base[64];title_of(item,title,sizeof title);meta_of(item,base,sizeof base);
    snprintf(meta,sizeof meta,"%s · %llu KB",base,(unsigned long long)(item->size/1024+(item->size%1024!=0)));
    pn_status_t s=cover_or_placeholder(font,frame,covers,i,item,32,y+5,80,112,false,10);
    if(s==PN_OK)s=pn_font_size(font,28);
    if(s==PN_OK)s=pn_w_text_lines_ex(font,frame,title,132,y+42,480,2,36,PN_UI_INK,true);
    if(s==PN_OK)s=pn_font_size(font,22);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,meta,132,y+106,480,PN_ALIGN_LEFT,PN_UI_MUTED,false);
    pn_w_icon(frame,PN_ICON_MORE,614,y+44,30,PN_UI_INK);
    if(selected)pn_w_round_fill(frame,20,y+12,6,98,3,PN_UI_INK);
    if(s==PN_OK)pn_frame_rect(frame,132,y+LIST_ROW_PITCH-2,520,1,PN_UI_SELECT);
    return s;
}
/* “导入图书”卡所在格；没有则-1。/ The grid slot of the import tile, or -1. */
static int import_slot(const pn_catalog_page_t *page,const pn_shelf_options_t *options,bool transfer){
    if(!transfer || !options->import_tile || options->list_mode || options->query || options->favorites || !page->count || page->more || page->count>=PN_CATALOG_PAGE_MAX)return -1;
    return (int)page->count;
}
/* 虚线圆角框：先画整圈，再在直边上按段擦成纸色。/ Dashed rounded frame: draw the whole outline, then erase gaps along the straight edges with paper. */
static void dashed_frame(pn_frame_t *frame,int x,int y,int w,int h,int radius){
    pn_w_round_stroke(frame,x,y,w,h,radius,2.0f,PN_UI_INK);
    for(int px=x+radius+8;px<x+w-radius-8;px+=20){pn_frame_rect(frame,px,y-1,8,4,PN_UI_PAPER);pn_frame_rect(frame,px,y+h-3,8,4,PN_UI_PAPER);}
    for(int py=y+radius+8;py<y+h-radius-8;py+=20){pn_frame_rect(frame,x-1,py,4,8,PN_UI_PAPER);pn_frame_rect(frame,x+w-3,py,4,8,PN_UI_PAPER);}
}
pn_status_t pn_shelf_render_ex(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,int selected,bool recent,bool transfer,const pn_shelf_covers_t *covers,const pn_shelf_options_t *options){
    if(!page || page->count>6 || !font || !font->impl || !frame || !frame->pixels || frame->width!=684 || frame->height!=1216 || frame->stride<342)return PN_INVALID;
    if(!options)options=&default_options;
    bool list=options->list_mode;size_t visible=list?LIST_ROWS:PN_CATALOG_PAGE_MAX;
    int original=font->pixels;pn_frame_clear(frame,PN_UI_PAPER);
    pn_status_t s=pn_w_status(font,frame);
    // 标题行：大标题与拟玻璃“搜索”胶囊。/ Title row: the large title and a faux-glass "search" pill.
    if(s==PN_OK)s=pn_font_size(font,50);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,options->query?"搜索结果":"书架",32,124,420,PN_ALIGN_LEFT,PN_UI_INK,true);
    if(s==PN_OK && options->search){
        pn_w_glass(frame,SEARCH_X,72,152,62,31);
        pn_w_icon(frame,options->query?PN_ICON_CLOSE:PN_ICON_SEARCH,SEARCH_X+22,87,32,PN_UI_INK);
        s=pn_font_size(font,28);if(s==PN_OK)s=pn_w_text(font,frame,options->query?"清除":"搜索",SEARCH_X+64,114,76,PN_ALIGN_LEFT);
    }
    if(s==PN_OK)s=continue_card(covers,font,frame);
    // 分类：全部/最近/收藏，深色胶囊为当前；右侧网格/列表切换。/ Categories: All/Recent/Favorites with a dark capsule for the current one; the grid/list toggle on the right.
    {static const char *const chips[]={"全部","最近","收藏"};int active=options->favorites?2:recent?1:0;unsigned count=options->favorites_tab?3u:2u;
        pn_w_round_fill(frame,32,CHIP_Y,(int)count*CHIP_W+12,CHIP_H,CHIP_H/2,PN_UI_SURFACE);
        for(unsigned i=0;i<count && s==PN_OK;i++){int x=38+(int)i*CHIP_W;s=pn_font_size(font,28);
            if(s==PN_OK)s=pn_w_text_ex(font,frame,chips[i],x,CHIP_Y+39,CHIP_W,PN_ALIGN_CENTER,PN_UI_INK,(int)i==active);
            if((int)i==active)pn_w_invert_round(frame,x,CHIP_Y+5,CHIP_W,CHIP_H-10,(CHIP_H-10)/2);}
    }
    if(options->layout_toggle){pn_w_glass(frame,LAYOUT_X+28,CHIP_Y,64,CHIP_H,18);if(list)pn_w_icon_grid(frame,LAYOUT_X+44,CHIP_Y+13,32);else pn_w_icon_list(frame,LAYOUT_X+44,CHIP_Y+13,32);}
    // 排序说明与总数。/ Sort note and total.
    if(s==PN_OK)s=pn_font_size(font,24);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,options->favorites?"按收藏时间":recent?"按最近打开":options->query?"按匹配书名":"按书名排序",32,SORT_BASE,300,PN_ALIGN_LEFT,PN_UI_MUTED,false);
    if(s==PN_OK && page->total){char total[32];snprintf(total,sizeof total,"共 %zu 本",page->total);s=pn_w_text_ex(font,frame,total,352,SORT_BASE,300,PN_ALIGN_RIGHT,PN_UI_MUTED,false);}
    // 书目：网格或列表。/ Books: grid or list.
    for(size_t i=0;i<page->count && i<visible && s==PN_OK;i++){
        const pn_catalog_item_t *item=&page->items[i];if(strnlen(item->name,sizeof item->name)>=sizeof item->name)return PN_INVALID;
        s=list?list_item(item,covers,i,font,frame,(int)i==selected):grid_item(item,covers,i,font,frame,(int)i==selected);
    }
    // 最后一页网格还有空位时放虚线“导入图书”卡（需要传书能力）。/ A dashed "import books" tile fills a free grid slot on the last page (needs transfer).
    if(s==PN_OK && import_slot(page,options,transfer)>=0){
        size_t i=(size_t)import_slot(page,options,transfer);int x=CELL_X0+(int)(i%3)*CELL_PITCH,y=GRID_Y+(int)(i/3)*ROW_PITCH;
        dashed_frame(frame,x,y,COVER_W,COVER_H,PN_UI_RADIUS);
        pn_w_icon(frame,PN_ICON_PLUS,x+(COVER_W-48)/2,y+72,48,PN_UI_INK);
        s=pn_font_size(font,26);if(s==PN_OK)s=pn_w_text_ex(font,frame,"导入图书",x,y+166,COVER_W,PN_ALIGN_CENTER,PN_UI_INK,true);
        if(s==PN_OK){s=pn_font_size(font,20);if(s==PN_OK)s=pn_w_text_ex(font,frame,"EPUB / TXT",x,y+198,COVER_W,PN_ALIGN_CENTER,PN_UI_MUTED,false);}
    }
    // 空状态：图标、标题、一句说明与一个主操作（规范10.2文案）。/ Empty state: icon, title, one sentence and one primary action (spec 10.2 copy).
    if(s==PN_OK && !page->count){
        pn_w_icon(frame,options->query?PN_ICON_SEARCH:options->favorites?PN_ICON_BOOKMARK:PN_ICON_SHELF,(684-96)/2,520,96,PN_UI_INK);
        const char *head=options->query?"没有找到相关图书":options->favorites?"还没有收藏的图书":recent?"还没有阅读记录":"还没有图书";
        const char *line=options->query?"试试更短的关键词。":options->favorites?"在书籍操作里点“收藏”，常读的书会出现在这里。":recent?"打开一本书，读到哪里会记在这里。":"导入一本书，开始阅读。";
        s=pn_font_size(font,36);if(s==PN_OK)s=pn_w_text_ex(font,frame,head,32,680,620,PN_ALIGN_CENTER,PN_UI_INK,true);
        if(s==PN_OK)s=pn_font_size(font,26);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,line,32,728,620,PN_ALIGN_CENTER,PN_UI_MUTED,false);
        if(s==PN_OK && options->query){s=pn_font_size(font,30);if(s==PN_OK)s=pn_w_button(font,frame,"修改搜索词",212,776,260,88,0u);}
        else if(s==PN_OK && !recent && !options->favorites && transfer && options->import_tile){s=pn_font_size(font,30);if(s==PN_OK)s=pn_w_button(font,frame,"导入图书",212,776,260,88,PN_W_SELECTED);}
    }
    // 分页栏：居中一枚拟玻璃胶囊“‹  n / m  ›”，没有上一页/下一页的一侧箭头变浅；整行左右两半分别是上一页/下一页的命中区。
    // Paging bar: one centered faux-glass pill "‹  n / m  ›" whose arrow fades on a side without a page; the left and right halves of the whole row are the previous/next hit areas.
    if(s==PN_OK && page->count && page->total){
        char info[48];unsigned per=(unsigned)visible,current=(unsigned)(page->index/per)+1,pages=(unsigned)((page->total+per-1)/per);
        snprintf(info,sizeof info,"%u / %u",current,pages<current?current:pages);
        bool can_back=page->index>0,can_next=page->more;
        pn_w_glass(frame,212,INFO_Y+2,260,50,25);
        pn_w_icon(frame,PN_ICON_BACK,232,INFO_Y+13,28,can_back?PN_UI_INK:PN_UI_STROKE);
        pn_w_icon(frame,PN_ICON_CHEVRON,424,INFO_Y+13,28,can_next?PN_UI_INK:PN_UI_STROKE);
        s=pn_font_size(font,26);if(s==PN_OK)s=pn_w_text_ex(font,frame,info,262,INFO_Y+36,160,PN_ALIGN_CENTER,PN_UI_INK,true);
    }
    // 固定底栏：书架 / 传书 / 设置。/ Fixed bottom bar: shelf / transfer / settings.
    if(s==PN_OK){static const char *const tabs[]={"书架","传书","设置"};static const pn_icon_t icons[]={PN_ICON_SHELF,PN_ICON_TRANSFER,PN_ICON_SETTINGS};s=pn_font_size(font,24);if(s==PN_OK)s=pn_w_tabbar_icons(font,frame,tabs,icons,3,0,0u,TAB_Y,TAB_H);}
    pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;
}
pn_status_t pn_shelf_render_covers(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,int selected,bool recent,bool transfer,const pn_shelf_covers_t *covers){
    return pn_shelf_render_ex(page,font,frame,selected,recent,transfer,covers,NULL);
}
int pn_shelf_hit_ex(const pn_catalog_page_t *page,int x,int y,const pn_shelf_options_t *options){
    if(!page || x<0 || x>=684 || y<0 || y>=1216)return -1;
    if(!options)options=&default_options;
    if(y>=TAB_Y){int tab=pn_w_tabbar_hit(3,TAB_Y,TAB_H,x,y);return tab==0?PN_SHELF_HOME:tab==2?PN_SHELF_MENU:-1;}
    if(y<56)return -1;
    if(y<CONT_Y){if(options->search && x>=SEARCH_X-12 && x<664)return PN_SHELF_SEARCH;return x>=32 && x<300?PN_SHELF_INDEX:-1;}
    if(y<CONT_Y+CONT_H)return x>=32 && x<652?PN_SHELF_CONTINUE:-1;
    if(y<GRID_Y-16){
        if(y<CHIP_Y-10)return -1;
        if(options->layout_toggle && x>=LAYOUT_X && x<664)return PN_SHELF_LAYOUT;
        int chip=(x-38)/CHIP_W;unsigned count=options->favorites_tab?3u:2u;
        if(x>=32 && chip>=0 && chip<(int)count)return chip==0?PN_SHELF_TAB_ALL:chip==1?PN_SHELF_TAB_RECENT:PN_SHELF_TAB_FAVORITES;
        return -1;
    }
    if(!page->count && y>=776 && y<864 && x>=212 && x<472){if(options->query)return PN_SHELF_SEARCH;if(options->import_tile && !options->list_mode && !options->favorites)return PN_SHELF_IMPORT;}
    if(y<INFO_Y){
        if(y<GRID_Y)return -1;
        {int slot=import_slot(page,options,true);if(slot>=0){int ix=CELL_X0+(slot%3)*CELL_PITCH,iy=GRID_Y+(slot/3)*ROW_PITCH;if(x>=ix && x<ix+COVER_W && y>=iy && y<iy+COVER_H)return PN_SHELF_IMPORT;}}
        if(options->list_mode){
            if(x<32 || x>=652)return -1;
            int row=(y-GRID_Y)/LIST_ROW_PITCH;if(row>=LIST_ROWS || row>=(int)page->count)return -1;
            return x>=572?PN_SHELF_MORE+row:row;
        }
        int column=(x-CELL_X0)/CELL_PITCH,row=(y-GRID_Y)/ROW_PITCH;
        if(x<CELL_X0 || column>2 || (x-CELL_X0)%CELL_PITCH>=CELL_W || row>1)return -1;
        int index=row*3+column;if(index>=(int)page->count)return -1;
        // 书名行右端约88×72的透明命中区给“⋯”。/ An invisible 88×72 area at the right end of the title rows belongs to "⋯".
        int cx=CELL_X0+column*CELL_PITCH,cy=GRID_Y+row*ROW_PITCH;
        if(y>=cy+COVER_H && x>=cx+CELL_W-60)return PN_SHELF_MORE+index;
        return index;
    }
    if(y<TAB_Y){if(x<342 && page->index>0)return PN_SHELF_PREVIOUS;if(x>=342 && page->more)return PN_SHELF_NEXT;}
    return -1;
}
int pn_shelf_hit(const pn_catalog_page_t *page,int x,int y){return pn_shelf_hit_ex(page,x,y,NULL);}

/* 4列7行：A–Z、#、返回。/ Four columns by seven rows: A–Z, #, Back. */
static const char cells[]="abcdefghijklmnopqrstuvwxyz#<";
pn_status_t pn_shelf_index_render(pn_font_t *font,pn_frame_t *frame){
    if(!font || !font->impl || !frame || !frame->pixels || frame->width!=684 || frame->height!=1216 || frame->stride<342)return PN_INVALID;
    pn_frame_clear(frame,15);pn_status_t s=at(font,frame,"小纸 Pico",32,20,620,52,1);
    if(s==PN_OK)s=at(font,frame,"按书名首字母跳转（中文按拼音）",32,90,620,50,1);
    for(int i=0;i<28 && s==PN_OK;i++){int x=32+(i%4)*157,y=180+(i/4)*125;
        pn_frame_rect(frame,x,y,150,1,5);pn_frame_rect(frame,x,y+114,150,1,5);pn_frame_rect(frame,x,y,1,115,5);pn_frame_rect(frame,x+149,y,1,115,5);
        char label[8];if(cells[i]=='<')strcpy(label,"返回");else if(cells[i]=='#')strcpy(label,"#");else{label[0]=(char)(cells[i]-'a'+'A');label[1]=0;}
        s=at(font,frame,label,x+(cells[i]=='<'?40:62),y+36,100,50,1);}
    return s;
}
char pn_shelf_index_hit(int x,int y){
    if(x<32 || y<180)return 0;
    int column=(x-32)/157,row=(y-180)/125;
    if(column>3 || row>6 || (x-32)%157>=150 || (y-180)%125>=115)return 0;
    return cells[row*4+column];
}
pn_status_t pn_shelf_render(const pn_catalog_page_t *p,pn_font_t *f,pn_frame_t *b){return pn_shelf_render_selected(p,f,b,-1);}
pn_status_t pn_shelf_render_selected(const pn_catalog_page_t *p,pn_font_t *f,pn_frame_t *b,int selected){return pn_shelf_render_mode(p,f,b,selected,false);}

pn_status_t pn_shelf_render_mode(const pn_catalog_page_t *p,pn_font_t *f,pn_frame_t *b,int selected,bool recent){return pn_shelf_render_mode_with_transfer(p,f,b,selected,recent,false);}
pn_status_t pn_shelf_render_mode_with_transfer(const pn_catalog_page_t *p,pn_font_t *f,pn_frame_t *b,int selected,bool recent,bool transfer){return pn_shelf_render_covers(p,f,b,selected,recent,transfer,NULL);}
int pn_shelf_hit_with_transfer(const pn_catalog_page_t *p,int x,int y,bool enabled){if(enabled && p && pn_w_tabbar_hit(3,TAB_Y,TAB_H,x,y)==1)return PN_SHELF_TRANSFER;return pn_shelf_hit(p,x,y);}

#include "pn_text_file.h"
pn_status_t pn_shelf_render_with_font_file_ex(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,int selected,bool recent,bool transfer,const pn_shelf_covers_t *covers,
    pn_pool_t *pool,pn_media_t *media,const char *font_path,const pn_shelf_options_t *options){
    pn_text_file_t file={0};pn_media_lease_t lease;pn_font_t extra={0};pn_text_source_t source={0};bool leased=false,file_open=false,font_open=false;
    // 书名可能含UI子集没有的字：借阅读字体补字。只开字体、不做全量字形校验（那要数百毫秒）；任何一步失败都退回方框，不影响书架。
    // Titles may contain glyphs outside the UI subset: borrow the reading font. Only open it, without the full glyph validation (hundreds of ms); any failure falls back to boxes without affecting the shelf.
    if(font_path && *font_path && pool && media && media->available){
        leased=pn_media_acquire(media,PN_MEDIA_READ,&lease)==PN_OK;
        file_open=leased && pn_text_file_open(&file,media,&lease,font_path,&source)==PN_OK;
        font_open=file_open && pn_font_open(&extra,pool,&source,28)==PN_OK;
        if(font_open)pn_w_set_fallback(&extra);
    }
    pn_status_t status=pn_shelf_render_ex(page,font,frame,selected,recent,transfer,covers,options);
    if(status==PN_OK && options && options->actions)status=pn_book_actions_render(options->actions,font,frame);
    pn_w_set_fallback(NULL);
    if(font_open)pn_font_close(&extra);
    if(file_open)(void)pn_text_file_close(&file);
    if(leased)(void)pn_media_release(media,&lease);
    return status;
}
pn_status_t pn_shelf_render_with_font_file(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,int selected,bool recent,bool transfer,const pn_shelf_covers_t *covers,
    pn_pool_t *pool,pn_media_t *media,const char *font_path){
    return pn_shelf_render_with_font_file_ex(page,font,frame,selected,recent,transfer,covers,pool,media,font_path,NULL);
}
