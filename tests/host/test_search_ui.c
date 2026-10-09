/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：搜索页状态、键盘命中与匹配规则。
 * English: search page state, keyboard hits and matching rules.
 */
#include <assert.h>
#include <string.h>
#include "pn_search_ui.h"
int main(void){
    pn_pool_t pool;assert(pn_pool_init(&pool,1024*1024,NULL,NULL,NULL)==0);
    pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();assert(pn_font_open(&font,&pool,&source,24)==PN_OK);
    uint8_t *bytes=pn_alloc(&pool,684*1216/2);pn_frame_t frame;assert(pn_frame_bind(&frame,bytes,684*1216/2,684,1216));
    pn_search_ui_t ui;pn_search_ui_open(&ui,NULL);assert(!*ui.query);
    assert(pn_search_ui_render(&ui,&font,&frame)==PN_OK && font.pixels==24);
    assert(pn_search_ui_append(&ui,'b') && pn_search_ui_append(&ui,'9') && !pn_search_ui_append(&ui,'B') && !pn_search_ui_append(&ui,' ') && !strcmp(ui.query,"b9"));
    assert(pn_search_ui_render(&ui,&font,&frame)==PN_OK);
    assert(pn_search_ui_delete(&ui) && !strcmp(ui.query,"b") && pn_search_ui_delete(&ui) && !pn_search_ui_delete(&ui));
    for(int i=0;i<PN_CATALOG_QUERY_MAX;i++)assert(pn_search_ui_append(&ui,'a'));
    assert(!pn_search_ui_append(&ui,'a'));
    /* 键盘：第一行a–f，最后一行'4'–'9'；键间空隙和输入框无命中。/ Keyboard: first row a–f, last row '4'–'9'; gaps and the query box are not hits. */
    assert(pn_search_ui_hit(60,360)=='a' && pn_search_ui_hit(60+103*5,360)=='f' && pn_search_ui_hit(60,360+92)=='g' && pn_search_ui_hit(60,360+92*5)=='4' && pn_search_ui_hit(60+103*5,360+92*5)=='9');
    assert(pn_search_ui_hit(130,360)==PN_SEARCH_NONE && pn_search_ui_hit(60,330+86)==PN_SEARCH_NONE && pn_search_ui_hit(300,200)==PN_SEARCH_NONE);
    assert(pn_search_ui_hit(60,60)==PN_SEARCH_BACK && pn_search_ui_hit(600,60)==PN_SEARCH_NONE);
    assert(pn_search_ui_hit(100,950)==PN_SEARCH_DELETE && pn_search_ui_hit(300,950)==PN_SEARCH_CLEAR && pn_search_ui_hit(550,950)==PN_SEARCH_DONE && pn_search_ui_hit(230,950)==PN_SEARCH_NONE);
    /* 匹配：英文子串、汉字拼音首字母连续片段、空词匹配全部。/ Matching: English substrings, runs of pinyin initials, and an empty query matching all. */
    assert(pn_catalog_match("English Sample.txt","sample") && pn_catalog_match("English Sample.txt","gli") && !pn_catalog_match("English Sample.txt","zz"));
    assert(pn_catalog_match("慢读时光.txt","mdsg") && pn_catalog_match("慢读时光.txt","dsg") && !pn_catalog_match("慢读时光.txt","mdsgx"));
    assert(pn_catalog_match("第2本.txt","d2b") && pn_catalog_match("anything","") && !pn_catalog_match(NULL,"a"));
    pn_font_close(&font);pn_free(bytes);assert(!pool.live && !pool.used);return 0;
}
