/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：同源 EPUB 脚注弹窗实现，见 pn_footnote_ui.h。
 * English: Shared EPUB footnote popup UI implementation, see pn_footnote_ui.h.
 */
#include "pn_footnote_ui.h"
#include "pn_widgets.h"
#include <stdio.h>
#include <string.h>

#define CARD_PAD_X 24
#define BODY_FONT_PX 25
#define BODY_LINE_PITCH 38
#define BODY_MAX_LINES 9u
#define BODY_X (PN_FOOTNOTE_CARD_X + CARD_PAD_X)
#define BODY_W (PN_FOOTNOTE_CARD_W - 2 * CARD_PAD_X)

typedef struct {
    pn_epub_footnote_t meta;
    char text[PN_FOOTNOTE_TEXT_MAX];
    size_t text_len;
    int sheet_y;
    bool top_docked;
    bool pages_ready;
    size_t page_starts[PN_FOOTNOTE_PAGES_MAX];
    size_t page_count;
    size_t page_index;
} fn_ui_impl_t;

static size_t utf8_len_one(const char *s){
    unsigned char c=(unsigned char)*s;
    if(!c)return 0;
    if(c<0x80u)return 1;
    if((c&0xE0u)==0xC0u)return s[1]?2:1;
    if((c&0xF0u)==0xE0u)return (s[1] && s[2])?3:1;
    if((c&0xF8u)==0xF0u)return (s[1] && s[2] && s[3])?4:1;
    return 1;
}

static pn_status_t sized_text(pn_font_t *font,pn_frame_t *frame,const char *value,int size,int x,int baseline,int width,pn_align_t align,uint8_t ink,bool bold){
    int original=font->pixels;
    pn_status_t s=pn_font_size(font,size);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,value,x,baseline,width,align,ink,bold);
    pn_status_t restored=pn_font_size(font,original);
    return s==PN_OK?restored:s;
}

/// 排版或绘制单页脚注正文，返回下一页起始字节偏移。/ Layout or render one wrapped footnote page, returning the next-page byte offset.
static pn_status_t layout_page(pn_font_t *font,pn_frame_t *frame,const char *text,size_t len,size_t start_off,int x,int y0,int max_w,unsigned max_lines,int pitch,size_t *next_off_out){
    size_t pos=start_off<len?start_off:len;
    unsigned line=0;
    pn_status_t s=PN_OK;
    char buf[256];
    while(pos<len && line<max_lines && s==PN_OK){
        if(text[pos]=='\n'){
            pos++;
            line++;
            continue;
        }
        size_t line_start=pos,line_end=pos,last_space=0;
        bool has_space=false;
        while(pos<len && text[pos]!='\n'){
            size_t clen=utf8_len_one(text+pos);
            size_t cand=(pos-line_start)+clen;
            if(cand>=sizeof buf)break;
            memcpy(buf,text+line_start,cand);
            buf[cand]=0;
            int w=0;
            s=pn_w_text_width(font,buf,&w);
            if(s!=PN_OK)break;
            if(w>max_w && line_end>line_start){
                if(has_space && last_space>line_start && (unsigned char)text[pos]<0x80u){
                    line_end=last_space;
                    pos=last_space;
                    while(pos<len && text[pos]==' ')pos++;
                }
                break;
            }
            if(text[pos]==' '){last_space=pos;has_space=true;}
            pos+=clen;
            line_end=pos;
        }
        if(s!=PN_OK)break;
        if(line_end==line_start && pos<len){
            size_t clen=utf8_len_one(text+pos);
            pos+=clen;
            line_end=pos;
        }
        if(frame && line_end>line_start){
            size_t out_n=line_end-line_start;
            if(out_n>=sizeof buf)out_n=sizeof buf-1;
            memcpy(buf,text+line_start,out_n);
            buf[out_n]=0;
            s=pn_w_text_ex(font,frame,buf,x,y0+(int)line*pitch,max_w,PN_ALIGN_LEFT,0,false);
        }
        if(pos<len && text[pos]=='\n')pos++;
        line++;
    }
    if(next_off_out)*next_off_out=pos;
    return s;
}

static pn_status_t ensure_pages(fn_ui_impl_t *d,pn_font_t *font){
    if(d->pages_ready)return PN_OK;
    d->page_count=0;
    d->page_starts[0]=0;
    int original=font->pixels;
    pn_status_t s=pn_font_size(font,BODY_FONT_PX);
    if(s==PN_OK){
        size_t pos=0;
        while(pos<d->text_len && d->page_count<PN_FOOTNOTE_PAGES_MAX){
            d->page_starts[d->page_count++]=pos;
            size_t next_pos=pos;
            s=layout_page(font,NULL,d->text,d->text_len,pos,BODY_X,0,BODY_W,BODY_MAX_LINES,BODY_LINE_PITCH,&next_pos);
            if(s!=PN_OK)break;
            if(next_pos<=pos)break;
            pos=next_pos;
        }
    }
    pn_status_t restored=pn_font_size(font,original);
    if(s==PN_OK)s=restored;
    if(s!=PN_OK)return s;
    if(!d->page_count){d->page_starts[0]=0;d->page_count=1;}
    if(d->page_index>=d->page_count)d->page_index=d->page_count-1;
    d->pages_ready=true;
    return PN_OK;
}

/// 绘制清晰 2px 粗关闭叉号图标。/ Draw a crisp 2px-thick close "×" icon.
static void draw_close_x(pn_frame_t *frame,int cx,int cy,int half,uint8_t ink){
    for(int d=-half;d<=half;d++){
        pn_frame_rect(frame,cx+d,cy+d,2,2,ink);
        pn_frame_rect(frame,cx+d,cy-d,2,2,ink);
    }
}

pn_status_t pn_footnote_ui_open(pn_footnote_ui_t *ui,pn_pool_t *pool,const pn_epub_footnote_t *meta,const char *text,size_t text_len){
    if(!ui || !pool || !meta || !text)return PN_INVALID;
    pn_footnote_ui_close(ui);
    fn_ui_impl_t *d=pn_alloc_tagged(pool,PN_MEM_UI,sizeof *d);
    if(!d)return PN_NO_MEMORY;
    memset(d,0,sizeof *d);
    d->meta=*meta;
    // 若标题已展示角标（如“注 [16]”）且注释正文开头重复了同一角标，则剥离冗余前缀。
    // If the header already shows the label (e.g., "注 [16]") and the note body starts with the same label, strip the redundant prefix.
    size_t skip=0;
    while(skip<text_len && (text[skip]==' ' || text[skip]=='\t'))skip++;
    if(meta->label[0]){
        size_t ll=strlen(meta->label);
        if(ll>0 && skip+ll<text_len && !memcmp(text+skip,meta->label,ll)){
            size_t after=skip+ll;
            while(after<text_len){
                unsigned char c=(unsigned char)text[after];
                if(c==' ' || c=='\t' || c==':' || c=='.'){after++;continue;}
                if(after+3<=text_len && (!memcmp(text+after,"\xef\xbc\x9a",3) || !memcmp(text+after,"\xe3\x80\x81",3) || !memcmp(text+after,"\xef\xbc\x8e",3))){after+=3;continue;}
                break;
            }
            if(after<text_len)skip=after;
        }
    }
    text+=skip;
    text_len-=skip;
    size_t copy=text_len;
    if(copy>=sizeof d->text-4)copy=sizeof d->text-4;
    // 防止截断落在 UTF-8 多字节字符中间。/ Avoid slicing inside a multi-byte UTF-8 sequence.
    while(copy>0 && (((unsigned char)text[copy]&0xC0u)==0x80u))copy--;
    memcpy(d->text,text,copy);
    if(meta->truncated && copy+3<sizeof d->text){
        memcpy(d->text+copy,"\xe2\x80\xa6",3);copy+=3;
    }
    d->text[copy]=0;
    d->text_len=copy;
    // 与离线词典卡片一致：统一在下方弹出，不随角标位置上下跳动。
    // Always dock at the bottom just like the offline dictionary floating card.
    d->top_docked=false;
    d->sheet_y=PN_FOOTNOTE_BOTTOM_Y;
    d->page_count=1;
    d->page_starts[0]=0;
    ui->impl=d;
    ui->active=true;
    return PN_OK;
}

pn_status_t pn_footnote_ui_render(pn_footnote_ui_t *ui,pn_font_t *font,pn_frame_t *frame){
    if(!ui || !ui->impl || !ui->active || !font || !frame || !frame->pixels || frame->width!=684 || frame->height!=1216)return PN_INVALID;
    fn_ui_impl_t *d=ui->impl;
    pn_status_t s=ensure_pages(d,font);
    if(s!=PN_OK)return s;
    const int cx=PN_FOOTNOTE_CARD_X,cy=d->sheet_y,cw=PN_FOOTNOTE_CARD_W,ch=PN_FOOTNOTE_SHEET_H;
    // 与离线词典完全一致的居中圆角浮层卡片：柔和右下投影 + 纯白圆角底 + 细描边。
    // Floating rounded card matching the offline dictionary: soft drop shadow, crisp white card, thin dark stroke.
    pn_w_round_fill(frame,cx+5,cy+5,cw,ch,14,13);
    pn_w_round_fill(frame,cx+2,cy+2,cw,ch,14,11);
    pn_w_round_fill(frame,cx,cy,cw,ch,14,PN_UI_PAPER);
    pn_w_round_stroke(frame,cx,cy,cw,ch,14,1.5f,3);
    // 标题栏：左“注 [103]”，右关闭叉号 ×（与词典同位置同尺寸）。
    // Header: "注 [103]" on the left, close × on the right (matching dictionary header geometry).
    char title[64];
    if(d->meta.label[0]){
        bool has_prefix=(d->meta.label[0]=='\xe6' && d->meta.label[1]=='\xb3' && d->meta.label[2]=='\xa8') ||
                        (d->meta.label[0]=='\xe8' && d->meta.label[1]=='\xa8' && d->meta.label[2]=='\xbb');
        if(has_prefix)snprintf(title,sizeof title,"%s",d->meta.label);
        else snprintf(title,sizeof title,"\xe6\xb3\xa8 %s",d->meta.label);
    }else{
        snprintf(title,sizeof title,"\xe6\xb3\xa8\xe9\x87\x8a");
    }
    s=sized_text(font,frame,title,28,cx+CARD_PAD_X,cy+45,cw-96,PN_ALIGN_LEFT,PN_UI_INK,true);
    if(s!=PN_OK)return s;
    draw_close_x(frame,cx+550,cy+34,10,2);
    pn_frame_rect(frame,cx+20,cy+68,cw-40,1,13);
    // 脚注正文区。/ Footnote body area.
    int original=font->pixels;
    s=pn_font_size(font,BODY_FONT_PX);
    if(s==PN_OK)s=layout_page(font,frame,d->text,d->text_len,d->page_starts[d->page_index],BODY_X,cy+104,BODY_W,BODY_MAX_LINES,BODY_LINE_PITCH,NULL);
    pn_status_t restored=pn_font_size(font,original);
    if(s==PN_OK)s=restored;
    if(s!=PN_OK)return s;
    // 右侧细进度条（仅多页时显示，与词典卡片一致）。/ Right subtle scrollbar shown on multi-page notes (matches dictionary card).
    if(d->page_count>1){
        int track_y=cy+80,track_h=386;
        pn_frame_rect(frame,cx+cw-12,track_y,3,track_h,14);
        int thumb_h=(int)(track_h/(int)d->page_count);
        if(thumb_h<28)thumb_h=28;
        int thumb_y=track_y+(int)((size_t)(track_h-thumb_h)*d->page_index/(d->page_count-1));
        pn_frame_rect(frame,cx+cw-12,thumb_y,3,thumb_h,4);
    }
    // 极简底栏（与词典卡片同高同样式）：有跳转时左侧放“前往脚注 >”，多页放 < 1 / N >，单页无跳转提示“点卡外关闭”。
    // Minimalist footer matching dictionary card: "前往脚注 >" left when jumpable, < 1 / N > when multi-page, else centered dismiss hint.
    pn_frame_rect(frame,cx+20,cy+476,cw-40,1,11);
    if(d->meta.can_jump){
        pn_w_round_fill(frame,cx+20,cy+486,162,44,10,PN_UI_SURFACE);
        pn_w_round_stroke(frame,cx+20,cy+486,162,44,10,1.5f,9);
        s=sized_text(font,frame,"\xe5\x89\x8d\xe5\xbe\x80\xe8\x84\x9a\xe6\xb3\xa8 >",21,cx+26,cy+515,150,PN_ALIGN_CENTER,PN_UI_INK,false);
        if(s!=PN_OK)return s;
    }
    if(d->page_count>1){
        bool has_prev=d->page_index>0,has_next=d->page_index+1<d->page_count;
        char pg[24];snprintf(pg,sizeof pg,"%u / %u",(unsigned)(d->page_index+1),(unsigned)d->page_count);
        if(d->meta.can_jump){
            pn_w_icon(frame,PN_ICON_BACK,cx+cw-196,cy+497,22,has_prev?2:12);
            s=sized_text(font,frame,pg,21,cx+cw-166,cy+516,112,PN_ALIGN_CENTER,PN_UI_MUTED,false);
            pn_w_icon(frame,PN_ICON_CHEVRON,cx+cw-52,cy+497,22,has_next?2:12);
        }else{
            pn_w_icon(frame,PN_ICON_BACK,cx+30,cy+497,22,has_prev?2:12);
            s=sized_text(font,frame,pg,21,cx+76,cy+516,cw-152,PN_ALIGN_CENTER,PN_UI_MUTED,false);
            pn_w_icon(frame,PN_ICON_CHEVRON,cx+cw-52,cy+497,22,has_next?2:12);
        }
    }else if(!d->meta.can_jump){
        s=sized_text(font,frame,"\xe7\x82\xb9\xe5\x8d\xa1\xe5\xa4\x96\xe5\x85\xb3\xe9\x97\xad",21,cx+76,cy+516,cw-152,PN_ALIGN_CENTER,PN_UI_MUTED,false);
    }
    return s;
}

pn_status_t pn_footnote_ui_present(pn_footnote_ui_t *ui,pn_pool_t *pool,pn_font_t *font,pn_frame_t *page_frame,pn_reader_present_fn present,void *ctx){
    if(!ui || !ui->impl || !ui->active || !pool || !font || !page_frame || !page_frame->pixels || page_frame->width!=684 || page_frame->height!=1216 || page_frame->stride<342 || !present)return PN_INVALID;
    fn_ui_impl_t *d=ui->impl;
    unsigned top=(unsigned)(d->sheet_y>2?d->sheet_y-2:0);
    unsigned bottom=(unsigned)(d->sheet_y+PN_FOOTNOTE_SHEET_H+8);
    if(bottom>1216u)bottom=1216u;
    unsigned rows=bottom-top;
    uint8_t *saved=pn_alloc_tagged(pool,PN_MEM_FRAME,(size_t)rows*342u);
    if(!saved)return PN_NO_MEMORY;
    for(unsigned y=0;y<rows;y++)memcpy(saved+(size_t)y*342u,page_frame->pixels+(size_t)(top+y)*page_frame->stride,342u);
    pn_status_t status=pn_footnote_ui_render(ui,font,page_frame);
    if(status==PN_OK)status=present(ctx,page_frame,PN_REFRESH_GL16);
    for(unsigned y=0;y<rows;y++)memcpy(page_frame->pixels+(size_t)(top+y)*page_frame->stride,saved+(size_t)y*342u,342u);
    pn_free(saved);
    return status;
}

int pn_footnote_ui_hit(const pn_footnote_ui_t *ui,int x,int y){
    if(!ui || !ui->impl || !ui->active || x<0 || x>=684 || y<0 || y>=1216)return -1;
    const fn_ui_impl_t *d=ui->impl;
    const int cx=PN_FOOTNOTE_CARD_X,cy=d->sheet_y,cw=PN_FOOTNOTE_CARD_W,ch=PN_FOOTNOTE_SHEET_H;
    // 点击浮层卡片外任何正文区域直接关闭（与词典弹窗一致）。/ Tapping anywhere outside the floating card closes it (matches dictionary popup).
    if(x<cx || x>=cx+cw || y<cy || y>=cy+ch)return PN_FNUI_DISMISS;
    // 右上角 × 关闭按钮。/ Top-right close × button.
    if(y<cy+68){
        if(x>=cx+516 && x<cx+cw)return PN_FNUI_CLOSE;
        return -1;
    }
    // 底栏操作区。/ Footer actions.
    if(y>=cy+476){
        if(d->meta.can_jump && x>=cx+16 && x<cx+196)return PN_FNUI_JUMP;
        if(d->page_count>1){
            if(d->meta.can_jump){
                if(d->page_index>0 && x>=cx+cw-216 && x<cx+cw-116)return PN_FNUI_PREV;
                if(d->page_index+1<d->page_count && x>=cx+cw-116 && x<cx+cw-8)return PN_FNUI_NEXT;
            }else{
                if(d->page_index>0 && x>=cx+8 && x<cx+148)return PN_FNUI_PREV;
                if(d->page_index+1<d->page_count && x>=cx+cw-148 && x<cx+cw-8)return PN_FNUI_NEXT;
            }
        }
        return -1;
    }
    // 正文区多页时点左 1/3 翻上一页、右 2/3 翻下一页（与词典弹窗一致）。
    // On multi-page notes, left 1/3 taps back and right 2/3 taps forward (matches dictionary popup).
    if(d->page_count>1 && y>=cy+68){
        if(x<cx+cw/3 && d->page_index>0)return PN_FNUI_BODY_PREV;
        if(x>=cx+cw/3 && d->page_index+1<d->page_count)return PN_FNUI_BODY_NEXT;
    }
    return -1;
}

pn_status_t pn_footnote_ui_step(pn_footnote_ui_t *ui,int delta){
    if(!ui || !ui->impl || !ui->active)return PN_INVALID;
    fn_ui_impl_t *d=ui->impl;
    if(delta<0){
        if(d->page_index==0)return PN_EMPTY;
        d->page_index--;return PN_OK;
    }
    if(delta>0){
        if(d->page_index+1>=d->page_count)return PN_EMPTY;
        d->page_index++;return PN_OK;
    }
    return PN_EMPTY;
}

const pn_epub_footnote_t *pn_footnote_ui_meta(const pn_footnote_ui_t *ui){
    return (ui && ui->impl && ui->active)?&((const fn_ui_impl_t *)ui->impl)->meta:NULL;
}
const char *pn_footnote_ui_text(const pn_footnote_ui_t *ui){
    return (ui && ui->impl && ui->active)?((const fn_ui_impl_t *)ui->impl)->text:NULL;
}
size_t pn_footnote_ui_page_index(const pn_footnote_ui_t *ui){
    return (ui && ui->impl && ui->active)?((const fn_ui_impl_t *)ui->impl)->page_index:0;
}
size_t pn_footnote_ui_page_count(const pn_footnote_ui_t *ui){
    return (ui && ui->impl && ui->active)?((const fn_ui_impl_t *)ui->impl)->page_count:0;
}
int pn_footnote_ui_sheet_y(const pn_footnote_ui_t *ui){
    return (ui && ui->impl && ui->active)?((const fn_ui_impl_t *)ui->impl)->sheet_y:0;
}

void pn_footnote_ui_close(pn_footnote_ui_t *ui){
    if(!ui)return;
    pn_free(ui->impl);
    ui->impl=NULL;
    ui->active=false;
}
