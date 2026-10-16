/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：诊断指令解析与合成触点时间线；只依赖文本、坐标与时间。
 * English: diagnostic command parsing and synthetic touch timelines; text, coordinates and time only.
 */
#include "pn_test_console.h"
#include <string.h>
#include <stddef.h>

// 读取一个十进制无符号数并跳过后续空格；溢出或缺失返回false。/ Read one unsigned decimal and skip trailing spaces; false on overflow or absence.
static bool number(const char **cursor,uint32_t limit,uint32_t *out){
    const char *p=*cursor;if(*p<'0' || *p>'9')return false;
    uint64_t value=0;while(*p>='0' && *p<='9'){value=value*10u+(uint64_t)(*p-'0');if(value>limit)return false;p++;}
    if(*p && *p!=' ')return false;
    while(*p==' ')p++;
    *cursor=p;*out=(uint32_t)value;return true;
}
// 读取一个小写单词并跳过后续空格。/ Read one lowercase word and skip trailing spaces.
static bool word(const char **cursor,char *out,size_t capacity){
    const char *p=*cursor;size_t n=0;
    while(*p>='a' && *p<='z'){if(n+1>=capacity)return false;out[n++]=*p++;}
    if(!n || (*p && *p!=' '))return false;
    while(*p==' ')p++;
    out[n]=0;*cursor=p;return true;
}
static bool point(const char **cursor,int *x,int *y){
    uint32_t a,b;if(!number(cursor,PN_TC_X_MAX,&a) || !number(cursor,PN_TC_Y_MAX,&b))return false;
    *x=(int)a;*y=(int)b;return true;
}
// 可选按住时长；缺省用默认值。/ Optional hold duration falling back to the default.
static bool hold(const char **cursor,unsigned fallback,unsigned *out){
    if(!**cursor){*out=fallback;return true;}
    uint32_t value;if(!number(cursor,PN_TC_HOLD_MAX,&value) || value<PN_TC_HOLD_MIN)return false;
    *out=value;return true;
}
pn_status_t pn_tc_parse(const char *line,pn_tc_command_t *command){
    if(!line || !command)return PN_INVALID;
    size_t length=0;while(length<=PN_TC_LINE_MAX && line[length])length++;
    if(length>PN_TC_LINE_MAX)return PN_INVALID;
    pn_tc_command_t c={0};const char *p=line;while(*p==' ')p++;
    char verb[8];if(!number(&p,UINT32_MAX,&c.seq) || !c.seq || !word(&p,verb,sizeof verb))return PN_INVALID;
    if(!strcmp(verb,"ping"))c.kind=PN_TC_PING;
    else if(!strcmp(verb,"version"))c.kind=PN_TC_VERSION;
    else if(!strcmp(verb,"status"))c.kind=PN_TC_STATUS;
    else if(!strcmp(verb,"shot"))c.kind=PN_TC_SHOT;
    else if(!strcmp(verb,"power"))c.kind=PN_TC_POWER;
    else if(!strcmp(verb,"tap") || !strcmp(verb,"long")){
        c.kind=verb[0]=='t'?PN_TC_TAP:PN_TC_LONG;
        if(!point(&p,&c.x0,&c.y0) || !hold(&p,c.kind==PN_TC_TAP?PN_TC_TAP_MS:PN_TC_LONG_MS,&c.hold_ms))return PN_INVALID;
        c.x1=c.x0;c.y1=c.y0;
    }else if(!strcmp(verb,"swipe")){
        c.kind=PN_TC_SWIPE;
        if(!point(&p,&c.x0,&c.y0) || !point(&p,&c.x1,&c.y1) || !hold(&p,PN_TC_SWIPE_MS,&c.hold_ms))return PN_INVALID;
    }else if(!strcmp(verb,"key")){
        char name[8];if(!word(&p,name,sizeof name))return PN_INVALID;
        int index=!strcmp(name,"left")?0:!strcmp(name,"middle")?1:!strcmp(name,"right")?2:-1;if(index<0)return PN_INVALID;
        c.kind=PN_TC_KEY;c.hold_ms=PN_TC_TAP_MS;
        if(*p){char mode[8];if(!word(&p,mode,sizeof mode) || strcmp(mode,"long"))return PN_INVALID;c.hold_ms=PN_TC_LONG_MS;}
        // 键中心沿用pn_key的160宽度分区。/ Key centers follow pn_key's 160-pixel partition.
        c.x0=c.x1=80+index*160;c.y0=c.y1=PN_TC_KEY_Y;
    }else return PN_INVALID;
    if(*p)return PN_INVALID;
    *command=c;return PN_OK;
}
bool pn_tc_is_gesture(const pn_tc_command_t *c){return c && (c->kind==PN_TC_TAP || c->kind==PN_TC_LONG || c->kind==PN_TC_SWIPE || c->kind==PN_TC_KEY);}
pn_status_t pn_tc_gesture_init(pn_tc_gesture_t *g,const pn_tc_command_t *c){
    if(!g || !pn_tc_is_gesture(c) || c->hold_ms<PN_TC_HOLD_MIN || c->hold_ms>PN_TC_HOLD_MAX)return PN_INVALID;
    *g=(pn_tc_gesture_t){.x0=c->x0,.y0=c->y0,.x1=c->x1,.y1=c->y1,.hold_ms=c->hold_ms};return PN_OK;
}
bool pn_tc_gesture_sample(pn_tc_gesture_t *g,uint64_t now,unsigned *count,int *x,int *y,bool *ending){
    if(!g || !count || !x || !y || !ending || g->finished)return false;
    if(!g->started){g->started=true;g->start_ms=now?now:1u;}
    uint64_t elapsed=now>g->start_ms?now-g->start_ms:0;
    *ending=false;
    if(elapsed<g->hold_ms){
        // 线性插值；整数运算避免浮点并保证终点在按住期末到达。/ Linear interpolation in integers so the end point is reached at the end of the hold.
        int64_t span=g->hold_ms>1u?(int64_t)g->hold_ms-1:1;
        *count=1;*x=g->x0+(int)(((int64_t)(g->x1-g->x0)*(int64_t)elapsed)/span);*y=g->y0+(int)(((int64_t)(g->y1-g->y0)*(int64_t)elapsed)/span);
        return true;
    }
    if(elapsed<(uint64_t)g->hold_ms+PN_TC_RELEASE_MS){
        *count=0;*x=g->x1;*y=g->y1;*ending=!g->ending_sent;g->ending_sent=true;return true;
    }
    g->finished=true;g->end_ms=now?now:1u;return false;
}
