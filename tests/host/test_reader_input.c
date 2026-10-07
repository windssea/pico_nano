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
    assert(!pn_reader_input_feed(&i,1,230,1160,true,&a));assert(release(&i,&a) && a==PN_APP_NEXT);
    assert(!pn_reader_input_feed(&i,0,0,0,true,&a));
    assert(!pn_reader_input_feed(&i,1,230,1160,true,&a));assert(!pn_reader_input_feed(&i,1,350,1160,true,&a));assert(!pn_reader_input_feed(&i,1,230,1160,true,&a));assert(!release(&i,&a));
    assert(!pn_reader_input_feed(&i,1,600,500,true,&a));assert(!pn_reader_input_feed(&i,1,350,510,true,&a));assert(release(&i,&a) && a==PN_APP_NEXT);
    assert(!pn_reader_input_feed(&i,1,100,500,true,&a));assert(release(&i,&a) && a==PN_APP_PREVIOUS);
    assert(!pn_reader_input_feed(&i,1,600,1160,true,&a));assert(!pn_reader_input_feed(&i,2,0,0,true,&a));assert(!release(&i,&a));
    assert(!pn_reader_input_feed(&i,1,600,1160,true,&a));pn_reader_input_cancel(&i);assert(!release(&i,&a));
    assert(!pn_reader_input_feed(&i,1,600,1160,true,&a));assert(!pn_reader_input_feed(&i,1,600,1160,false,&a));assert(!release(&i,&a));
    assert(!pn_reader_input_feed(&i,1,-1,1160,true,&a));assert(!release(&i,&a));
    assert(!pn_reader_input_feed(&i,1,600,1160,true,&a));assert(release(&i,&a) && a==PN_APP_LARGER);
    puts("reader input: release once, button leave, swipe, multitouch and cancellation passed");return 0;
}
