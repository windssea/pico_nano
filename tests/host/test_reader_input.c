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
    /* 点按：左右半屏翻页，中间不再打开工具栏；只提交一次。/ Taps: left/right halves turn pages, the middle no longer opens the toolbar; each commits once. */
    assert(!pn_reader_input_feed(&i,1,230,500,true,&a));assert(release(&i,&a) && a==PN_APP_PREVIOUS);
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
    /* 上下翻页模式：上滑/点下半屏下一页，下滑/点上半屏上一页，遵守禁用开关。
     * Vertical page-turn mode: swipe up / tap bottom half = next, swipe down / tap top half = previous, respecting disable switches. */
    i=(pn_reader_input_t){.config={.vertical_turn=true}};
    assert(!pn_reader_input_feed(&i,1,340,700,true,&a));assert(!pn_reader_input_feed(&i,1,345,580,true,&a));assert(release(&i,&a) && a==PN_APP_NEXT);
    assert(!pn_reader_input_feed(&i,1,340,500,true,&a));assert(!pn_reader_input_feed(&i,1,345,620,true,&a));assert(release(&i,&a) && a==PN_APP_PREVIOUS);
    assert(!pn_reader_input_feed(&i,1,500,300,true,&a));assert(release(&i,&a) && a==PN_APP_PREVIOUS);
    assert(!pn_reader_input_feed(&i,1,100,900,true,&a));assert(release(&i,&a) && a==PN_APP_NEXT);
    i.config.no_swipe=true;
    assert(!pn_reader_input_feed(&i,1,340,700,true,&a));assert(!pn_reader_input_feed(&i,1,345,580,true,&a));assert(!release(&i,&a));
    i.config.no_swipe=false;i.config.no_edge_tap=true;
    assert(!pn_reader_input_feed(&i,1,340,900,true,&a));assert(!release(&i,&a));
    /* 去上下栏全屏热区：顶部 y<32 与底部 y>=1144 都可翻页。/ Bar-less full-screen hit zones accept top y<32 and bottom y>=1144. */
    i=(pn_reader_input_t){.config={.full_screen=true}};
    assert(!pn_reader_input_feed(&i,1,600,10,true,&a));assert(release(&i,&a) && a==PN_APP_NEXT);
    assert(!pn_reader_input_feed(&i,1,100,1180,true,&a));assert(release(&i,&a) && a==PN_APP_PREVIOUS);
    i.config.vertical_turn=true;
    assert(!pn_reader_input_feed(&i,1,600,10,true,&a));assert(release(&i,&a) && a==PN_APP_PREVIOUS);
    assert(!pn_reader_input_feed(&i,1,100,1180,true,&a));assert(release(&i,&a) && a==PN_APP_NEXT);
    /* 长按正文查词：满 450 ms 立即触发 PN_APP_LOOKUP 且抬起不再翻页；短按正常翻页；大幅滑动不误触长按。
     * Long-press body lookup: fires PN_APP_LOOKUP at >=450 ms and suppresses release turn; short tap still turns; large drag never triggers lookup. */
    i=(pn_reader_input_t){0};
    assert(!pn_reader_input_feed_timed(&i,1,300,400,true,1000,&a));
    assert(!pn_reader_input_feed_timed(&i,1,304,405,true,1300,&a));
    assert(pn_reader_input_feed_timed(&i,1,305,406,true,1460,&a) && a==PN_APP_LOOKUP && i.start_x==300 && i.start_y==400);
    assert(!pn_reader_input_feed_timed(&i,1,305,406,true,1600,&a));
    assert(!pn_reader_input_feed_timed(&i,0,0,0,true,1650,&a));
    assert(!pn_reader_input_feed_timed(&i,0,0,0,true,1700,&a));
    assert(!pn_reader_input_feed_timed(&i,1,600,400,true,2000,&a));
    assert(!pn_reader_input_feed_timed(&i,0,0,0,true,2100,&a));
    assert(pn_reader_input_feed_timed(&i,0,0,0,true,2120,&a) && a==PN_APP_NEXT);
    puts("reader input: release once, long-press lookup, vertical turn, full screen, edges turn, footer ignored, swipe, multitouch and cancellation passed");return 0;
}
