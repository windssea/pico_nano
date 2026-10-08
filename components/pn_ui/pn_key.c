/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：触摸三键识别，按下-保持-释放一次周期最多发一个事件。
 * English: touch-key recognition; one press-hold-release cycle emits at most one event.
 * 冻结：只依赖坐标与时间，不做硬件访问。/ Frozen: coordinates and time only, no hardware access.
 */
#include "pn_key.h"
#include <stddef.h>
int pn_key_hit(int x,int y){
    if(y<PN_KEY_AREA_TOP || x<0)return -1;
    int index=x/PN_KEY_PITCH;
    return index<3?index:-1;
}
void pn_key_cancel(pn_key_t *k){if(k && k->down)k->cancelled=true;}
pn_key_event_t pn_key_feed(pn_key_t *k,unsigned count,int x,int y,bool valid,uint64_t now,int *key){
    if(!k || !key)return PN_KEY_NONE;
    if(!valid || count>1){if(k->down)k->cancelled=true;k->empty=0;return PN_KEY_NONE;}
    if(!count){
        if(!k->down)return PN_KEY_NONE;
        if(++k->empty<2)return PN_KEY_NONE;
        bool submit=!k->cancelled && !k->fired && !k->foreign && k->key>=0;int pressed=k->key;
        *k=(pn_key_t){.key=-1};
        if(!submit)return PN_KEY_NONE;
        *key=pressed;return PN_KEY_SHORT;
    }
    k->empty=0;int hit=pn_key_hit(x,y);
    if(!k->down){*k=(pn_key_t){.key=hit,.since=now,.down=true,.foreign=hit<0};return PN_KEY_NONE;}
    if(k->foreign || k->cancelled || k->fired)return PN_KEY_NONE;
    if(hit!=k->key){k->cancelled=true;return PN_KEY_NONE;}
    if(now>=k->since && now-k->since>=PN_KEY_LONG_MS){k->fired=true;*key=k->key;return PN_KEY_LONG;}
    return PN_KEY_NONE;
}
