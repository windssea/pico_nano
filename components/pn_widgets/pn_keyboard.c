/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：同源屏幕键盘绘制与命中，见pn_keyboard.h。
 * English: shared on-screen keyboard drawing and hit testing, see pn_keyboard.h.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_keyboard.h"
#include "pn_widgets.h"
#include <string.h>

/* 版式：每行20个半键宽，横跨x=17至667；键间留6px、行间留8px。字母层第三行缩进半键。
 * Layout: each row spans 20 half-key units from x=17 to 667 with 6 px between keys and 8 px between rows; the third letter row is indented by half a key. */
#define KB_X 17
#define KB_W 650
#define KB_UNITS 20
#define KB_GAP 6
#define KB_KEY_H (PN_KB_ROW_H-8)
#define SPACER 0 ///< 占位不可点 / Non-interactive spacer
typedef struct {int code;unsigned char span;} kb_key_t;
#define K(c) {(c),2}
static const kb_key_t digits[]={K('1'),K('2'),K('3'),K('4'),K('5'),K('6'),K('7'),K('8'),K('9'),K('0'),{0,0}};
static const kb_key_t letters1[]={K('q'),K('w'),K('e'),K('r'),K('t'),K('y'),K('u'),K('i'),K('o'),K('p'),{0,0}};
static const kb_key_t letters2[]={{SPACER,1},K('a'),K('s'),K('d'),K('f'),K('g'),K('h'),K('j'),K('k'),K('l'),{0,0}};
static const kb_key_t letters3[]={{PN_KB_SHIFT,3},K('z'),K('x'),K('c'),K('v'),K('b'),K('n'),K('m'),{PN_KB_DELETE,3},{0,0}};
static const kb_key_t letters4[]={{PN_KB_LAYER,4},K('-'),K('_'),{' ',8},K('.'),K('@'),{0,0}};
static const kb_key_t symbols1[]={K('!'),K('@'),K('#'),K('$'),K('%'),K('^'),K('&'),K('*'),K('('),K(')'),{0,0}};
static const kb_key_t symbols2[]={K('-'),K('_'),K('='),K('+'),K('['),K(']'),K('{'),K('}'),K('\\'),K('|'),{0,0}};
static const kb_key_t symbols3[]={K(';'),K(':'),K('\''),K('"'),K(','),K('.'),K('?'),K('<'),K('>'),K('/'),{0,0}};
static const kb_key_t symbols4[]={{PN_KB_LAYER,4},K('`'),K('~'),{' ',8},{PN_KB_DELETE,4},{0,0}};
static const kb_key_t *const letter_rows[PN_KB_ROWS]={digits,letters1,letters2,letters3,letters4};
static const kb_key_t *const symbol_rows[PN_KB_ROWS]={digits,symbols1,symbols2,symbols3,symbols4};

void pn_keyboard_reset(pn_keyboard_t *kb){if(kb)*kb=(pn_keyboard_t){0};}
static int unit_x(unsigned unit){return KB_X+(int)(unit*KB_W/KB_UNITS);}
/// 字母层大写时把字母键映射为大写。/ Map letter keys to upper case while the caps latch is on.
static int effective(const pn_keyboard_t *kb,int code){return !kb->symbols && kb->caps && code>='a' && code<='z'?code-'a'+'A':code;}
static const char *label_of(const pn_keyboard_t *kb,int code,char text[2]){
    if(code==PN_KB_SHIFT)return "大写";
    if(code==PN_KB_DELETE)return "删除";
    if(code==PN_KB_LAYER)return kb->symbols?"字母":"符号";
    if(code==' ')return "空格";
    text[0]=(char)effective(kb,code);text[1]=0;return text;
}
pn_status_t pn_keyboard_render(const pn_keyboard_t *kb,pn_font_t *font,pn_frame_t *frame,int y){
    if(!kb || !font || !font->impl || !frame || !frame->pixels || y<0 || y+PN_KB_HEIGHT>frame->height)return PN_INVALID;
    const kb_key_t *const *rows=kb->symbols?symbol_rows:letter_rows;int original=font->pixels;pn_status_t s=PN_OK;
    for(int r=0;r<PN_KB_ROWS && s==PN_OK;r++){
        unsigned unit=0;int top=y+r*PN_KB_ROW_H;
        for(const kb_key_t *k=rows[r];k->span && s==PN_OK;k++){
            int x=unit_x(unit),w=unit_x(unit+k->span)-x-KB_GAP;unit+=k->span;
            if(k->code==SPACER)continue;
            bool function=k->code<0 || k->code==' ',latched=k->code==PN_KB_SHIFT && kb->caps;
            // 功能键用浅灰底区分；大写锁定时反色。/ Function keys use a light base; the latched caps key is inverted.
            pn_w_round_fill(frame,x,top,w,KB_KEY_H,12,function?PN_UI_SURFACE:PN_UI_PAPER);
            pn_w_round_stroke(frame,x,top,w,KB_KEY_H,12,2.0f,PN_UI_STROKE);
            char text[2];const char *label=label_of(kb,k->code,text);
            s=pn_font_size(font,function?26:36);
            if(s==PN_OK)s=pn_w_text_ex(font,frame,label,x+2,top+KB_KEY_H/2+font->pixels*3/8,w-4,PN_ALIGN_CENTER,PN_UI_INK,latched);
            if(s==PN_OK && latched)pn_w_invert_round(frame,x,top,w,KB_KEY_H,12);
        }
    }
    pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;
}
int pn_keyboard_hit(const pn_keyboard_t *kb,int y,int px,int py){
    if(!kb || py<y || py>=y+PN_KB_HEIGHT || px<KB_X || px>=KB_X+KB_W)return PN_KB_NONE;
    int r=(py-y)/PN_KB_ROW_H;if((py-y)%PN_KB_ROW_H>=KB_KEY_H)return PN_KB_NONE;
    unsigned unit=0;
    for(const kb_key_t *k=(kb->symbols?symbol_rows:letter_rows)[r];k->span;k++){
        int x=unit_x(unit),w=unit_x(unit+k->span)-x-KB_GAP;unit+=k->span;
        if(px>=x && px<x+w)return k->code==SPACER?PN_KB_NONE:effective(kb,k->code);
    }
    return PN_KB_NONE;
}
bool pn_keyboard_toggle(pn_keyboard_t *kb,int code){
    if(!kb)return false;
    if(code==PN_KB_SHIFT && !kb->symbols){kb->caps=!kb->caps;return true;}
    if(code==PN_KB_LAYER){kb->symbols=!kb->symbols;return true;}
    return false;
}
bool pn_text_field_append(char *text,size_t cap,size_t max,int code){
    if(!text || code<32 || code>126)return false;
    size_t n=strnlen(text,cap);
    if(n>=max || n+1>=cap)return false;
    text[n]=(char)code;text[n+1]=0;return true;
}
bool pn_text_field_delete(char *text){
    if(!text || !*text)return false;
    size_t n=strlen(text);
    // 回退到UTF-8首字节，保留的SSID可能含中文。/ Step back to a UTF-8 lead byte; saved SSIDs may contain CJK text.
    do n--;while(n>0 && ((unsigned char)text[n]&0xc0)==0x80);
    text[n]=0;return true;
}
