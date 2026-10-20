#define _POSIX_C_SOURCE 200809L
#include "pn_brand.h"
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
#include <stdlib.h>
#include "pn_widgets.h"
#include "pn_builtin_art.h"
#include "pn_profile.h"
bool pn_shelf_gesture_feed(pn_shelf_gesture_t *g,bool active,bool swipe,unsigned count,int x,int y,bool valid,int book,uint64_t now,int *command){
    if(!g || !command)return false;
    bool owned=g->claimed;
    if(g->down && !active)g->blocked=true;
    if(count && (x<0 || x>=684 || y<0 || y>=1216))valid=false;
    if(!valid || count>1){g->blocked=true;g->empty=0;return owned;}
    if(!count){
        if(g->empty<2)g->empty++;
        if(g->empty<2)return owned;
        if(g->down && !g->blocked && g->moved && g->swipe){int dx=g->last_x-g->x,dy=g->last_y-g->y;if(abs(dx)>=100 && abs(dx)>abs(dy)*2)*command=dx<0?PN_SHELF_NEXT:PN_SHELF_PREVIOUS;}
        *g=(pn_shelf_gesture_t){0};return owned;
    }
    g->empty=0;if(g->blocked)return owned;
    if(!g->down){g->down=true;g->x=g->last_x=x;g->y=g->last_y=y;g->began=now;g->hit=book;g->swipe=swipe;
        if(!active || y<266 || y>=1104){g->blocked=true;return false;}}
    if(!active){g->blocked=true;return owned;}
    g->last_x=x;g->last_y=y;
    if(abs(y-g->y)>80 && abs(y-g->y)>=abs(x-g->x))g->swipe=false;
    if(abs(x-g->x)>20 || abs(y-g->y)>20){g->moved=true;g->claimed=true;}
    if(!g->moved && !g->claimed && g->hit>=0 && g->hit<PN_CATALOG_PAGE_MAX && now>=g->began && now-g->began>=600){g->claimed=true;*command=PN_SHELF_MORE+g->hit;}
    return g->claimed;
}
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
static const char *kind(pn_book_format_t f){switch(f){case PN_BOOK_TXT:return "TXT";case PN_BOOK_EPUB:return "EPUB";case PN_BOOK_FB2:return "FB2";case PN_BOOK_CBZ:return "CBZ";default:return "?";}}

/* 版式常量（Mono Glass V2，1u≈1.9px）：状态带、标题行、继续阅读卡、分类、排序行、3/4列封面网格、分页、固定底栏。
 * Layout constants (Mono Glass V2, 1u≈1.9 px): status band, title row, continue card, categories, sort row, a three/four-column cover grid, paging and the fixed bottom bar. */
#define CELL_X0 32
#define COVER_W 152
#define COVER_H 210
#define CELL_PITCH 222
#define CELL_W 176
#define GRID_Y 266
#define LIST_Y 268
#define ROW_PITCH 262
#define LIST_ROW_PITCH 150
#define LIST_ROWS 5
#define TAB_Y 1104
#define TAB_H 112
#define INFO_Y 1060
#define SEARCH_X 500
#define LAYOUT_X 560
#define CONT_Y 180
#define CONT_H 350
#define CHIP_Y 158
#define CHIP_H 58
#define CHIP_W 124
#define SORT_BASE 252
static const pn_shelf_options_t default_options={.battery_percent=-1};
static unsigned columns_of(const pn_shelf_options_t *o){return o && o->columns==4?4u:3u;}
size_t pn_shelf_capacity(const pn_shelf_options_t *o){return o && o->list_mode?LIST_ROWS:columns_of(o)*3u;}
typedef struct {int x,y,w,h;} grid_box_t;
/* 两种密度共用几何，绘制与命中不分叉。/ Both densities share geometry for drawing and hit testing. */
static grid_box_t grid_box(size_t i,const pn_shelf_options_t *o){
    unsigned cols=columns_of(o);int pitch=cols==4?162:CELL_PITCH,w=cols==4?134:COVER_W,h=cols==4?186:COVER_H;
    int inset=o && o->style==PN_SHELF_WOOD?4:0;
    if(inset){w-=8;h-=12;}
    return (grid_box_t){CELL_X0+(int)(i%cols)*pitch+(cols==3?12:0)+inset,GRID_Y+(int)(i/cols)*ROW_PITCH+(cols==4?12:0),w,h};
}
/* 拟物只画书挡、搁板与书脊，不贴纹理或分配额外画布。/ Skeuomorphism uses ledges and spines without textures or extra canvases. */
static void ledge(pn_frame_t *f,int x,int y,int w){
    pn_frame_rect(f,x,y,w,2,4);pn_frame_rect(f,x+2,y+2,w-4,6,8);pn_frame_rect(f,x+8,y+8,w-16,2,5);
}
static void style_cover(pn_frame_t *f,unsigned style,int x,int y,int w,int h){
    if(style==PN_SHELF_WOOD){pn_frame_rect(f,x+4,y+8,2,h-16,10);pn_frame_rect(f,x+7,y+8,1,h-16,13);pn_frame_rect(f,x+w-5,y+8,1,h-16,13);}
    if(style==PN_SHELF_PAPER){pn_frame_rect(f,x+w,y+8,5,h-8,13);pn_frame_rect(f,x+8,y+h,w,5,13);pn_frame_rect(f,x+w+2,y+12,1,h-12,9);}
}
void pn_shelf_style_preview(pn_frame_t *f,unsigned style,unsigned columns,int x,int y,int w,int h){
    if(!f || !f->pixels || w<64 || h<40)return;
    unsigned cols=columns==4?4u:3u;int pitch=w/(int)cols,bw=pitch-14,bh=h-28;
    for(unsigned i=0;i<cols;i++){int bx=x+6+(int)i*pitch;pn_w_round_fill(f,bx,y,bw,bh,style==PN_SHELF_DEFAULT?8:2,PN_UI_PAPER);
        pn_w_round_stroke(f,bx,y,bw,bh,style==PN_SHELF_DEFAULT?8:2,2,PN_UI_MUTED);
        if(style!=PN_SHELF_MINIMAL){pn_frame_rect(f,bx+8,y+12,bw-16,4,8);pn_frame_rect(f,bx+8,y+22,bw-24,3,11);}
        style_cover(f,style,bx,y,bw,bh);
    }
    if(style==PN_SHELF_WOOD)ledge(f,x,y+bh+2,w);
    else if(style==PN_SHELF_PAPER)pn_frame_rect(f,x,y+h-6,w,2,PN_UI_INK);
}
/* 书名：去掉已识别的扩展名。/ Title: strip a recognised extension. */
static void title_of(const pn_catalog_item_t *item,const pn_shelf_covers_t *covers,size_t slot,char *out,size_t cap){
    if(covers && slot<=PN_COVER_SLOT_LAST && *covers->display[slot].title){snprintf(out,cap,"%s",covers->display[slot].title);return;}
    snprintf(out,cap,"%s",item->name);
    char *dot=strrchr(out,'.');
    if(dot && (!strcasecmp(dot,".txt") || !strcasecmp(dot,".epub") || !strcasecmp(dot,".fb2") || !strcasecmp(dot,".cbz")))*dot=0;
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
    if(w>0 && w<=256 && h>0 && !(x&1) && !(w&1) && x>=0 && y>=0 && x+w<=frame->width && y+h<=frame->height){
        // 横向采样边界只算一次，保留原盒滤波取整，成对写4bpp。/ Compute horizontal sample bounds once, preserving box-filter rounding with paired 4bpp writes.
        uint16_t first[256],last[256];
        for(int dx=0;dx<w;dx++){first[dx]=(uint16_t)(dx*cover->width/w);last[dx]=(uint16_t)((dx+1)*cover->width/w);if(last[dx]<=first[dx])last[dx]=first[dx]+1;}
        for(int dy=0;dy<h;dy++){int y0=dy*cover->height/h,y1=(dy+1)*cover->height/h;if(y1<=y0)y1=y0+1;uint8_t *dst=frame->pixels+(size_t)(y+dy)*frame->stride+x/2;uint8_t pair=0;
            const uint8_t *top=cover->pixels+(size_t)y0*cover->stride;bool single_row=y1-y0==1;
            for(int dx=0;dx<w;dx++){uint8_t pixel;
                // 近1:1缩放时多数采样框只有一个源像素，直接取值，结果与盒滤波相同。/ Near 1:1 scaling most boxes hold one source pixel; take it directly, identical to the box filter.
                if(single_row && last[dx]-first[dx]==1){unsigned sx=first[dx];pixel=(uint8_t)((top[sx/2]>>((sx&1)*4))&15u);}
                else{unsigned sum=0,n=(unsigned)(last[dx]-first[dx])*(unsigned)(y1-y0);
                    for(int sy=y0;sy<y1;sy++){const uint8_t *row=cover->pixels+(size_t)sy*cover->stride;for(int sx=first[dx];sx<last[dx];sx++)sum+=(row[sx/2]>>((sx&1)*4))&15u;}
                    pixel=(uint8_t)((sum+n/2)/n);}
                if(!(dx&1))pair=pixel;else dst[dx/2]=(uint8_t)(pair|(pixel<<4));}
        }
        return;
    }
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
static pn_status_t monogram_cover(pn_font_t *font,pn_frame_t *frame,int x,int y,int w,int h,const char *title,unsigned style);
/// 格式标签独立于封面缓存，真实图片与占位图均放在右下角。/ Format badges are independent of cached pixels and sit bottom-right on real and placeholder covers.
static pn_status_t format_badge(pn_font_t *font,pn_frame_t *frame,pn_book_format_t format,int x,int y,int w,int h){
    int original=font->pixels;pn_status_t s=pn_font_size(font,18);int width=0;
    if(s==PN_OK)s=pn_w_text_width(font,kind(format),&width);
    int box=width+14;if(box>w-8)box=w-8;
    pn_w_round_fill(frame,x+w-box-5,y+h-37,box,26,4,PN_UI_PAPER);pn_w_round_stroke(frame,x+w-box-5,y+h-37,box,26,4,1,PN_UI_MUTED);
    if(s==PN_OK)s=pn_w_text(font,frame,kind(format),x+w-box-4,y+h-17,box-2,PN_ALIGN_CENTER);
    pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;
}
static pn_status_t cover_or_placeholder(pn_font_t *font,pn_frame_t *frame,const pn_shelf_covers_t *covers,size_t slot,const pn_catalog_item_t *item,int x,int y,int w,int h,bool large,int radius){
    pn_frame_t cover;pn_status_t s=PN_OK;char title[PN_CATALOG_NAME_MAX];title_of(item,covers,slot,title,sizeof title);
    pn_profile_span_t span;pn_profile_begin(&span,NULL,"ui.cover.shade");
    pn_w_round_fill(frame,x+2,y+5,w-4,h,radius,PN_UI_SELECT); // 下沿浅影，先画以免盖住封面 / Under-edge shade, drawn first so it never covers the cover
    pn_profile_end(&span,PN_OK);pn_profile_begin(&span,NULL,"ui.cover.blit");
    if(pn_shelf_cover_frame(covers,slot,&cover) || pn_builtin_cover_frame(title,&cover)==PN_OK)blit_scaled(frame,&cover,x,y,w,h);
    else s=monogram_cover(font,frame,x,y,w,h,title,PN_SHELF_MINIMAL);
    (void)large;
    pn_profile_end(&span,s);pn_profile_begin(&span,NULL,"ui.cover.frame");
    if(s==PN_OK)frame_cover(frame,x,y,w,h,radius);
    pn_profile_end(&span,s);pn_profile_begin(&span,NULL,"ui.cover.badge");
    if(s==PN_OK)s=format_badge(font,frame,item->format,x,y,w,h);
    pn_profile_end(&span,s);
    return s;
}
// 白底黑书签在深浅封面上均清晰；不增加点击入口。/ A black bookmark on white stays visible on any cover; no extra tap target.
static void favorite_badge(pn_frame_t *frame,int x,int y,int size){
    pn_w_round_fill(frame,x,y,size,size,8,PN_UI_PAPER);
    pn_w_round_stroke(frame,x,y,size,size,8,1.5f,PN_UI_INK);
    pn_w_icon(frame,PN_ICON_BOOKMARK,x+5,y+5,size-10,PN_UI_INK);
}
#define HOME_CARD_Y 190
#define HOME_CARD_H 384
#define HOME_BOOKS_Y 844
#define HOME_ROW_PITCH 130
/// 固定远山轮廓按列填充，不加载图片或分配画布。/ Fill fixed mountain silhouettes by column without image I/O or canvas allocation.
static void mountain(pn_frame_t *frame,const int (*points)[2],size_t count,int bottom,uint8_t gray){
    for(size_t i=1;i<count;i++)for(int x=points[i-1][0];x<points[i][0];x++){
        int y=points[i-1][1]+(points[i][1]-points[i-1][1])*(x-points[i-1][0])/(points[i][0]-points[i-1][0]);
        pn_frame_rect(frame,x,y,1,bottom-y,gray);
    }
}
/// 山形与纸页线只落在边角，文字区域保持白纸。/ Keep mountains and paper lines at the edges, leaving the text area white.
static void home_ornament(pn_frame_t *frame){
    static const int far[][2]={{318,176},{385,120},{420,140},{468,80},{508,122},{543,108},{620,160},{684,128}};
    static const int near[][2]={{394,178},{470,140},{516,158},{559,136},{613,172},{656,158},{684,174}};
    mountain(frame,far,sizeof far/sizeof *far,180,14);mountain(frame,near,sizeof near/sizeof *near,180,13);
    for(int i=0;i<5;i++)pn_w_line(frame,(float)(574+i*11),214.0f,632.0f,(float)(236+i*4),1.0f,14);
}
/// 续读主卡保持完整点击区域；时长仅使用与该书匹配的统计。/ Keep the full Continue card tappable; book time uses matching statistics only.
static pn_status_t continue_card(const pn_shelf_covers_t *covers,const pn_shelf_options_t *options,pn_font_t *font,pn_frame_t *frame){
    pn_status_t s=pn_font_size(font,24);char meta[80];
    pn_w_round_stroke(frame,32,HOME_CARD_Y,620,HOME_CARD_H,16,1,PN_UI_STROKE);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,"正在阅读",56,236,500,PN_ALIGN_LEFT,PN_UI_INK,true);
    if(covers && covers->has_last){
        const pn_catalog_item_t *last=&covers->last;char title[PN_CATALOG_NAME_MAX];title_of(last,covers,PN_COVER_SLOT_LAST,title,sizeof title);
        if(s==PN_OK)s=cover_or_placeholder(font,frame,covers,PN_COVER_SLOT_LAST,last,56,254,152,210,false,4);
        if(s==PN_OK)s=pn_font_size(font,36);
        if(s==PN_OK)s=pn_w_text_lines_ex(font,frame,title,242,280,382,2,44,PN_UI_INK,true);
        if(s==PN_OK && *covers->display[PN_COVER_SLOT_LAST].author){s=pn_font_size(font,23);if(s==PN_OK)s=pn_w_text_ex(font,frame,pn_brand_label(covers->display[PN_COVER_SLOT_LAST].author),242,360,382,PN_ALIGN_LEFT,PN_UI_MUTED,false);}
        if(s==PN_OK)s=pn_font_size(font,20);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,"阅读进度",242,400,300,PN_ALIGN_LEFT,PN_UI_MUTED,false);
        unsigned basis=(last->identified || last->has_progress) && last->progress<=10000?last->progress:0;
        progress_bar(frame,242,420,308,basis);snprintf(meta,sizeof meta,"%u%%",basis/100u);
        if(s==PN_OK){s=pn_font_size(font,25);if(s==PN_OK)s=pn_w_text_ex(font,frame,meta,556,432,68,PN_ALIGN_RIGHT,PN_UI_INK,false);}
        snprintf(meta,sizeof meta,"本书已读 %llu 分钟",(unsigned long long)(options->reading.known?options->reading.total_ms/60000u:0));
        if(s==PN_OK){s=pn_font_size(font,22);if(s==PN_OK)s=pn_w_text_ex(font,frame,meta,242,468,382,PN_ALIGN_LEFT,PN_UI_MUTED,false);}
    }else{
        pn_w_icon(frame,PN_ICON_SHELF,60,294,64,PN_UI_MUTED);
        if(s==PN_OK)s=pn_font_size(font,36);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,"从一本书开始",160,324,464,PN_ALIGN_LEFT,PN_UI_INK,true);
        if(s==PN_OK)s=pn_font_size(font,24);
        if(s==PN_OK)s=pn_w_text_lines_ex(font,frame,"在书库挑选一本书，下次从这里继续。",160,380,464,2,36,PN_UI_MUTED,false);
    }
    pn_w_round_fill(frame,56,490,572,60,8,PN_UI_INK);
    if(s==PN_OK)s=pn_font_size(font,28);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,covers && covers->has_last?"继续阅读":"打开书库",80,530,524,PN_ALIGN_CENTER,PN_UI_PAPER,true);
    pn_w_icon(frame,PN_ICON_CHEVRON,592,508,24,PN_UI_PAPER);return s;
}
/// 今日取全局周槽而非本书today_ms；分钟分桶与菜单缓存键一致。/ Today uses the global weekly bucket, not book today_ms; minute buckets match the menu cache key.
static pn_status_t home_reading(const pn_shelf_options_t *o,pn_font_t *font,pn_frame_t *frame){
    static const char *const days[]={"一","二","三","四","五","六","日"};
    uint64_t minutes[7]={0},total=0,remainder=0,maximum=1,today=0;bool dated=o->reading.dated && o->reading.weekday<7;char text[64];
    for(unsigned i=0;i<7;i++){minutes[i]=dated?o->reading.week_ms[i]/60000u:0;total+=minutes[i];remainder+=dated?o->reading.week_ms[i]%60000u:0;if(minutes[i]>maximum)maximum=minutes[i];}
    total+=remainder/60000u;if(dated)today=minutes[o->reading.weekday];
    pn_status_t s=pn_font_size(font,27);if(s==PN_OK)s=pn_w_text_ex(font,frame,"阅读节奏",32,632,300,PN_ALIGN_LEFT,PN_UI_INK,true);
    snprintf(text,sizeof text,"本周 %llu 分钟",(unsigned long long)total);
    if(s==PN_OK){s=pn_font_size(font,22);if(s==PN_OK)s=pn_w_text_ex(font,frame,text,340,632,312,PN_ALIGN_RIGHT,PN_UI_MUTED,false);}
    if(s==PN_OK){s=pn_font_size(font,21);if(s==PN_OK)s=pn_w_text_ex(font,frame,"今日阅读",32,674,180,PN_ALIGN_LEFT,PN_UI_MUTED,false);}
    snprintf(text,sizeof text,"%llu",(unsigned long long)today);
    if(s==PN_OK){s=pn_font_size(font,44);if(s==PN_OK)s=pn_w_text_ex(font,frame,text,32,728,126,PN_ALIGN_LEFT,PN_UI_INK,true);}
    if(s==PN_OK){s=pn_font_size(font,21);if(s==PN_OK)s=pn_w_text_ex(font,frame,"分钟",152,728,60,PN_ALIGN_LEFT,PN_UI_MUTED,false);}
    for(unsigned i=0;i<7 && s==PN_OK;i++){
        int x=232+(int)i*58,h=(int)(minutes[i]*64u/maximum);bool active=dated && i==o->reading.weekday;
        if(minutes[i] && h<3)h=3;
        if(h)pn_w_round_fill(frame,x,728-h,26,h,2,active?PN_UI_INK:9);else pn_frame_rect(frame,x,728,26,1,12);
        s=pn_font_size(font,20);if(s==PN_OK)s=pn_w_text_ex(font,frame,days[i],x-4,764,34,PN_ALIGN_CENTER,active?PN_UI_INK:PN_UI_MUTED,active);
    }
    pn_frame_rect(frame,32,784,620,1,12);return s;
}
/// 首页只消费最近、封面及统计快照，不读取文件。/ Home consumes recent, cover and reading snapshots without file I/O.
static pn_status_t home_row(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,const pn_shelf_covers_t *covers,size_t i,int selected){
    int y=HOME_BOOKS_Y+(int)(i-1)*HOME_ROW_PITCH;char title[PN_CATALOG_NAME_MAX],meta[64];const pn_catalog_item_t *item=&page->items[i];title_of(item,covers,i,title,sizeof title);meta_of(item,meta,sizeof meta);
    pn_status_t s=cover_or_placeholder(font,frame,covers,i,item,32,y,72,100,false,3);
    if(s==PN_OK)s=pn_font_size(font,28);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,title,128,y+30,478,PN_ALIGN_LEFT,PN_UI_INK,selected==(int)i);
    if(s==PN_OK && covers && *covers->display[i].author){s=pn_font_size(font,22);if(s==PN_OK)s=pn_w_text_ex(font,frame,pn_brand_label(covers->display[i].author),128,y+66,478,PN_ALIGN_LEFT,PN_UI_MUTED,false);}
    if(s==PN_OK)s=pn_font_size(font,20);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,meta,128,y+100,478,PN_ALIGN_LEFT,PN_UI_MUTED,false);
    pn_w_icon(frame,PN_ICON_CHEVRON,630,y+42,24,PN_UI_MUTED);
    pn_frame_rect(frame,128,y+116,524,1,13);return s;
}
static pn_status_t home_render(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,const pn_shelf_covers_t *covers,const pn_shelf_options_t *options,int selected){
    int original=font->pixels;pn_frame_clear(frame,PN_UI_PAPER);home_ornament(frame);pn_status_t s=pn_w_status(font,frame);
    if(s==PN_OK)s=pn_font_size(font,46);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,"读书",32,122,300,PN_ALIGN_LEFT,PN_UI_INK,true);
    if(s==PN_OK){s=pn_font_size(font,22);if(s==PN_OK)s=pn_w_text_ex(font,frame,"拾起上次的阅读",32,162,300,PN_ALIGN_LEFT,PN_UI_MUTED,false);}
    if(s==PN_OK){s=pn_font_size(font,24);if(s==PN_OK)s=pn_w_text_ex(font,frame,*options->home_date?options->home_date:"日期待校时",370,130,282,PN_ALIGN_RIGHT,PN_UI_MUTED,false);}
    if(s==PN_OK)s=continue_card(covers,options,font,frame);
    if(s==PN_OK)s=home_reading(options,font,frame);
    if(s==PN_OK)s=pn_font_size(font,27);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,"最近阅读",32,818,380,PN_ALIGN_LEFT,PN_UI_INK,true);
    if(s==PN_OK)s=pn_font_size(font,24);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,"书库",440,818,172,PN_ALIGN_RIGHT,PN_UI_MUTED,false);
    pn_w_icon(frame,PN_ICON_CHEVRON,626,795,24,PN_UI_MUTED);
    for(size_t i=1;i<page->count && i<3 && s==PN_OK;i++)s=home_row(page,font,frame,covers,i,selected);
    if(s==PN_OK && page->count<=1){s=pn_font_size(font,26);if(s==PN_OK)s=pn_w_text_ex(font,frame,"下一本，从书库里挑选。",32,900,620,PN_ALIGN_LEFT,PN_UI_MUTED,false);}
    if(s==PN_OK && !page->count){s=pn_font_size(font,28);if(s==PN_OK)s=pn_w_button(font,frame,"传入图书",32,936,620,88,0);}
    if(s==PN_OK)s=pn_w_navigation(font,frame,PN_NAV_HOME);
    pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;
}
/* 缺封面时用两个完整字符作排版标记，避免重复的窄栏书名折行。/ Two complete characters form a typographic monogram instead of repeating narrow wrapped titles. */
static pn_status_t monogram_cover(pn_font_t *font,pn_frame_t *frame,int x,int y,int w,int h,const char *title,unsigned style){
    const char *start=title;while((*start>='0' && *start<='9') || *start=='-' || *start=='_' || *start==' ')start++;
    if(!*start)start=title;
    pn_text_source_t source={(void *)start,strlen(start),rd,NULL};pn_text_reader_t reader;pn_status_t s=pn_text_open(&reader,&source,PN_TEXT_UTF8);
    char mark[9]={0};size_t n=0;
    for(unsigned i=0;i<2 && s==PN_OK;i++){pn_text_char_t c;s=pn_text_next(&reader,&c);if(s==PN_EMPTY){s=PN_OK;break;}
        if(s==PN_OK){size_t bytes=(size_t)(c.end-c.begin);if(bytes>sizeof mark-1-n)return PN_INVALID;memcpy(mark+n,start+c.begin,bytes);n+=bytes;}}
    if(s!=PN_OK)return s;
    pn_frame_t v=view_of(frame,x,y,w,h);pn_frame_rect(&v,0,0,w,h,style==PN_SHELF_DEFAULT || style==PN_SHELF_WOOD?PN_UI_SURFACE:PN_UI_PAPER);
    if(style==PN_SHELF_DEFAULT){for(int i=0;i<4;i++)pn_w_line(&v,12.0f+(float)i*24.0f,(float)h-62.0f,44.0f+(float)i*24.0f,(float)h-88.0f,2.0f,12);}
    if(style==PN_SHELF_PAPER){pn_frame_rect(&v,14,24,w-64,2,PN_UI_MUTED);pn_frame_rect(&v,14,h-60,w-28,1,PN_UI_RULE);}
    s=pn_font_size(font,w>=160?52:w>=120?40:24);
    if(s==PN_OK)s=pn_w_text_ex(font,&v,*mark?mark:"书",12,h/2+12,w-24,PN_ALIGN_CENTER,PN_UI_MUTED,false);
    return s;
}
/* 网格书名使用完整列宽和单行省略，操作入口不挤占书名。/ Grid titles use the full column width and one-line elision; actions never squeeze titles. */
static pn_status_t grid_item(const pn_catalog_item_t *item,const pn_shelf_covers_t *covers,size_t i,pn_font_t *font,pn_frame_t *frame,bool selected,bool favorite,const pn_shelf_options_t *o){
    grid_box_t b=grid_box(i,o);int x=b.x,y=b.y,w=b.w,h=b.h;char title[PN_CATALOG_NAME_MAX];title_of(item,covers,i,title,sizeof title);
    unsigned style=o->style<PN_SHELF_STYLE_COUNT?o->style:PN_SHELF_DEFAULT;
    int radius=style==PN_SHELF_DEFAULT?10:3;
    // 占位图与真实图共用独立右下角格式标签。/ Placeholder and real covers share the separate bottom-right format badge.
    pn_frame_t cover;pn_status_t s=PN_OK;
    if(style==PN_SHELF_DEFAULT)pn_w_round_fill(frame,x+2,y+5,w-4,h,radius,PN_UI_SELECT);
    pn_profile_span_t span;pn_profile_begin(&span,NULL,"ui.grid.blit");
    if(pn_shelf_cover_frame(covers,i,&cover) || pn_builtin_cover_frame(title,&cover)==PN_OK)blit_scaled(frame,&cover,x,y,w,h);
    else s=monogram_cover(font,frame,x,y,w,h,title,style);
    pn_profile_end(&span,s);pn_profile_begin(&span,NULL,"ui.grid.frame");
    frame_cover(frame,x,y,w,h,radius);style_cover(frame,style,x,y,w,h);pn_profile_end(&span,PN_OK);
    if(s==PN_OK && favorite)favorite_badge(frame,x+10,y+10,28);
    // 书籍操作由长按打开，封面保持完整。/ Long press opens book actions, leaving covers unobstructed.
    if(s==PN_OK)s=format_badge(font,frame,item->format,x,y,w,h);
    bool started=item->identified || item->has_progress;
    if(started && item->progress<=10000){pn_frame_rect(frame,x+8,y+h-9,w-16,4,PN_UI_PAPER);pn_frame_rect(frame,x+8,y+h-9,(int)((unsigned)(w-16)*item->progress/10000u),4,PN_UI_INK);}
    int inset=style==PN_SHELF_WOOD?4:0,cx=x-inset,cw=w+inset*2,baseline=y+h+(inset?46:34);
    if(s==PN_OK)s=pn_font_size(font,columns_of(o)==4?22:24);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,title,cx,baseline,cw,PN_ALIGN_CENTER,PN_UI_INK,selected);
    if(selected)pn_frame_rect(frame,cx+24,baseline+5,cw-48,2,PN_UI_MUTED);
    return s;
}
/* 列表：80×112封面、全名最多两行、“进度 · 格式 · 大小”，整行可点。/ List: 80×112 cover, full name up to two lines, "progress · format · size"; the whole row is tappable. */
static pn_status_t list_item(const pn_catalog_item_t *item,const pn_shelf_covers_t *covers,size_t i,pn_font_t *font,pn_frame_t *frame,bool selected,bool favorite){
    int y=LIST_Y+(int)i*LIST_ROW_PITCH;char title[PN_CATALOG_NAME_MAX],meta[128],base[64];title_of(item,covers,i,title,sizeof title);meta_of(item,base,sizeof base);
    snprintf(meta,sizeof meta,"%s%s · %llu KB",favorite?"已收藏 · ":"",base,(unsigned long long)(item->size/1024+(item->size%1024!=0)));
    pn_status_t s=cover_or_placeholder(font,frame,covers,i,item,32,y+5,80,112,false,10);
    if(s==PN_OK && favorite)favorite_badge(frame,76,y+11,30);
    if(s==PN_OK)s=pn_font_size(font,28);
    if(s==PN_OK)s=pn_w_text_lines_ex(font,frame,title,132,y+42,480,2,36,PN_UI_INK,true);
    if(s==PN_OK && covers && *covers->display[i].author){s=pn_font_size(font,22);if(s==PN_OK)s=pn_w_text_ex(font,frame,pn_brand_label(covers->display[i].author),132,y+106,480,PN_ALIGN_LEFT,PN_UI_MUTED,false);}
    if(s==PN_OK)s=pn_font_size(font,22);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,meta,132,y+138,480,PN_ALIGN_LEFT,PN_UI_MUTED,false);
    if(selected)pn_w_round_fill(frame,20,y+12,6,98,3,PN_UI_INK);
    if(s==PN_OK)pn_frame_rect(frame,132,y+LIST_ROW_PITCH-2,520,1,PN_UI_SELECT);
    return s;
}
/* “导入图书”卡所在格；没有则-1。/ The grid slot of the import tile, or -1. */
static int import_slot(const pn_catalog_page_t *page,const pn_shelf_options_t *options,bool transfer){
    if(!transfer || !options->import_tile || options->list_mode || options->query || options->favorites || !page->count || page->more || page->count>=pn_shelf_capacity(options))return -1;
    return (int)page->count;
}
/* 虚线圆角框：先画整圈，再在直边上按段擦成纸色。/ Dashed rounded frame: draw the whole outline, then erase gaps along the straight edges with paper. */
static void dashed_frame(pn_frame_t *frame,int x,int y,int w,int h,int radius){
    pn_w_round_stroke(frame,x,y,w,h,radius,2.0f,PN_UI_INK);
    for(int px=x+radius+8;px<x+w-radius-8;px+=20){pn_frame_rect(frame,px,y-1,8,4,PN_UI_PAPER);pn_frame_rect(frame,px,y+h-3,8,4,PN_UI_PAPER);}
    for(int py=y+radius+8;py<y+h-radius-8;py+=20){pn_frame_rect(frame,x-1,py,4,8,PN_UI_PAPER);pn_frame_rect(frame,x+w-3,py,4,8,PN_UI_PAPER);}
}
pn_status_t pn_shelf_render_ex(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,int selected,bool recent,bool transfer,const pn_shelf_covers_t *covers,const pn_shelf_options_t *options){
    if(!page || page->count>PN_CATALOG_PAGE_MAX || !font || !font->impl || !frame || !frame->pixels || frame->width!=684 || frame->height!=1216 || frame->stride<342)return PN_INVALID;
    if(!options)options=&default_options;
    if(options->home)return home_render(page,font,frame,covers,options,selected);
    bool list=options->list_mode;size_t visible=pn_shelf_capacity(options);
    int original=font->pixels;pn_frame_clear(frame,PN_UI_PAPER);
    pn_status_t s=pn_w_status(font,frame);
    // 标题行只保留书库标题。/ Keep only the library heading in the title row.
    if(s==PN_OK)s=pn_font_size(font,40);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,options->query?"搜索结果":"书库",32,120,420,PN_ALIGN_LEFT,PN_UI_INK,true);


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
    // 拟物搁板在书名前留空间；纸页用横线分组。/ Ledges leave room before titles; paper rows use a grouping rule.
    if(!list && page->count){for(size_t row=0;row<3;row++){grid_box_t b=grid_box(row*columns_of(options),options);
        if(options->style==PN_SHELF_WOOD)ledge(frame,24,b.y+b.h+1,636);
    }}
    // 书目：网格或列表。/ Books: grid or list.
    for(size_t i=0;i<page->count && i<visible && s==PN_OK;i++){
        const pn_catalog_item_t *item=&page->items[i];if(strnlen(item->name,sizeof item->name)>=sizeof item->name)return PN_INVALID;
        s=list?list_item(item,covers,i,font,frame,(int)i==selected,pn_favorites_contains(options->favorite_items,item->path)):grid_item(item,covers,i,font,frame,(int)i==selected,pn_favorites_contains(options->favorite_items,item->path),options);
    }
    // 最后一页网格还有空位时放虚线“导入图书”卡（需要传书能力）。/ A dashed "import books" tile fills a free grid slot on the last page (needs transfer).
    if(s==PN_OK && import_slot(page,options,transfer)>=0){
        grid_box_t b=grid_box((size_t)import_slot(page,options,transfer),options);int x=b.x,y=b.y;
        dashed_frame(frame,x,y,b.w,b.h,PN_UI_RADIUS);
        pn_w_icon(frame,PN_ICON_PLUS,x+(b.w-48)/2,y+b.h/3,48,PN_UI_INK);
        s=pn_font_size(font,24);if(s==PN_OK)s=pn_w_text_ex(font,frame,"导入图书",x,y+b.h-42,b.w,PN_ALIGN_CENTER,PN_UI_INK,true);
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
    // 页码只读，内容区左右滑动翻页。/ Read-only page number; swipe horizontally over content to turn.
    if(s==PN_OK && page->count && page->total){
        char info[48];unsigned per=(unsigned)visible,current=(unsigned)(page->index/per)+1,pages=(unsigned)((page->total+per-1)/per);
        snprintf(info,sizeof info,"%u / %u",current,pages<current?current:pages);
        s=pn_font_size(font,22);if(s==PN_OK)s=pn_w_text_ex(font,frame,info,262,INFO_Y+26,160,PN_ALIGN_CENTER,PN_UI_MUTED,false);
    }
    // 固定底栏：书架 / 传书 / 设置。/ Fixed bottom bar: shelf / transfer / settings.
    if(s==PN_OK){s=pn_font_size(font,24);if(s==PN_OK)s=pn_w_navigation(font,frame,PN_NAV_LIBRARY);}
    pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;
}
pn_status_t pn_shelf_render_covers(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,int selected,bool recent,bool transfer,const pn_shelf_covers_t *covers){
    return pn_shelf_render_ex(page,font,frame,selected,recent,transfer,covers,NULL);
}
pn_status_t pn_shelf_render_focus(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,int before,int after,const pn_shelf_covers_t *covers,const pn_shelf_options_t *options){
    if(!page || page->count>PN_CATALOG_PAGE_MAX || !font || !font->impl || !frame || !frame->pixels || frame->width!=684 || frame->height!=1216 || frame->stride<342 || before< -1 || after< -1)return PN_INVALID;
    if(!options)options=&default_options;
    if(options->actions)return PN_UNSUPPORTED;
    size_t visible=options->home?3u:pn_shelf_capacity(options);if(visible>page->count)visible=page->count;
    if(before>=(int)visible || (options->home && before==0))before=-1;
    if(after>=(int)visible || (options->home && after==0))after=-1;
    if(before==after)return PN_EMPTY;
    if(!options->home && !options->list_mode){int slot=import_slot(page,options,true);unsigned cols=columns_of(options);if(slot>=0 && ((before>=0 && (unsigned)before/cols==(unsigned)slot/cols) || (after>=0 && (unsigned)after/cols==(unsigned)slot/cols)))return PN_UNSUPPORTED;}
    int original=font->pixels;pn_status_t s=PN_OK;
    if(options->home){
        for(size_t i=1;i<visible && s==PN_OK;i++)if((int)i==before || (int)i==after){pn_frame_rect(frame,0,HOME_BOOKS_Y+(int)(i-1)*HOME_ROW_PITCH,684,HOME_ROW_PITCH,PN_UI_PAPER);s=home_row(page,font,frame,covers,i,after);}
    }else if(options->list_mode){
        // 列表焦点只有左侧标记变化，封面和文字保持原像素。/ List focus changes only the left marker, preserving cover and text pixels.
        if(before>=0)pn_frame_rect(frame,18,LIST_Y+before*LIST_ROW_PITCH+10,10,102,PN_UI_PAPER);
        if(after>=0)pn_w_round_fill(frame,20,LIST_Y+after*LIST_ROW_PITCH+12,6,98,3,PN_UI_INK);
    }else{
        // 按整条网格行清理，覆盖字形边界和书架装饰，不碰另一行、题头或导航。
        // Restore whole grid bands including glyph edges and shelf decoration, preserving other rows, headings and navigation.
        unsigned cols=columns_of(options);for(unsigned row=0;row<3 && s==PN_OK;row++)if((before>=0 && (unsigned)before/cols==row) || (after>=0 && (unsigned)after/cols==row)){
            pn_frame_rect(frame,0,GRID_Y+(int)row*ROW_PITCH,684,ROW_PITCH,PN_UI_PAPER);
            grid_box_t b=grid_box((size_t)row*cols,options);if(options->style==PN_SHELF_WOOD)ledge(frame,24,b.y+b.h+1,636);
            for(size_t i=(size_t)row*cols;i<visible && i<(size_t)(row+1)*cols && s==PN_OK;i++)s=grid_item(&page->items[i],covers,i,font,frame,(int)i==after,pn_favorites_contains(options->favorite_items,page->items[i].path),options);
        }
    }
    pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;
}
int pn_shelf_hit_ex(const pn_catalog_page_t *page,int x,int y,const pn_shelf_options_t *options){
    if(!page || x<0 || x>=684 || y<0 || y>=1216)return -1;
    if(!options)options=&default_options;
    if(y>=TAB_Y){int tab=pn_w_navigation_hit(x,y);return tab==PN_NAV_HOME?PN_SHELF_READING_HOME:tab==PN_NAV_LIBRARY?PN_SHELF_HOME:tab==PN_NAV_TRANSFER?PN_SHELF_TRANSFER:tab==PN_NAV_SETTINGS?PN_SHELF_MENU:-1;}
    if(options->home){
        if(x>=32 && x<652 && y>=HOME_CARD_Y && y<HOME_CARD_Y+HOME_CARD_H)return page->count?PN_SHELF_CONTINUE:PN_SHELF_HOME;
        if(y>=754 && y<842 && x>=440 && x<652)return PN_SHELF_HOME;
        if(y>=936 && y<1024 && x>=32 && x<652 && !page->count)return PN_SHELF_TRANSFER;
        if(y>=HOME_BOOKS_Y && y<HOME_BOOKS_Y+2*HOME_ROW_PITCH && x>=32 && x<652){int row=(y-HOME_BOOKS_Y)/HOME_ROW_PITCH+1;return row<(int)page->count && row<3?row:-1;}
        return -1;
    }
    if(y<56)return -1;
    if(y<CHIP_Y-10){return x>=32 && x<300?PN_SHELF_INDEX:-1;}
    if(y<LIST_Y-16){
        if(y<CHIP_Y-10)return -1;
        if(options->layout_toggle && x>=LAYOUT_X && x<664)return PN_SHELF_LAYOUT;
        int chip=(x-38)/CHIP_W;unsigned count=options->favorites_tab?3u:2u;
        if(x>=32 && chip>=0 && chip<(int)count)return chip==0?PN_SHELF_TAB_ALL:chip==1?PN_SHELF_TAB_RECENT:PN_SHELF_TAB_FAVORITES;
        return -1;
    }
    if(!page->count && y>=776 && y<864 && x>=212 && x<472){if(options->query)return PN_SHELF_SEARCH;if(options->import_tile && !options->list_mode && !options->favorites)return PN_SHELF_IMPORT;}
    if(y<INFO_Y){
        if(y<LIST_Y)return -1;
        {int slot=import_slot(page,options,true);if(slot>=0){grid_box_t b=grid_box((size_t)slot,options);if(x>=b.x && x<b.x+b.w && y>=b.y && y<b.y+b.h)return PN_SHELF_IMPORT;}}
        if(options->list_mode){
            if(x<32 || x>=652)return -1;
            int row=(y-LIST_Y)/LIST_ROW_PITCH;if(row>=LIST_ROWS || row>=(int)page->count)return -1;
            return row;
        }
        for(size_t i=0;i<page->count && i<pn_shelf_capacity(options);i++){grid_box_t b=grid_box(i,options);
            int inset=options->style==PN_SHELF_WOOD?4:0;
            if(x>=b.x-inset && x<b.x+b.w+inset && y>=b.y && y<b.y+b.h+48){
                return (int)i;
            }
        }
        return -1;
    }
    return -1;
}
int pn_shelf_hit(const pn_catalog_page_t *page,int x,int y){return pn_shelf_hit_ex(page,x,y,NULL);}

/* 4列7行：A–Z、#、返回。/ Four columns by seven rows: A–Z, #, Back. */
static const char cells[]="abcdefghijklmnopqrstuvwxyz#<";
pn_status_t pn_shelf_index_render(pn_font_t *font,pn_frame_t *frame){
    if(!font || !font->impl || !frame || !frame->pixels || frame->width!=684 || frame->height!=1216 || frame->stride<342)return PN_INVALID;
    pn_frame_clear(frame,15);pn_status_t s=at(font,frame,PN_BRAND_NAME,32,20,620,52,1);
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
int pn_shelf_hit_with_transfer(const pn_catalog_page_t *p,int x,int y,bool enabled){if(enabled && p && pn_w_navigation_hit(x,y)==PN_NAV_TRANSFER)return PN_SHELF_TRANSFER;return pn_shelf_hit(p,x,y);}

#include "pn_text_file.h"
static pn_status_t with_font_file(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,int selected,bool recent,bool transfer,const pn_shelf_covers_t *covers,
    pn_pool_t *pool,pn_media_t *media,const char *font_path,const pn_shelf_options_t *options,bool focus,int before){
    if(focus && options && options->list_mode && !options->home)return pn_shelf_render_focus(page,font,frame,before,selected,covers,options);
    pn_text_file_t file={0};pn_media_lease_t lease;pn_font_t extra={0};pn_text_source_t source={0};bool leased=false,file_open=false,font_open=false;
    // 动态书名先用系统中文补字；借阅读字体作为末级补字。只开字体、不做全量字形校验；外置字体失败仍可使用系统字库。
    // Dynamic titles use system CJK first and the borrowed reading font last. Only open the font without full glyph validation; an external-font failure still allows system glyphs.
    if(font_path && *font_path && pool && media && media->available){
        leased=pn_media_acquire(media,PN_MEDIA_READ,&lease)==PN_OK;
        file_open=leased && pn_text_file_open(&file,media,&lease,font_path,&source)==PN_OK;
        font_open=file_open && pn_font_open(&extra,pool,&source,28)==PN_OK;
        if(font_open)pn_w_set_fallback(&extra);
    }
    pn_w_font_scope_t scope={0};pn_status_t status=pool?pn_w_font_scope_begin(&scope,pool,font_open?&extra:NULL):PN_OK;
    if(status==PN_OK)status=focus?pn_shelf_render_focus(page,font,frame,before,selected,covers,options):pn_shelf_render_ex(page,font,frame,selected,recent,transfer,covers,options);
    if(status==PN_OK && options && options->actions)status=pn_book_actions_render(options->actions,font,frame);
    pn_w_font_scope_end(&scope);pn_w_set_fallback(NULL);
    if(font_open)pn_font_close(&extra);
    if(file_open)(void)pn_text_file_close(&file);
    if(leased)(void)pn_media_release(media,&lease);
    return status;
}
pn_status_t pn_shelf_render_with_font_file_ex(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,int selected,bool recent,bool transfer,const pn_shelf_covers_t *covers,pn_pool_t *pool,pn_media_t *media,const char *font_path,const pn_shelf_options_t *options){return with_font_file(page,font,frame,selected,recent,transfer,covers,pool,media,font_path,options,false,-1);}
pn_status_t pn_shelf_focus_with_font_file(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,int before,int after,const pn_shelf_covers_t *covers,pn_pool_t *pool,pn_media_t *media,const char *font_path,const pn_shelf_options_t *options){return with_font_file(page,font,frame,after,false,false,covers,pool,media,font_path,options,true,before);}
pn_status_t pn_shelf_render_with_font_file(const pn_catalog_page_t *page,pn_font_t *font,pn_frame_t *frame,int selected,bool recent,bool transfer,const pn_shelf_covers_t *covers,
    pn_pool_t *pool,pn_media_t *media,const char *font_path){
    return pn_shelf_render_with_font_file_ex(page,font,frame,selected,recent,transfer,covers,pool,media,font_path,NULL);
}
