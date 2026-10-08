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
static int region(int x,int y){
    if(x<0 || x>=684 || y<0 || y>=1216)return -1;
    if(y>=1120 && y<=1200 && x>=32 && x<660){int column=(x-32)/157;if((x-32)%157<148)return column;}
    return y>=100 && y<1060?4:-1;
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
            if(i->region>=0 && i->region<4){const pn_reader_action_t actions[]={PN_APP_PREVIOUS,PN_APP_NEXT,PN_APP_SMALLER,PN_APP_LARGER};action=actions[i->region];}
            else if(i->region==4){int dx=i->last_x-i->start_x,dy=i->last_y-i->start_y;
                // 横滑：|dx|≥64且≥1.5|dy|；点按：移动≤24，左0–171、右513–683。/ Swipe: |dx|≥64 and ≥1.5|dy|; tap: movement ≤24, left 0–171, right 513–683.
                if(abs(dx)>=64 && 2*abs(dx)>=3*abs(dy)){if(i->config.no_swipe)submit=false;else action=dx<0?PN_APP_NEXT:PN_APP_PREVIOUS;}
                else if(abs(dx)<=24 && abs(dy)<=24 && !i->config.no_edge_tap){
                    bool left=i->start_x<172,right=i->start_x>=513;
                    if(left || right)action=(left!=i->config.left_hand)?PN_APP_PREVIOUS:PN_APP_NEXT;else submit=false;}
                else submit=false;
            }else submit=false;
        }
        i->down=false;i->cancelled=false;i->blocked=false;
        if(submit){*out=action;return true;}return false;
    }
    i->empty_samples=0;if(i->blocked)return false;
    int hit=region(x,y);
    if(!i->down){i->down=true;i->cancelled=hit<0;i->region=hit;i->start_x=x;i->start_y=y;}
    else if(hit<0 || (i->region<4 && hit!=i->region) || (i->region==4 && hit!=4))i->cancelled=true;
    i->last_x=x;i->last_y=y;return false;
}
