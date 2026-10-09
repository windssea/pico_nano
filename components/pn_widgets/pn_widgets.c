#define _POSIX_C_SOURCE 200809L
/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：共用绘制控件，见pn_widgets.h。
 * English: shared drawing widgets, see pn_widgets.h.
 */
#include "pn_widgets.h"
#include <string.h>
#define MAX_CHARS 256
/* 文字来源：主字体、方框占位、备用字体。/ Glyph source: primary font, box placeholder, fallback font. */
enum {SRC_PRIMARY=0,SRC_BOX=1,SRC_FALLBACK=2};
static pn_font_t *fallback_font;
void pn_w_set_fallback(pn_font_t *font){fallback_font=font;}
static pn_status_t string_read(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){
    const char *s=ctx;size_t size=strlen(s);if(off>size)return PN_INVALID;
    size_t take=size-(size_t)off;if(take>cap)take=cap;memcpy(out,s+off,take);*n=take;return PN_OK;
}
/* 把UTF-8解成码点与26.6字宽；超出MAX_CHARS的部分忽略。/ Decode UTF-8 into code points and 26.6 advances; anything past MAX_CHARS is ignored. */
static pn_status_t decode(pn_font_t *font,const char *s,uint32_t *cp,int32_t *adv,uint8_t *missing,size_t *count){
    pn_text_source_t source={(void *)s,strlen(s),string_read,NULL};pn_text_reader_t reader;
    pn_status_t status=pn_text_open(&reader,&source,PN_TEXT_UTF8);if(status!=PN_OK)return status;
    pn_text_char_t c;size_t n=0;
    while(n<MAX_CHARS && (status=pn_text_next(&reader,&c))==PN_OK){
        int32_t advance;pn_status_t measured=pn_font_advance(font,c.codepoint,&advance);
        missing[n]=SRC_PRIMARY;
        if(measured==PN_EMPTY){
            // 主字体缺字时先问备用字体（例如阅读字体），仍缺才画方框。/ When the primary font lacks a glyph ask the fallback (e.g. the reading font) before drawing a box.
            if(fallback_font && fallback_font->impl && pn_font_size(fallback_font,font->pixels)==PN_OK && pn_font_advance(fallback_font,c.codepoint,&advance)==PN_OK)missing[n]=SRC_FALLBACK;
            else{advance=font->pixels*64;missing[n]=SRC_BOX;}
        }
        else if(measured!=PN_OK)return measured;
        if(advance<0 || advance>PN_UI_WIDTH*64)return PN_LIMIT;
        cp[n]=c.codepoint;adv[n]=advance;n++;
    }
    *count=n;return status==PN_EMPTY || n==MAX_CHARS?PN_OK:status;
}
static pn_status_t glyph(pn_font_t *font,pn_frame_t *frame,uint32_t cp,uint8_t source,int32_t x_64,int baseline){
    if(source==SRC_PRIMARY)return pn_font_draw(font,frame,cp,x_64,baseline,PN_FONT_GRAY);
    if(source==SRC_FALLBACK)return pn_font_draw(fallback_font,frame,cp,x_64,baseline,PN_FONT_GRAY);
    int x=x_64/64,size=font->pixels;
    pn_frame_rect(frame,x+2,baseline-size+4,size-6,1,0);pn_frame_rect(frame,x+2,baseline-2,size-6,1,0);
    pn_frame_rect(frame,x+2,baseline-size+4,1,size-6,0);pn_frame_rect(frame,x+size-5,baseline-size+4,1,size-6,0);return PN_OK;
}
static bool is_word(uint32_t c){return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9');}
static int32_t total(const int32_t *adv,size_t n){int32_t sum=0;for(size_t i=0;i<n;i++)sum+=adv[i];return sum;}
static pn_status_t text_width_impl(pn_font_t *font,const char *utf8,int *width){
    if(!font || !font->impl || !utf8 || !width)return PN_INVALID;
    uint32_t cp[MAX_CHARS];int32_t adv[MAX_CHARS];uint8_t miss[MAX_CHARS];size_t n=0;
    pn_status_t status=decode(font,utf8,cp,adv,miss,&n);if(status!=PN_OK)return status;
    *width=(total(adv,n)+32)/64;return PN_OK;
}
static pn_status_t text_impl(pn_font_t *font,pn_frame_t *frame,const char *utf8,int x,int baseline,int max_width,pn_align_t align){
    if(!font || !font->impl || !frame || !frame->pixels || !utf8)return PN_INVALID;
    uint32_t cp[MAX_CHARS];int32_t adv[MAX_CHARS];uint8_t miss[MAX_CHARS];size_t n=0;
    pn_status_t status=decode(font,utf8,cp,adv,miss,&n);if(status!=PN_OK)return status;
    int32_t width=total(adv,n),limit=max_width*64;size_t shown=n;bool dots=false;int32_t dot=0;
    if(max_width>0 && width>limit){
        // 留出“...”的位置，逐字回退到放得下为止。/ Reserve room for "..." and back off one character at a time.
        int32_t period;if(pn_font_advance(font,'.',&period)!=PN_OK)period=font->pixels*16;
        dot=period;int32_t used=0;shown=0;
        while(shown<n && used+adv[shown]+3*dot<=limit){used+=adv[shown];shown++;}
        dots=true;width=used+3*dot;
    }
    int32_t start=(int32_t)x*64;
    if(max_width>0 && !dots){int32_t room=limit-width;if(align==PN_ALIGN_CENTER)start+=room/2;else if(align==PN_ALIGN_RIGHT)start+=room;}
    else if(max_width==0){if(align==PN_ALIGN_CENTER)start-=width/2;else if(align==PN_ALIGN_RIGHT)start-=width;}
    for(size_t i=0;i<shown;i++){status=glyph(font,frame,cp[i],miss[i],start,baseline);if(status!=PN_OK)return status;start+=adv[i];}
    for(int i=0;dots && i<3;i++){status=pn_font_draw(font,frame,'.',start,baseline,PN_FONT_GRAY);if(status!=PN_OK)return status;start+=dot;}
    return PN_OK;
}
static pn_status_t text_lines_impl(pn_font_t *font,pn_frame_t *frame,const char *utf8,int x,int baseline,int width,unsigned max_lines,int pitch,int first_indent,int tracking_64,unsigned *used){
    if(!font || !font->impl || !frame || !frame->pixels || !utf8 || width<=0 || !max_lines || pitch<=0)return PN_INVALID;
    uint32_t cp[MAX_CHARS];int32_t adv[MAX_CHARS];uint8_t miss[MAX_CHARS];size_t n=0;
    pn_status_t status=decode(font,utf8,cp,adv,miss,&n);if(status!=PN_OK)return status;
    for(size_t i=0;i<n;i++)adv[i]+=tracking_64;
    int32_t limit=width*64,period;if(pn_font_advance(font,'.',&period)!=PN_OK)period=font->pixels*16;
    size_t at=0;unsigned line=0;
    while(at<n && line<max_lines){
        while(line>0 && at<n && cp[at]==' ')at++; // 续行不以空格开头。/ Continuation lines do not start with a space.
        if(at>=n)break;
        // 本行能放下的字符数；最后一行若还有剩余则预留省略号。/ Characters that fit on this line; the last line reserves room for an ellipsis when text remains.
        int32_t room=limit-(line==0?first_indent*64:0);
        int32_t sum=0;size_t end=at;bool last=line+1==max_lines;
        while(end<n && sum+adv[end]<=room){sum+=adv[end];end++;}
        if(end==at)end=at+1,sum=adv[at]; // 单字比整行还宽也至少画一个。/ Draw at least one character even if it is wider than the line.
        // 拉丁单词不在中间折断：退回到最近的空格之后。/ Do not split a Latin word: back up to just after the nearest space.
        if(end<n && end>at+1 && is_word(cp[end]) && is_word(cp[end-1])){
            size_t back=end;
            while(back>at+1 && cp[back-1]!=' ')back--;
            if(back>at+1)end=back;
        }
        bool cut=last && end<n;
        if(cut){sum=0;end=at;while(end<n && sum+adv[end]+3*period<=room){sum+=adv[end];end++;}}
        int32_t pen=(int32_t)x*64+(line==0?first_indent*64:0);
        for(size_t i=at;i<end;i++){status=glyph(font,frame,cp[i],miss[i],pen,baseline+(int)line*pitch);if(status!=PN_OK)return status;pen+=adv[i];}
        for(int i=0;cut && i<3;i++){status=pn_font_draw(font,frame,'.',pen,baseline+(int)line*pitch,PN_FONT_GRAY);if(status!=PN_OK)return status;pen+=period;}
        at=end;line++;
    }
    if(used)*used=line;
    return PN_OK;
}
/* 备用字体借用时会被改字号；调用方（例如阅读页的正文字体）依赖原字号，所以每次绘制后还原。
 * Borrowing the fallback font changes its size; callers (e.g. the reading page's body font) depend on the original size, so it is restored after every drawing call. */
static int fallback_saved(void){return fallback_font && fallback_font->impl?fallback_font->pixels:0;}
static pn_status_t fallback_restore(int saved,pn_status_t status){
    if(saved>0 && fallback_font && fallback_font->pixels!=saved){pn_status_t restored=pn_font_size(fallback_font,saved);if(status==PN_OK)return restored;}
    return status;
}
pn_status_t pn_w_text_width(pn_font_t *font,const char *utf8,int *width){int saved=fallback_saved();return fallback_restore(saved,text_width_impl(font,utf8,width));}
pn_status_t pn_w_text(pn_font_t *font,pn_frame_t *frame,const char *utf8,int x,int baseline,int max_width,pn_align_t align){int saved=fallback_saved();return fallback_restore(saved,text_impl(font,frame,utf8,x,baseline,max_width,align));}
pn_status_t pn_w_text_lines(pn_font_t *font,pn_frame_t *frame,const char *utf8,int x,int baseline,int width,unsigned max_lines,int pitch,unsigned *used){int saved=fallback_saved();return fallback_restore(saved,text_lines_impl(font,frame,utf8,x,baseline,width,max_lines,pitch,0,0,used));}
pn_status_t pn_w_text_flow(pn_font_t *font,pn_frame_t *frame,const char *utf8,int x,int baseline,int width,unsigned max_lines,int pitch,int first_indent,int tracking_64,unsigned *used){int saved=fallback_saved();return fallback_restore(saved,text_lines_impl(font,frame,utf8,x,baseline,width,max_lines,pitch,first_indent,tracking_64,used));}
void pn_w_outline(pn_frame_t *frame,int x,int y,int width,int height,int thickness,uint8_t shade){
    if(!frame || width<=0 || height<=0 || thickness<=0)return;
    if(thickness*2>width)thickness=width/2;
    if(thickness*2>height)thickness=height/2;
    if(thickness<=0)return;
    pn_frame_rect(frame,x,y,width,thickness,shade);pn_frame_rect(frame,x,y+height-thickness,width,thickness,shade);
    pn_frame_rect(frame,x,y+thickness,thickness,height-2*thickness,shade);pn_frame_rect(frame,x+width-thickness,y+thickness,thickness,height-2*thickness,shade);
}
/* 圆角矩形内部判定：四角按圆弧，其余为真。/ Rounded-rectangle containment: arcs at the corners, solid elsewhere. */
static bool inside(int px,int py,int width,int height,int radius){
    int dx=px<width-1-px?px:width-1-px,dy=py<height-1-py?py:height-1-py;
    if(dx<0 || dy<0)return false;
    if(dx>=radius || dy>=radius)return true;
    int ax=radius-dx,ay=radius-dy;return ax*ax+ay*ay<=radius*radius;
}
void pn_w_round_outline(pn_frame_t *frame,int x,int y,int width,int height,int radius,int thickness,uint8_t shade){
    if(!frame || width<=0 || height<=0 || thickness<=0)return;
    int cap=(width<height?width:height)/2;if(radius>cap)radius=cap;if(radius<0)radius=0;
    int inner_radius=radius>thickness?radius-thickness:0;
    for(int py=0;py<height;py++){
        for(int px=0;px<width;px++){
            if(!inside(px,py,width,height,radius))continue;
            bool core=px>=thickness && py>=thickness && px<width-thickness && py<height-thickness && inside(px-thickness,py-thickness,width-2*thickness,height-2*thickness,inner_radius);
            if(!core)pn_frame_pixel(frame,x+px,y+py,shade);
        }
    }
}
pn_status_t pn_w_button(pn_font_t *font,pn_frame_t *frame,const char *label,int x,int y,int width,int height,unsigned style){
    if(!font || !font->impl || !frame || !label || width<=0 || height<=0)return PN_INVALID;
    if(!(style&PN_W_PLAIN))pn_w_round_outline(frame,x,y,width,height,PN_UI_RADIUS,(style&PN_W_SELECTED)?4:2,PN_UI_INK);
    int baseline=y+height/2+font->pixels*3/8;
    pn_status_t status=pn_w_text(font,frame,label,x+12,baseline,width-24,PN_ALIGN_CENTER);
    if(status==PN_OK && (style&PN_W_DISABLED)){int w;if(pn_w_text_width(font,label,&w)==PN_OK){if(w>width-24)w=width-24;pn_frame_rect(frame,x+(width-w)/2,y+height/2,w,2,PN_UI_INK);}}
    return status;
}
pn_status_t pn_w_tabbar(pn_font_t *font,pn_frame_t *frame,const char *const *labels,unsigned count,unsigned active,unsigned disabled,int y,int height){
    if(!font || !font->impl || !frame || !labels || !count || count>4 || height<=0)return PN_INVALID;
    pn_frame_rect(frame,0,y,PN_UI_WIDTH,2,PN_UI_INK);
    pn_status_t status=PN_OK;
    for(unsigned i=0;i<count && status==PN_OK;i++){
        int x0=(int)(PN_UI_WIDTH*i/count),x1=(int)(PN_UI_WIDTH*(i+1)/count);
        if(i==active)pn_frame_rect(frame,x0+24,y+2,x1-x0-48,6,PN_UI_INK);
        status=pn_w_text(font,frame,labels[i],x0,y+height/2+font->pixels*3/8+4,x1-x0,PN_ALIGN_CENTER);
        if(status==PN_OK && (disabled&(1u<<i))){int w;if(pn_w_text_width(font,labels[i],&w)==PN_OK)pn_frame_rect(frame,x0+(x1-x0-w)/2,y+height/2+4,w,2,PN_UI_INK);}
    }
    return status;
}
int pn_w_tabbar_hit(unsigned count,int y,int height,int x,int hit_y){
    if(!count || count>4 || x<0 || x>=PN_UI_WIDTH || hit_y<y || hit_y>=y+height)return -1;
    return (int)((unsigned)x*count/PN_UI_WIDTH);
}
pn_status_t pn_w_header(pn_font_t *font,pn_frame_t *frame,const char *back,const char *title,const char *action){
    if(!font || !font->impl || !frame || !back || !title)return PN_INVALID;
    int original=font->pixels;
    pn_status_t status=pn_font_size(font,34);
    if(status==PN_OK)status=pn_w_text(font,frame,back,PN_UI_MARGIN,78,200,PN_ALIGN_LEFT);
    if(status==PN_OK)status=pn_font_size(font,40);
    if(status==PN_OK)status=pn_w_text(font,frame,title,0,80,PN_UI_WIDTH,PN_ALIGN_CENTER);
    if(status==PN_OK)status=pn_font_size(font,34);
    if(status==PN_OK && action)status=pn_w_button(font,frame,action,500,24,152,80,PN_W_SELECTED);
    pn_frame_rect(frame,PN_UI_MARGIN,PN_W_HEADER_H-4,PN_UI_WIDTH-2*PN_UI_MARGIN,2,PN_UI_RULE);
    pn_status_t restored=pn_font_size(font,original);return status==PN_OK?restored:status;
}
int pn_w_header_hit(int x,int y,bool has_action){
    if(y<0 || y>=112 || x<0 || x>=PN_UI_WIDTH)return 0;
    if(x<240)return 1;
    return has_action && x>=500 && x<652 && y>=24?2:0;
}
pn_status_t pn_w_section(pn_font_t *font,pn_frame_t *frame,const char *title,int baseline){
    if(!font || !font->impl || !frame || !title)return PN_INVALID;
    int original=font->pixels;pn_status_t status=pn_font_size(font,28);
    if(status==PN_OK)status=pn_w_text(font,frame,title,PN_UI_MARGIN,baseline,PN_UI_WIDTH-2*PN_UI_MARGIN,PN_ALIGN_LEFT);
    pn_frame_rect(frame,PN_UI_MARGIN,baseline+10,PN_UI_WIDTH-2*PN_UI_MARGIN,2,PN_UI_INK);
    pn_status_t restored=pn_font_size(font,original);return status==PN_OK?restored:status;
}
pn_status_t pn_w_row(pn_font_t *font,pn_frame_t *frame,const char *label,const char *value,bool chevron,int y){
    if(!font || !font->impl || !frame || !label)return PN_INVALID;
    int original=font->pixels;pn_status_t status=pn_font_size(font,34);
    int tail=chevron?36:0,value_width=0;
    if(status==PN_OK && value && *value){status=pn_font_size(font,30);if(status==PN_OK)status=pn_w_text_width(font,value,&value_width);if(status==PN_OK)status=pn_font_size(font,34);}
    int label_room=PN_UI_WIDTH-2*PN_UI_MARGIN-tail-(value_width?value_width+24:0);
    if(status==PN_OK)status=pn_w_text(font,frame,label,PN_UI_MARGIN,y+56,label_room,PN_ALIGN_LEFT);
    if(status==PN_OK && value && *value){status=pn_font_size(font,30);if(status==PN_OK)status=pn_w_text(font,frame,value,PN_UI_WIDTH-PN_UI_MARGIN-tail-value_width,y+56,value_width,PN_ALIGN_RIGHT);}
    if(status==PN_OK && chevron){status=pn_font_size(font,34);if(status==PN_OK)status=pn_w_text(font,frame,">",PN_UI_WIDTH-PN_UI_MARGIN-24,y+56,24,PN_ALIGN_RIGHT);}
    pn_frame_rect(frame,PN_UI_MARGIN,y+PN_W_ROW_H-2,PN_UI_WIDTH-2*PN_UI_MARGIN,1,10);
    pn_status_t restored=pn_font_size(font,original);return status==PN_OK?restored:status;
}
void pn_w_icon_search(pn_frame_t *frame,int x,int y,int size){
    if(!frame || size<12)return;
    int lens=size*2/3;
    pn_w_round_outline(frame,x,y,lens,lens,lens/2,3,PN_UI_INK);
    // 手柄：从镜片右下沿45°伸出。/ Handle: leaves the lens at 45 degrees from its lower right.
    int from=lens-lens/6;
    for(int i=0;from+i<size;i++){pn_frame_rect(frame,x+from+i,y+from+i,3,3,PN_UI_INK);}
}
void pn_w_icon_grid(pn_frame_t *frame,int x,int y,int size){
    if(!frame || size<12)return;
    int cell=(size-6)/2;
    for(int r=0;r<2;r++)for(int c=0;c<2;c++)pn_w_outline(frame,x+c*(cell+6),y+r*(cell+6),cell,cell,3,PN_UI_INK);
}
void pn_w_icon_list(pn_frame_t *frame,int x,int y,int size){
    if(!frame || size<12)return;
    int gap=(size-9)/2;
    for(int i=0;i<3;i++)pn_frame_rect(frame,x,y+i*(gap+3),size,3,PN_UI_INK);
}

