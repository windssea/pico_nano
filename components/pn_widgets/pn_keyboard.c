/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：同源屏幕键盘绘制、命中与离线全拼中文输入法，见pn_keyboard.h。
 * English: shared on-screen keyboard drawing, hit testing and offline full-pinyin Chinese IME, see pn_keyboard.h.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_keyboard.h"
#include "pn_pinyin_ime.h"
#include "pn_widgets.h"
#include <string.h>

/* 版式：每行20个半键宽，横跨x=17至667；键间留6px、行间留8px。字母层第三行缩进半键。
 * 中文全拼模式下，第0行（650×88px）作为拼音显示与候选字/中文标点栏，整体高度保持480px不变。
 * Layout: each row spans 20 half-key units from x=17 to 667 with 6 px between keys and 8 px between rows; the third letter row is indented by half a key.
 * In Chinese Pinyin mode, row 0 (650×88 px) hosts the pinyin pill and candidate/punctuation bar, keeping total height 480 px unchanged. */
#define KB_X 17
#define KB_W 650
#define KB_UNITS 20
#define KB_GAP 6
#define KB_KEY_H (PN_KB_ROW_H-8)
#define SPACER 0 ///< 占位不可点 / Non-interactive spacer

/* 第0行中文候选栏几何：左拼音徽章118 + 6×68候选（间距4） + 44上页 + 44下页 = 650px（x=17..667）。
 * Row 0 Chinese candidate bar geometry: 118 px pinyin pill + 6×68 px candidates (4 px gap) + 44 px prev + 44 px next = 650 px (x=17..667). */
#define BAR_PY_X 17
#define BAR_PY_W 118
#define BAR_CAND_X 141
#define BAR_CAND_W 68
#define BAR_CAND_STEP 72
#define BAR_PREV_X 575
#define BAR_PREV_W 44
#define BAR_NEXT_X 623
#define BAR_NEXT_W 44

typedef struct {int code;unsigned char span;} kb_key_t;
#define K(c) {(c),2}
static const char *const cn_punct[PN_KB_PUNCT_COUNT]={"，","。","·","、","？","！","：","「","」","《","》"};
static const kb_key_t digits[]={K('1'),K('2'),K('3'),K('4'),K('5'),K('6'),K('7'),K('8'),K('9'),K('0'),{0,0}};
static const kb_key_t letters1[]={K('q'),K('w'),K('e'),K('r'),K('t'),K('y'),K('u'),K('i'),K('o'),K('p'),{0,0}};
static const kb_key_t letters2[]={{SPACER,1},K('a'),K('s'),K('d'),K('f'),K('g'),K('h'),K('j'),K('k'),K('l'),{0,0}};
static const kb_key_t letters3[]={{PN_KB_SHIFT,3},K('z'),K('x'),K('c'),K('v'),K('b'),K('n'),K('m'),{PN_KB_DELETE,3},{0,0}};
static const kb_key_t cn_letters3[]={{PN_KB_IME_CLEAR,3},K('z'),K('x'),K('c'),K('v'),K('b'),K('n'),K('m'),{PN_KB_DELETE,3},{0,0}};
static const kb_key_t letters4[]={{PN_KB_LAYER,4},K('-'),K('_'),{' ',8},K('.'),K('@'),{0,0}};
static const kb_key_t ime_en_row4[]={{PN_KB_LAYER,3},{PN_KB_IME_TOGGLE,3},K('-'),{' ',8},K('.'),K('_'),{0,0}};
static const kb_key_t ime_cn_row4[]={{PN_KB_LAYER,3},{PN_KB_IME_TOGGLE,3},{PN_KB_PUNCT_BASE-0,2},{' ',8},{PN_KB_PUNCT_BASE-1,2},{PN_KB_PUNCT_BASE-2,2},{0,0}};
static const kb_key_t symbols1[]={K('!'),K('@'),K('#'),K('$'),K('%'),K('^'),K('&'),K('*'),K('('),K(')'),{0,0}};
static const kb_key_t symbols2[]={K('-'),K('_'),K('='),K('+'),K('['),K(']'),K('{'),K('}'),K('\\'),K('|'),{0,0}};
static const kb_key_t symbols3[]={K(';'),K(':'),K('\''),K('"'),K(','),K('.'),K('?'),K('<'),K('>'),K('/'),{0,0}};
static const kb_key_t symbols4[]={{PN_KB_LAYER,4},K('`'),K('~'),{' ',8},{PN_KB_DELETE,4},{0,0}};
static const kb_key_t ime_sym_row4[]={{PN_KB_LAYER,3},{PN_KB_IME_TOGGLE,3},K('`'),K('~'),{' ',6},{PN_KB_DELETE,4},{0,0}};

static bool is_cn_mode(const pn_keyboard_t *kb){return kb && kb->ime_enabled && kb->chinese && !kb->symbols;}
static const kb_key_t *row_keys(const pn_keyboard_t *kb,int r){
    if(kb->symbols){
        if(r==0)return digits;
        if(r==1)return symbols1;
        if(r==2)return symbols2;
        if(r==3)return symbols3;
        return kb->ime_enabled?ime_sym_row4:symbols4;
    }
    if(r==0)return digits;
    if(r==1)return letters1;
    if(r==2)return letters2;
    if(r==3)return is_cn_mode(kb)?cn_letters3:letters3;
    if(!kb->ime_enabled)return letters4;
    return kb->chinese?ime_cn_row4:ime_en_row4;
}

void pn_keyboard_reset(pn_keyboard_t *kb){if(kb)*kb=(pn_keyboard_t){0};}
void pn_keyboard_set_ime(pn_keyboard_t *kb,bool enabled,bool start_chinese){
    if(!kb)return;
    *kb=(pn_keyboard_t){.ime_enabled=enabled,.chinese=enabled && start_chinese};
}
static int unit_x(unsigned unit){return KB_X+(int)(unit*KB_W/KB_UNITS);}
/// 字母层大写时把字母键映射为大写。/ Map letter keys to upper case while the caps latch is on.
static int effective(const pn_keyboard_t *kb,int code){return !kb->symbols && !is_cn_mode(kb) && kb->caps && code>='a' && code<='z'?code-'a'+'A':code;}
static const char *label_of(const pn_keyboard_t *kb,int code,char text[2]){
    if(code==PN_KB_SHIFT)return "大写";
    if(code==PN_KB_DELETE)return "删除";
    if(code==PN_KB_LAYER)return kb->symbols?(kb->ime_enabled && kb->chinese?"拼音":"字母"):"符号";
    if(code==PN_KB_IME_TOGGLE)return (kb->chinese && !kb->symbols)?"中文":"英文";
    if(code==PN_KB_IME_CLEAR)return "清除";
    if(code<=PN_KB_PUNCT_BASE && code>PN_KB_PUNCT_BASE-PN_KB_PUNCT_COUNT)return cn_punct[PN_KB_PUNCT_BASE-code];
    if(code==' ')return (is_cn_mode(kb) && *kb->pinyin)?"选首字":"空格";
    text[0]=(char)effective(kb,code);text[1]=0;return text;
}

/// 绘制全拼第0行：拼写时显示拼音与6字候选翻页，空时显示常用中文标点。
/// Render row 0 in Chinese Pinyin mode: pinyin pill + 6 candidates + paging when composing, or quick CJK punctuation when idle.
static pn_status_t render_cn_bar(const pn_keyboard_t *kb,pn_font_t *font,pn_frame_t *frame,int top){
    pn_status_t s=PN_OK;
    if(*kb->pinyin){
        char cands[PN_KB_CAND_COUNT][5];size_t total=0;
        size_t n=pn_pinyin_ime_lookup(kb->pinyin,(size_t)kb->cand_page*PN_KB_CAND_COUNT,cands,PN_KB_CAND_COUNT,&total);
        // 左侧拼音胶囊（深框强调，点击可提交原始字母）/ Left pinyin pill (dark border, tap commits raw ASCII).
        pn_w_round_fill(frame,BAR_PY_X,top,BAR_PY_W,KB_KEY_H,12,PN_UI_SURFACE);
        pn_w_round_stroke(frame,BAR_PY_X,top,BAR_PY_W,KB_KEY_H,12,2.5f,PN_UI_INK);
        s=pn_font_size(font,strlen(kb->pinyin)>4?22:26);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,kb->pinyin,BAR_PY_X+4,top+KB_KEY_H/2+font->pixels*3/8,BAR_PY_W-8,PN_ALIGN_CENTER,PN_UI_INK,true);
        // 6个候选汉字键（首字深框提示空格直出）/ 6 candidate hanzi buttons (first has darker border for Space commit).
        for(size_t i=0;i<PN_KB_CAND_COUNT && s==PN_OK;i++){
            int cx=BAR_CAND_X+(int)i*BAR_CAND_STEP;
            bool has=i<n;
            pn_w_round_fill(frame,cx,top,BAR_CAND_W,KB_KEY_H,12,has?PN_UI_PAPER:PN_UI_SURFACE);
            pn_w_round_stroke(frame,cx,top,BAR_CAND_W,KB_KEY_H,12,(has && i==0)?2.5f:1.5f,(has && i==0)?PN_UI_INK:PN_UI_STROKE);
            if(has){
                s=pn_font_size(font,38);
                if(s==PN_OK)s=pn_w_text_ex(font,frame,cands[i],cx+2,top+KB_KEY_H/2+font->pixels*3/8,BAR_CAND_W-4,PN_ALIGN_CENTER,PN_UI_INK,false);
            }
        }
        bool has_prev=kb->cand_page>0;
        bool has_next=((size_t)kb->cand_page+1u)*PN_KB_CAND_COUNT<total;
        pn_w_round_fill(frame,BAR_PREV_X,top,BAR_PREV_W,KB_KEY_H,12,PN_UI_SURFACE);
        pn_w_round_stroke(frame,BAR_PREV_X,top,BAR_PREV_W,KB_KEY_H,12,1.5f,has_prev?PN_UI_INK:PN_UI_STROKE);
        pn_w_round_fill(frame,BAR_NEXT_X,top,BAR_NEXT_W,KB_KEY_H,12,PN_UI_SURFACE);
        pn_w_round_stroke(frame,BAR_NEXT_X,top,BAR_NEXT_W,KB_KEY_H,12,1.5f,has_next?PN_UI_INK:PN_UI_STROKE);
        if(s==PN_OK)s=pn_font_size(font,28);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,"<",BAR_PREV_X+2,top+KB_KEY_H/2+font->pixels*3/8,BAR_PREV_W-4,PN_ALIGN_CENTER,has_prev?PN_UI_INK:PN_UI_DISABLED,has_prev);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,">",BAR_NEXT_X+2,top+KB_KEY_H/2+font->pixels*3/8,BAR_NEXT_W-4,PN_ALIGN_CENTER,has_next?PN_UI_INK:PN_UI_DISABLED,has_next);
        return s;
    }
    // 空闲态：左“全拼”标识 + 8个快捷中文标点（、？！：「」《》）/ Idle state: "全拼" badge + 8 quick CJK punctuation keys.
    pn_w_round_fill(frame,BAR_PY_X,top,BAR_PY_W,KB_KEY_H,12,PN_UI_SURFACE);
    pn_w_round_stroke(frame,BAR_PY_X,top,BAR_PY_W,KB_KEY_H,12,1.5f,PN_UI_STROKE);
    s=pn_font_size(font,24);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,"全拼",BAR_PY_X+4,top+KB_KEY_H/2+font->pixels*3/8,BAR_PY_W-8,PN_ALIGN_CENTER,PN_UI_MUTED,false);
    for(int i=0;i<6 && s==PN_OK;i++){
        int cx=BAR_CAND_X+i*BAR_CAND_STEP;
        pn_w_round_fill(frame,cx,top,BAR_CAND_W,KB_KEY_H,12,PN_UI_PAPER);
        pn_w_round_stroke(frame,cx,top,BAR_CAND_W,KB_KEY_H,12,1.5f,PN_UI_STROKE);
        s=pn_font_size(font,34);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,cn_punct[3+i],cx+2,top+KB_KEY_H/2+font->pixels*3/8,BAR_CAND_W-4,PN_ALIGN_CENTER,PN_UI_INK,false);
    }
    pn_w_round_fill(frame,BAR_PREV_X,top,BAR_PREV_W,KB_KEY_H,12,PN_UI_PAPER);
    pn_w_round_stroke(frame,BAR_PREV_X,top,BAR_PREV_W,KB_KEY_H,12,1.5f,PN_UI_STROKE);
    pn_w_round_fill(frame,BAR_NEXT_X,top,BAR_NEXT_W,KB_KEY_H,12,PN_UI_PAPER);
    pn_w_round_stroke(frame,BAR_NEXT_X,top,BAR_NEXT_W,KB_KEY_H,12,1.5f,PN_UI_STROKE);
    if(s==PN_OK)s=pn_font_size(font,30);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,cn_punct[9],BAR_PREV_X+2,top+KB_KEY_H/2+font->pixels*3/8,BAR_PREV_W-4,PN_ALIGN_CENTER,PN_UI_INK,false);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,cn_punct[10],BAR_NEXT_X+2,top+KB_KEY_H/2+font->pixels*3/8,BAR_NEXT_W-4,PN_ALIGN_CENTER,PN_UI_INK,false);
    return s;
}

static int hit_cn_bar(const pn_keyboard_t *kb,int px){
    if(*kb->pinyin){
        if(px>=BAR_PY_X && px<BAR_PY_X+BAR_PY_W)return PN_KB_IME_RAW;
        for(int i=0;i<PN_KB_CAND_COUNT;i++){
            int cx=BAR_CAND_X+i*BAR_CAND_STEP;
            if(px>=cx && px<cx+BAR_CAND_W)return PN_KB_CAND_BASE-i;
        }
        if(px>=BAR_PREV_X && px<BAR_PREV_X+BAR_PREV_W)return PN_KB_IME_PREV;
        if(px>=BAR_NEXT_X && px<BAR_NEXT_X+BAR_NEXT_W)return PN_KB_IME_NEXT;
        return PN_KB_NONE;
    }
    for(int i=0;i<6;i++){
        int cx=BAR_CAND_X+i*BAR_CAND_STEP;
        if(px>=cx && px<cx+BAR_CAND_W)return PN_KB_PUNCT_BASE-(3+i);
    }
    if(px>=BAR_PREV_X && px<BAR_PREV_X+BAR_PREV_W)return PN_KB_PUNCT_BASE-9;
    if(px>=BAR_NEXT_X && px<BAR_NEXT_X+BAR_NEXT_W)return PN_KB_PUNCT_BASE-10;
    return PN_KB_NONE;
}

pn_status_t pn_keyboard_render(const pn_keyboard_t *kb,pn_font_t *font,pn_frame_t *frame,int y){
    if(!kb || !font || !font->impl || !frame || !frame->pixels || y<0 || y+PN_KB_HEIGHT>frame->height)return PN_INVALID;
    int original=font->pixels;pn_status_t s=PN_OK;
    bool cn=is_cn_mode(kb);
    for(int r=0;r<PN_KB_ROWS && s==PN_OK;r++){
        int top=y+r*PN_KB_ROW_H;
        if(r==0 && cn){s=render_cn_bar(kb,font,frame,top);continue;}
        unsigned unit=0;
        for(const kb_key_t *k=row_keys(kb,r);k->span && s==PN_OK;k++){
            int x=unit_x(unit),w=unit_x(unit+k->span)-x-KB_GAP;unit+=k->span;
            if(k->code==SPACER)continue;
            bool is_punct=k->code<=PN_KB_PUNCT_BASE && k->code>PN_KB_PUNCT_BASE-PN_KB_PUNCT_COUNT;
            bool function=(k->code<0 && !is_punct) || k->code==' ';
            bool disabled=k->code==PN_KB_IME_CLEAR && !*kb->pinyin;
            bool latched=(k->code==PN_KB_SHIFT && kb->caps) || (k->code==PN_KB_IME_TOGGLE && cn);
            // 功能键用浅灰底区分；大写锁定与中文激活态反色强调。/ Function keys use a light base; latched caps and active Chinese toggle are inverted.
            pn_w_round_fill(frame,x,top,w,KB_KEY_H,12,function?PN_UI_SURFACE:PN_UI_PAPER);
            pn_w_round_stroke(frame,x,top,w,KB_KEY_H,12,2.0f,PN_UI_STROKE);
            char text[2];const char *label=label_of(kb,k->code,text);
            s=pn_font_size(font,function?26:(is_punct?34:36));
            uint8_t ink=disabled?PN_UI_DISABLED:PN_UI_INK;
            if(s==PN_OK)s=pn_w_text_ex(font,frame,label,x+2,top+KB_KEY_H/2+font->pixels*3/8,w-4,PN_ALIGN_CENTER,ink,latched);
            if(s==PN_OK && latched)pn_w_invert_round(frame,x,top,w,KB_KEY_H,12);
        }
    }
    pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;
}

int pn_keyboard_hit(const pn_keyboard_t *kb,int y,int px,int py){
    if(!kb || py<y || py>=y+PN_KB_HEIGHT || px<KB_X || px>=KB_X+KB_W)return PN_KB_NONE;
    int r=(py-y)/PN_KB_ROW_H;if((py-y)%PN_KB_ROW_H>=KB_KEY_H)return PN_KB_NONE;
    if(r==0 && is_cn_mode(kb))return hit_cn_bar(kb,px);
    unsigned unit=0;
    for(const kb_key_t *k=row_keys(kb,r);k->span;k++){
        int x=unit_x(unit),w=unit_x(unit+k->span)-x-KB_GAP;unit+=k->span;
        if(px>=x && px<x+w)return k->code==SPACER?PN_KB_NONE:effective(kb,k->code);
    }
    return PN_KB_NONE;
}

bool pn_keyboard_toggle(pn_keyboard_t *kb,int code){
    if(!kb)return false;
    if(code==PN_KB_SHIFT && !kb->symbols && !is_cn_mode(kb)){kb->caps=!kb->caps;return true;}
    if(code==PN_KB_LAYER){kb->pinyin[0]=0;kb->cand_page=0;kb->symbols=!kb->symbols;return true;}
    if(code==PN_KB_IME_TOGGLE && kb->ime_enabled){
        kb->pinyin[0]=0;kb->cand_page=0;
        if(kb->symbols){kb->symbols=false;kb->chinese=true;}
        else kb->chinese=!kb->chinese;
        return true;
    }
    return false;
}

bool pn_keyboard_flush(pn_keyboard_t *kb,char *text,size_t cap,size_t max){
    if(!kb || !*kb->pinyin)return false;
    char cand[1][5];
    size_t n=pn_pinyin_ime_lookup(kb->pinyin,0,cand,1,NULL);
    bool wrote=false;
    if(text && cap){
        if(n>0)wrote=pn_text_field_append_utf8(text,cap,max,cand[0]);
        else wrote=pn_text_field_append_utf8(text,cap,max,kb->pinyin);
    }
    kb->pinyin[0]=0;kb->cand_page=0;
    return wrote || true;
}

bool pn_keyboard_apply(pn_keyboard_t *kb,int code,char *text,size_t cap,size_t max){
    if(!kb || code==PN_KB_NONE)return false;
    if(pn_keyboard_toggle(kb,code))return true;
    if(code==PN_KB_IME_CLEAR){
        if(!*kb->pinyin)return false;
        kb->pinyin[0]=0;kb->cand_page=0;return true;
    }
    if(code==PN_KB_IME_RAW){
        if(!*kb->pinyin)return false;
        if(text && cap)(void)pn_text_field_append_utf8(text,cap,max,kb->pinyin);
        kb->pinyin[0]=0;kb->cand_page=0;return true;
    }
    if(code==PN_KB_IME_PREV){
        if(!*kb->pinyin || kb->cand_page==0)return false;
        kb->cand_page--;return true;
    }
    if(code==PN_KB_IME_NEXT){
        if(!*kb->pinyin)return false;
        size_t total=0;(void)pn_pinyin_ime_lookup(kb->pinyin,0,NULL,0,&total);
        if(((size_t)kb->cand_page+1u)*PN_KB_CAND_COUNT>=total)return false;
        kb->cand_page++;return true;
    }
    if(code<=PN_KB_CAND_BASE && code>PN_KB_CAND_BASE-PN_KB_CAND_COUNT){
        if(!*kb->pinyin)return false;
        size_t idx=(size_t)(PN_KB_CAND_BASE-code);
        char cand[1][5];
        size_t n=pn_pinyin_ime_lookup(kb->pinyin,(size_t)kb->cand_page*PN_KB_CAND_COUNT+idx,cand,1,NULL);
        if(!n)return false;
        if(text && cap)(void)pn_text_field_append_utf8(text,cap,max,cand[0]);
        kb->pinyin[0]=0;kb->cand_page=0;return true;
    }
    if(code<=PN_KB_PUNCT_BASE && code>PN_KB_PUNCT_BASE-PN_KB_PUNCT_COUNT){
        int idx=PN_KB_PUNCT_BASE-code;
        bool changed=false;
        if(*kb->pinyin){(void)pn_keyboard_flush(kb,text,cap,max);changed=true;}
        if(text && cap && pn_text_field_append_utf8(text,cap,max,cn_punct[idx]))changed=true;
        return changed;
    }
    if(code==PN_KB_DELETE){
        if(is_cn_mode(kb) && *kb->pinyin){
            size_t n=strlen(kb->pinyin);
            kb->pinyin[n-1u]=0;kb->cand_page=0;return true;
        }
        return text && pn_text_field_delete(text);
    }
    if(code==' ' && is_cn_mode(kb) && *kb->pinyin){
        return pn_keyboard_flush(kb,text,cap,max);
    }
    if(is_cn_mode(kb) && code>='a' && code<='z'){
        size_t len=strlen(kb->pinyin);
        if(len+1u<PN_KB_PINYIN_MAX){
            char next[PN_KB_PINYIN_MAX];
            memcpy(next,kb->pinyin,len);next[len]=(char)code;next[len+1u]=0;
            if(pn_pinyin_ime_has_prefix(next)){
                strcpy(kb->pinyin,next);kb->cand_page=0;return true;
            }
        }
        // 新字母不能延续当前音节：自动提交当前音节首字并开启新音节。
        // Cannot extend current syllable: auto-commit first candidate and start a new syllable.
        bool changed=false;
        if(*kb->pinyin){(void)pn_keyboard_flush(kb,text,cap,max);changed=true;}
        char single[2]={(char)code,0};
        if(pn_pinyin_ime_has_prefix(single)){
            kb->pinyin[0]=(char)code;kb->pinyin[1]=0;kb->cand_page=0;changed=true;
        }
        return changed;
    }
    if(code>=32 && code<=126)return text && pn_text_field_append(text,cap,max,code);
    return false;
}

bool pn_text_field_append(char *text,size_t cap,size_t max,int code){
    if(!text || code<32 || code>126)return false;
    size_t n=strnlen(text,cap);
    if(n>=max || n+1>=cap)return false;
    text[n]=(char)code;text[n+1]=0;return true;
}

bool pn_text_field_append_utf8(char *text,size_t cap,size_t max,const char *utf8){
    if(!text || !utf8 || !*utf8)return false;
    size_t n=strnlen(text,cap),add=strlen(utf8);
    if(n+add>max || n+add+1u>cap)return false;
    memcpy(text+n,utf8,add);text[n+add]=0;return true;
}

bool pn_text_field_delete(char *text){
    if(!text || !*text)return false;
    size_t n=strlen(text);
    // 回退到UTF-8首字节，保留的SSID可能含中文。/ Step back to a UTF-8 lead byte; saved SSIDs may contain CJK text.
    do n--;while(n>0 && ((unsigned char)text[n]&0xc0)==0x80);
    text[n]=0;return true;
}
