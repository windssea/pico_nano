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

/* 版式常量，对应docs/UI_UX.md第3节。/ Layout constants from docs/UI_UX.md section 3. */
#define CELL_X0 32
#define CELL_PITCH 212
#define CELL_W 196
#define GRID_Y 400
#define ROW_PITCH 328
#define LIST_ROW_PITCH 130
#define LIST_ROWS 5
#define TAB_Y 1104
#define TAB_H 112
#define INFO_Y 1056
#define SEARCH_X 480
#define LAYOUT_X 560
static const pn_shelf_options_t default_options={.battery_percent=-1};
/* 书名：去掉已识别的扩展名。/ Title: strip a recognised extension. */
static void title_of(const pn_catalog_item_t *item,char *out,size_t cap){
    snprintf(out,cap,"%s",item->name);
    char *dot=strrchr(out,'.');
    if(dot && (!strcasecmp(dot,".txt") || !strcasecmp(dot,".epub") || !strcasecmp(dot,".pdf") || !strcasecmp(dot,".fb2") || !strcasecmp(dot,".cbz")))*dot=0;
}
/* 条目的第二行：“27% · EPUB”或“未读 · TXT”。/ Second line of an entry: "27% · EPUB" or "unread · TXT". */
static void meta_of(const pn_catalog_item_t *item,char *out,size_t cap){
    // 有阅读记录：能算出进度显示百分比（含0%），算不出显示“已开始”；没有记录才是“未读”。/ With a reading record: a percentage when known (including 0%) and "started" when not; "unread" only without a record.
    if(item->identified || item->has_progress){
        if(item->progress<=10000)snprintf(out,cap,"%u%% · %s",(unsigned)(item->progress/100),kind(item->format));
        else snprintf(out,cap,"已开始 · %s",kind(item->format));
    }else snprintf(out,cap,"未读 · %s",kind(item->format));
}
/* 无封面时的排版卡：细框、书名、底部格式标签。/ Typographic card when there is no cover: thin frame, title and a format tag at the bottom. */
static pn_status_t card(pn_font_t *font,pn_frame_t *frame,int x,int y,int w,int h,const char *title,const char *tag,bool large){
    pn_w_round_outline(frame,x,y,w,h,10,2,PN_UI_INK);
    pn_status_t s=pn_font_size(font,large?30:26);
    if(s==PN_OK)s=pn_w_text_lines(font,frame,title,x+14,y+(large?50:38),w-28,large?4:3,large?38:32,NULL);
    if(s==PN_OK){pn_frame_rect(frame,x+14,y+h-(large?52:40),w-28,1,PN_UI_RULE);s=pn_font_size(font,large?26:22);}
    if(s==PN_OK)s=pn_w_text(font,frame,tag,x+14,y+h-(large?16:12),w-28,PN_ALIGN_LEFT);
    return s;
}
/* 把已就绪封面逐行拷入；x为偶数时按字节拷贝。/ Copy a ready cover row by row; byte copies when x is even. */
static void blit(pn_frame_t *frame,const pn_frame_t *cover,int x,int y){
    for(int cy=0;cy<cover->height;cy++){
        if(!(x&1) && y+cy>=0 && y+cy<frame->height && x>=0 && x+cover->width<=frame->width)memcpy(frame->pixels+(size_t)(y+cy)*frame->stride+(size_t)x/2,cover->pixels+(size_t)cy*cover->stride,(size_t)cover->width/2);
        else{for(int cx=0;cx<cover->width;cx++)pn_frame_pixel(frame,x+cx,y+cy,pn_frame_get(cover,cx,cy));}
    }
}
/* 面积平均缩小到w×h：继续阅读卡与列表模式使用。/ Area-averaged shrink to w by h, used by the continue card and list mode. */
static void blit_scaled(pn_frame_t *frame,const pn_frame_t *cover,int x,int y,int w,int h){
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
/* 进度条：圆头灰槽加黑色进度。/ Progress bar: a rounded gray track with a black fill. */
static void progress_bar(pn_frame_t *frame,int x,int y,int w,unsigned basis){
    int fill=(int)((unsigned long)w*basis/10000u);pn_w_round_fill(frame,x,y,w,8,4,11);
    if(fill>=8)pn_w_round_fill(frame,x,y,fill,8,4,PN_UI_INK);else if(fill>0)pn_w_round_fill(frame,x,y,8,8,4,PN_UI_INK);
}
/* 封面加圆角与细边。/ Give a cover rounded corners and a hairline edge. */
static void frame_cover(pn_frame_t *frame,int x,int y,int w,int h,int radius){
    pn_w_mask_corners(frame,x,y,w,h,radius,PN_UI_PAPER);pn_w_round_stroke(frame,x,y,w,h,radius,2.0f,6);
}
/* 继续阅读卡：浅灰圆角面板，左封面、中书名与进度、右“继续阅读”。/ Continue-reading card: a light-gray rounded panel with the cover on the left, title and progress in the middle and "continue" on the right. */
static pn_status_t continue_card(const pn_shelf_covers_t *covers,pn_font_t *font,pn_frame_t *frame){
    pn_status_t s=PN_OK;
    pn_w_round_fill(frame,32,152,620,160,16,14);pn_w_round_stroke(frame,32,152,620,160,16,2.0f,10);
    if(covers && covers->has_last){
        const pn_catalog_item_t *last=&covers->last;char title[PN_CATALOG_NAME_MAX];title_of(last,title,sizeof title);
        pn_frame_t cover;
        if(pn_shelf_cover_frame(covers,PN_COVER_SLOT_LAST,&cover)){blit_scaled(frame,&cover,48,168,92,128);frame_cover(frame,48,168,92,128,8);}
        else s=card(font,frame,48,168,92,128,"",kind(last->format),false);
        if(s==PN_OK)s=pn_font_size(font,34);
        if(s==PN_OK)s=pn_w_text(font,frame,title,160,212,476,PN_ALIGN_LEFT);
        char meta[64];bool known=(last->identified || last->has_progress) && last->progress<=10000;
        if(known)snprintf(meta,sizeof meta,"已读 %u%% · %s",(unsigned)(last->progress/100),kind(last->format));
        else if(last->identified || last->has_progress)snprintf(meta,sizeof meta,"已开始 · %s",kind(last->format));
        else snprintf(meta,sizeof meta,"%s",kind(last->format));
        if(s==PN_OK)s=pn_font_size(font,26);
        if(s==PN_OK)s=pn_w_text(font,frame,meta,160,256,290,PN_ALIGN_LEFT);
        if(known)progress_bar(frame,160,276,270,last->progress);
        // 黑底主按钮“继续阅读”，整卡仍可点。/ A black primary "continue" button; the whole card stays tappable.
        if(s==PN_OK)s=pn_font_size(font,28);
        if(s==PN_OK)s=pn_w_button(font,frame,"继续阅读",462,240,174,56,PN_W_SELECTED);
    }else{
        pn_w_icon(frame,PN_ICON_SHELF,60,204,64,PN_UI_INK);
        s=pn_font_size(font,34);
        if(s==PN_OK)s=pn_w_text(font,frame,"还没有阅读记录",148,214,490,PN_ALIGN_LEFT);
        if(s==PN_OK)s=pn_font_size(font,26);
        if(s==PN_OK)s=pn_w_text_lines(font,frame,"点下面的封面开始阅读，读到哪里会记在这里。",148,258,490,2,36,NULL);
    }
    return s;
}
/* 网格：封面184×256，下方一行书名与一行“进度 · 格式”。/ Grid: 184×256 cover, a title line and a "progress · format" line below. */
static pn_status_t grid_item(const pn_catalog_item_t *item,const pn_shelf_covers_t *covers,size_t i,pn_font_t *font,pn_frame_t *frame,bool selected){
    int x=CELL_X0+(int)(i%3)*CELL_PITCH,y=GRID_Y+(int)(i/3)*ROW_PITCH;char title[PN_CATALOG_NAME_MAX],meta[64];title_of(item,title,sizeof title);meta_of(item,meta,sizeof meta);
    pn_frame_t cover;pn_status_t s=PN_OK;bool has_cover=pn_shelf_cover_frame(covers,i,&cover);
    if(has_cover){blit(frame,&cover,x,y);frame_cover(frame,x,y,PN_COVER_WIDTH,PN_COVER_HEIGHT,10);}
    else s=card(font,frame,x,y,PN_COVER_WIDTH,PN_COVER_HEIGHT,title,kind(item->format),true);
    // 排版卡底部有格式标签，进度条放到分隔线上方，避免压字。/ Typographic cards carry a format tag at the bottom, so the bar sits above their rule to avoid covering text.
    if(s==PN_OK && (item->identified || item->has_progress) && item->progress>0 && item->progress<=10000)progress_bar(frame,x+12,y+PN_COVER_HEIGHT-(has_cover?20:68),PN_COVER_WIDTH-24,item->progress);
    if(s==PN_OK)s=pn_font_size(font,28);
    if(s==PN_OK)s=pn_w_text(font,frame,title,x,y+PN_COVER_HEIGHT+32,CELL_W-12,PN_ALIGN_LEFT);
    if(s==PN_OK)s=pn_font_size(font,26);
    if(s==PN_OK)s=pn_w_text(font,frame,meta,x,y+PN_COVER_HEIGHT+64,CELL_W-12,PN_ALIGN_LEFT);
    if(selected)pn_w_round_outline(frame,x-6,y-6,PN_COVER_WIDTH+12,PN_COVER_HEIGHT+12,16,3,PN_UI_INK);
    return s;
}
/* 列表：80×112封面、全名最多两行、“进度 · 格式 · 大小”。/ List: 80×112 cover, full name up to two lines, "progress · format · size". */
static pn_status_t list_item(const pn_catalog_item_t *item,const pn_shelf_covers_t *covers,size_t i,pn_font_t *font,pn_frame_t *frame,bool selected){
    int y=GRID_Y+(int)i*LIST_ROW_PITCH;char title[PN_CATALOG_NAME_MAX],meta[96],base[64];title_of(item,title,sizeof title);meta_of(item,base,sizeof base);
    snprintf(meta,sizeof meta,"%s · %llu KB",base,(unsigned long long)(item->size/1024+(item->size%1024!=0)));
    pn_frame_t cover;pn_status_t s=PN_OK;
    if(pn_shelf_cover_frame(covers,i,&cover)){blit_scaled(frame,&cover,32,y+9,80,112);frame_cover(frame,32,y+9,80,112,8);}
    else s=card(font,frame,32,y+9,80,112,"",kind(item->format),false);
    if(s==PN_OK)s=pn_font_size(font,30);
    if(s==PN_OK)s=pn_w_text_lines(font,frame,title,132,y+44,520,2,40,NULL);
    if(s==PN_OK)s=pn_font_size(font,26);
    if(s==PN_OK)s=pn_w_text(font,frame,meta,132,y+110,520,PN_ALIGN_LEFT);
    if(selected)pn_frame_rect(frame,24,y+9,4,112,PN_UI_INK);
    if(s==PN_OK)pn_frame_rect(frame,32,y+LIST_ROW_PITCH-4,620,1,10);
    return s;
}
/* “导入图书”卡所在格；没有则-1。/ The grid slot of the import tile, or -1. */
static int import_slot(const pn_catalog_page_t *page,const pn_shelf_options_t *options,bool transfer){
    if(!transfer || !options->import_tile || options->list_mode || options->query || !page->count || page->more || page->count>=PN_CATALOG_PAGE_MAX)return -1;
    return (int)page->count;
}
pn_status_t pn_shelf_render_ex(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,int selected,bool recent,bool transfer,const pn_shelf_covers_t *covers,const pn_shelf_options_t *options){
    if(!page || page->count>6 || !font || !font->impl || !frame || !frame->pixels || frame->width!=684 || frame->height!=1216 || frame->stride<342)return PN_INVALID;
    if(!options)options=&default_options;
    bool list=options->list_mode;size_t visible=list?LIST_ROWS:PN_CATALOG_PAGE_MAX;
    int original=font->pixels;pn_frame_clear(frame,PN_UI_PAPER);
    // 状态带：产品名与电量。/ Status band: product name and battery.
    pn_status_t s=pn_w_status(font,frame);
    // 标题行：标题与搜索入口。/ Title row: the title and the search entry.
    if(s==PN_OK)s=pn_font_size(font,48);
    if(s==PN_OK)s=pn_w_text(font,frame,options->query?"搜索结果":recent?"最近阅读":"书架",32,122,420,PN_ALIGN_LEFT);
    if(s==PN_OK && options->search){pn_w_icon(frame,options->query?PN_ICON_CLOSE:PN_ICON_SEARCH,SEARCH_X+52,80,36,PN_UI_INK);s=pn_font_size(font,30);if(s==PN_OK)s=pn_w_text(font,frame,options->query?"清除":"搜索",SEARCH_X+96,114,76,PN_ALIGN_LEFT);}
    pn_frame_rect(frame,32,143,620,2,PN_UI_RULE);
    if(s==PN_OK)s=continue_card(covers,font,frame);
    // 全部/最近、排序说明与网格/列表切换。/ All/Recent, the sort note and the grid/list toggle.
    if(s==PN_OK)s=pn_font_size(font,36);
    if(s==PN_OK)s=pn_w_text(font,frame,"全部",32,372,128,PN_ALIGN_LEFT);
    if(s==PN_OK)s=pn_w_text(font,frame,"最近",160,372,128,PN_ALIGN_LEFT);
    pn_w_round_fill(frame,recent?160:32,383,72,5,2,PN_UI_INK);
    if(s==PN_OK)s=pn_font_size(font,26);
    if(s==PN_OK)s=pn_w_text(font,frame,recent?"阅读时间":options->query?"匹配度":"名称排序",300,370,252,PN_ALIGN_RIGHT);
    if(options->layout_toggle){if(list)pn_w_icon_grid(frame,LAYOUT_X+44,342,44);else pn_w_icon_list(frame,LAYOUT_X+44,346,44);}
    // 书目：网格或列表。/ Books: grid or list.
    for(size_t i=0;i<page->count && i<visible && s==PN_OK;i++){
        const pn_catalog_item_t *item=&page->items[i];if(strnlen(item->name,sizeof item->name)>=sizeof item->name)return PN_INVALID;
        s=list?list_item(item,covers,i,font,frame,(int)i==selected):grid_item(item,covers,i,font,frame,(int)i==selected);
    }
    // 最后一页网格还有空位时放“导入图书”卡（需要传书能力）。/ An "import books" tile fills a free grid slot on the last page (needs transfer).
    if(s==PN_OK && import_slot(page,options,transfer)>=0){
        size_t i=(size_t)import_slot(page,options,transfer);int x=CELL_X0+(int)(i%3)*CELL_PITCH,y=GRID_Y+(int)(i/3)*ROW_PITCH;
        pn_w_round_fill(frame,x,y,PN_COVER_WIDTH,PN_COVER_HEIGHT,10,14);pn_w_round_stroke(frame,x,y,PN_COVER_WIDTH,PN_COVER_HEIGHT,10,2.0f,10);
        pn_w_icon(frame,PN_ICON_PLUS,x+(PN_COVER_WIDTH-56)/2,y+84,56,PN_UI_INK);
        s=pn_font_size(font,28);if(s==PN_OK)s=pn_w_text(font,frame,"导入图书",x,y+190,PN_COVER_WIDTH,PN_ALIGN_CENTER);
    }
    // 空状态：图标、标题、一句说明与一个主操作（docs/UI_UX.md的文案规范）。/ Empty state: icon, title, one sentence and one primary action.
    if(s==PN_OK && !page->count){
        pn_w_icon(frame,options->query?PN_ICON_SEARCH:PN_ICON_SHELF,(684-96)/2,470,96,PN_UI_INK);
        s=pn_font_size(font,36);if(s==PN_OK)s=pn_w_text(font,frame,options->query?"没有找到相关书籍":recent?"还没有阅读记录":"书架还是空的",32,620,620,PN_ALIGN_CENTER);
        if(s==PN_OK)s=pn_font_size(font,28);
        if(s==PN_OK)s=pn_w_text(font,frame,options->query?"试试更短的关键词。":recent?"打开一本书，读到哪里会记在这里。":"导入一本书，开始阅读。",32,672,620,PN_ALIGN_CENTER);
        if(s==PN_OK && options->query){s=pn_font_size(font,30);if(s==PN_OK)s=pn_w_button(font,frame,"清除搜索",222,720,240,72,0u);}
        else if(s==PN_OK && !recent && transfer && options->import_tile){s=pn_font_size(font,30);if(s==PN_OK)s=pn_w_button(font,frame,"导入图书",222,720,240,72,PN_W_SELECTED);}
    }
    // 页码信息行：当前页/总页数，两侧小箭头用于翻页。/ Page info row: current/total pages with small arrows for turning.
    if(s==PN_OK && page->count && page->total){
        char info[64];unsigned per=(unsigned)visible,current=(unsigned)(page->index/per)+1,pages=(unsigned)((page->total+per-1)/per);
        snprintf(info,sizeof info,"%u / %u 页 · 共 %zu 本",current,pages<current?current:pages,page->total);
        s=pn_font_size(font,26);if(s==PN_OK)s=pn_w_text(font,frame,info,32,INFO_Y+34,620,PN_ALIGN_CENTER);
        s=s==PN_OK?pn_font_size(font,34):s;
        if(s==PN_OK && page->index>0)s=pn_w_text(font,frame,"<",32,INFO_Y+36,60,PN_ALIGN_LEFT);
        if(s==PN_OK && page->more)s=pn_w_text(font,frame,">",592,INFO_Y+36,60,PN_ALIGN_RIGHT);
    }
    // 底栏。/ Bottom bar.
    if(s==PN_OK){static const char *const tabs[]={"书架","传书","设置"};static const pn_icon_t icons[]={PN_ICON_SHELF,PN_ICON_TRANSFER,PN_ICON_SETTINGS};s=pn_font_size(font,28);if(s==PN_OK)s=pn_w_tabbar_icons(font,frame,tabs,icons,3,0,transfer?0u:2u,TAB_Y,TAB_H);}
    pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;
}
pn_status_t pn_shelf_render_covers(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,int selected,bool recent,bool transfer,const pn_shelf_covers_t *covers){
    return pn_shelf_render_ex(page,font,frame,selected,recent,transfer,covers,NULL);
}
int pn_shelf_hit_ex(const pn_catalog_page_t *page,int x,int y,const pn_shelf_options_t *options){
    if(!page || x<0 || x>=684 || y<0 || y>=1216)return -1;
    if(!options)options=&default_options;
    if(y>=TAB_Y){int tab=pn_w_tabbar_hit(3,TAB_Y,TAB_H,x,y);return tab==0?PN_SHELF_HOME:tab==2?PN_SHELF_MENU:-1;}
    if(y<64)return x<300?PN_SHELF_MENU:-1;
    if(y<144){if(options->search && x>=SEARCH_X && x<652)return PN_SHELF_SEARCH;return x>=32 && x<300?PN_SHELF_INDEX:-1;}
    if(y<320)return x>=32 && x<652?PN_SHELF_CONTINUE:-1;
    if(y<400){if(options->layout_toggle && x>=LAYOUT_X && x<652)return PN_SHELF_LAYOUT;return x>=32 && x<160?PN_SHELF_TAB_ALL:x>=160 && x<288?PN_SHELF_TAB_RECENT:-1;}
    if(!page->count && y>=720 && y<792 && x>=222 && x<462){if(options->query)return PN_SHELF_SEARCH;if(options->import_tile && !options->list_mode)return PN_SHELF_IMPORT;}
    if(y<INFO_Y){
        {int slot=import_slot(page,options,true);if(slot>=0){int ix=CELL_X0+(slot%3)*CELL_PITCH,iy=GRID_Y+(slot/3)*ROW_PITCH;if(x>=ix && x<ix+PN_COVER_WIDTH && y>=iy && y<iy+PN_COVER_HEIGHT)return PN_SHELF_IMPORT;}}
        if(options->list_mode){
            if(x<32 || x>=652)return -1;
            int row=(y-GRID_Y)/LIST_ROW_PITCH;return row<LIST_ROWS && row<(int)page->count && (y-GRID_Y)%LIST_ROW_PITCH<LIST_ROW_PITCH-4?row:-1;
        }
        int column=(x-CELL_X0)/CELL_PITCH,row=(y-GRID_Y)/ROW_PITCH;
        if(x<CELL_X0 || column>2 || (x-CELL_X0)%CELL_PITCH>=CELL_W || (y-GRID_Y)%ROW_PITCH>=ROW_PITCH-8)return -1;
        int index=row*3+column;return index<(int)page->count?index:-1;
    }
    if(y<TAB_Y){if(x>=32 && x<200 && page->index>0)return PN_SHELF_PREVIOUS;if(x>=484 && x<652 && page->more)return PN_SHELF_NEXT;}
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
