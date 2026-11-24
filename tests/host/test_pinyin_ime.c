/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：离线全拼中文输入法、键盘绘制/命中与文本框编辑回归测试。
 * English: Regression tests for offline full-pinyin Chinese IME, keyboard rendering/hit-testing, and UTF-8 field editing.
 */
#include "pn_alloc.h"
#include "pn_file_manager.h"
#include "pn_font.h"
#include "pn_frame.h"
#include "pn_keyboard.h"
#include "pn_pinyin_ime.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void type_str(pn_keyboard_t *kb,const char *s,char *out,size_t cap,size_t max){
    for(;*s;s++)(void)pn_keyboard_apply(kb,(unsigned char)*s,out,cap,max);
}

int main(void){
    /* 1. 查表：精确全拼、前缀补全、多音字与 lv/nv/lue 别名。/ Lookup: exact syllables, prefix fallback, polyphones and lv/nv/lue aliases. */
    char out[8][5];size_t total=0;
    assert(pn_pinyin_ime_has_prefix("zh") && pn_pinyin_ime_has_prefix("zhuang") && !pn_pinyin_ime_has_prefix("v"));
    assert(pn_pinyin_ime_lookup("ni",0,out,6,&total)>=1 && strcmp(out[0],"你")==0);
    assert(pn_pinyin_ime_lookup("hao",0,out,6,&total)>=1 && strcmp(out[0],"好")==0);
    assert(pn_pinyin_ime_lookup("shu",0,out,6,&total)>=1 && strcmp(out[0],"书")==0);
    assert(pn_pinyin_ime_lookup("du",0,out,6,&total)>=1 && strcmp(out[0],"读")==0);
    assert(pn_pinyin_ime_lookup("de",0,out,6,&total)>=3 && strcmp(out[0],"的")==0 && strcmp(out[1],"得")==0 && strcmp(out[2],"地")==0);
    assert(pn_pinyin_ime_lookup("lv",0,out,6,&total)>=1 && strcmp(out[0],"绿")==0);
    assert(pn_pinyin_ime_lookup("nv",0,out,6,&total)>=1 && strcmp(out[0],"女")==0);
    assert(pn_pinyin_ime_lookup("lue",0,out,6,&total)>=1 && strcmp(out[0],"略")==0);
    assert(pn_pinyin_ime_lookup("lve",0,out,6,&total)>=1 && strcmp(out[0],"略")==0);
    // 前缀匹配（如 "zh"、"zho"）非空且返回有效UTF-8汉字。/ Prefix fallback returns valid UTF-8 hanzi.
    assert(pn_pinyin_ime_lookup("zh",0,out,6,&total)==6 && total>=20);
    assert(pn_pinyin_ime_lookup("zho",0,out,6,&total)==6 && strcmp(out[0],"中")==0);

    /* 2. 键盘连续全拼输入、自动切分音节、翻页选字、退格与中英切换。/ Continuous pinyin typing, syllable auto-commit, paging, delete, and CN/EN toggle. */
    pn_keyboard_t kb;pn_keyboard_set_ime(&kb,true,true);
    char field[64]="";
    // 连续输入 "nihao " -> 自动提交 "你" 再提交 "好" / Continuous "nihao " auto-commits "你" then "好".
    type_str(&kb,"nihao ",field,sizeof field,sizeof field-1);
    assert(strcmp(field,"你好")==0 && !*kb.pinyin);
    // 输入中文标点 "，" + 拼音 "man" 选第2候选 "慢" + "du" + flush -> "你好，慢读"
    assert(pn_keyboard_apply(&kb,PN_KB_PUNCT_BASE-0,field,sizeof field,sizeof field-1));
    type_str(&kb,"man",field,sizeof field,sizeof field-1);
    assert(strcmp(kb.pinyin,"man")==0);
    assert(pn_keyboard_apply(&kb,PN_KB_CAND_BASE-1,field,sizeof field,sizeof field-1));
    type_str(&kb,"du",field,sizeof field,sizeof field-1);
    assert(pn_keyboard_flush(&kb,field,sizeof field,sizeof field-1));
    assert(strcmp(field,"你好，慢读")==0 && !*kb.pinyin);

    // 候选翻页与退格：先删拼音字母，空后再删UTF-8汉字。/ Candidate paging and backspace: deletes pinyin first, then UTF-8 hanzi.
    type_str(&kb,"shi",field,sizeof field,sizeof field-1);
    assert(pn_keyboard_apply(&kb,PN_KB_IME_NEXT,field,sizeof field,sizeof field-1) && kb.cand_page==1);
    assert(pn_keyboard_apply(&kb,PN_KB_IME_PREV,field,sizeof field,sizeof field-1) && kb.cand_page==0);
    assert(!pn_keyboard_apply(&kb,PN_KB_IME_PREV,field,sizeof field,sizeof field-1));
    assert(pn_keyboard_apply(&kb,PN_KB_DELETE,field,sizeof field,sizeof field-1) && strcmp(kb.pinyin,"sh")==0);
    assert(pn_keyboard_apply(&kb,PN_KB_IME_CLEAR,field,sizeof field,sizeof field-1) && !*kb.pinyin);
    assert(pn_keyboard_apply(&kb,PN_KB_DELETE,field,sizeof field,sizeof field-1) && strcmp(field,"你好，慢")==0);

    // 提交拼音原文 + 切英文模式 + 切符号层。/ Raw pinyin commit + English mode + Symbol layer.
    type_str(&kb,"abc",field,sizeof field,sizeof field-1); // a->auto-commit? wait 'ab' isn't valid -> 'a' commits '啊', 'b' starts 'b'
    kb.pinyin[0]=0;field[0]=0;
    type_str(&kb,"hu",field,sizeof field,sizeof field-1);
    assert(pn_keyboard_apply(&kb,PN_KB_IME_RAW,field,sizeof field,sizeof field-1) && strcmp(field,"hu")==0 && !*kb.pinyin);
    assert(pn_keyboard_apply(&kb,PN_KB_IME_TOGGLE,field,sizeof field,sizeof field-1) && !kb.chinese);
    assert(pn_keyboard_apply(&kb,PN_KB_SHIFT,field,sizeof field,sizeof field-1) && kb.caps);
    assert(pn_keyboard_apply(&kb,'P',field,sizeof field,sizeof field-1) && strcmp(field,"huP")==0);
    assert(pn_keyboard_apply(&kb,PN_KB_LAYER,field,sizeof field,sizeof field-1) && kb.symbols);
    assert(pn_keyboard_apply(&kb,PN_KB_IME_TOGGLE,field,sizeof field,sizeof field-1) && !kb.symbols && kb.chinese);

    /* 3. 命中与绘制不越界（空闲中文、拼写中文、英文、符号四种状态）。/ Hit-test and render stay strictly inside [y, y+PN_KB_HEIGHT). */
    pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));
    uint8_t *pixels=calloc(342u*1216u,1);assert(pixels);
    pn_frame_t frame;assert(pn_frame_bind(&frame,pixels,342u*1216u,684,1216));
    pn_text_source_t builtin=pn_font_builtin_source();pn_font_t font={0};assert(pn_font_open(&font,&pool,&builtin,28)==PN_OK);
    const int kby=712;
    pn_keyboard_set_ime(&kb,true,true);
    for(int mode=0;mode<4;mode++){
        if(mode==1)strcpy(kb.pinyin,"shu");
        else if(mode==2){kb.pinyin[0]=0;kb.chinese=false;}
        else if(mode==3){kb.symbols=true;}
        pn_frame_clear(&frame,15);
        assert(pn_keyboard_render(&kb,&font,&frame,kby)==PN_OK && font.pixels==28);
        unsigned inked=0;
        for(int y=0;y<1216;y++)for(int x=0;x<684;x++){
            if(pn_frame_get(&frame,x,y)!=15){assert(y>=kby && y<kby+PN_KB_HEIGHT);inked++;}
        }
        assert(inked>8000);
    }
    // 验证中文候选栏命中码。/ Verify Chinese candidate bar hit codes.
    pn_keyboard_set_ime(&kb,true,true);
    assert(pn_keyboard_hit(&kb,kby,170,kby+40)==PN_KB_PUNCT_BASE-3);
    strcpy(kb.pinyin,"shu");
    assert(pn_keyboard_hit(&kb,kby,60,kby+40)==PN_KB_IME_RAW);
    assert(pn_keyboard_hit(&kb,kby,170,kby+40)==PN_KB_CAND_BASE-0);
    assert(pn_keyboard_hit(&kb,kby,242,kby+40)==PN_KB_CAND_BASE-1);
    assert(pn_keyboard_hit(&kb,kby,595,kby+40)==PN_KB_IME_PREV);
    assert(pn_keyboard_hit(&kb,kby,645,kby+40)==PN_KB_IME_NEXT);

    /* 4. 文件管理新建文件夹中文全拼辅助入口。/ File manager mkdir Chinese Pinyin IME helper. */
    pn_file_manager_t fm;pn_file_manager_init(&fm,"/tmp","16 GB");pn_keyboard_set_ime(&fm.keyboard,true,true);
    bool changed=false;
    assert(pn_file_manager_apply_kb(&fm,PN_FM_KB_BASE+'s',&changed) && changed);
    assert(pn_file_manager_apply_kb(&fm,PN_FM_KB_BASE+'h',&changed) && changed);
    assert(pn_file_manager_apply_kb(&fm,PN_FM_KB_BASE+'u',&changed) && changed);
    assert(pn_file_manager_apply_kb(&fm,PN_FM_KB_IME_BASE+(-PN_KB_CAND_BASE),&changed) && changed);
    assert(strcmp(fm.input,"书")==0);

    pn_font_close(&font);free(pixels);
    return 0;
}
