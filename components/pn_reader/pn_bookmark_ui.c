/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：书签六行列表、选择操作、UTF-8名称草稿与删除确认。
 * English: six-row bookmarks, item actions, UTF-8 name drafts and deletion confirmation.
 * 冻结：绘图不操作书签存储；字形可借受租约保护的只读字体源。
 * Frozen: painting never operates bookmark storage; glyphs may borrow lease-protected read-only font sources.
 */
#include "pn_bookmark_ui.h"
#include <stdio.h>
#include <string.h>
static bool live(const pn_bookmark_ui_t *u){return u && ((u->reader && u->reader->impl) || (u->epub && u->epub->impl));}
static bool confirmed(const pn_bookmark_ui_t *u){return u->epub?pn_epub_app_last_confirmed(u->epub):pn_reader_app_last_confirmed(u->reader);}
static pn_status_t page_list(pn_bookmark_ui_t *u,uint64_t cursor,pn_txt_bookmark_t *items,size_t *count,bool *more){
    if(!u->epub)return pn_reader_app_bookmark_list(u->reader,cursor,items,PN_BOOKMARK_UI_ROWS,count,more);
    pn_epub_bookmark_t marks[PN_BOOKMARK_UI_ROWS];pn_status_t status=pn_epub_app_bookmark_list(u->epub,cursor,marks,PN_BOOKMARK_UI_ROWS,count,more);
    if(status==PN_OK)for(size_t i=0;i<*count;i++){items[i]=(pn_txt_bookmark_t){.id=marks[i].id};memcpy(items[i].label,marks[i].label,sizeof items[i].label);}
    return status;
}
static const char keys[5][9]={"abcdefgh","ijklmnop","qrstuvwx","yz012345","6789 .-_"};
static void missing(pn_font_t *font,pn_frame_t *part,int cursor,int baseline){
    int px=cursor/64,size=font->pixels-4;
    pn_frame_rect(part,px,baseline-font->pixels,size,1,0);pn_frame_rect(part,px,baseline-4,size,1,0);
    pn_frame_rect(part,px,baseline-font->pixels,1,size,0);pn_frame_rect(part,px+size-1,baseline-font->pixels,1,size,0);
}
static pn_status_t read_string(void *ctx,uint64_t offset,uint8_t *out,size_t capacity,size_t *n){
    const char *s=ctx;size_t length=strlen(s);if(offset>length)return PN_INVALID;
    size_t take=length-(size_t)offset;if(take>capacity)take=capacity;memcpy(out,s+offset,take);*n=take;return PN_OK;
}
static pn_status_t draw_name(pn_font_t *font,pn_font_t *fallback,pn_frame_t *frame,const char *value,int x,int y,int width,int height){
    pn_frame_t part={frame->pixels+(size_t)y*frame->stride+(size_t)x/2,width,height,frame->stride};
    pn_text_source_t source={(void *)value,strlen(value),read_string,NULL};pn_text_reader_t decoder;
    pn_status_t status=pn_text_open(&decoder,&source,PN_TEXT_UTF8);if(status!=PN_OK)return status;
    int cursor=0,baseline=font->pixels+4;pn_text_char_t c;
    while((status=pn_text_next(&decoder,&c))==PN_OK){
        if(c.codepoint==10){cursor=0;baseline+=font->pixels+4;continue;}
        pn_font_t *used=font;int32_t advance;pn_status_t measured=pn_font_advance(used,c.codepoint,&advance);
        if(measured==PN_EMPTY && fallback){used=fallback;measured=pn_font_advance(used,c.codepoint,&advance);
            if(measured!=PN_OK || advance<0 || advance>width*64){used=font;measured=PN_EMPTY;}
        }
        if(measured==PN_EMPTY)advance=font->pixels*64;else if(measured!=PN_OK)return measured;
        if(advance<0 || advance>width*64)return PN_LIMIT;
        if(cursor>width*64-advance){cursor=0;baseline+=font->pixels+4;}
        if(baseline>height)return PN_OK;
        if(measured==PN_EMPTY)missing(font,&part,cursor,baseline);
        else{status=pn_font_draw(used,&part,c.codepoint,cursor,baseline,PN_FONT_GRAY);
            if(status!=PN_OK){if(used==font)return status;missing(font,&part,cursor,baseline);advance=font->pixels*64;}
        }
        cursor+=advance;
    }
    return status==PN_EMPTY?PN_OK:status;
}
static pn_status_t draw_text(pn_font_t *font,pn_frame_t *frame,const char *value,int x,int y,int width,int height){return draw_name(font,NULL,frame,value,x,y,width,height);}
static pn_status_t button(pn_font_t *font,pn_frame_t *frame,const char *label,int x,int y,int w,int h){
    pn_frame_rect(frame,x,y,w,1,5);pn_frame_rect(frame,x,y+h-1,w,1,5);
    pn_frame_rect(frame,x,y,1,h,5);pn_frame_rect(frame,x+w-1,y,1,h,5);
    pn_text_source_t source={(void *)label,strlen(label),read_string,NULL};pn_text_reader_t decoder;pn_status_t status=pn_text_open(&decoder,&source,PN_TEXT_UTF8);if(status!=PN_OK)return status;
    int32_t total=0;pn_text_char_t c;while((status=pn_text_next(&decoder,&c))==PN_OK){int32_t advance;status=pn_font_advance(font,c.codepoint,&advance);if(status!=PN_OK)return status;if(advance<0 || advance>(w-24)*64-total)return PN_LIMIT;total+=advance;}
    if(status!=PN_EMPTY)return status;
    int ascent,descent;status=pn_font_vertical(font,&ascent,&descent);if(status!=PN_OK)return status;
    int dx=(w-(total+63)/64)/2;dx&=~1;int dy=(h+ascent-descent)/2-font->pixels-4;if(dy<4)dy=4;
    return draw_text(font,frame,label,x+dx,y+dy,w-dx,h-dy);
}
static unsigned percent(const pn_txt_progress_t *p){
    if(!p->source_size)return 0;
    if(p->source_offset>=p->source_size)return 100;
    unsigned result=0;uint64_t remainder=0;
    for(unsigned i=0;i<100;i++){if(remainder>=p->source_size-p->source_offset){remainder-=p->source_size-p->source_offset;result++;}else remainder+=p->source_offset;}
    return result;
}
static pn_txt_bookmark_t *selected(pn_bookmark_ui_t *u){return u->selected>=0 && (size_t)u->selected<u->count?&u->items[u->selected]:NULL;}
static pn_status_t paint(void *ctx,pn_font_t *font,pn_font_t *metadata,pn_frame_t *frame){
    pn_bookmark_ui_t *u=ctx;if(u->count>PN_BOOKMARK_UI_ROWS || frame->width!=684 || frame->height!=1216 || frame->stride<342)return PN_INVALID;
    pn_frame_clear(frame,15);pn_status_t status=pn_font_size(font,48);if(status!=PN_OK)return status;
    status=button(font,frame,u->mode==PN_BUI_LIST?"返回阅读":"返回",32,16,220,80);if(status!=PN_OK)return status;
    status=draw_text(font,frame,u->mode==PN_BUI_RENAME?"修改名称":u->mode==PN_BUI_DELETE?"删除书签":"书签",280,36,236,60);if(status!=PN_OK)return status;
    status=button(font,frame,"重试",532,16,120,80);if(status!=PN_OK)return status;pn_frame_rect(frame,32,120,620,1,8);
    status=pn_font_size(font,36);if(status!=PN_OK)return status;
    if(u->mode==PN_BUI_LIST){
        for(size_t i=0;i<u->count;i++){int y=160+(int)i*144;char label[80];const pn_txt_bookmark_t *mark=&u->items[i];
            if(*mark->label)snprintf(label,sizeof label,"%s",mark->label);else snprintf(label,sizeof label,"书签 #%llu",(unsigned long long)mark->id);
            status=draw_name(font,metadata,frame,label,48,y+4,584,96);if(status!=PN_OK)return status;
            if(u->epub)snprintf(label,sizeof label,"原文位置  |  #%llu",(unsigned long long)mark->id);else snprintf(label,sizeof label,"%u%%  |  #%llu",percent(&mark->position),(unsigned long long)mark->id);
            status=pn_font_size(font,28);if(status!=PN_OK)return status;
            status=draw_text(font,frame,label,48,y+96,584,40);if(status!=PN_OK)return status;
            status=pn_font_size(font,36);if(status!=PN_OK)return status;
            if((int)i==u->selected){pn_frame_rect(frame,32,y,2,132,0);pn_frame_rect(frame,650,y,2,132,0);}pn_frame_rect(frame,48,y+136,584,1,10);
        }
        if(!u->count){status=draw_text(font,frame,u->notice?"暂时无法读取":"还没有书签",48,220,584,60);if(status!=PN_OK)return status;}
        const char *labels[]={"上一页","下一页","添加书签"};for(int i=0;i<3;i++){status=button(font,frame,labels[i],32+i*212,1130,196,80);if(status!=PN_OK)return status;}
    }else{
        pn_txt_bookmark_t *mark=selected(u);if(!mark)return PN_INVALID;
        status=draw_name(font,metadata,frame,*mark->label?mark->label:"未命名书签",48,180,584,96);if(status!=PN_OK)return status;
        if(u->mode==PN_BUI_ACTIONS){
            const char *labels[]={"跳转阅读","修改名称","删除书签"};for(int i=0;i<3;i++){status=button(font,frame,labels[i],32,420+i*128,620,96);if(status!=PN_OK)return status;}
        }else if(u->mode==PN_BUI_DELETE){
            status=draw_text(font,frame,"删除后无法撤销\n",48,350,584,60);if(status!=PN_OK)return status;
            status=button(font,frame,"取消",32,620,300,96);if(status!=PN_OK)return status;
            status=button(font,frame,"确认删除",352,620,300,96);if(status!=PN_OK)return status;
        }else if(u->mode==PN_BUI_RENAME){
            pn_frame_rect(frame,32,286,620,1,8);pn_frame_rect(frame,32,388,620,1,8);
            status=draw_name(font,metadata,frame,*u->draft?u->draft:"请输入名称",48,300,584,96);if(status!=PN_OK)return status;
            for(int row=0;row<5;row++)for(int col=0;col<8;col++){char value=keys[row][col],label[8]={0};
                if(u->shift && value>='a' && value<='z')value=(char)(value-'a'+'A');
                if(value==' ')strcpy(label,"空");else label[0]=value;
                status=button(font,frame,label,22+col*80,512+row*96,80,88);if(status!=PN_OK)return status;
            }
            const char *labels[]={u->shift?"小写":"大写","退格","清空"};for(int i=0;i<3;i++){status=button(font,frame,labels[i],22+i*212,1016,208,88);if(status!=PN_OK)return status;}
            status=button(font,frame,"取消",32,1130,300,80);if(status!=PN_OK)return status;
            status=button(font,frame,"保存名称",352,1130,300,80);if(status!=PN_OK)return status;
        }
    }
    if(u->notice)status=draw_text(font,frame,u->notice,48,u->mode==PN_BUI_RENAME?416:1074,584,48);
    return status;
}
static pn_status_t load_page(pn_bookmark_ui_t *u,unsigned page,uint64_t cursor){
    pn_txt_bookmark_t items[PN_BOOKMARK_UI_ROWS];size_t count;bool more;pn_status_t status;
    if(page>=PN_BOOKMARK_UI_PAGES)return PN_LIMIT;
    for(;;){status=page_list(u,cursor,items,&count,&more);
        if(status!=PN_OK || count || !page)break;
        cursor=u->cursors[--page];
    }
    if(status!=PN_OK)return status;
    memcpy(u->items,items,count*sizeof *items);u->count=count;u->more=more;u->page=page;u->cursors[page]=cursor;u->selected=count?0:-1;return PN_OK;
}
static const char *failure(pn_status_t status,int command){
    if(status==PN_UNSUPPORTED)return "尚未启用书签存储";
    if(status==PN_LIMIT)return command==PN_BUI_ADD?"书签已满，请先删除":"名称太长，请缩短";
    if(status==PN_CORRUPT || status==PN_STALE_JOB)return "记录异常，已保留文件";
    return "操作失败，请重试";
}
pn_status_t pn_bookmark_ui_present(pn_bookmark_ui_t *u,pn_reader_present_fn present,void *ctx){
    if(!u || !live(u) || u->mode==PN_BUI_CLOSED)return PN_INVALID;
    pn_refresh_t profile=u->reference_known && u->shown_mode==u->mode && u->updates<12?PN_REFRESH_GL16:PN_REFRESH_GC16;
    u->presented=false;pn_status_t status=u->epub?pn_epub_app_overlay(u->epub,paint,u,present,ctx,profile):pn_reader_app_overlay(u->reader,paint,u,present,ctx,profile);
    u->presented=u->reference_known=status==PN_OK;
    if(status==PN_OK){u->shown_mode=u->mode;if(profile==PN_REFRESH_GC16)u->updates=0;else u->updates++;}
    return status;
}
pn_status_t pn_bookmark_ui_open(pn_bookmark_ui_t *u,pn_reader_app_t *reader,pn_reader_present_fn present,void *ctx){
    if(!u || !reader || !reader->impl || !present)return PN_INVALID;
    memset(u,0,sizeof *u);u->reader=reader;u->mode=PN_BUI_LIST;u->selected=-1;
    pn_status_t status=load_page(u,0,0);if(status!=PN_OK)u->notice=failure(status,0);
    pn_status_t drawn=pn_bookmark_ui_present(u,present,ctx);return drawn!=PN_OK?drawn:status;
}
static pn_status_t append(pn_bookmark_ui_t *u,const char *value){
    if(!value)return PN_INVALID;
    size_t old=strlen(u->draft),add=0;while(add<=PN_BOOKMARK_LABEL_MAX && value[add])add++;
    if(add>PN_BOOKMARK_LABEL_MAX-old)return PN_LIMIT;
    pn_text_source_t source={(void *)value,add,read_string,NULL};pn_text_reader_t decoder;
    pn_status_t status=pn_text_open(&decoder,&source,PN_TEXT_UTF8);if(status!=PN_OK)return status;
    pn_text_char_t c;while((status=pn_text_next(&decoder,&c))==PN_OK)if(c.codepoint<32 || c.codepoint==127)return PN_INVALID;
    if(status!=PN_EMPTY)return status;
    memcpy(u->draft+old,value,add+1);return PN_OK;
}
pn_status_t pn_bookmark_ui_event(pn_bookmark_ui_t *u,int command,const char *value,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(!u || !live(u) || u->mode==PN_BUI_CLOSED || !present)return PN_INVALID;
    if(!u->presented && command!=PN_BUI_RETRY)return PN_BUSY;
    if(command==PN_BUI_RETRY){if(u->presented && u->mode==PN_BUI_LIST){pn_status_t status=load_page(u,u->page,u->cursors[u->page]);u->notice=status==PN_OK?NULL:failure(status,command);}return pn_bookmark_ui_present(u,present,ctx);}
    u->presented=false;u->notice=NULL;pn_status_t status=PN_OK;pn_txt_bookmark_t *mark=selected(u);
    if(command==PN_BUI_BACK || command==PN_BUI_CANCEL){
        if(u->mode==PN_BUI_LIST){status=u->epub?pn_epub_app_step(u->epub,PN_APP_OPEN,now,present,ctx):pn_reader_app_step(u->reader,PN_APP_OPEN,now,present,ctx);
            if(confirmed(u)){u->mode=PN_BUI_CLOSED;return status;}
        }else if(u->mode==PN_BUI_ACTIONS)u->mode=PN_BUI_LIST;
        else u->mode=PN_BUI_ACTIONS;
    }else if(u->mode==PN_BUI_LIST){
        if(command==PN_BUI_ADD){
            uint64_t id=0;char label[48];
            if(u->epub){status=pn_epub_app_bookmark_add(u->epub,"书签",&id);}
            else{pn_txt_progress_t position;status=pn_reader_app_progress(u->reader,&position);
                if(status==PN_OK){snprintf(label,sizeof label,"书签 %u%%",percent(&position));status=pn_reader_app_bookmark_add(u->reader,label,&id);}}
            if(status==PN_OK){status=load_page(u,0,0);
                while(status==PN_OK && u->more && u->count && u->items[u->count-1].id<id)status=load_page(u,u->page+1,u->items[u->count-1].id);
                if(status==PN_OK){for(size_t i=0;i<u->count;i++)if(u->items[i].id==id)u->selected=(int)i;u->notice="已保存书签";}
            }
        }else if(command==PN_BUI_NEXT)status=u->more && u->count?load_page(u,u->page+1,u->items[u->count-1].id):PN_EMPTY;
        else if(command==PN_BUI_PREVIOUS)status=u->page?load_page(u,u->page-1,u->cursors[u->page-1]):PN_EMPTY;
        else if(command>=PN_BUI_ROW && command<PN_BUI_ROW+PN_BOOKMARK_UI_ROWS){int row=command-PN_BUI_ROW;if((size_t)row<u->count){u->selected=row;u->mode=PN_BUI_ACTIONS;}else status=PN_EMPTY;}
        else if(command==PN_BUI_SELECT){if(mark)u->mode=PN_BUI_ACTIONS;else status=PN_EMPTY;}
        else if(command==PN_BUI_UP || command==PN_BUI_DOWN){int row=u->selected+(command==PN_BUI_UP?-1:1);if(row>=0 && (size_t)row<u->count)u->selected=row;else status=PN_EMPTY;}
        else status=PN_INVALID;
    }else if(u->mode==PN_BUI_ACTIONS && mark){
        if(command==PN_BUI_JUMP){status=u->epub?pn_epub_app_bookmark_jump(u->epub,mark->id,now,present,ctx):pn_reader_app_bookmark_jump(u->reader,mark->id,now,present,ctx);if(confirmed(u)){u->mode=PN_BUI_CLOSED;return status;}}
        else if(command==PN_BUI_EDIT){strcpy(u->draft,mark->label);u->shift=false;u->mode=PN_BUI_RENAME;}
        else if(command==PN_BUI_REMOVE)u->mode=PN_BUI_DELETE;
        else status=PN_INVALID;
    }else if(u->mode==PN_BUI_RENAME && mark){
        if(command==PN_BUI_SAVE){status=u->epub?pn_epub_app_bookmark_rename(u->epub,mark->id,u->draft):pn_reader_app_bookmark_rename(u->reader,mark->id,u->draft);if(status==PN_OK){status=load_page(u,u->page,u->cursors[u->page]);if(status==PN_OK)u->mode=PN_BUI_LIST;}}
        else if(command==PN_BUI_TEXT)status=append(u,value);
        else if(command>=PN_BUI_CHARACTER && command<PN_BUI_CHARACTER+128){char letter=(char)(command-PN_BUI_CHARACTER);if(u->shift && letter>='a' && letter<='z')letter=(char)(letter-'a'+'A');char s[2]={letter,0};status=append(u,s);}
        else if(command==PN_BUI_BACKSPACE){size_t n=strlen(u->draft);if(n){do{n--;}while(n && ((unsigned char)u->draft[n]&0xc0)==0x80);u->draft[n]=0;}}
        else if(command==PN_BUI_CLEAR)u->draft[0]=0;
        else if(command==PN_BUI_SHIFT)u->shift=!u->shift;
        else status=PN_INVALID;
    }else if(u->mode==PN_BUI_DELETE && mark && command==PN_BUI_CONFIRM){
        status=u->epub?pn_epub_app_bookmark_delete(u->epub,mark->id):pn_reader_app_bookmark_delete(u->reader,mark->id);if(status==PN_OK){status=load_page(u,u->page,u->cursors[u->page]);if(status==PN_OK)u->mode=PN_BUI_LIST;}
    }else status=PN_INVALID;
    if(status!=PN_OK && status!=PN_EMPTY)u->notice=failure(status,command);
    pn_status_t drawn=pn_bookmark_ui_present(u,present,ctx);return drawn!=PN_OK?drawn:status;
}
int pn_bookmark_ui_hit(const pn_bookmark_ui_t *u,int x,int y){
    if(!u || u->count>PN_BOOKMARK_UI_ROWS || u->mode==PN_BUI_CLOSED || x<0 || x>=684 || y<0 || y>=1216)return -1;
    if(y>=16 && y<96){if(x>=32 && x<252)return PN_BUI_BACK;if(x>=532 && x<652)return PN_BUI_RETRY;}
    if(u->mode==PN_BUI_LIST){
        if(x>=32 && x<652 && y>=160 && y<1024 && (y-160)%144<136){int row=(y-160)/144;if((size_t)row<u->count)return PN_BUI_ROW+row;}
        if(y>=1130 && y<1210){for(int i=0;i<3;i++)if(x>=32+i*212 && x<228+i*212)return i==0?PN_BUI_PREVIOUS:i==1?PN_BUI_NEXT:PN_BUI_ADD;}
    }else if(u->mode==PN_BUI_ACTIONS){if(x>=32 && x<652 && y>=420 && y<772 && (y-420)%128<96){int row=(y-420)/128;return row==0?PN_BUI_JUMP:row==1?PN_BUI_EDIT:PN_BUI_REMOVE;}}
    else if(u->mode==PN_BUI_DELETE){if(y>=620 && y<716){if(x>=32 && x<332)return PN_BUI_CANCEL;if(x>=352 && x<652)return PN_BUI_CONFIRM;}}
    else if(u->mode==PN_BUI_RENAME){
        if(x>=22 && x<662 && y>=512 && y<992 && (y-512)%96<88)return PN_BUI_CHARACTER+(unsigned char)keys[(y-512)/96][(x-22)/80];
        if(y>=1016 && y<1104){for(int i=0;i<3;i++)if(x>=22+i*212 && x<230+i*212)return i==0?PN_BUI_SHIFT:i==1?PN_BUI_BACKSPACE:PN_BUI_CLEAR;}
        if(y>=1130 && y<1210){if(x>=32 && x<332)return PN_BUI_CANCEL;if(x>=352 && x<652)return PN_BUI_SAVE;}
    }
    return -1;
}
void pn_bookmark_ui_cancel(pn_bookmark_ui_t *u){if(u)memset(u,0,sizeof *u);}

pn_status_t pn_bookmark_ui_open_epub(pn_bookmark_ui_t *u,pn_epub_app_t *reader,pn_reader_present_fn present,void *ctx){
    if(!u || !reader || !reader->impl || !present)return PN_INVALID;
    memset(u,0,sizeof *u);u->epub=reader;u->mode=PN_BUI_LIST;u->selected=-1;
    pn_status_t status=load_page(u,0,0);if(status!=PN_OK)u->notice=failure(status,0);
    pn_status_t drawn=pn_bookmark_ui_present(u,present,ctx);return drawn!=PN_OK?drawn:status;
}
