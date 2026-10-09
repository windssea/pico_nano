/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：书架搜索页绘制与命中，见pn_search_ui.h。
 * English: shelf search page drawing and hit testing, see pn_search_ui.h.
 */
#include "pn_search_ui.h"
#include <string.h>
#include "pn_widgets.h"

/* 版式：顶栏0–127，输入框y=152，键盘6列×6行，操作行在键盘下方。/ Layout: header 0–127, query box at y=152, a 6×6 keyboard and an action row below it. */
#define BOX_Y 152
#define BOX_H 96
#define HINT_BASE 296
#define KEY_X 32
#define KEY_Y 330
#define KEY_W 103
#define KEY_H 92
#define KEY_COLS 6
#define KEY_ROWS 6
#define ACTION_Y 906
#define ACTION_H 96
static const char keys[]="abcdefghijklmnopqrstuvwxyz0123456789";

void pn_search_ui_open(pn_search_ui_t *ui,const char *query){
    if(!ui)return;
    memset(ui,0,sizeof *ui);
    if(query && strlen(query)<=PN_CATALOG_QUERY_MAX)strcpy(ui->query,query);
}
bool pn_search_ui_append(pn_search_ui_t *ui,char key){
    if(!ui || !((key>='a' && key<='z') || (key>='0' && key<='9')))return false;
    size_t n=strlen(ui->query);
    if(n>=PN_CATALOG_QUERY_MAX)return false;
    ui->query[n]=key;ui->query[n+1]=0;return true;
}
bool pn_search_ui_delete(pn_search_ui_t *ui){
    if(!ui)return false;
    size_t n=strlen(ui->query);
    if(!n)return false;
    ui->query[n-1]=0;return true;
}
pn_status_t pn_search_ui_render(const pn_search_ui_t *ui,pn_font_t *font,pn_frame_t *frame){
    if(!ui || !font || !font->impl || !frame || !frame->pixels || frame->width!=684 || frame->height!=1216 || frame->stride<342)return PN_INVALID;
    int original=font->pixels;pn_frame_clear(frame,PN_UI_PAPER);
    pn_status_t s=pn_w_header(font,frame,"< 返回","搜索",NULL);
    // 输入框：显示搜索词，空时给出提示。/ Query box showing the query, or a hint when empty.
    if(s==PN_OK){pn_w_round_outline(frame,32,BOX_Y,620,BOX_H,PN_UI_RADIUS,2,PN_UI_INK);pn_w_icon_search(frame,52,BOX_Y+28,40);s=pn_font_size(font,40);}
    if(s==PN_OK)s=pn_w_text(font,frame,*ui->query?ui->query:"书名拼音首字母或英文",112,BOX_Y+64,520,PN_ALIGN_LEFT);
    if(s==PN_OK)s=pn_font_size(font,26);
    if(s==PN_OK)s=pn_w_text(font,frame,"中文书名输入各字拼音首字母，如“bnzd”",32,HINT_BASE,620,PN_ALIGN_LEFT);
    // 键盘。/ Keyboard.
    if(s==PN_OK)s=pn_font_size(font,36);
    for(int i=0;i<(int)sizeof keys-1 && s==PN_OK;i++){
        char label[2]={keys[i],0};
        s=pn_w_button(font,frame,label,KEY_X+(i%KEY_COLS)*KEY_W,KEY_Y+(i/KEY_COLS)*KEY_H,KEY_W-8,KEY_H-8,0);
    }
    // 操作行：删除、清空、搜索。/ Action row: delete, clear and search.
    if(s==PN_OK)s=pn_font_size(font,32);
    if(s==PN_OK)s=pn_w_button(font,frame,"删除",32,ACTION_Y,196,ACTION_H,0);
    if(s==PN_OK)s=pn_w_button(font,frame,"清空",240,ACTION_Y,196,ACTION_H,*ui->query?0:PN_W_DISABLED);
    if(s==PN_OK)s=pn_w_button(font,frame,"搜索",448,ACTION_Y,204,ACTION_H,PN_W_SELECTED);
    pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;
}
int pn_search_ui_hit(int x,int y){
    if(x<0 || x>=684 || y<0 || y>=1216)return PN_SEARCH_NONE;
    if(y<PN_W_HEADER_H)return pn_w_header_hit(x,y,false)==1?PN_SEARCH_BACK:PN_SEARCH_NONE;
    if(y>=KEY_Y && y<KEY_Y+KEY_ROWS*KEY_H && x>=KEY_X){
        int column=(x-KEY_X)/KEY_W,row=(y-KEY_Y)/KEY_H;
        if(column>=KEY_COLS || (x-KEY_X)%KEY_W>=KEY_W-8 || (y-KEY_Y)%KEY_H>=KEY_H-8)return PN_SEARCH_NONE;
        return keys[row*KEY_COLS+column];
    }
    if(y>=ACTION_Y && y<ACTION_Y+ACTION_H){
        if(x>=32 && x<228)return PN_SEARCH_DELETE;
        if(x>=240 && x<436)return PN_SEARCH_CLEAR;
        if(x>=448 && x<652)return PN_SEARCH_DONE;
    }
    return PN_SEARCH_NONE;
}
