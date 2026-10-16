/* SPDX-License-Identifier: Apache-2.0
 * 中文：诊断指令解析边界，及合成时间线经真实三键识别得到短按/长按。
 * English: diagnostic command parsing bounds, and synthetic timelines yielding short/long presses through the real key recognizer. */
#include "pn_test_console.h"
#include "pn_key.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static pn_tc_command_t parse_ok(const char *line){pn_tc_command_t c;assert(pn_tc_parse(line,&c)==PN_OK);return c;}
// 按10ms输入采样驱动时间线并喂给真实pn_key；返回识别到的事件。/ Drive a timeline at 10 ms input samples into the real pn_key and return the recognized event.
static pn_key_event_t run_key(const char *line,int *key,unsigned *samples){
    pn_tc_command_t c=parse_ok(line);pn_tc_gesture_t g;assert(pn_tc_gesture_init(&g,&c)==PN_OK);
    pn_key_t keys={.key=-1};pn_key_event_t seen=PN_KEY_NONE;*samples=0;
    for(uint64_t now=1000;now<1000+20000;now+=10){unsigned count;int x,y;bool ending;
        if(!pn_tc_gesture_sample(&g,now,&count,&x,&y,&ending))break;
        (*samples)++;
        int hit=-1;pn_key_event_t e=pn_key_feed(&keys,count,x,y,true,now,&hit);if(e!=PN_KEY_NONE){assert(seen==PN_KEY_NONE);seen=e;*key=hit;}}
    assert(g.finished && g.start_ms==1000 && g.end_ms>g.start_ms);
    return seen;
}
int main(void){
    pn_tc_command_t c=parse_ok("7 ping");assert(c.kind==PN_TC_PING && c.seq==7);
    assert(parse_ok("1 version").kind==PN_TC_VERSION && parse_ok("2 status").kind==PN_TC_STATUS && parse_ok("3 shot").kind==PN_TC_SHOT && parse_ok("4 power").kind==PN_TC_POWER);
    c=parse_ok("  9 tap 85 1150");assert(c.kind==PN_TC_TAP && c.x0==85 && c.y0==1150 && c.x1==85 && c.y1==1150 && c.hold_ms==PN_TC_TAP_MS);
    c=parse_ok("10 long 0 0 1500");assert(c.kind==PN_TC_LONG && c.hold_ms==1500);
    c=parse_ok("11 swipe 600 500 80 520");assert(c.kind==PN_TC_SWIPE && c.x1==80 && c.y1==520 && c.hold_ms==PN_TC_SWIPE_MS);
    c=parse_ok("12 key right long");assert(c.kind==PN_TC_KEY && c.x0==400 && c.y0==PN_TC_KEY_Y && c.hold_ms==PN_TC_LONG_MS);
    c=parse_ok("4294967295 key left");assert(c.seq==4294967295u && c.x0==80);
    const char *bad[]={"","ping","0 ping","4294967296 ping","1 PING","1 ping extra","1 tap 684 0","1 tap 0 1400","1 tap 5","1 tap -1 5","1 tap 5 5 19",
        "1 tap 5 5 10001","1 swipe 1 2 3","1 key up","1 key left short","1 key","1 shot 1","1 tap 5x 5","1 unknownverb",NULL};
    for(unsigned i=0;bad[i];i++){pn_tc_command_t out={.seq=99};assert(pn_tc_parse(bad[i],&out)==PN_INVALID && out.seq==99);}
    char longline[PN_TC_LINE_MAX+8];memset(longline,' ',sizeof longline);memcpy(longline,"1 ping",6);longline[PN_TC_LINE_MAX+1]=0;assert(pn_tc_parse(longline,&c)==PN_INVALID);
    longline[PN_TC_LINE_MAX]=0;assert(pn_tc_parse(longline,&c)==PN_OK);
    assert(pn_tc_parse(NULL,&c)==PN_INVALID && pn_tc_parse("1 ping",NULL)==PN_INVALID);
    pn_tc_gesture_t g;c=parse_ok("1 ping");assert(pn_tc_gesture_init(&g,&c)==PN_INVALID && !pn_tc_is_gesture(&c));
    // 三键短按/长按经真实识别器各只触发一次。/ Short and long key presses each fire exactly once through the real recognizer.
    const char *names[]={"left","middle","right"};
    for(int k=0;k<3;k++){char line[32];int key=-1;unsigned samples;
        snprintf(line,sizeof line,"5 key %s",names[k]);assert(run_key(line,&key,&samples)==PN_KEY_SHORT && key==k);
        snprintf(line,sizeof line,"6 key %s long",names[k]);key=-1;assert(run_key(line,&key,&samples)==PN_KEY_LONG && key==k);}
    // 滑动端点、松手终点只报一次、结束后不再采样。/ Swipe endpoints, a single release endpoint and no samples after finishing.
    c=parse_ok("8 swipe 600 500 80 540 200");assert(pn_tc_gesture_init(&g,&c)==PN_OK);
    unsigned count;int x,y;bool ending;unsigned endings=0,held=0;int first_x=-1,last_x=-1,last_y=-1;
    for(uint64_t now=50;pn_tc_gesture_sample(&g,now,&count,&x,&y,&ending);now+=10){
        if(count){if(first_x<0)first_x=x;last_x=x;last_y=y;held++;assert(x<=600 && x>=80 && y>=500 && y<=540);}
        else{assert(x==80 && y==540);}
        if(ending){endings++;assert(!count);}}
    assert(first_x==600 && endings==1 && held==20 && last_x<=110 && last_y>=538 && g.end_ms>=50+200+PN_TC_RELEASE_MS);
    assert(!pn_tc_gesture_sample(&g,100000,&count,&x,&y,&ending));
    // 首个采样延迟到达时时间线从实际首采样开始。/ A late first sample starts the timeline at the actual first sample.
    c=parse_ok("9 tap 10 10");assert(pn_tc_gesture_init(&g,&c)==PN_OK && pn_tc_gesture_sample(&g,5000,&count,&x,&y,&ending) && count==1 && g.start_ms==5000);
    puts("Test console: parsing bounds, swipe timeline and key short/long through pn_key passed");return 0;
}
