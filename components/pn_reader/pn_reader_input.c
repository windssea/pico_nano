/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：阅读手势与按钮的一次提交识别器，设备和PC共用。
 * English: single-commit reader gesture/button recognizer shared by device and PC.
 * 冻结：多点/读错取消，滑出按钮不再触发；两次空采样确认释放。
 * Frozen: multitouch/read errors cancel; leaving a button prevents activation; two empty samples confirm release.
 */
#include "pn_reader_input.h"
#include <stdlib.h>
/* 只有正文区（y=32..1143）接受翻页和点按；页脚与其他位置忽略。工具栏由调用方的专用命中处理。
 * Only the text area (y=32..1143) accepts turns and taps; the footer and elsewhere are ignored. The toolbar uses its own hit test in the caller. */
static int region(int x,int y){
    if(x<0 || x>=684 || y<0 || y>=1216)return -1;
    return y>=32 && y<1144?4:-1;
}
void pn_reader_input_cancel(pn_reader_input_t *i){if(i){i->cancelled=true;i->blocked=true;i->empty_samples=0;}}
bool pn_reader_input_feed(pn_reader_input_t *i,unsigned count,int x,int y,bool valid,pn_reader_action_t *out){
    if(!i || !out)return false;
    if(!valid || count>1){pn_reader_input_cancel(i);return false;}
    if(!count){
        if(i->empty_samples<2)i->empty_samples++;
        if(i->empty_samples<2)return false;
        bool submit=i->down && !i->cancelled && !i->blocked;pn_reader_action_t action=PN_APP_OPEN;
        if(submit){
            if(i->region==4){int dx=i->last_x-i->start_x,dy=i->last_y-i->start_y;
                // 横滑：|dx|≥64且≥1.5|dy|；点按：移动≤24，左0–171、右513–683翻页，中间172–512打开工具栏。
                // Swipe: |dx|≥64 and ≥1.5|dy|; tap: movement ≤24, left 0–171 / right 513–683 turn the page, the middle 172–512 opens the toolbar.
                if(abs(dx)>=64 && 2*abs(dx)>=3*abs(dy)){if(i->config.no_swipe)submit=false;else action=dx<0?PN_APP_NEXT:PN_APP_PREVIOUS;}
                else if(abs(dx)<=24 && abs(dy)<=24){
                    bool left=i->start_x<172,right=i->start_x>=513;
                    if(left || right){if(i->config.no_edge_tap)submit=false;else action=(left!=i->config.left_hand)?PN_APP_PREVIOUS:PN_APP_NEXT;}
                    else action=PN_APP_TOOLS;}
                else submit=false;
            }else submit=false;
        }
        i->down=false;i->cancelled=false;i->blocked=false;
        if(submit){*out=action;return true;}return false;
    }
    i->empty_samples=0;if(i->blocked)return false;
    int hit=region(x,y);
    if(!i->down){i->down=true;i->cancelled=hit<0;i->region=hit;i->start_x=x;i->start_y=y;}
    else if(hit!=4)i->cancelled=true;
    i->last_x=x;i->last_y=y;return false;
}
