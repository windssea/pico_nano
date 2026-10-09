/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：进度跳转面板：步进只改草稿，确认才移动位置，取消重画当前页，并可返回跳转前位置。
 * English: progress jump panel: stepping edits only the draft, confirming moves the position, cancelling redraws the page and the position before the jump stays reachable.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_jump_ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct {bool fail;unsigned presents;} screen_t;
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){(void)frame;(void)profile;screen_t *s=ctx;s->presents++;return s->fail?PN_IO:PN_OK;}
int main(void){
    char root[]="/tmp/pn-jump-ui-XXXXXX";assert(mkdtemp(root));char book[384],state[384];snprintf(book,sizeof book,"%s/book.txt",root);snprintf(state,sizeof state,"%s/state",root);
    FILE *f=fopen(book,"wb");assert(f);for(unsigned i=0;i<600;i++)assert(fprintf(f,"第%u段：文字沿着清晰的行列展开，调整字距和段落间距，找到舒服的阅读节奏。\n",i)>0);assert(fclose(f)==0);
    pn_pool_t pool;assert(pn_pool_init(&pool,2*1024*1024,NULL,NULL,NULL)==0);pn_reader_app_t app={0};screen_t screen={0};pn_jump_ui_t ui={0};
    assert(pn_reader_app_open(&app,&pool,book,NULL,state,44,0)==PN_OK && pn_reader_app_step(&app,PN_APP_OPEN,1,present,&screen)==PN_OK);
    /* 呈现失败：面板不激活，位置不变。/ A failed presentation leaves the panel inactive and the position unchanged. */
    pn_txt_progress_t before,after;assert(pn_reader_app_progress(&app,&before)==PN_OK);
    screen.fail=true;assert(pn_jump_ui_open(&ui,&app,present,&screen)==PN_IO && !ui.active);screen.fail=false;
    assert(pn_jump_ui_open(&ui,&app,present,&screen)==PN_OK && ui.active && ui.presented && ui.current==0 && ui.draft==0 && ui.maximum==100 && ui.large==10);
    /* 命中：四个步进键、跳转、取消；空白无命中。/ Hits: four step keys, Jump and Cancel; blank space is no hit. */
    assert(pn_jump_ui_hit(&ui,60,690)==PN_JUI_STEP && pn_jump_ui_hit(&ui,220,690)==PN_JUI_STEP+1 && pn_jump_ui_hit(&ui,380,690)==PN_JUI_STEP+2 && pn_jump_ui_hit(&ui,540,690)==PN_JUI_STEP+3);
    assert(pn_jump_ui_hit(&ui,175,690)==-1 && pn_jump_ui_hit(&ui,300,860)==PN_JUI_CONFIRM && pn_jump_ui_hit(&ui,300,980)==PN_JUI_CANCEL && pn_jump_ui_hit(&ui,60,60)==PN_JUI_CANCEL && pn_jump_ui_hit(&ui,300,300)==-1);
    /* 步进只改草稿，位置不动；两端夹紧。/ Stepping edits only the draft and the position stays put; both ends clamp. */
    assert(pn_jump_ui_event(&ui,PN_JUI_STEP+3,2,present,&screen)==PN_OK && ui.draft==10);
    assert(pn_jump_ui_event(&ui,PN_JUI_STEP+2,2,present,&screen)==PN_OK && ui.draft==11);
    assert(pn_jump_ui_event(&ui,PN_JUI_STEP,2,present,&screen)==PN_OK && pn_jump_ui_event(&ui,PN_JUI_STEP,2,present,&screen)==PN_OK && ui.draft==0);
    for(int i=0;i<12;i++)assert(pn_jump_ui_event(&ui,PN_JUI_STEP+3,2,present,&screen)==PN_OK);
    assert(ui.draft==100 && pn_reader_app_progress(&app,&after)==PN_OK && after.source_offset==before.source_offset);
    for(int i=0;i<6;i++)assert(pn_jump_ui_event(&ui,PN_JUI_STEP,2,present,&screen)==PN_OK);
    assert(ui.draft==40);
    /* 取消：重画当前页并关闭，位置不变。/ Cancel redraws the current page and closes; the position is unchanged. */
    unsigned presents=screen.presents;assert(pn_jump_ui_event(&ui,PN_JUI_CANCEL,3,present,&screen)==PN_OK && !ui.active && screen.presents>presents);
    assert(pn_reader_app_progress(&app,&after)==PN_OK && after.source_offset==before.source_offset);
    /* 确认：跳到约40%，之后可返回跳转前位置。/ Confirm jumps to roughly 40%, after which the pre-jump position is reachable. */
    assert(pn_jump_ui_open(&ui,&app,present,&screen)==PN_OK);
    for(int i=0;i<4;i++)assert(pn_jump_ui_event(&ui,PN_JUI_STEP+3,4,present,&screen)==PN_OK);
    assert(ui.draft==40 && pn_jump_ui_event(&ui,PN_JUI_CONFIRM,5,present,&screen)==PN_OK && !ui.active && pn_reader_app_last_confirmed(&app));
    assert(pn_reader_app_progress(&app,&after)==PN_OK && after.source_offset>=before.source_size*38/100 && after.source_offset<=before.source_size*42/100 && pn_reader_app_bookmark_can_return(&app));
    assert(pn_reader_app_bookmark_return(&app,6,present,&screen)==PN_OK && pn_reader_app_progress(&app,&after)==PN_OK && after.source_offset==before.source_offset);
    /* 任意百分比都落在字符边界上（多字节文本里7%、33%也能跳）。/ Any percentage lands on a character boundary (7% and 33% work in multi-byte text too). */
    for(unsigned percent=1;percent<=100;percent+=6){
        assert(pn_jump_ui_open(&ui,&app,present,&screen)==PN_OK);
        for(unsigned i=0;i<percent;i++)assert(pn_jump_ui_event(&ui,PN_JUI_STEP+2,7,present,&screen)==PN_OK);
        pn_status_t jumped=pn_jump_ui_event(&ui,PN_JUI_CONFIRM,7+percent,present,&screen);
        assert(jumped==PN_OK || ui.draft==ui.current);if(ui.active)assert(pn_jump_ui_event(&ui,PN_JUI_CANCEL,200+percent,present,&screen)==PN_OK);
    }
    /* 呈现失败的确认保留面板可重试。/ A confirmation whose presentation fails keeps the panel for retry. */
    assert(pn_jump_ui_open(&ui,&app,present,&screen)==PN_OK && pn_jump_ui_event(&ui,PN_JUI_STEP+3,7,present,&screen)==PN_OK);
    screen.fail=true;assert(pn_jump_ui_event(&ui,PN_JUI_CONFIRM,8,present,&screen)!=PN_OK && ui.active);screen.fail=false;
    assert(pn_jump_ui_event(&ui,PN_JUI_RETRY,9,present,&screen)==PN_OK && ui.presented);
    pn_jump_ui_close(&ui);assert(!ui.active && pn_jump_ui_event(&ui,PN_JUI_CANCEL,10,present,&screen)==PN_INVALID);
    assert(pn_reader_app_close(&app,100000)==PN_OK);
    return 0;
}
