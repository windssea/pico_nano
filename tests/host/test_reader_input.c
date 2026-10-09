/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：阅读手势与按钮的一次提交识别器，设备和PC共用。
 * English: single-commit reader gesture/button recognizer shared by device and PC.
 * 冻结：多点/读错取消，滑出按钮不再触发；两次空采样确认释放。
 * Frozen: multitouch/read errors cancel; leaving a button prevents activation; two empty samples confirm release.
 */
#include "pn_reader_input.h"
#include <assert.h>
#include <stdio.h>
static bool release(pn_reader_input_t *i,pn_reader_action_t *a){assert(!pn_reader_input_feed(i,0,0,0,true,a));return pn_reader_input_feed(i,0,0,0,true,a);}
int main(void){
    pn_reader_input_t i={0};pn_reader_action_t a=PN_APP_OPEN;
    /* 点按：左右边缘翻页，中间172–512打开工具栏；只提交一次。/ Taps: the edges turn pages, the middle 172–512 opens the toolbar; each commits once. */
    assert(!pn_reader_input_feed(&i,1,230,500,true,&a));assert(release(&i,&a) && a==PN_APP_TOOLS);
    assert(!pn_reader_input_feed(&i,0,0,0,true,&a));
    assert(!pn_reader_input_feed(&i,1,600,500,true,&a));assert(!pn_reader_input_feed(&i,1,350,510,true,&a));assert(release(&i,&a) && a==PN_APP_NEXT); /* 从右边缘横向拖到中间是向左滑，翻下一页 / dragging from the right edge to the middle is a leftward swipe, next page */
    assert(!pn_reader_input_feed(&i,1,600,500,true,&a));assert(!pn_reader_input_feed(&i,1,610,510,true,&a));assert(release(&i,&a) && a==PN_APP_NEXT);
    assert(!pn_reader_input_feed(&i,1,100,500,true,&a));assert(release(&i,&a) && a==PN_APP_PREVIOUS);
    /* 页脚（y≥1144）与顶部留白（y<32）不接受点按和翻页。/ The footer (y>=1144) and the top margin (y<32) accept no taps or turns. */
    assert(!pn_reader_input_feed(&i,1,600,1160,true,&a));assert(!release(&i,&a));
    assert(!pn_reader_input_feed(&i,1,230,1160,true,&a));assert(!release(&i,&a));
    assert(!pn_reader_input_feed(&i,1,600,10,true,&a));assert(!release(&i,&a));
    assert(!pn_reader_input_feed(&i,1,600,1143,true,&a));assert(release(&i,&a) && a==PN_APP_NEXT);
    /* 滑动、多点、取消与读错。/ Swipes, multitouch, cancellation and read errors. */
    assert(!pn_reader_input_feed(&i,1,500,500,true,&a));assert(!pn_reader_input_feed(&i,1,400,505,true,&a));assert(release(&i,&a) && a==PN_APP_NEXT);
    assert(!pn_reader_input_feed(&i,1,600,500,true,&a));assert(!pn_reader_input_feed(&i,2,0,0,true,&a));assert(!release(&i,&a));
    assert(!pn_reader_input_feed(&i,1,600,500,true,&a));pn_reader_input_cancel(&i);assert(!release(&i,&a));
    assert(!pn_reader_input_feed(&i,1,600,500,true,&a));assert(!pn_reader_input_feed(&i,1,600,500,false,&a));assert(!release(&i,&a));
    assert(!pn_reader_input_feed(&i,1,-1,500,true,&a));assert(!release(&i,&a));
    puts("reader input: release once, middle opens the toolbar, edges turn, footer ignored, swipe, multitouch and cancellation passed");return 0;
}
