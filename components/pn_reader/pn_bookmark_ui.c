/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：书签六行列表、选择操作、UTF-8名称草稿与删除确认。
 * English: six-row bookmarks, item actions, UTF-8 name drafts and deletion confirmation.
 * 冻结：绘图不操作书签存储；字形可借受租约保护的只读字体源。
 * Frozen: painting never operates bookmark storage; glyphs may borrow lease-protected read-only font sources.
 */
#include "pn_bookmark_ui.h"
#include "pn_widgets.h"
#include <stdio.h>
#include <string.h>
static bool live(const pn_bookmark_ui_t *u){return u && ((u->reader && u->reader->impl) || (u->epub && u->epub->impl));}
static bool confirmed(const pn_bookmark_ui_t *u){return u->epub?pn_epub_app_last_confirmed(u->epub):pn_reader_app_last_confirmed(u->reader);}
static pn_status_t page_list(pn_bookmark_ui_t *u,uint64_t cursor,pn_txt_bookmark_t *items,int *sections,size_t *count,bool *more){
    if(!u->epub)return pn_reader_app_bookmark_list(u->reader,cursor,items,PN_BOOKMARK_UI_ROWS,count,more);
    pn_epub_bookmark_t marks[PN_BOOKMARK_UI_ROWS];pn_status_t status=pn_epub_app_bookmark_list(u->epub,cursor,marks,PN_BOOKMARK_UI_ROWS,count,more);
    if(status==PN_OK)for(size_t i=0;i<*count;i++){items[i]=(pn_txt_bookmark_t){.id=marks[i].id};memcpy(items[i].label,marks[i].label,sizeof items[i].label);
        size_t index=0,total=0;sections[i]=pn_epub_app_section_of(u->epub,marks[i].position.location.path,&index,&total)==PN_OK?(int)index+1:0;if(total)u->section_count=(int)total;}
    return status;
}
static const char keys[5][9]={"abcdefgh","ijklmnop","qrstuvwx","yz012345","6789 .-_"};
static pn_status_t read_string(void *ctx,uint64_t offset,uint8_t *out,size_t capacity,size_t *n){
    const char *s=ctx;size_t length=strlen(s);if(offset>length)return PN_INVALID;
    size_t take=length-(size_t)offset;if(take>capacity)take=capacity;memcpy(out,s+offset,take);*n=take;return PN_OK;
}
static unsigned percent(const pn_txt_progress_t *p){
    if(!p->source_size)return 0;
    if(p->source_offset>=p->source_size)return 100;
    unsigned result=0;uint64_t remainder=0;
    for(unsigned i=0;i<100;i++){if(remainder>=p->source_size-p->source_offset){remainder-=p->source_size-p->source_offset;result++;}else remainder+=p->source_offset;}
    return result;
}
/* 当前位置是否已有书签（按位置比较，逐页遍历全部书签）；found输出其id。/ Whether the current position already has a bookmark (compared by position over every page); found receives its id. */
static bool same_location(const pn_epub_location_t *a,const pn_epub_location_t *b){
    return !strcmp(a->path,b->path) && a->chapter_start==b->chapter_start && a->position.element==b->position.element && a->position.run==b->position.run && a->position.offset==b->position.offset && a->position.kind==b->position.kind;
}
static bool existing_mark(pn_bookmark_ui_t *u,uint64_t *found){
    uint64_t cursor=0;bool more=true;
    if(u->epub){
        pn_epub_progress_t here;if(pn_epub_app_progress(u->epub,&here)!=PN_OK)return false;
        for(int pages=0;more && pages<64;pages++){
            pn_epub_bookmark_t marks[PN_BOOKMARK_UI_ROWS];size_t count=0;
            if(pn_epub_app_bookmark_list(u->epub,cursor,marks,PN_BOOKMARK_UI_ROWS,&count,&more)!=PN_OK || !count)return false;
            for(size_t i=0;i<count;i++)if(same_location(&marks[i].position.location,&here.location)){*found=marks[i].id;return true;}
            cursor=marks[count-1].id;
        }
        return false;
    }
    pn_txt_progress_t here;if(pn_reader_app_progress(u->reader,&here)!=PN_OK)return false;
    for(int pages=0;more && pages<64;pages++){
        pn_txt_bookmark_t marks[PN_BOOKMARK_UI_ROWS];size_t count=0;
        if(pn_reader_app_bookmark_list(u->reader,cursor,marks,PN_BOOKMARK_UI_ROWS,&count,&more)!=PN_OK || !count)return false;
        for(size_t i=0;i<count;i++)if(marks[i].position.source_offset==here.source_offset){*found=marks[i].id;return true;}
        cursor=marks[count-1].id;
    }
    return false;
}
static pn_txt_bookmark_t *selected(pn_bookmark_ui_t *u){return u->selected>=0 && (size_t)u->selected<u->count?&u->items[u->selected]:NULL;}
/* 版式：顶栏；列表=“添加当前位置”按钮、六行书签、翻页；单条操作、删除确认、改名键盘各自按钮。
 * Layout: header; list = "add current position" button, six bookmark rows and paging; item actions, delete confirmation and the rename keyboard have their own buttons. */
#define ADD_Y 148
#define ROW_Y0 252
#define ROW_PITCH 104
#define PAGER_Y 1084
#define ACTION_Y0 420
#define ACTION_PITCH 128
#define DELETE_BUTTON_Y 620
#define KEY_X0 22
#define KEY_Y0 512
#define SHIFT_Y 1016
#define RENAME_BUTTON_Y 1130
static pn_status_t sized_text(pn_font_t *font,pn_frame_t *frame,const char *value,int size,int x,int baseline,int width,pn_align_t align){
    int original=font->pixels;pn_status_t s=pn_font_size(font,size);
    if(s==PN_OK)s=pn_w_text(font,frame,value,x,baseline,width,align);
    pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;
}
static pn_status_t sized_button(pn_font_t *font,pn_frame_t *frame,const char *label,int size,int x,int y,int w,int h,unsigned style){
    int original=font->pixels;pn_status_t s=pn_font_size(font,size);
    if(s==PN_OK)s=pn_w_button(font,frame,label,x,y,w,h,style);
    pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;
}
/* 成功提示不是错误：只有失败提示才给“重试”入口。/ A success notice is not an error: only failure notices offer Retry. */
static const char saved_notice[]="已保存书签";
static const char duplicate_notice[]="当前位置已添加";
static bool failed(const pn_bookmark_ui_t *u){return u->notice && u->notice!=saved_notice && u->notice!=duplicate_notice;}
static pn_status_t paint(void *ctx,pn_font_t *font,pn_font_t *metadata,pn_frame_t *frame){
    pn_bookmark_ui_t *u=ctx;if(u->count>PN_BOOKMARK_UI_ROWS || frame->width!=684 || frame->height!=1216 || frame->stride<342)return PN_INVALID;
    pn_frame_clear(frame,15);
    pn_status_t status=pn_w_header(font,frame,u->mode==PN_BUI_LIST?"< 返回阅读":"< 返回",u->mode==PN_BUI_RENAME?"修改名称":u->mode==PN_BUI_DELETE?"删除书签":"书签",failed(u)?"重试":NULL);
    pn_w_set_fallback(metadata);
    if(status==PN_OK && u->mode==PN_BUI_LIST){
        // 主按钮“添加当前页为书签”，书签是带图标的圆角卡片行。/ A primary "bookmark this page" button; bookmarks are rounded card rows with an icon.
        status=sized_button(font,frame,"+ 添加当前页为书签",32,32,ADD_Y,620,80,PN_W_SELECTED);
        for(size_t i=0;i<u->count && status==PN_OK;i++){int y=ROW_Y0+(int)i*ROW_PITCH;char label[96];const pn_txt_bookmark_t *mark=&u->items[i];bool on=(int)i==u->selected;
            pn_w_round_fill(frame,32,y,620,ROW_PITCH-12,PN_UI_RADIUS,14);pn_w_round_stroke(frame,32,y,620,ROW_PITCH-12,PN_UI_RADIUS,on?3.0f:2.0f,on?PN_UI_INK:10);
            pn_w_icon(frame,PN_ICON_BOOKMARK,48,y+24,32,PN_UI_INK);pn_w_icon(frame,PN_ICON_MORE,604,y+30,28,PN_UI_INK);
            if(*mark->label)snprintf(label,sizeof label,"%s",mark->label);else snprintf(label,sizeof label,"书签 #%llu",(unsigned long long)mark->id);
            status=sized_text(font,frame,label,32,96,y+42,496,PN_ALIGN_LEFT);
            if(status==PN_OK){
                if(u->epub && u->sections[i] && u->section_count)snprintf(label,sizeof label,"第 %d/%d 节",u->sections[i],u->section_count);
                else if(u->epub)snprintf(label,sizeof label,"书签 #%llu",(unsigned long long)mark->id);
                else snprintf(label,sizeof label,"已读 %u%%",percent(&mark->position));
                status=sized_text(font,frame,label,24,96,y+76,496,PN_ALIGN_LEFT);}
        }
        if(status==PN_OK && !u->count){
            pn_w_icon(frame,PN_ICON_BOOKMARK,(684-72)/2,ROW_Y0+80,72,PN_UI_INK);
            status=sized_text(font,frame,failed(u)?"暂时无法读取书签":"还没有书签",34,PN_UI_MARGIN,ROW_Y0+210,620,PN_ALIGN_CENTER);
            if(status==PN_OK)status=sized_text(font,frame,failed(u)?"点右上“重试”再读一次。":"读到喜欢的地方，随时留个记号。",28,PN_UI_MARGIN,ROW_Y0+260,620,PN_ALIGN_CENTER);
        }
        if(status==PN_OK)status=sized_button(font,frame,"上一页",30,32,PAGER_Y,196,80,0u);
        if(status==PN_OK)status=sized_button(font,frame,"下一页",30,456,PAGER_Y,196,80,0u);
    }else if(status==PN_OK){
        pn_txt_bookmark_t *mark=selected(u);if(!mark){pn_w_set_fallback(NULL);return PN_INVALID;}
        int original=font->pixels;status=pn_font_size(font,36);
        if(status==PN_OK)status=pn_w_text_lines(font,frame,*mark->label?mark->label:"未命名书签",PN_UI_MARGIN,ROW_Y0-40,620,2,48,NULL);
        pn_status_t restored=pn_font_size(font,original);if(status==PN_OK)status=restored;
        if(status==PN_OK && u->mode==PN_BUI_ACTIONS){
            const char *labels[]={"跳转阅读","修改名称","删除书签"};
            for(int i=0;i<3 && status==PN_OK;i++)status=sized_button(font,frame,labels[i],34,32,ACTION_Y0+i*ACTION_PITCH,620,96,0u);
        }else if(status==PN_OK && u->mode==PN_BUI_DELETE){
            status=sized_text(font,frame,"删除这条书签？此操作无法撤销。",32,PN_UI_MARGIN,ROW_Y0+120,620,PN_ALIGN_LEFT);
            if(status==PN_OK)status=sized_button(font,frame,"取消",34,32,DELETE_BUTTON_Y,300,96,0u);
            if(status==PN_OK)status=sized_button(font,frame,"删除",34,352,DELETE_BUTTON_Y,300,96,0u); // 危险操作不用黑底强调 / Destructive actions are not emphasised in black
        }else if(status==PN_OK && u->mode==PN_BUI_RENAME){
            pn_w_round_outline(frame,32,296,620,100,PN_UI_RADIUS,2,PN_UI_INK);
            status=sized_text(font,frame,*u->draft?u->draft:"请输入名称",34,PN_UI_MARGIN+16,360,588,PN_ALIGN_LEFT);
            for(int row=0;row<5 && status==PN_OK;row++)for(int col=0;col<8 && status==PN_OK;col++){char value=keys[row][col],label[8]={0};
                if(u->shift && value>='a' && value<='z')value=(char)(value-'a'+'A');
                if(value==' ')strcpy(label,"空");else label[0]=value;
                status=sized_button(font,frame,label,34,KEY_X0+col*80,KEY_Y0+row*96,76,88,0u);
            }
            const char *labels[]={u->shift?"小写":"大写","退格","清空"};
            for(int i=0;i<3 && status==PN_OK;i++)status=sized_button(font,frame,labels[i],30,KEY_X0+i*212,SHIFT_Y,200,88,0u);
            if(status==PN_OK)status=sized_button(font,frame,"取消",30,32,RENAME_BUTTON_Y,300,80,0u);
            if(status==PN_OK)status=sized_button(font,frame,"保存名称",30,352,RENAME_BUTTON_Y,300,80,PN_W_SELECTED);
        }
    }
    pn_w_set_fallback(NULL);
    if(status==PN_OK && u->notice)status=sized_text(font,frame,u->notice,28,PN_UI_MARGIN,u->mode==PN_BUI_RENAME?460:PAGER_Y-24,620,PN_ALIGN_LEFT);
    return status;
}
static pn_status_t load_page(pn_bookmark_ui_t *u,unsigned page,uint64_t cursor){
    pn_txt_bookmark_t items[PN_BOOKMARK_UI_ROWS];int sections[PN_BOOKMARK_UI_ROWS]={0};size_t count;bool more;pn_status_t status;
    if(page>=PN_BOOKMARK_UI_PAGES)return PN_LIMIT;
    for(;;){status=page_list(u,cursor,items,sections,&count,&more);
        if(status!=PN_OK || count || !page)break;
        cursor=u->cursors[--page];
    }
    if(status!=PN_OK)return status;
    memcpy(u->items,items,count*sizeof *items);memcpy(u->sections,sections,sizeof u->sections);u->count=count;u->more=more;u->page=page;u->cursors[page]=cursor;u->selected=count?0:-1;return PN_OK;
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
            uint64_t id=0;char label[48];bool exists=existing_mark(u,&id);
            // 同一位置不重复添加，选中已有的那条并提示（规范R04）。/ Never add the same position twice; select the existing one and say so (spec R04).
            if(exists){status=load_page(u,0,0);
                while(status==PN_OK && u->more && u->count && u->items[u->count-1].id<id)status=load_page(u,u->page+1,u->items[u->count-1].id);
                if(status==PN_OK){for(size_t i=0;i<u->count;i++)if(u->items[i].id==id)u->selected=(int)i;u->notice=duplicate_notice;}}
            else if(u->epub){status=pn_epub_app_bookmark_add(u->epub,"书签",&id);}
            else{pn_txt_progress_t position;status=pn_reader_app_progress(u->reader,&position);
                if(status==PN_OK){snprintf(label,sizeof label,"书签 %u%%",percent(&position));status=pn_reader_app_bookmark_add(u->reader,label,&id);}}
            if(!exists && status==PN_OK){status=load_page(u,0,0);
                while(status==PN_OK && u->more && u->count && u->items[u->count-1].id<id)status=load_page(u,u->page+1,u->items[u->count-1].id);
                if(status==PN_OK){for(size_t i=0;i<u->count;i++)if(u->items[i].id==id)u->selected=(int)i;u->notice=saved_notice;}
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
    int header=pn_w_header_hit(x,y,failed(u));
    if(header==1)return PN_BUI_BACK;
    if(header==2)return PN_BUI_RETRY;
    if(u->mode==PN_BUI_LIST){
        if(x>=32 && x<652 && y>=ADD_Y && y<ADD_Y+88)return PN_BUI_ADD;
        if(x>=32 && x<652 && y>=ROW_Y0 && y<ROW_Y0+PN_BOOKMARK_UI_ROWS*ROW_PITCH && (y-ROW_Y0)%ROW_PITCH<ROW_PITCH-8){int row=(y-ROW_Y0)/ROW_PITCH;if((size_t)row<u->count)return PN_BUI_ROW+row;}
        if(y>=PAGER_Y && y<PAGER_Y+80){if(x>=32 && x<228)return PN_BUI_PREVIOUS;if(x>=456 && x<652)return PN_BUI_NEXT;}
    }else if(u->mode==PN_BUI_ACTIONS){if(x>=32 && x<652 && y>=ACTION_Y0 && y<ACTION_Y0+3*ACTION_PITCH && (y-ACTION_Y0)%ACTION_PITCH<96){int row=(y-ACTION_Y0)/ACTION_PITCH;return row==0?PN_BUI_JUMP:row==1?PN_BUI_EDIT:PN_BUI_REMOVE;}}
    else if(u->mode==PN_BUI_DELETE){if(y>=DELETE_BUTTON_Y && y<DELETE_BUTTON_Y+96){if(x>=32 && x<332)return PN_BUI_CANCEL;if(x>=352 && x<652)return PN_BUI_CONFIRM;}}
    else if(u->mode==PN_BUI_RENAME){
        if(x>=KEY_X0 && x<KEY_X0+640 && y>=KEY_Y0 && y<KEY_Y0+5*96 && (y-KEY_Y0)%96<88 && (x-KEY_X0)%80<76)return PN_BUI_CHARACTER+(unsigned char)keys[(y-KEY_Y0)/96][(x-KEY_X0)/80];
        if(y>=SHIFT_Y && y<SHIFT_Y+88){for(int i=0;i<3;i++)if(x>=KEY_X0+i*212 && x<KEY_X0+i*212+200)return i==0?PN_BUI_SHIFT:i==1?PN_BUI_BACKSPACE:PN_BUI_CLEAR;}
        if(y>=RENAME_BUTTON_Y && y<RENAME_BUTTON_Y+80){if(x>=32 && x<332)return PN_BUI_CANCEL;if(x>=352 && x<652)return PN_BUI_SAVE;}
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
